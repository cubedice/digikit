//! Boot-local optional-provider boundary for the V2 coordinator.
//! Presence is not a V2 capability. DSP identity/capabilities are checked by the
//! mailbox; timeout, reboot detection and exclusive link ownership belong to
//! the coordinator. No allocation or firmware pointer is retained here.
use crate::{
    instrument_load::{Command, Loads},
    instrument_mailbox::{self, Mailbox},
    instrument_transport::{Api, Poll, Send},
};

pub type Snapshot = unsafe extern "C" fn(*mut u32, u32) -> i32;

pub struct Providers {
    transport: Api,
    replies: Replies,
}

impl Providers {
    /// Bind only a complete provider set; missing symbols must be represented
    /// as None, never transmuted into a null function pointer. Calls no provider.
    ///
    /// # Safety
    /// All supplied functions must be validated entries from the same DSP boot.
    /// Send/poll must obey instrument_transport::Transport. Snapshot must obey
    /// mi_registry_reply: write at most the supplied capacity, retain no pointer,
    /// return 16 for a complete copy, 0 when unavailable, -1 for invalid capacity,
    /// -2 when boot trust is lost (sticky until a cold boot).
    /// The caller must serialize UI-task access and invalidate on firmware reboot.
    pub unsafe fn bind(
        send: Option<Send>,
        poll: Option<Poll>,
        snapshot: Option<Snapshot>,
    ) -> Option<Self> {
        Some(Self {
            transport: Api::new(send?, poll?),
            replies: Replies {
                snapshot: snapshot?,
                invalidated: false,
            },
        })
    }

    pub fn into_parts(self) -> (Api, Replies) {
        (self.transport, self.replies)
    }
}

pub struct Replies {
    snapshot: Snapshot,
    invalidated: bool,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    BootInvalidated,
    Provider(i32),
    Mailbox(instrument_mailbox::Error),
}

#[derive(Debug, PartialEq, Eq)]
pub enum Progress {
    /// No frame available, or a reply belonging to a different transaction.
    Waiting,
    /// A physical row completed; more rows or an install receipt may remain.
    RowComplete,
    /// All rows completed. Only the coordinator can publish/release resources.
    Complete(Command),
}

impl Replies {
    /// Discard trust in this provider for the rest of its boot. Never call it
    /// again, even if its cached bytes match a formerly pending transaction.
    pub fn invalidate_boot(&mut self) {
        self.invalidated = true;
    }

    /// Read a private stack snapshot and pass it through the mailbox validator.
    /// Call only while awaiting a DSP reply, after all three writes drained.
    /// Waiting never resets the coordinator's deadline. Repeated old frames are
    /// harmless because mailbox identities are never reused within a DSP boot.
    pub fn poll(&mut self, mailbox: &mut Mailbox, loads: &mut Loads) -> Result<Progress, Error> {
        if self.invalidated {
            return Err(Error::BootInvalidated);
        }
        let mut words = [0u32; 16];
        match unsafe { (self.snapshot)(words.as_mut_ptr(), words.len() as u32) } {
            0 => return Ok(Progress::Waiting),
            -2 => {
                self.invalidate_boot();
                return Err(Error::BootInvalidated);
            }
            16 => {}
            other => return Err(Error::Provider(other)),
        }
        match mailbox.reply(&words, loads) {
            Ok(Some(command)) => Ok(Progress::Complete(command)),
            Ok(None) => Ok(Progress::RowComplete),
            Err(instrument_mailbox::Error::MismatchedReply) => Ok(Progress::Waiting),
            Err(error) => Err(Error::Mailbox(error)),
        }
    }
}

#[cfg(test)]
mod tests;
