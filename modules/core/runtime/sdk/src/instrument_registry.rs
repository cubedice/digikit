//! Bounded V2 registration ownership for the loader's single task context.
//! This tracks reservations, not DSP addresses or the transport itself. The loader
//! must publish only after DSP initialization ACK, and release retiring rows only
//! after a matching DSP detach/reset ACK. A timeout must retain the reservation.
//! No device capability is enabled by this module. Never recreate it while DSP
//! callbacks from the previous registry may still be running.

pub const MAX_ROWS: usize = 8;
pub const MAX_TRACKS_PER_TYPE: u32 = 16;
const FREE: u32 = 0;
const RESERVED: u32 = 1;
const PUBLISHED: u32 = 2;
const RETIRING: u32 = 3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Request {
    pub type_no: u32,
    pub tracks: u32,
}

/// One module's whole registration transaction. Private fields keep callers from inventing
/// generations; the future transport must bind replies to this ticket.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Ticket {
    owner: u32,
    generation: u32,
}
impl Ticket {
    pub fn owner(self) -> u32 {
        self.owner
    }
    pub fn generation(self) -> u32 {
        self.generation
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Entry {
    pub owner: u32,
    pub generation: u32,
    pub type_no: u32,
    pub tracks: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Row {
    entry: Entry,
    phase: u32,
}
const EMPTY: Row = Row {
    entry: Entry {
        owner: 0,
        generation: 0,
        type_no: 0,
        tracks: 0,
    },
    phase: FREE,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Capacity {
    pub rows_limit: u32,
    pub rows_used: u32,
    pub tracks_limit: u32,
    pub tracks_used: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    DuplicateType,
    OwnerBusy,
    Capacity,
    GenerationExhausted,
    StaleTicket,
    WrongPhase,
}

/// Fixed storage; every scan is bounded by eight rows. No interrupt/concurrent use.
pub struct Registry {
    rows: [Row; MAX_ROWS],
    track_limit: u32,
    last_generation: u32,
}
impl Registry {
    /// `track_limit` counts allocated per-type instances, not currently held notes.
    /// Device admission owns type-number ranges and V1/V2 cross-registry conflicts.
    pub const fn new(track_limit: u32) -> Self {
        Self {
            rows: [EMPTY; MAX_ROWS],
            track_limit,
            last_generation: 0,
        }
    }

    /// Reserved and retiring rows still consume capacity. No row is reusable until
    /// rollback before publication, or confirmed DSP detachment after publication.
    pub fn capacity(&self) -> Capacity {
        let mut c = Capacity {
            rows_limit: MAX_ROWS as u32,
            rows_used: 0,
            tracks_limit: self.track_limit,
            tracks_used: 0,
        };
        for row in &self.rows {
            if row.phase != FREE {
                c.rows_used += 1;
                c.tracks_used += row.entry.tracks;
            }
        }
        c
    }

    /// Reserve an entire module or change nothing. One outstanding transaction per
    /// module slot; all requested types remain invisible until `publish`.
    pub fn reserve(&mut self, owner: u32, requests: &[Request]) -> Result<Ticket, Error> {
        if requests.is_empty() || requests.len() > MAX_ROWS {
            return Err(Error::Invalid);
        }
        let mut tracks = 0;
        for (i, request) in requests.iter().enumerate() {
            if request.tracks == 0
                || request.tracks > MAX_TRACKS_PER_TYPE
                || request.type_no == u32::MAX
            {
                return Err(Error::Invalid);
            }
            tracks += request.tracks;
            for previous in &requests[..i] {
                if previous.type_no == request.type_no {
                    return Err(Error::DuplicateType);
                }
            }
            for row in &self.rows {
                if row.phase != FREE {
                    if row.entry.owner == owner {
                        return Err(Error::OwnerBusy);
                    }
                    if row.entry.type_no == request.type_no {
                        return Err(Error::DuplicateType);
                    }
                }
            }
        }
        let c = self.capacity();
        if requests.len() as u32 > c.rows_limit - c.rows_used
            || tracks > c.tracks_limit.saturating_sub(c.tracks_used)
        {
            return Err(Error::Capacity);
        }
        let generation = self
            .last_generation
            .checked_add(1)
            .ok_or(Error::GenerationExhausted)?;
        let ticket = Ticket { owner, generation };
        // No fallible operations after this point.
        let mut next = 0;
        for row in &mut self.rows {
            if row.phase == FREE && next < requests.len() {
                let r = requests[next];
                *row = Row {
                    entry: Entry {
                        owner,
                        generation,
                        type_no: r.type_no,
                        tracks: r.tracks,
                    },
                    phase: RESERVED,
                };
                next += 1;
            }
        }
        self.last_generation = generation;
        Ok(ticket)
    }

    /// Called after code, state and DSP rows have been acknowledged as initialized.
    pub fn publish(&mut self, ticket: Ticket) -> Result<(), Error> {
        self.transition(ticket, RESERVED, PUBLISHED)
    }

    /// Called only before any DSP callbacks have been made reachable. If DSP work
    /// has started, the loader must quiesce it before using this rollback.
    pub fn abort(&mut self, ticket: Ticket) -> Result<(), Error> {
        self.transition(ticket, RESERVED, FREE)
    }

    /// A reserved module has started DSP work and can no longer be rolled back
    /// immediately. Retain its rows until a matching detach acknowledgement.
    pub fn begin_cancel(&mut self, ticket: Ticket) -> Result<(), Error> {
        self.transition(ticket, RESERVED, RETIRING)
    }

    /// Hide the module from new admissions before requesting DSP detachment.
    pub fn begin_retire(&mut self, ticket: Ticket) -> Result<(), Error> {
        self.transition(ticket, PUBLISHED, RETIRING)
    }

    /// Release after the matching DSP detach/reset completion, never on timeout.
    pub fn finish_retire(&mut self, ticket: Ticket) -> Result<(), Error> {
        self.transition(ticket, RETIRING, FREE)
    }

    pub fn lookup(&self, type_no: u32) -> Option<Entry> {
        for row in &self.rows {
            if row.phase == PUBLISHED && row.entry.type_no == type_no {
                return Some(row.entry);
            }
        }
        None
    }

    fn transition(&mut self, ticket: Ticket, from: u32, to: u32) -> Result<(), Error> {
        let mut found = false;
        for row in &self.rows {
            if row.phase != FREE
                && row.entry.owner == ticket.owner
                && row.entry.generation == ticket.generation
            {
                if row.phase != from {
                    return Err(Error::WrongPhase);
                }
                found = true;
            }
        }
        if !found {
            return Err(Error::StaleTicket);
        }
        for row in &mut self.rows {
            if row.phase != FREE
                && row.entry.owner == ticket.owner
                && row.entry.generation == ticket.generation
            {
                if to == FREE {
                    *row = EMPTY;
                } else {
                    row.phase = to;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests;
