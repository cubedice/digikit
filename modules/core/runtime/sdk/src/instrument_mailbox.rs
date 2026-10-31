//! Single-owner V2 mailbox client. Logical DSP words, not host-endian wire bytes.
//! The caller must retain this instance for the entire DSP boot and serialize all
//! window-3 traffic through it. This does not enable V2 or schedule SPI writes.
//! DSP lifecycle/install operations remain gated by trusted admission.
//! This client does not establish live SPI drainage or enable audio dispatch.
use crate::instrument_load::{Command, Loads};
use crate::instrument_registry::{Error as LoadError, Ticket};

const MAGIC: u32 = 0x49524731;
const CLEAR: u32 = 1;
const BODY: u32 = 2;
const COMMIT: u32 = 3;
const REPLY: u32 = 4;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Busy,
    NotDrained,
    SequenceExhausted,
    WrongTransfer,
    InvalidReply,
    MismatchedReply,
    Rejected(u32),
    MissingCapability,
    Load(LoadError),
    Image(crate::instrument_image::Error),
    InvalidPlan,
}

/// Retry exactly this write until its own transfer ACK. Its token must be echoed
/// by the caller only after dl_poll confirms this transfer, never an older one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Write {
    pub sequence: u32,
    pub stage: u32,
    pub offset: u32,
    pub count: usize,
    pub words: [u32; 16],
}

#[derive(Clone, Copy)]
struct Pending {
    ticket: Ticket,
    operation: u32,
    rows: u32,
    row: u32,
    sequence: u32,
    stage: u32,
}

/// Created only after every row has a matching successful DSP install reply.
/// This receipt is boot-local, as are its Mailbox, Loads and Transfer owners.
#[derive(Debug, PartialEq, Eq)]
pub struct Installed {
    ticket: Ticket,
}
impl Installed {
    pub(crate) fn matches(&self, ticket: Ticket) -> bool {
        self.ticket == ticket
    }
    #[cfg(test)]
    pub(crate) fn test_receipt(ticket: Ticket) -> Self {
        Self { ticket }
    }
}

pub struct Mailbox {
    sequence: u32,
    pending: Option<Pending>,
    metadata: [[u32; 8]; 8],
    installed: Option<Installed>,
}

impl Mailbox {
    /// Cold boot only. Never recreate to recover from a timeout or sequence limit.
    pub const fn new() -> Self {
        Self {
            sequence: 0,
            pending: None,
            metadata: [[0; 8]; 8],
            installed: None,
        }
    }

    /// Caller must drain previous image/mailbox transfers before starting,
    /// especially before detach. A false drain confirmation changes nothing.
    pub fn begin(
        &mut self,
        command: Command,
        loads: &Loads,
        prior_writes_drained: bool,
    ) -> Result<(), Error> {
        if self.pending.is_some() {
            return Err(Error::Busy);
        }
        if !prior_writes_drained {
            return Err(Error::NotDrained);
        }
        let rows = loads.command_rows(command).map_err(Error::Load)?;
        self.sequence
            .checked_add(rows.count_ones())
            .ok_or(Error::SequenceExhausted)?;
        let (ticket, operation) = match command {
            Command::Initialize(ticket) => (ticket, 2),
            Command::Detach(ticket) => (ticket, 3),
            _ => return Err(Error::Load(LoadError::WrongPhase)),
        };
        self.sequence += 1;
        self.pending = Some(Pending {
            ticket,
            operation,
            rows,
            row: rows.trailing_zeros(),
            sequence: self.sequence,
            stage: CLEAR,
        });
        Ok(())
    }

    /// Install only after every image/BSS write has been acknowledged. Plan must
    /// match the allocator grant and every reserved physical row for this ticket.
    pub fn begin_install(
        &mut self,
        image: &crate::instrument_image::Transfer<'_>,
        plan: &crate::instrument_admission::Plan,
        loads: &Loads,
        prior_writes_drained: bool,
    ) -> Result<(), Error> {
        if self.pending.is_some() || self.installed.is_some() {
            return Err(Error::Busy);
        }
        if !prior_writes_drained {
            return Err(Error::NotDrained);
        }
        let ticket = image.install_ticket(loads).map_err(Error::Image)?;
        loads
            .check_memory_grant(ticket, &plan.grant)
            .map_err(Error::Load)?;
        let rows = loads.install_rows(ticket).map_err(Error::Load)?;
        let mut mask = 0u32;
        let mut metadata = [[0; 8]; 8];
        for row in plan.rows().iter().flatten() {
            if row.index >= 8
                || mask & (1 << row.index) != 0
                || row.words[..3] != [1, ticket.owner(), ticket.generation()]
            {
                return Err(Error::InvalidPlan);
            }
            mask |= 1 << row.index;
            metadata[row.index as usize].copy_from_slice(&row.words[3..11]);
        }
        if mask != rows {
            return Err(Error::InvalidPlan);
        }
        self.sequence
            .checked_add(rows.count_ones())
            .ok_or(Error::SequenceExhausted)?;
        self.sequence += 1;
        self.metadata = metadata;
        self.pending = Some(Pending {
            ticket,
            operation: 4,
            rows,
            row: rows.trailing_zeros(),
            sequence: self.sequence,
            stage: CLEAR,
        });
        Ok(())
    }

    pub fn take_installed(&mut self) -> Option<Installed> {
        self.installed.take()
    }

    pub fn write(&self) -> Option<Write> {
        let p = self.pending?;
        if p.stage == REPLY {
            return None;
        }
        let mut words = [0; 16];
        let (offset, count) = if p.stage == CLEAR {
            (0, 1)
        } else if p.stage == COMMIT {
            words[0] = MAGIC;
            (0, 1)
        } else {
            words[..6].copy_from_slice(&[
                1,
                p.sequence,
                p.ticket.owner(),
                p.ticket.generation(),
                p.operation,
                p.row,
            ]);
            if p.operation == 4 {
                words[6..14].copy_from_slice(&self.metadata[p.row as usize]);
            }
            words[14] = words[..14].iter().fold(MAGIC, |sum, word| sum ^ word);
            (1, 15)
        };
        Some(Write {
            sequence: p.sequence,
            stage: p.stage,
            offset,
            count,
            words,
        })
    }

    pub fn write_acked(&mut self, sequence: u32, stage: u32) -> Result<(), Error> {
        let p = self.pending.as_mut().ok_or(Error::WrongTransfer)?;
        if p.sequence != sequence || p.stage != stage || stage == REPLY {
            return Err(Error::WrongTransfer);
        }
        p.stage += 1;
        Ok(())
    }

    /// Validate the complete 16-word instrument reply. No transfer ACK can reach
    /// this path. Publish/release only after every physical row succeeds. Required
    /// capability bits: bit 0 reset completed, bit 1 detach, bit 2 install.
    /// Bootstrap status 1/capabilities 0 cannot authorize either transition.
    pub fn reply(
        &mut self,
        words: &[u32; 16],
        loads: &mut Loads,
    ) -> Result<Option<Command>, Error> {
        let mut p = self.pending.ok_or(Error::MismatchedReply)?;
        if p.stage != REPLY {
            return Err(Error::WrongTransfer);
        }
        let (mut a, mut b) = (0u32, 0u32);
        for word in &words[..15] {
            a = a.wrapping_add(*word);
            b = b.wrapping_add(a);
        }
        if words[0] != 0x494ea101 || words[4] != 0x49525231 || words[15] != a ^ b || words[12..15] != [0; 3] {
            return Err(Error::InvalidReply);
        }
        if words[5..10]
            != [
                p.sequence,
                p.ticket.owner(),
                p.ticket.generation(),
                p.operation,
                p.row,
            ]
        {
            return Err(Error::MismatchedReply);
        }
        if words[10] != 0 {
            return Err(Error::Rejected(words[10]));
        }
        let capability = match p.operation {
            2 => 1,
            3 => 2,
            _ => 4,
        };
        if words[11] & capability == 0 {
            return Err(Error::MissingCapability);
        }
        let command = if p.operation == 2 {
            Command::Initialize(p.ticket)
        } else {
            Command::Detach(p.ticket)
        };
        if p.operation == 4 {
            loads.install_rows(p.ticket).map_err(Error::Load)?;
        } else {
            loads.command_rows(command).map_err(Error::Load)?;
        }
        p.rows &= !(1 << p.row);
        if p.rows == 0 {
            if p.operation == 4 {
                self.installed = Some(Installed { ticket: p.ticket });
                self.pending = None;
                return Ok(None);
            }
            let completion = if p.operation == 2 {
                loads.initialized(p.ticket)
            } else {
                loads.detached(p.ticket)
            }
            .map_err(Error::Load)?;
            self.pending = None;
            return Ok(Some(completion));
        }
        self.sequence += 1; // whole batch checked for exhaustion in begin
        p.sequence = self.sequence;
        p.row = p.rows.trailing_zeros();
        p.stage = CLEAR;
        self.pending = Some(p);
        Ok(None)
    }

    /// On timeout/rejection, first drain all old writes. Quarantine the entire
    /// module even if some rows succeeded; return a fresh detach request. Do not
    /// reset the sequence, release memory, or reuse rows on this path.
    pub fn abandon_after_drain(
        &mut self,
        loads: &mut Loads,
        drained: bool,
    ) -> Result<Command, Error> {
        if !drained {
            return Err(Error::NotDrained);
        }
        let p = self.pending.ok_or(Error::MismatchedReply)?;
        let command = loads.timeout(p.ticket).map_err(Error::Load)?;
        self.pending = None;
        Ok(command)
    }
}

#[cfg(test)]
mod tests;
