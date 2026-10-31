//! Bounded UI-task pump for one already-started registry mailbox transaction.
//! Holds exclusive borrows of its controllers. Drop does not cancel, drain or
//! release anything: after interruption the owner must resume recovery using
//! the same boot-local controllers. This is not a firmware scheduler.
use crate::{
    instrument_load::{Command, Loads},
    instrument_mailbox::Mailbox,
    instrument_provider::{Error as ReplyError, Progress as ReplyProgress, Replies},
    instrument_transport::{Driver, Error as LinkError, Progress as LinkProgress, Transport},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Failure {
    Boot,
    Timeout,
    Link,
    Reply,
    Protocol,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Progress {
    Pending,
    /// Transaction completed; None means an INSTALL receipt is in the mailbox.
    Complete(Option<Command>),
    /// Old writes drained; rows quarantined. Drive this fresh detach command
    /// through the same mailbox before releasing anything.
    Recover {
        cause: Failure,
        command: Command,
    },
    /// Drainage/identity could not be proved. Retain rows, memory and buffer.
    /// Reboot recovery must replace the entire boot namespace, never just IDs.
    Blocked(Failure),
    Finished,
}

pub struct Exchange<'a, T> {
    driver: &'a mut Driver<T>,
    mailbox: &'a mut Mailbox,
    replies: &'a mut Replies,
    loads: &'a mut Loads,
    state: PumpState,
}

/// Persistent state owned by the coordinator; no references into its owner.
pub(crate) struct PumpState {
    start: u32,
    budget: u32,
    draining: Option<(Failure, u32)>,
    blocked: Option<Failure>,
    finished: bool,
}
impl<'a, T: Transport> Exchange<'a, T> {
    /// Start after Mailbox::begin/begin_install. Budget is monotonic tick units,
    /// in 1..2^31; call tick at least once per 2^31 ticks. Clock reset requires
    /// external boot invalidation. One deadline covers all rows and busy retries.
    /// Driver must belong to this mailbox transaction (never another image).
    pub fn new(
        driver: &'a mut Driver<T>,
        mailbox: &'a mut Mailbox,
        replies: &'a mut Replies,
        loads: &'a mut Loads,
        now: u32,
        budget: u32,
    ) -> Option<Self> {
        if !mailbox.has_pending() {
            return None;
        }
        Some(Self {
            driver,
            mailbox,
            replies,
            loads,
            state: PumpState::new(now, budget)?,
        })
    }
    pub fn invalidate_boot(&mut self) {
        self.state.invalidate_boot(self.driver, self.replies);
    }
    pub fn tick(&mut self, now: u32) -> Progress {
        self.state
            .tick(self.driver, self.mailbox, self.replies, self.loads, now)
    }
}

impl PumpState {
    pub(crate) fn new(now: u32, budget: u32) -> Option<Self> {
        if budget == 0 || budget >= 0x80000000 {
            return None;
        }
        Some(Self {
            start: now,
            budget,
            draining: None,
            blocked: None,
            finished: false,
        })
    }
    /// Stop this boot's link and reply APIs without releasing any resources.
    pub(crate) fn invalidate_boot<T: Transport>(
        &mut self,
        driver: &mut Driver<T>,
        replies: &mut Replies,
    ) {
        driver.invalidate_boot();
        replies.invalidate_boot();
        self.finished = false;
        self.blocked = Some(Failure::Boot);
    }
    fn block(&mut self, failure: Failure) -> Progress {
        self.blocked = Some(failure);
        Progress::Blocked(failure)
    }
    fn recover(&mut self, mailbox: &mut Mailbox, loads: &mut Loads, cause: Failure) -> Progress {
        match mailbox.abandon_after_drain(loads, true) {
            Ok(command) => {
                self.finished = true;
                Progress::Recover { cause, command }
            }
            Err(_) => self.block(Failure::Protocol),
        }
    }
    /// At most one provider poll or send per tick, plus one bounded snapshot
    /// when the link is idle. No busy loop and no timeout-driven buffer reuse.
    pub(crate) fn tick<T: Transport>(
        &mut self,
        driver: &mut Driver<T>,
        mailbox: &mut Mailbox,
        replies: &mut Replies,
        loads: &mut Loads,
        now: u32,
    ) -> Progress {
        if self.finished {
            return Progress::Finished;
        }
        if let Some(failure) = self.blocked {
            return Progress::Blocked(failure);
        }
        if self.draining.is_none() && now.wrapping_sub(self.start) >= self.budget {
            self.draining = Some((Failure::Timeout, now));
        }
        let link = driver.poll();
        if let Some((cause, began)) = self.draining {
            return match link {
                Ok(LinkProgress::Idle | LinkProgress::Complete(_) | LinkProgress::Failed(_)) => {
                    self.recover(mailbox, loads, cause)
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
            Ok(LinkProgress::Failed(_)) => return self.recover(mailbox, loads, Failure::Link),
            Ok(LinkProgress::Complete(completed)) => {
                return if completed.mailbox(mailbox).is_ok() {
                    Progress::Pending
                } else {
                    self.block(Failure::Protocol)
                };
            }
            Ok(LinkProgress::Idle) => {}
        }
        if let Some(write) = mailbox.write() {
            return match driver.start_mailbox(&write) {
                Ok(_) => Progress::Pending,
                Err(LinkError::Uncertain | LinkError::Busy) => self.block(Failure::Link),
                Err(_) => self.recover(mailbox, loads, Failure::Link),
            };
        }
        match replies.poll(mailbox, loads) {
            Ok(ReplyProgress::Waiting) => Progress::Pending,
            Ok(ReplyProgress::Complete(command)) => {
                self.finished = true;
                Progress::Complete(Some(command))
            }
            Ok(ReplyProgress::RowComplete) if mailbox.has_pending() => Progress::Pending,
            Ok(ReplyProgress::RowComplete) => {
                self.finished = true;
                Progress::Complete(None)
            }
            Err(ReplyError::BootInvalidated) => {
                self.invalidate_boot(driver, replies);
                Progress::Blocked(Failure::Boot)
            }
            Err(ReplyError::Mailbox(_) | ReplyError::Provider(_)) => {
                self.recover(mailbox, loads, Failure::Reply)
            }
        }
    }
}

#[cfg(test)]
mod tests;
