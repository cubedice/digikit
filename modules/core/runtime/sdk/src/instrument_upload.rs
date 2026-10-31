//! Bounded image/BSS upload pump. Completion permits metadata installation only.
//! Retain the same controllers after interruption; dropping is not cancellation.
use crate::{
    instrument_exchange::Failure,
    instrument_image::Transfer,
    instrument_load::{Command, Loads},
    instrument_transport::{Driver, Error as LinkError, Progress as LinkProgress, Transport},
};

#[derive(Debug, PartialEq, Eq)]
pub enum Progress {
    Pending,
    ReadyToInstall,
    Recover { cause: Failure, command: Command },
    Blocked(Failure),
    Finished,
}
pub struct Upload<'a, 'image, T> {
    driver: &'a mut Driver<T>,
    transfer: &'a mut Transfer<'image>,
    loads: &'a mut Loads,
    start: u32,
    budget: u32,
    draining: Option<(Failure, u32)>,
    blocked: Option<Failure>,
    finished: bool,
}
impl<'a, 'image, T: Transport> Upload<'a, 'image, T> {
    /// One deadline covers all chunks, zero fills and retries. Tick clock must be
    /// monotonic modulo u32, sampled within 2^31 ticks. Driver belongs exclusively
    /// to this upload. Use invalidate_boot on boot loss, not a new deadline.
    pub fn new(
        driver: &'a mut Driver<T>,
        transfer: &'a mut Transfer<'image>,
        loads: &'a mut Loads,
        now: u32,
        budget: u32,
    ) -> Option<Self> {
        if budget == 0 || budget >= 0x80000000 || transfer.next(loads).is_err() {
            return None;
        }
        Some(Self {
            driver,
            transfer,
            loads,
            start: now,
            budget,
            draining: None,
            blocked: None,
            finished: false,
        })
    }
    fn block(&mut self, cause: Failure) -> Progress {
        self.blocked = Some(cause);
        Progress::Blocked(cause)
    }
    fn recover(&mut self, cause: Failure) -> Progress {
        match self.transfer.failed(self.loads) {
            Ok(command) => {
                self.finished = true;
                Progress::Recover { cause, command }
            }
            Err(_) => self.block(Failure::Protocol),
        }
    }
    /// Caller has lost trust in this DSP boot. Preserve all storage and stop
    /// provider calls; whole-boot teardown is the owning runtime's responsibility.
    pub fn invalidate_boot(&mut self) {
        self.driver.invalidate_boot();
        self.finished = false;
        self.blocked = Some(Failure::Boot);
    }
    pub fn tick(&mut self, now: u32) -> Progress {
        if self.finished {
            return Progress::Finished;
        }
        if let Some(cause) = self.blocked {
            return Progress::Blocked(cause);
        }
        if self.draining.is_none() && now.wrapping_sub(self.start) >= self.budget {
            self.draining = Some((Failure::Timeout, now));
        }
        let link = self.driver.poll();
        if let Some((cause, began)) = self.draining {
            return match link {
                Ok(LinkProgress::Idle | LinkProgress::Complete(_) | LinkProgress::Failed(_)) => {
                    self.recover(cause)
                }
                Ok(LinkProgress::Pending) if now.wrapping_sub(began) < self.budget => {
                    Progress::Pending
                }
                _ => self.block(cause),
            };
        }
        match link {
            Err(_) => return self.block(Failure::Link),
            Ok(LinkProgress::Pending) => return Progress::Pending,
            Ok(LinkProgress::Failed(_)) => return self.recover(Failure::Link),
            Ok(LinkProgress::Complete(completed)) => {
                return if completed.image(self.transfer, self.loads).is_ok() {
                    Progress::Pending
                } else {
                    self.block(Failure::Protocol)
                }
            }
            Ok(LinkProgress::Idle) => {}
        }
        let write = match self.transfer.next(self.loads) {
            Ok(Some(write)) => write,
            Ok(None) => {
                self.finished = true;
                return Progress::ReadyToInstall;
            }
            Err(_) => return self.block(Failure::Protocol),
        };
        match self.driver.start_image(&write) {
            Ok(_) => Progress::Pending,
            Err(LinkError::Uncertain | LinkError::Busy) => self.block(Failure::Link),
            Err(_) => self.recover(Failure::Link),
        }
    }
}
#[cfg(test)]
mod tests;
