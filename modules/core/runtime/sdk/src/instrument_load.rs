//! V2 load sequencing, independent of the transfer implementation. Only the
//! loader's owning task may call this API. A command describes work to schedule,
//! never proof that the DSP performed it. Feed completions back with the exact
//! ticket and expected operation. No timeout permits memory/row reuse.
use crate::instrument_registry::{Capacity, Entry, Error, Registry, Request, Ticket, MAX_ROWS};

const RESERVED: u32 = 1;
const IMAGE: u32 = 2;
const INITIALIZE: u32 = 3;
const LIVE: u32 = 4;
const DETACH: u32 = 5;
const QUARANTINED: u32 = 6;
const RELEASE: u32 = 7;

mod memory;
pub use memory::{Ledger, MemoryError, MemoryRequest};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    SendImage(Ticket),
    Initialize(Ticket),
    Detach(Ticket),
    /// The caller may now free image/state storage. For a memory-backed ticket,
    /// call `release_memory` before freeing/reusing the ColdFire module record.
    Release(Ticket),
    /// The caller may now expose the module to MIDI and machine selection.
    Publish(Ticket),
}

#[derive(Clone, Copy)]
struct Pending {
    ticket: Ticket,
    phase: u32,
    allocation: Option<memory::Allocation>,
}

/// The only owner of its registry, preventing a caller from bypassing transfer
/// ordering with a direct registry publish/abort. Fixed storage for eight modules.
pub struct Loads {
    registry: Registry,
    pending: [Option<Pending>; MAX_ROWS],
}
impl Loads {
    /// Cold boot only. Recreating this while old DSP work is pending is unsafe.
    pub const fn new(track_limit: u32) -> Self {
        Self {
            registry: Registry::new(track_limit),
            pending: [None; MAX_ROWS],
        }
    }

    pub fn capacity(&self) -> Capacity {
        self.registry.capacity()
    }
    pub fn lookup(&self, type_no: u32) -> Option<Entry> {
        self.registry.lookup(type_no)
    }

    pub fn reserve(&mut self, owner: u32, requests: &[Request]) -> Result<Ticket, Error> {
        if self
            .pending
            .iter()
            .flatten()
            .any(|p| p.ticket.owner() == owner)
        {
            return Err(Error::OwnerBusy);
        }
        let mut free = None;
        for (i, p) in self.pending.iter().enumerate() {
            if p.is_none() {
                free = Some(i);
                break;
            }
        }
        let slot = free.ok_or(Error::Capacity)?;
        let ticket = self.registry.reserve(owner, requests)?;
        self.pending[slot] = Some(Pending {
            ticket,
            phase: RESERVED,
            allocation: None,
        });
        Ok(ticket)
    }

    /// Mark the transfer as in flight BEFORE calling the link. Even an immediate
    /// send failure is cancelled through detach unless no work was ever started.
    pub fn start(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let slot = self.at(ticket, RESERVED)?;
        self.set_phase(slot, IMAGE);
        Ok(Command::SendImage(ticket))
    }

    pub(crate) fn check_image_transfer(&self, ticket: Ticket) -> Result<(), Error> {
        self.at(ticket, IMAGE).map(|_| ())
    }

    pub(crate) fn install_rows(&self, ticket: Ticket) -> Result<u32, Error> {
        self.check_image_transfer(ticket)?;
        self.registry.row_mask(ticket)
    }

    pub fn image_acked(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let slot = self.at(ticket, IMAGE)?;
        self.set_phase(slot, INITIALIZE);
        Ok(Command::Initialize(ticket))
    }

    /// A byte-transfer ACK is insufficient: all instance state and reset callbacks
    /// must have completed before this separate initialization ACK is supplied.
    pub fn initialized(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let slot = self.at(ticket, INITIALIZE)?;
        self.registry.publish(ticket)?;
        self.set_phase(slot, LIVE);
        Ok(Command::Publish(ticket))
    }

    /// Begin retirement of a published instance, never a prepared/in-flight load.
    pub fn retire(&mut self, ticket: Ticket) -> Result<Command, Error> {
        self.at(ticket, LIVE)?;
        self.cancel(ticket)
    }

    /// Pre-transfer cancellation releases immediately. Once work may be in flight,
    /// hide admissions and retain every row until a detach ACK; failure to send a
    /// detach command must be reported via `timeout`, not by freeing storage.
    pub fn cancel(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let (slot, phase) = self.find(ticket)?;
        if phase == RESERVED {
            self.registry.abort(ticket)?;
            self.begin_release(slot);
            return Ok(Command::Release(ticket));
        }
        if phase == IMAGE || phase == INITIALIZE {
            self.registry.begin_cancel(ticket)?;
        } else if phase == LIVE {
            self.registry.begin_retire(ticket)?;
        } else {
            return Err(Error::WrongPhase);
        }
        self.set_phase(slot, DETACH);
        Ok(Command::Detach(ticket))
    }

    /// Any uncertain in-flight failure quarantines the rows. Return a detach
    /// request, but do not expose or release the module while awaiting its ACK.
    pub fn timeout(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let (slot, phase) = self.find(ticket)?;
        if phase == RESERVED || phase == RELEASE {
            return Err(Error::WrongPhase);
        }
        if phase == IMAGE || phase == INITIALIZE {
            self.registry.begin_cancel(ticket)?;
        } else if phase == LIVE {
            self.registry.begin_retire(ticket)?;
        }
        self.set_phase(slot, QUARANTINED);
        Ok(Command::Detach(ticket))
    }

    /// Completion must mean both callbacks AND earlier writes are quiescent.
    /// The transport must drain pending transfers before acknowledging detachment;
    /// otherwise a late write could overwrite a replacement's code/state.
    pub fn detached(&mut self, ticket: Ticket) -> Result<Command, Error> {
        let (slot, phase) = self.find(ticket)?;
        if phase != DETACH && phase != QUARANTINED {
            return Err(Error::WrongPhase);
        }
        self.registry.finish_retire(ticket)?;
        self.begin_release(slot);
        Ok(Command::Release(ticket))
    }

    fn begin_release(&mut self, slot: usize) {
        if self.pending[slot].is_some_and(|p| p.allocation.is_some()) {
            self.set_phase(slot, RELEASE);
        } else {
            self.pending[slot] = None;
        }
    }

    pub(crate) fn admission_entries(
        &self,
        ticket: Ticket,
    ) -> Result<[Option<Entry>; MAX_ROWS], Error> {
        self.at(ticket, RESERVED)?;
        self.registry.reserved_entries(ticket)
    }

    /// Bind a mailbox transaction to the current phase and exact physical rows.
    pub(crate) fn command_rows(&self, command: Command) -> Result<u32, Error> {
        let ticket = match command {
            Command::Initialize(ticket) => {
                self.at(ticket, INITIALIZE)?;
                ticket
            }
            Command::Detach(ticket) => {
                let (_, phase) = self.find(ticket)?;
                if phase != DETACH && phase != QUARANTINED {
                    return Err(Error::WrongPhase);
                }
                ticket
            }
            _ => return Err(Error::WrongPhase),
        };
        self.registry.row_mask(ticket)
    }

    fn find(&self, ticket: Ticket) -> Result<(usize, u32), Error> {
        for (i, p) in self.pending.iter().enumerate() {
            if let Some(p) = p {
                if p.ticket == ticket {
                    return Ok((i, p.phase));
                }
            }
        }
        Err(Error::StaleTicket)
    }
    fn at(&self, ticket: Ticket, phase: u32) -> Result<usize, Error> {
        let (i, current) = self.find(ticket)?;
        if current != phase {
            return Err(Error::WrongPhase);
        }
        Ok(i)
    }
    fn set_phase(&mut self, slot: usize, phase: u32) {
        if let Some(p) = &mut self.pending[slot] {
            p.phase = phase;
        }
    }
}

#[cfg(test)]
mod tests;
