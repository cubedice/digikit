//! One-shot bootstrap over the existing owned driver. No recovery/rebind: a
//! failed or ambiguous commit retains the link and source buffer until reboot.
use super::*;
use crate::instrument_transport::{Progress as LinkProgress, Started};

pub(super) struct Session {
    token: u32,
    sequence: u32,
    stage: u32,
    start: u32,
    budget: u32,
    bound: bool,
}
impl<'image, T: Transport> Coordinator<'image, T> {
    /// Arm before the first wire write, only with an idle, exclusively owned
    /// link and no admitted instances. One deadline covers all three writes and
    /// the session ACK. This does not enable V2 admission or audio routing.
    /// # Safety
    /// `expect` must be this boot's validated mi_registry_expect_session entry,
    /// paired with this owner's snapshot provider and serialized on the UI task.
    /// It must arm once and retain no pointer; never rebuild the owner to retry.
    pub unsafe fn begin_session(
        &mut self,
        expect: crate::instrument_provider::ExpectSession,
        token: u32,
        now: u32,
        budget: u32,
    ) -> Result<(), Error> {
        if self.blocked.is_some() {
            return Err(Error::Blocked);
        }
        if token == 0 || budget == 0 || budget >= 0x80000000 {
            return Err(Error::Budget);
        }
        if self.session.is_some() || self.job.is_some() || self.loads.capacity().rows_used != 0 {
            return Err(Error::Busy);
        }
        if !matches!(self.handoff, Handoff::Owned) || !self.driver.transaction_ready() {
            return Err(Error::NotOwned);
        }
        if !self.driver.is_idle() {
            return Err(Error::Busy);
        }
        if self.observe_boot().is_err() {
            return Err(Error::Blocked);
        }
        let sequence = self
            .mailbox
            .reserve_session_sequence()
            .map_err(|_| Error::Budget)?;
        self.session = Some(Session {
            token,
            sequence,
            stage: 1,
            start: now,
            budget,
            bound: false,
        });
        match unsafe { expect(token, sequence) } {
            0 => Ok(()),
            -2 => {
                self.invalidate_boot();
                Err(Error::Blocked)
            }
            _ => {
                self.blocked = Some(Failure::Reply);
                Err(Error::Blocked)
            }
        }
    }
    /// Drive only the one-shot bootstrap. Kept separate from image-load ticking
    /// so a bootstrap-only firmware does not link the dormant image loader.
    pub fn tick_bootstrap(&mut self, now: u32) -> Progress {
        if let Err(cause) = self.observe_boot() {
            return Progress::Blocked(cause);
        }
        if self.session.is_none() {
            return Progress::Idle;
        }
        self.tick_session(now)
    }

    /// A successful bind exchange only; not a capability or reset-path proof.
    pub fn session_bound(&self) -> bool {
        self.blocked.is_none() && self.session.as_ref().is_some_and(|s| s.bound)
    }
    pub(super) fn session_pending(&self) -> bool {
        self.session.as_ref().is_some_and(|s| !s.bound)
    }
    pub(super) fn session_expired(&self, now: u32) -> bool {
        self.session
            .as_ref()
            .is_some_and(|s| !s.bound && now.wrapping_sub(s.start) >= s.budget)
    }
    pub(super) fn tick_session(&mut self, now: u32) -> Progress {
        if self.session_bound() {
            return Progress::Idle;
        }
        if self.session_expired(now) {
            self.blocked = Some(Failure::Timeout);
            return Progress::Blocked(Failure::Timeout);
        }
        let s = self.session.as_mut().unwrap();
        let failure = match self.driver.poll() {
            Ok(LinkProgress::Pending) => return Progress::Pending,
            Ok(LinkProgress::Complete(done)) if done.session(s.sequence, s.stage) => {
                s.stage += 1;
                return Progress::Pending;
            }
            Ok(LinkProgress::Idle) if s.stage < 4 => {
                let mut w = crate::instrument_mailbox::Write {
                    sequence: s.sequence,
                    stage: s.stage,
                    offset: 0,
                    count: 1,
                    words: [0; 16],
                };
                if s.stage == 2 {
                    w.offset = 1;
                    w.count = 15;
                    w.words[..6].copy_from_slice(&[1, s.sequence, 0, s.token, 5, 0]);
                    w.words[14] = w.words[..14].iter().fold(0x49524731, |a, b| a ^ b);
                } else if s.stage == 3 {
                    w.words[0] = 0x49524731;
                }
                match self.driver.start_mailbox(&w) {
                    Ok(Started::Busy | Started::Accepted) => return Progress::Pending,
                    Err(_) => Failure::Link,
                }
            }
            Ok(LinkProgress::Idle) if s.stage == 4 => {
                match self.replies.session_bound(s.token, s.sequence) {
                    Ok(true) => {
                        s.bound = true;
                        return Progress::Idle;
                    }
                    Ok(false) => return Progress::Pending,
                    Err(crate::instrument_provider::Error::BootInvalidated) => Failure::Boot,
                    Err(_) => Failure::Reply,
                }
            }
            Ok(LinkProgress::Complete(_)) => Failure::Protocol,
            _ => Failure::Link,
        };
        if failure == Failure::Boot {
            self.invalidate_boot();
        }
        self.blocked = Some(failure);
        Progress::Blocked(failure)
    }
}
