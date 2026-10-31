//! Boot-local optional-provider boundary for the V2 coordinator.
//! Presence is not a V2 capability. DSP identity/capabilities are checked by the
//! mailbox; timeout, reboot detection and exclusive link ownership belong to
//! the coordinator. No allocation or firmware pointer is retained here.
use crate::{
    instrument_load::{Command, Loads},
    instrument_mailbox::{self, Mailbox},
    instrument_transport::{Api, Poll, Send},
};

pub type ExpectSession = unsafe extern "C" fn(u32, u32) -> i32;

pub type Snapshot = unsafe extern "C" fn(*mut u32, u32) -> i32;

/// ColdFire-boot-local resolution state. First three u32 words are diagnostic:
/// attempted, present-symbol mask, successfully bound. None means unavailable,
/// never permission to retry later in the same boot. Not a V2 capability.
#[repr(C)]
pub struct Binding {
    attempted: u32,
    present: u32,
    bound: u32,
    providers: Option<(OwnedProviders, ExpectSession)>,
}
impl Binding {
    pub const fn new() -> Self {
        Self {
            attempted: 0,
            present: 0,
            bound: 0,
            providers: None,
        }
    }
    /// # Safety
    /// Nonzero addresses must be validated functions from this patched firmware
    /// image, in order: claim, release, owned send, tagged poll, reply snapshot,
    /// one-shot session expectation.
    /// Their signatures and lifetime must satisfy OwnedProviders::bind. Call only
    /// on the owning UI task. A DSP restart does not authorize rebinding.
    pub unsafe fn bind_once(&mut self, addresses: [usize; 6]) {
        if self.attempted != 0 {
            return;
        }
        for (i, address) in addresses.iter().enumerate() {
            if *address != 0 {
                self.present |= 1 << i;
            }
        }
        if self.present == 63 {
            // Never form a function pointer from an optional zero address.
            let providers = OwnedProviders::bind(
                Some(core::mem::transmute::<usize, crate::instrument_lease::Claim>(addresses[0])),
                Some(core::mem::transmute::<
                    usize,
                    crate::instrument_lease::Release,
                >(addresses[1])),
                Some(core::mem::transmute::<
                    usize,
                    crate::instrument_lease::SendOwned,
                >(addresses[2])),
                Some(core::mem::transmute::<usize, Poll>(addresses[3])),
                Some(core::mem::transmute::<usize, Snapshot>(addresses[4])),
            );
            self.providers = providers.map(|p| {
                (
                    p,
                    core::mem::transmute::<usize, ExpectSession>(addresses[5]),
                )
            });
            self.bound = 1;
        }
        self.attempted = 1;
    }
    /// Move the provider set to its boot-long owner at most once. Presence and
    /// successful binding diagnostics remain unchanged after consumption.
    pub fn take(&mut self) -> Option<(OwnedProviders, ExpectSession)> {
        self.providers.take()
    }
    pub fn diagnostics(&self) -> [u32; 3] {
        [self.attempted, self.present, self.bound]
    }
}

pub struct Providers {
    transport: Api,
    replies: Replies,
}

pub struct OwnedProviders {
    transport: crate::instrument_lease::OwnedApi,
    replies: Replies,
}
impl OwnedProviders {
    /// Bind the entire leased provider set without acquiring or calling it.
    /// # Safety
    /// Entries must satisfy Providers::bind and OwnedApi::new's same-boot,
    /// ABI, exclusive UI-task and invalidation requirements.
    pub unsafe fn bind(
        claim: Option<crate::instrument_lease::Claim>,
        release: Option<crate::instrument_lease::Release>,
        send: Option<crate::instrument_lease::SendOwned>,
        poll: Option<Poll>,
        snapshot: Option<Snapshot>,
    ) -> Option<Self> {
        Some(Self {
            transport: crate::instrument_lease::OwnedApi::new(claim?, release?, send?, poll?),
            replies: Replies {
                snapshot: snapshot?,
                invalidated: false,
            },
        })
    }
    pub fn into_parts(self) -> (crate::instrument_lease::OwnedApi, Replies) {
        (self.transport, self.replies)
    }
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

    /// Observe the provider's sticky loss-of-boot-trust flag even when no
    /// mailbox reply is pending. This neither establishes boot identity nor
    /// consumes an ACK. Missing/current data does not grant any capability.
    pub fn check_boot(&mut self) -> Result<(), Error> {
        let mut words = [0u32; 16];
        self.snapshot_into(&mut words).map(|_| ())
    }

    fn snapshot_into(&mut self, words: &mut [u32; 16]) -> Result<bool, Error> {
        if self.invalidated {
            return Err(Error::BootInvalidated);
        }
        match unsafe { (self.snapshot)(words.as_mut_ptr(), words.len() as u32) } {
            0 => Ok(false),
            16 => Ok(true),
            -2 => {
                self.invalidate_boot();
                Err(Error::BootInvalidated)
            }
            other => Err(Error::Provider(other)),
        }
    }

    /// Bootstrap-only reply check, after all bind writes have drained. Does not
    /// admit lifecycle commands or relax the existing mailbox version contract.
    pub(crate) fn session_bound(&mut self, token: u32, sequence: u32) -> Result<bool, Error> {
        let mut w = [0u32; 16];
        if !self.snapshot_into(&mut w)? {
            return Ok(false);
        }
        let (mut a, mut b) = (0u32, 0u32);
        for word in &w[..15] {
            a = a.wrapping_add(*word);
            b = b.wrapping_add(a);
        }
        if w[0] != 0x494ea102
            || w[4..12] != [0x49525231, sequence, 0, token, 5, 0, 0, 0]
            || token == 0
            || w[12] != token
            || w[13] != 0
            || w[14] != 0
            || w[15] != (a ^ b)
        {
            return Err(Error::Mailbox(instrument_mailbox::Error::InvalidReply));
        }
        Ok(true)
    }

    /// Read a private stack snapshot and pass it through the mailbox validator.
    /// Call only while awaiting a DSP reply, after all three writes drained.
    /// Waiting never resets the coordinator's deadline. Repeated old frames are
    /// harmless because mailbox identities are never reused within a DSP boot.
    pub fn poll(&mut self, mailbox: &mut Mailbox, loads: &mut Loads) -> Result<Progress, Error> {
        let mut words = [0u32; 16];
        if !self.snapshot_into(&mut words)? {
            return Ok(Progress::Waiting);
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
