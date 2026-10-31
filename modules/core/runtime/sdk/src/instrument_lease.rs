//! Typed ownership of the production DSP-link lease ABI. Dropping never releases
//! ownership: only the coordinator may authorize release after a whole job ends.
use crate::instrument_transport::{Poll, Status, Transport};

pub type Claim = unsafe extern "C" fn(*mut u32) -> i32;
pub type Release = unsafe extern "C" fn(u32) -> i32;
pub type SendOwned = unsafe extern "C" fn(u32, u32, u32, *const u8, u32, *mut u32) -> i32;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Busy,
    Rejected,
    Uncertain,
}

/// Ownership calls are serialized with all starts and polls on the UI task.
pub trait LeaseTransport: Transport {
    /// False is busy, with no lease acquired; retry only within a caller deadline.
    fn claim(&mut self) -> Result<bool, Error>;
    /// Busy retains ownership. Unknown/stale ownership must stop this boot.
    fn release(&mut self) -> Result<(), Error>;
}

pub struct OwnedApi {
    claim: Claim,
    release: Release,
    send: SendOwned,
    poll: Poll,
    lease: u32,
    last_lease: u32,
    uncertain: bool,
    releasing: bool,
}
impl OwnedApi {
    /// # Safety
    /// All entries must belong to the same boot and obey the DSP-link lease and
    /// tagged-transfer ABIs. Caller must stop legacy job pollers before claiming,
    /// serialize UI access, retain this owner, and invalidate on boot trust loss.
    pub unsafe fn new(claim: Claim, release: Release, send: SendOwned, poll: Poll) -> Self {
        Self {
            claim,
            release,
            send,
            poll,
            lease: 0,
            last_lease: 0,
            uncertain: false,
            releasing: false,
        }
    }
    fn uncertain(&mut self) -> Error {
        self.uncertain = true;
        Error::Uncertain
    }
}
impl LeaseTransport for OwnedApi {
    fn claim(&mut self) -> Result<bool, Error> {
        if self.uncertain {
            return Err(Error::Uncertain);
        }
        if self.releasing {
            return Err(Error::Busy);
        }
        if self.lease != 0 {
            return Ok(true);
        }
        let mut lease = 0;
        match unsafe { (self.claim)(&mut lease) } {
            0 if lease > self.last_lease => {
                self.lease = lease;
                self.last_lease = lease;
                Ok(true)
            }
            -1 if lease == 0 => Ok(false),
            -2 if lease == 0 => Err(Error::Rejected),
            _ => Err(self.uncertain()),
        }
    }
    fn release(&mut self) -> Result<(), Error> {
        if self.uncertain {
            return Err(Error::Uncertain);
        }
        if self.lease == 0 {
            return Ok(());
        }
        match unsafe { (self.release)(self.lease) } {
            0 => {
                self.lease = 0;
                self.releasing = false;
                Ok(())
            }
            -1 => {
                self.releasing = true;
                Err(Error::Busy)
            }
            _ => Err(self.uncertain()),
        }
    }
}
unsafe impl Transport for OwnedApi {
    fn transaction_ready(&self) -> bool {
        self.lease != 0 && !self.uncertain && !self.releasing
    }
    unsafe fn start(
        &mut self,
        window: u32,
        destination: u32,
        source: *const u8,
        bytes: u32,
    ) -> (i32, u32) {
        if !self.transaction_ready() {
            return (-3, 0);
        }
        let mut id = 0;
        let result = (self.send)(self.lease, window, destination, source, bytes, &mut id);
        (result, id)
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        if !self.transaction_ready() {
            return -1;
        }
        unsafe { (self.poll)(id, status) }
    }
}

#[cfg(test)]
mod tests;
