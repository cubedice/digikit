//! Owning UI-task coordinator for an admitted V2 load. No self-references,
//! allocation or public V2 capability. Transfers run in tick; explicit lease
//! acquisition/release also run on the owning UI task.
//! Keep this owner for the entire DSP boot, including after a blocked load.
use crate::{
    instrument_admission::Plan,
    instrument_exchange::{self as exchange, Failure},
    instrument_image::Transfer,
    instrument_load::{Command, Ledger, Loads, MemoryError},
    instrument_mailbox::Mailbox,
    instrument_provider::Replies,
    instrument_registry::Ticket,
    instrument_transport::{Driver, Transport},
    instrument_upload as upload,
};

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    NotOwned,
    Busy,
    Blocked,
    Budget,
    Image(crate::instrument_image::Error),
}

#[derive(Debug, PartialEq, Eq)]
pub enum Progress {
    Idle,
    Pending,
    /// Only a validated initialization reply can produce Publish. A recovered
    /// load produces Release only after its fresh detach reply is validated.
    Complete {
        command: Command,
        recovered: Option<Failure>,
    },
    Blocked(Failure),
}

/// Scheduler-visible link handoff. Only Released permits legacy producers to
/// resume; Pending, Owned and Blocked require them to remain paused.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LinkProgress {
    Released,
    Pending,
    Owned,
    Blocked(Failure),
}
#[derive(Clone, Copy)]
enum Handoff {
    Released,
    Acquire { start: u32, budget: u32 },
    Owned,
    Release { start: u32, budget: u32 },
}

enum Phase {
    Upload(upload::PumpState),
    Install(exchange::PumpState),
    Initialize(exchange::PumpState),
    Detach(exchange::PumpState),
}
struct Job<'image> {
    transfer: Transfer<'image>,
    plan: Plan,
    phase: Phase,
    budget: u32,
    recovered: Option<Failure>,
}

pub struct Coordinator<'image, T> {
    driver: Driver<T>,
    replies: Replies,
    mailbox: Mailbox,
    loads: Loads,
    job: Option<Job<'image>>,
    blocked: Option<Failure>,
    handoff: Handoff,
}

impl<'image, T: Transport> Coordinator<'image, T> {
    /// Adopt the boot's dedicated idle driver and admitted load ledger, before
    /// any V2 mailbox traffic. Never reconstruct this owner to recover a fault.
    /// Providers and legacy traffic must already be serialized by the caller.
    pub fn new(driver: Driver<T>, replies: Replies, loads: Loads) -> Self {
        Self {
            driver,
            replies,
            loads,
            mailbox: Mailbox::new(),
            job: None,
            blocked: None,
            handoff: Handoff::Released,
        }
    }

    /// Inspect capacity/publication without permitting out-of-order ACKs.
    pub fn loads(&self) -> &Loads {
        &self.loads
    }

    /// Start an already admitted, relocated image. A deadline covers each whole
    /// phase (all chunks or all rows); it is never restarted by a UI tick.
    pub fn begin(
        &mut self,
        plan: Plan,
        image: &'image [u8],
        now: u32,
        budget: u32,
    ) -> Result<(), Error> {
        if self.blocked.is_some() {
            return Err(Error::Blocked);
        }
        if self.job.is_some()
            || matches!(
                self.handoff,
                Handoff::Acquire { .. } | Handoff::Release { .. }
            )
        {
            return Err(Error::Busy);
        }
        if !self.driver.transaction_ready() {
            return Err(Error::NotOwned);
        }
        let phase = upload::PumpState::new(now, budget).ok_or(Error::Budget)?;
        // No new traffic has started, but an adopted driver must also be idle.
        if !self.driver.is_idle() {
            self.blocked = Some(Failure::Link);
            return Err(Error::Blocked);
        }
        let transfer =
            Transfer::begin(&mut self.loads, plan.grant.ticket, &plan.grant, image, true)
                .map_err(Error::Image)?;
        self.job = Some(Job {
            transfer,
            plan,
            phase: Phase::Upload(phase),
            budget,
            recovered: None,
        });
        Ok(())
    }

    /// Whole-boot trust loss is terminal, including between jobs. No further
    /// provider calls, blind detach, or allocator release is allowed.
    pub fn invalidate_boot(&mut self) {
        self.driver.invalidate_boot();
        self.replies.invalidate_boot();
        self.blocked = Some(Failure::Boot);
    }

    /// Release only after validated detach, against the original shared ledger.
    pub fn release_memory(
        &mut self,
        ticket: Ticket,
        ledger: &mut Ledger<'_>,
    ) -> Result<(), MemoryError> {
        // Loads checks the RELEASE phase; boot loss must also prevent reuse.
        if self.blocked.is_some() {
            return Err(MemoryError::Load(
                crate::instrument_registry::Error::WrongPhase,
            ));
        }
        self.loads.release_memory(ticket, ledger)
    }

    pub fn tick(&mut self, now: u32) -> Progress {
        if let Some(cause) = self.blocked {
            return Progress::Blocked(cause);
        }
        let Some(job) = self.job.as_mut() else {
            return Progress::Idle;
        };
        let outcome = match &mut job.phase {
            Phase::Upload(state) => {
                match state.tick(&mut self.driver, &mut job.transfer, &mut self.loads, now) {
                    upload::Progress::Pending => return Progress::Pending,
                    upload::Progress::ReadyToInstall => {
                        if self
                            .mailbox
                            .begin_install(&job.transfer, &job.plan, &self.loads, true)
                            .is_err()
                        {
                            match job.transfer.failed(&mut self.loads) {
                                Ok(command) => exchange::Progress::Recover {
                                    cause: Failure::Protocol,
                                    command,
                                },
                                Err(_) => exchange::Progress::Blocked(Failure::Protocol),
                            }
                        } else {
                            job.phase =
                                Phase::Install(exchange::PumpState::new(now, job.budget).unwrap());
                            return Progress::Pending;
                        }
                    }
                    upload::Progress::Recover { cause, command } => {
                        exchange::Progress::Recover { cause, command }
                    }
                    upload::Progress::Blocked(cause) => exchange::Progress::Blocked(cause),
                    upload::Progress::Finished => exchange::Progress::Blocked(Failure::Protocol),
                }
            }
            Phase::Install(state) | Phase::Initialize(state) | Phase::Detach(state) => state.tick(
                &mut self.driver,
                &mut self.mailbox,
                &mut self.replies,
                &mut self.loads,
                now,
            ),
        };
        let cause = match outcome {
            exchange::Progress::Pending => return Progress::Pending,
            exchange::Progress::Complete(None) if matches!(job.phase, Phase::Install(_)) => {
                let command = self
                    .mailbox
                    .take_installed()
                    .and_then(|receipt| job.transfer.finish(&mut self.loads, &receipt).ok());
                if let Some(command) = command {
                    if self.mailbox.begin(command, &self.loads, true).is_ok() {
                        job.phase =
                            Phase::Initialize(exchange::PumpState::new(now, job.budget).unwrap());
                        return Progress::Pending;
                    }
                }
                Failure::Protocol
            }
            exchange::Progress::Complete(Some(command))
                if matches!(
                    (&job.phase, command),
                    (Phase::Initialize(_), Command::Publish(_))
                        | (Phase::Detach(_), Command::Release(_))
                ) =>
            {
                let recovered = job.recovered;
                self.job = None;
                return Progress::Complete { command, recovered };
            }
            exchange::Progress::Recover { cause, command } => {
                // One recovery attempt only. A failed detach keeps its storage;
                // repeated retries must not extend recovery indefinitely.
                if !matches!(job.phase, Phase::Detach(_))
                    && self.mailbox.begin(command, &self.loads, true).is_ok()
                {
                    job.recovered = Some(cause);
                    job.phase = Phase::Detach(exchange::PumpState::new(now, job.budget).unwrap());
                    return Progress::Pending;
                }
                cause
            }
            exchange::Progress::Blocked(cause) => cause,
            _ => Failure::Protocol,
        };
        if cause == Failure::Boot {
            self.invalidate_boot();
        }
        self.blocked = Some(cause);
        Progress::Blocked(cause)
    }
}

impl<T: crate::instrument_lease::LeaseTransport> Coordinator<'_, T> {
    /// Stop admitting new legacy jobs before requesting ownership, then let
    /// existing legacy load/FX jobs drain. The same deadline covers that drain
    /// and every claim retry. Repeated requests cannot renew the deadline.
    /// Clock units are monotonic; budget is 1..2^31 and ticks must be less than
    /// 2^31 apart. Clock reset requires invalidate_boot.
    pub fn request_link(&mut self, now: u32, budget: u32) -> Result<(), Error> {
        self.request_handoff(now, budget, true)
    }

    /// Request release only after the entire job (including recovery) ends.
    /// Legacy producers must remain paused until tick_link returns Released.
    pub fn request_link_release(&mut self, now: u32, budget: u32) -> Result<(), Error> {
        self.request_handoff(now, budget, false)
    }

    fn request_handoff(&mut self, now: u32, budget: u32, acquire: bool) -> Result<(), Error> {
        if self.blocked.is_some() {
            return Err(Error::Blocked);
        }
        if self.job.is_some()
            || !matches!(
                (acquire, self.handoff),
                (true, Handoff::Released) | (false, Handoff::Owned)
            )
        {
            return Err(Error::Busy);
        }
        if budget == 0 || budget >= 0x80000000 {
            return Err(Error::Budget);
        }
        self.handoff = if acquire {
            Handoff::Acquire { start: now, budget }
        } else {
            Handoff::Release { start: now, budget }
        };
        Ok(())
    }

    /// One bounded UI pass. legacy_idle means all logical load/FX transactions
    /// and their pollers have drained, not merely that the wire is idle. Caller
    /// must keep new legacy starts paused throughout acquisition and ownership.
    /// No callbacks occur after deadline expiry or loss of boot trust. A failed
    /// handoff is terminal for this owner, retaining any lease and allocations.
    pub fn tick_link(&mut self, now: u32, legacy_idle: bool) -> LinkProgress {
        if let Some(cause) = self.blocked {
            return LinkProgress::Blocked(cause);
        }
        let (start, budget, acquire) = match self.handoff {
            Handoff::Released => return LinkProgress::Released,
            Handoff::Owned => return LinkProgress::Owned,
            Handoff::Acquire { start, budget } => (start, budget, true),
            Handoff::Release { start, budget } => (start, budget, false),
        };
        if now.wrapping_sub(start) >= budget {
            self.blocked = Some(Failure::Timeout);
            return LinkProgress::Blocked(Failure::Timeout);
        }
        if acquire && !legacy_idle {
            return LinkProgress::Pending;
        }
        let result = if acquire {
            self.claim_link()
        } else {
            self.release_link().map(|()| true)
        };
        match result {
            Ok(true) => {
                self.handoff = if acquire {
                    Handoff::Owned
                } else {
                    Handoff::Released
                };
                if acquire {
                    LinkProgress::Owned
                } else {
                    LinkProgress::Released
                }
            }
            Ok(false) | Err(crate::instrument_lease::Error::Busy) => LinkProgress::Pending,
            Err(_) => {
                self.blocked = Some(Failure::Link);
                LinkProgress::Blocked(Failure::Link)
            }
        }
    }

    /// Scheduler must first quiesce legacy load/FX pollers. Busy acquisition
    /// needs an external bounded wait; it does not start a load's deadline.
    fn claim_link(&mut self) -> Result<bool, crate::instrument_lease::Error> {
        if self.blocked.is_some() {
            return Err(crate::instrument_lease::Error::Uncertain);
        }
        if self.job.is_some() {
            return Err(crate::instrument_lease::Error::Busy);
        }
        let result = self.driver.claim_link();
        if result == Err(crate::instrument_lease::Error::Uncertain) {
            self.blocked = Some(Failure::Link);
        }
        result
    }
    /// Only between jobs, never during reply gaps, recovery or after blockage.
    /// Busy release retains ownership; the scheduler must keep legacy paused.
    /// No Drop implementation releases on cancellation or lost boot trust.
    fn release_link(&mut self) -> Result<(), crate::instrument_lease::Error> {
        if self.blocked.is_some() {
            return Err(crate::instrument_lease::Error::Uncertain);
        }
        if self.job.is_some() {
            return Err(crate::instrument_lease::Error::Busy);
        }
        let result = self.driver.release_link();
        if result == Err(crate::instrument_lease::Error::Uncertain) {
            self.blocked = Some(Failure::Link);
        }
        result
    }
}

#[cfg(test)]
mod tests;
