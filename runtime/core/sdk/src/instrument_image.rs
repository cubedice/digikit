//! V2 image/BSS transfer sequencing. The caller drives the actual link and must
//! report an ACK only after the exact write has finished. No DSP row is exposed
//! here. The borrowed relocated image must outlive all link reads, even on error.
//! The image is a relocated copy in memory, or a [`Source`] that relocates it one
//! chunk at a time (`instrument_stream`: from the file, with no copy).
use crate::instrument_admission::Grant;
use crate::instrument_load::{Command, Loads};
use crate::instrument_registry::{Error as LoadError, Ticket};

/// Larger transfers amortize the UI task's scheduling and acknowledgement
/// overhead while keeping the link's in-flight source bounded and stable.
pub const CHUNK: usize = 4096;
static ZERO: [u8; CHUNK] = [0; CHUNK];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Load(LoadError),
    NotDrained,
    ImageSize,
    WrongAck,
    NotReady,
    Stopped,
    /// The source could not give a chunk, or what it gave was not the admitted image. The transfer offers no further write; recover with
    /// `failed` (detach): nothing it sent is ever initialized.
    Source,
}

/// The relocated image, one chunk at a time. `fill` is called once for each chunk, in order: for the first when the transfer begins and
/// for each next one when the write before it is acknowledged. The transfer holds the chunk until its own write is acknowledged, so a
/// retried write sends the same bytes.
pub trait Source {
    /// Write the relocated image bytes `[offset, offset + out.len())` into `out`.
    fn fill(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()>;
    /// Called once the last chunk of the image was acknowledged: whether every chunk held the admitted bytes. False stops the transfer
    /// before the BSS is cleared, and before any initialization.
    fn complete(&mut self) -> bool {
        true
    }
}

enum Image<'a> {
    /// A relocated copy in memory.
    Copy(&'a [u8]),
    /// The chunk at the cursor, in `chunk`.
    Stream {
        source: &'a mut dyn Source,
        chunk: &'a mut [u8; CHUNK],
    },
}

/// ACK identity is the exact load generation and destination span. Retries of
/// a pending write use identical bytes. A different span is never accepted.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Ack {
    pub ticket: Ticket,
    pub address: u32,
    pub bytes: u32,
}
#[derive(Debug, PartialEq, Eq)]
pub struct Write<'a> {
    pub ack: Ack,
    pub data: &'a [u8],
}

pub struct Transfer<'a> {
    ticket: Ticket,
    image: Image<'a>,
    image_len: u32,
    base: u32,
    end: u32,
    cursor: u32,
    stopped: bool,
    /// The source failed: no further write is offered (`Error::Source`).
    source_failed: bool,
}
impl<'a> Transfer<'a> {
    /// Admission and relocation must already have succeeded. Bind the exact
    /// allocator-owned grant before changing the load phase or offering a write.
    pub fn begin(
        loads: &mut Loads,
        ticket: Ticket,
        grant: &Grant,
        relocated: &'a [u8],
        prior_writes_drained: bool,
    ) -> Result<Self, Error> {
        Self::checked(loads, ticket, grant, relocated.len(), prior_writes_drained)?;
        Self::started(loads, ticket, grant, Image::Copy(relocated))
    }
    /// As `begin`, with the image (`image_len` bytes) relocated by `source` one chunk at a time into `chunk`. The first chunk is filled
    /// before the load phase changes, so a source that cannot give it leaves the load as it was.
    pub fn begin_streamed(
        loads: &mut Loads,
        ticket: Ticket,
        grant: &Grant,
        source: &'a mut dyn Source,
        image_len: u32,
        chunk: &'a mut [u8; CHUNK],
        prior_writes_drained: bool,
    ) -> Result<Self, Error> {
        Self::checked(
            loads,
            ticket,
            grant,
            image_len as usize,
            prior_writes_drained,
        )?;
        let first = (image_len as usize).min(CHUNK);
        source
            .fill(0, &mut chunk[..first])
            .map_err(|_| Error::Source)?;
        Self::started(loads, ticket, grant, Image::Stream { source, chunk })
    }
    fn checked(
        loads: &Loads,
        ticket: Ticket,
        grant: &Grant,
        image_len: usize,
        prior_writes_drained: bool,
    ) -> Result<(), Error> {
        if !prior_writes_drained {
            return Err(Error::NotDrained);
        }
        loads
            .check_memory_grant(ticket, grant)
            .map_err(Error::Load)?;
        if image_len % 4 != 0 || image_len != (grant.image.end - grant.image.start) as usize {
            return Err(Error::ImageSize);
        }
        Ok(())
    }
    fn started(
        loads: &mut Loads,
        ticket: Ticket,
        grant: &Grant,
        image: Image<'a>,
    ) -> Result<Self, Error> {
        loads.start(ticket).map_err(Error::Load)?;
        Ok(Self {
            ticket,
            image,
            image_len: grant.image.end - grant.image.start,
            base: grant.image.start,
            end: grant.state.start,
            cursor: grant.image.start,
            stopped: false,
            source_failed: false,
        })
    }
    fn check(&self, loads: &Loads) -> Result<(), Error> {
        if self.stopped {
            return Err(Error::Stopped);
        }
        loads.check_image_transfer(self.ticket).map_err(Error::Load)
    }
    /// Image first, then BSS and alignment padding; never writes instance states.
    /// Call repeatedly while the link is busy to retrieve the identical write.
    pub fn next(&self, loads: &Loads) -> Result<Option<Write<'_>>, Error> {
        self.check(loads)?;
        if self.source_failed {
            return Err(Error::Source);
        }
        if self.cursor == self.end {
            return Ok(None);
        }
        let offset = (self.cursor - self.base) as usize;
        let len = self.image_len as usize;
        let data = if offset < len {
            let n = (len - offset).min(CHUNK);
            match &self.image {
                Image::Copy(image) => &image[offset..offset + n],
                Image::Stream { chunk, .. } => &chunk[..n],
            }
        } else {
            &ZERO[..((self.end - self.cursor) as usize).min(CHUNK)]
        };
        Ok(Some(Write {
            ack: Ack {
                ticket: self.ticket,
                address: self.cursor,
                bytes: data.len() as u32,
            },
            data,
        }))
    }
    /// A streamed image gets its next chunk here. When the source fails, the cursor stays on the acknowledged chunk and the error is
    /// `Source`: the transfer never reaches its end, so only `failed` (detach) follows.
    pub fn acknowledged(&mut self, loads: &Loads, ack: Ack) -> Result<(), Error> {
        let current = self.next(loads)?.ok_or(Error::WrongAck)?;
        if current.ack != ack {
            return Err(Error::WrongAck);
        }
        let next = (self.cursor + ack.bytes - self.base) as usize;
        let len = self.image_len as usize;
        if let Image::Stream { source, chunk } = &mut self.image {
            let ok = if next < len {
                source
                    .fill(next as u32, &mut chunk[..(len - next).min(CHUNK)])
                    .is_ok()
            } else {
                // the last chunk of the image (once), or a BSS write
                next > len || source.complete()
            };
            if !ok {
                self.source_failed = true;
                return Err(Error::Source);
            }
        }
        self.cursor += ack.bytes;
        Ok(())
    }
    pub(crate) fn install_ticket(&self, loads: &Loads) -> Result<Ticket, Error> {
        self.check(loads)?;
        if self.cursor != self.end {
            return Err(Error::NotReady);
        }
        Ok(self.ticket)
    }
    /// Row metadata must also be installed and acknowledged before initialization.
    /// An image or zero-fill ACK alone is never an initialization completion.
    pub fn finish(
        &mut self,
        loads: &mut Loads,
        installed: &crate::instrument_mailbox::Installed,
    ) -> Result<Command, Error> {
        self.check(loads)?;
        if self.cursor != self.end || !installed.matches(self.ticket) {
            return Err(Error::NotReady);
        }
        let command = loads.image_acked(self.ticket).map_err(Error::Load)?;
        self.stopped = true;
        Ok(command)
    }
    /// Retain storage and stop issuing writes after an uncertain transfer error.
    /// The transport must still drain prior writes before sending the detach.
    pub fn failed(&mut self, loads: &mut Loads) -> Result<Command, Error> {
        self.check(loads)?;
        let command = loads.timeout(self.ticket).map_err(Error::Load)?;
        self.stopped = true;
        Ok(command)
    }
}

#[cfg(test)]
mod tests;
