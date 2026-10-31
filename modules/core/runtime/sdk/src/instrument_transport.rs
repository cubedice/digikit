//! Adapter between V2 write controllers and the tagged production DSP-link ABI.
//! The driver owns a static buffer: accepted writes cannot outlive or move it.
//! A single UI-task owner must retain this driver for the DSP boot. No V2
//! capability or registry reply is inferred from byte-transfer completion.
use crate::{instrument_image as image, instrument_mailbox as mailbox};

#[repr(C)]
#[derive(Default)]
pub struct Status {
    pub state: u32,
    pub err: u32,
    pub nchunks: u32,
    pub acked: u32,
    pub sent: u32,
    pub resent: u32,
    pub nerr: u32,
}

/// # Safety
/// Implementations must obey dl_send_tagged/dl_poll_tagged: only an accepted
/// start retains the source pointer; no source reads after a drained poll;
/// IDs identify a single serialized boot-wide request namespace. Poll return 1
/// proves END-frame drainage, not just terminal status. No concurrent UI starts.
pub unsafe trait Transport {
    /// Whether this transport owns the transaction boundary required for starts.
    /// Unleased implementations must provide equivalent external serialization.
    fn transaction_ready(&self) -> bool {
        true
    }
    /// # Safety
    /// Source must remain valid and immutable through drainage.
    unsafe fn start(
        &mut self,
        window: u32,
        destination: u32,
        source: *const u8,
        bytes: u32,
    ) -> (i32, u32);
    fn poll(&mut self, id: u32, status: &mut Status) -> i32;
}

pub type Send = unsafe extern "C" fn(u32, u32, *const u8, u32, *mut u32) -> i32;
pub type Poll = unsafe extern "C" fn(u32, *mut Status) -> i32;
pub struct Api {
    send: Send,
    poll: Poll,
}
impl Api {
    /// # Safety
    /// Functions must be the matching tagged DSP-link entries for this boot,
    /// resolved only after optional-provider validation. They must obey Transport.
    pub unsafe fn new(send: Send, poll: Poll) -> Self {
        Self { send, poll }
    }
}
unsafe impl Transport for Api {
    unsafe fn start(
        &mut self,
        window: u32,
        destination: u32,
        source: *const u8,
        bytes: u32,
    ) -> (i32, u32) {
        let mut id = 0;
        let result = (self.send)(window, destination, source, bytes, &mut id);
        (result, id)
    }
    fn poll(&mut self, id: u32, status: &mut Status) -> i32 {
        unsafe { (self.poll)(id, status) }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Busy,
    InvalidWrite,
    Rejected,
    Uncertain,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Started {
    Busy,
    Accepted,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Target {
    Image(image::Ack),
    Mailbox { sequence: u32, stage: u32 },
}
#[derive(Debug, PartialEq, Eq)]
pub struct Completed {
    target: Target,
}
impl Completed {
    pub(crate) fn session(&self, sequence: u32, stage: u32) -> bool {
        self.target == Target::Mailbox { sequence, stage }
    }

    pub fn image(
        self,
        transfer: &mut image::Transfer<'_>,
        loads: &crate::instrument_load::Loads,
    ) -> Result<(), image::Error> {
        match self.target {
            Target::Image(ack) => transfer.acknowledged(loads, ack),
            _ => Err(image::Error::WrongAck),
        }
    }
    pub fn mailbox(self, mailbox: &mut mailbox::Mailbox) -> Result<(), mailbox::Error> {
        match self.target {
            Target::Mailbox { sequence, stage } => mailbox.write_acked(sequence, stage),
            _ => Err(mailbox::Error::WrongTransfer),
        }
    }
}
#[derive(Debug, PartialEq, Eq)]
pub enum Progress {
    Idle,
    Pending,
    Complete(Completed),
    Failed(u32),
}

pub struct Driver<T> {
    transport: T,
    buffer: &'static mut [u8; 256],
    pending: Option<(u32, Target)>,
    last_id: u32,
    uncertain: bool,
}
impl<T: Transport> Driver<T> {
    pub(crate) fn transaction_ready(&self) -> bool {
        !self.uncertain && self.transport.transaction_ready()
    }
    pub(crate) fn is_idle(&self) -> bool {
        self.pending.is_none() && !self.uncertain
    }
    /// The buffer is dedicated for this driver's lifetime and cannot be reclaimed
    /// by dropping it, even if the transport is still reading it. No allocator.
    pub fn new(transport: T, buffer: &'static mut [u8; 256]) -> Self {
        Self {
            transport,
            buffer,
            pending: None,
            last_id: 0,
            uncertain: false,
        }
    }
    /// Irrevocably stop this boot's provider calls without freeing its buffer.
    /// A boot change does not prove outstanding DMA/source reads have stopped.
    pub fn invalidate_boot(&mut self) {
        self.uncertain = true;
    }
    fn available(&self) -> Result<(), Error> {
        if self.uncertain {
            Err(Error::Uncertain)
        } else if self.pending.is_some() {
            Err(Error::Busy)
        } else {
            Ok(())
        }
    }
    pub fn start_image(&mut self, write: &image::Write<'_>) -> Result<Started, Error> {
        self.available()?;
        let n = write.data.len();
        if n == 0
            || n > 256
            || n % 4 != 0
            || n != write.ack.bytes as usize
            || write.ack.address % 4 != 0
            || write.ack.address.checked_add(n as u32).is_none()
        {
            return Err(Error::InvalidWrite);
        }
        self.buffer[..n].copy_from_slice(write.data);
        self.start(0, write.ack.address, n, Target::Image(write.ack))
    }
    pub fn start_mailbox(&mut self, write: &mailbox::Write) -> Result<Started, Error> {
        self.available()?;
        let shape = match write.stage {
            1 | 3 => write.offset == 0 && write.count == 1,
            2 => write.offset == 1 && write.count == 15,
            _ => false,
        };
        if !shape || write.sequence == 0 {
            return Err(Error::InvalidWrite);
        }
        for (i, word) in write.words[..write.count].iter().enumerate() {
            // ColdFire rustc_codegen_gcc currently lowers swap_bytes to a
            // recursive __bswapsi2 helper. Byte stores also prevent combining
            // this serialization back into that broken intrinsic. The driver
            // exclusively owns this buffer until start accepts the transfer.
            for byte in 0..4 {
                unsafe {
                    core::ptr::write_volatile(
                        self.buffer.as_mut_ptr().add(4 * i + byte),
                        (word >> (8 * byte)) as u8,
                    );
                }
            }
        }
        self.start(
            3,
            write.offset,
            write.count * 4,
            Target::Mailbox {
                sequence: write.sequence,
                stage: write.stage,
            },
        )
    }
    fn start(
        &mut self,
        window: u32,
        destination: u32,
        bytes: usize,
        target: Target,
    ) -> Result<Started, Error> {
        let (result, id) = unsafe {
            self.transport
                .start(window, destination, self.buffer.as_ptr(), bytes as u32)
        };
        match result {
            -1 => Ok(Started::Busy),
            -2 => Err(Error::Rejected),
            0 if id > self.last_id => {
                self.last_id = id;
                self.pending = Some((id, target));
                Ok(Started::Accepted)
            }
            _ => {
                self.uncertain = true;
                Err(Error::Uncertain)
            }
        }
    }
    /// Failed is drained but unsuccessful: quarantine the load and request detach,
    /// never feed it into an image/mailbox ACK. Uncertain freezes this buffer until
    /// reboot; no caller timeout or identity mismatch may permit reuse.
    pub fn poll(&mut self) -> Result<Progress, Error> {
        if self.uncertain {
            return Err(Error::Uncertain);
        }
        let Some((id, target)) = self.pending else {
            return Ok(Progress::Idle);
        };
        let mut status = Status::default();
        match self.transport.poll(id, &mut status) {
            0 => Ok(Progress::Pending),
            1 if status.state == 2 || status.state == 3 => {
                self.pending = None;
                if status.state == 2 {
                    Ok(Progress::Complete(Completed { target }))
                } else {
                    Ok(Progress::Failed(status.err))
                }
            }
            _ => {
                self.uncertain = true;
                Err(Error::Uncertain)
            }
        }
    }
}

impl<T: crate::instrument_lease::LeaseTransport> Driver<T> {
    pub(crate) fn claim_link(&mut self) -> Result<bool, crate::instrument_lease::Error> {
        if self.uncertain {
            return Err(crate::instrument_lease::Error::Uncertain);
        }
        if self.pending.is_some() {
            return Err(crate::instrument_lease::Error::Busy);
        }
        let result = self.transport.claim();
        if result == Err(crate::instrument_lease::Error::Uncertain) {
            self.uncertain = true;
        }
        result
    }
    pub(crate) fn release_link(&mut self) -> Result<(), crate::instrument_lease::Error> {
        if self.uncertain {
            return Err(crate::instrument_lease::Error::Uncertain);
        }
        if self.pending.is_some() {
            return Err(crate::instrument_lease::Error::Busy);
        }
        let result = self.transport.release();
        if result == Err(crate::instrument_lease::Error::Uncertain) {
            self.uncertain = true;
        }
        result
    }
}

#[cfg(test)]
mod tests;
