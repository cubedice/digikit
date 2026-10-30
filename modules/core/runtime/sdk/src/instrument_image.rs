//! V2 image/BSS transfer sequencing. The caller drives the actual link and must
//! report an ACK only after the exact write has finished. No DSP row is exposed
//! here. The borrowed relocated image must outlive all link reads, even on error.
use crate::instrument_admission::Grant;
use crate::instrument_load::{Command, Loads};
use crate::instrument_registry::{Error as LoadError, Ticket};

const CHUNK: usize = 256;
static ZERO: [u8; CHUNK] = [0; CHUNK];

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Load(LoadError),
    NotDrained,
    ImageSize,
    WrongAck,
    NotReady,
    Stopped,
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
    image: &'a [u8],
    base: u32,
    end: u32,
    cursor: u32,
    stopped: bool,
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
        if !prior_writes_drained {
            return Err(Error::NotDrained);
        }
        loads
            .check_memory_grant(ticket, grant)
            .map_err(Error::Load)?;
        if relocated.len() % 4 != 0
            || relocated.len() != (grant.image.end - grant.image.start) as usize
        {
            return Err(Error::ImageSize);
        }
        loads.start(ticket).map_err(Error::Load)?;
        Ok(Self {
            ticket,
            image: relocated,
            base: grant.image.start,
            end: grant.state.start,
            cursor: grant.image.start,
            stopped: false,
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
        if self.cursor == self.end {
            return Ok(None);
        }
        let offset = (self.cursor - self.base) as usize;
        let data = if offset < self.image.len() {
            &self.image[offset..(offset + CHUNK).min(self.image.len())]
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
    pub fn acknowledged(&mut self, loads: &Loads, ack: Ack) -> Result<(), Error> {
        let current = self.next(loads)?.ok_or(Error::WrongAck)?;
        if current.ack != ack {
            return Err(Error::WrongAck);
        }
        self.cursor += ack.bytes;
        Ok(())
    }
    /// Row metadata must also be installed and acknowledged before initialization.
    /// An image or zero-fill ACK alone is never an initialization completion.
    pub fn finish(&mut self, loads: &mut Loads, rows_installed: bool) -> Result<Command, Error> {
        self.check(loads)?;
        if self.cursor != self.end || !rows_installed {
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
