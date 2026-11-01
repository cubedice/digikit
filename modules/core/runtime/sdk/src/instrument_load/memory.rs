//! Reservations in the caller's shared DSP ledger, bound to load generations.
//! The live loader must pass its actual mod_off/mod_len arrays, exclusively from
//! its owning task. This is not a second allocator and does not start transfers.
use super::*;
use crate::dsp_memory::first_fit;
use crate::instrument_admission::{Grant, Range};

pub struct Ledger<'a> {
    pub base: u32,
    pub bytes: u32,
    pub offsets: &'a mut [u32],
    pub lengths: &'a mut [u32],
}

pub struct MemoryRequest {
    pub image_bytes: u32,
    pub bss_bytes: u32,
    /// Sum of per-type, eight-byte-stride instance allocations.
    pub state_bytes: u32,
    pub align: u32,
}

#[derive(Clone, Copy)]
pub(super) struct Allocation {
    base: u32,
    bytes: u32,
    offset: u32,
    len: u32,
    image: Range,
    state: Range,
}

#[derive(Debug, PartialEq, Eq)]
pub enum MemoryError {
    Load(Error),
    Invalid,
    Occupied,
    NoSpace,
    LedgerChanged,
}

impl Loads {
    /// Claim image, BSS and state together, before any transfer. A failure changes
    /// neither the ledger nor the ticket; the caller may retry or cancel it.
    /// The returned grant is derived from the claimed range, not module addresses.
    pub fn reserve_memory(
        &mut self,
        ticket: Ticket,
        ledger: &mut Ledger<'_>,
        request: &MemoryRequest,
    ) -> Result<Grant, MemoryError> {
        let slot = self.at(ticket, RESERVED).map_err(MemoryError::Load)?;
        let owner = ticket.owner() as usize;
        if ledger.offsets.len() != ledger.lengths.len()
            || owner >= ledger.lengths.len()
            || ledger.base < 0x2000_0000
            || ledger
                .base
                .checked_add(ledger.bytes)
                .is_none_or(|end| end > 0x200f_e000)
            || request.image_bytes == 0
            || request.image_bytes % 2 != 0
            || request.state_bytes == 0
            || request.state_bytes % 8 != 0
            || !request.align.is_power_of_two()
        {
            return Err(MemoryError::Invalid);
        }
        if self.pending[slot].unwrap().allocation.is_some() || ledger.lengths[owner] != 0 {
            return Err(MemoryError::Occupied);
        }
        let state_offset = request
            .image_bytes
            .checked_add(request.bss_bytes)
            .and_then(|n| n.checked_add(7))
            .map(|n| n & !7)
            .ok_or(MemoryError::Invalid)?;
        let len = state_offset
            .checked_add(request.state_bytes)
            .and_then(|n| n.checked_add(63))
            .map(|n| n & !63)
            .ok_or(MemoryError::Invalid)?;
        let offset = first_fit(
            ledger.base,
            ledger.bytes,
            len,
            request.align.max(8),
            ledger.offsets,
            ledger.lengths,
        )
        .ok_or(MemoryError::NoSpace)?;
        let start = ledger.base + offset;
        // The shared pool may include upper L2 used by legacy data. V2 code
        // still uses the lower executable alias; constrain this allocation,
        // not the entire pool, and preserve upper-L2 reservation bookkeeping.
        if start.checked_add(len).is_none_or(|end| end > 0x2008_0000) {
            return Err(MemoryError::NoSpace);
        }
        let grant = Grant {
            ticket,
            image: Range {
                start,
                end: start + request.image_bytes,
            },
            state: Range {
                start: start + state_offset,
                end: start + state_offset + request.state_bytes,
            },
        };
        // No fallible work remains. The single loader owns both state and ledger.
        ledger.offsets[owner] = offset;
        ledger.lengths[owner] = len;
        self.pending[slot].as_mut().unwrap().allocation = Some(Allocation {
            base: ledger.base,
            bytes: ledger.bytes,
            offset,
            len,
            image: grant.image,
            state: grant.state,
        });
        Ok(grant)
    }

    pub(crate) fn check_memory_grant(&self, ticket: Ticket, grant: &Grant) -> Result<(), Error> {
        let (slot, _) = self.find(ticket)?;
        let allocation = self.pending[slot]
            .unwrap()
            .allocation
            .ok_or(Error::Invalid)?;
        if grant.ticket != ticket
            || allocation.image != grant.image
            || allocation.state != grant.state
        {
            return Err(Error::Invalid);
        }
        Ok(())
    }

    /// Finish a Release returned by cancel/detached. A forged Release, stale
    /// generation or changed ledger cannot clear memory. Detach completion must
    /// already include draining all earlier writes (see `detached`). Keep the CF
    /// module record occupied until this succeeds. Never call legacy unreserve
    /// for a V2 allocation, including error and timeout paths.
    pub fn release_memory(
        &mut self,
        ticket: Ticket,
        ledger: &mut Ledger<'_>,
    ) -> Result<(), MemoryError> {
        let slot = self.at(ticket, RELEASE).map_err(MemoryError::Load)?;
        let allocation = self.pending[slot]
            .unwrap()
            .allocation
            .ok_or(MemoryError::Invalid)?;
        let owner = ticket.owner() as usize;
        if ledger.base != allocation.base
            || ledger.bytes != allocation.bytes
            || ledger.offsets.len() != ledger.lengths.len()
            || owner >= ledger.lengths.len()
            || ledger.offsets[owner] != allocation.offset
            || ledger.lengths[owner] != allocation.len
        {
            return Err(MemoryError::LedgerChanged);
        }
        ledger.offsets[owner] = 0;
        ledger.lengths[owner] = 0;
        self.pending[slot] = None;
        Ok(())
    }
}

#[cfg(test)]
mod tests;
