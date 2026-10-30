//! Pure, fixed-storage admission planning. The single trusted loader must supply
//! exclusive allocator grants and executable extents from validated image data.
//! Range checks cannot establish allocation ownership or instruction boundaries.
//! This module performs no allocation/transfer and never enables DSP readiness.
use crate::instrument_load::Loads;
use crate::instrument_registry::{Error as LoadError, Ticket, MAX_ROWS};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Range {
    pub start: u32,
    pub end: u32,
}
impl Range {
    fn valid(self) -> bool {
        self.start >= 0x2000_0000 && self.start < self.end && self.end <= 0x7fff_ffff
    }
    fn contains(self, other: Self) -> bool {
        other.valid() && self.start <= other.start && other.end <= self.end
    }
    fn overlaps(self, other: Self) -> bool {
        self.start < other.end && other.start < self.end
    }
}

/// Exclusive natural-byte allocations belonging to this ticket, supplied by the
/// future live allocator integration, never accepted directly from a module file.
pub struct Grant {
    pub ticket: Ticket,
    pub image: Range,
    pub state: Range,
}

pub struct Limits {
    pub l2: Range,
    pub reset_frame: u32,
    /// Caller-reserved scheduler/wrapper instruction cost, excluding clear loop.
    pub reset_overhead: u32,
    pub process_total: u32,
    pub call_depth: u32,
    pub stack_bytes: u32,
}

#[derive(Clone, Copy)]
pub struct Spec {
    pub type_no: u32,
    pub state_base: u32,
    pub state_bytes: u32,
    /// Actual executable byte extent, not the whole image including data.
    pub text: Range,
    pub reset_sw: u32,
    pub process_sw: u32,
    pub reset_instructions: u32,
    pub process_instructions: u32,
    pub call_depth: u32,
    pub stack_bytes: u32,
    pub max_events: u32,
    pub max_controls: u32,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Load(LoadError),
    Grant,
    Types,
    State,
    Executable,
    Budget,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Row {
    pub index: u32,
    pub words: [u32; 16],
    /// Retained for future dispatch admission; the reset row does not publish it.
    pub process_sw: u32,
}

pub struct Plan {
    rows: [Option<Row>; MAX_ROWS],
}
impl Plan {
    pub fn rows(&self) -> &[Option<Row>; MAX_ROWS] {
        &self.rows
    }
}

fn pc(byte: u32) -> u32 {
    0x00b8_0000 + (byte - 0x2000_0000) / 2
}
fn callback(entry: u32, text: Range) -> bool {
    entry >= pc(text.start) && entry.checked_add(3).is_some_and(|end| end <= pc(text.end))
}

/// Validate the whole batch before returning any installable row metadata.
/// Call only before image transfer starts. The returned rows are inert data; the
/// live loader must atomically install them and retain grants through detach.
pub fn prepare(
    loads: &Loads,
    ticket: Ticket,
    grant: &Grant,
    specs: &[Spec],
    limits: &Limits,
) -> Result<Plan, Error> {
    let entries = loads.admission_entries(ticket).map_err(Error::Load)?;
    if grant.ticket != ticket
        || !limits.l2.valid()
        || limits.l2.end > 0x2008_0000
        || !limits.l2.contains(grant.image)
        || !limits.l2.contains(grant.state)
        || grant.image.overlaps(grant.state)
        || grant.image.start % 2 != 0
        || grant.image.end % 2 != 0
        || grant.state.start % 8 != 0
        || grant.state.end % 8 != 0
    {
        return Err(Error::Grant);
    }
    if specs.len() != entries.iter().flatten().count() {
        return Err(Error::Types);
    }
    let mut rows = [None; MAX_ROWS];
    let mut extents: [Option<Range>; MAX_ROWS] = [None; MAX_ROWS];
    let mut process_total = 0u32;
    for spec in specs {
        let (index, entry) = entries
            .iter()
            .enumerate()
            .find_map(|(i, e)| e.filter(|e| e.type_no == spec.type_no).map(|e| (i, e)))
            .ok_or(Error::Types)?;
        if rows[index].is_some() {
            return Err(Error::Types);
        }
        if spec.state_base % 8 != 0
            || spec.state_bytes == 0
            || spec.state_bytes % 4 != 0
            || spec.state_bytes > 4096
        {
            return Err(Error::State);
        }
        let stride = (spec.state_bytes + 7) & !7;
        let end = stride
            .checked_mul(entry.tracks)
            .and_then(|bytes| spec.state_base.checked_add(bytes))
            .ok_or(Error::State)?;
        let state = Range {
            start: spec.state_base,
            end,
        };
        if !grant.state.contains(state)
            || extents.iter().flatten().any(|other| state.overlaps(*other))
        {
            return Err(Error::State);
        }
        if !grant.image.contains(spec.text)
            || spec.text.start % 2 != 0
            || spec.text.end % 2 != 0
            || !callback(spec.reset_sw, spec.text)
            || !callback(spec.process_sw, spec.text)
        {
            return Err(Error::Executable);
        }
        let reset_cost = (spec.state_bytes / 4)
            .checked_mul(3)
            .and_then(|clear| clear.checked_add(limits.reset_overhead))
            .and_then(|cost| cost.checked_add(spec.reset_instructions))
            .ok_or(Error::Budget)?;
        process_total = spec
            .process_instructions
            .checked_mul(entry.tracks)
            .and_then(|cost| process_total.checked_add(cost))
            .ok_or(Error::Budget)?;
        if limits.reset_overhead == 0
            || spec.reset_instructions == 0
            || spec.process_instructions == 0
            || reset_cost > limits.reset_frame
            || process_total > limits.process_total
            || spec.call_depth > limits.call_depth
            || spec.stack_bytes % 4 != 0
            || spec.stack_bytes > limits.stack_bytes
            || spec.max_events == 0
            || spec.max_events > 8
            || spec.max_controls == 0
            || spec.max_controls > 32
        {
            return Err(Error::Budget);
        }
        rows[index] = Some(Row {
            index: index as u32,
            words: [
                1,
                ticket.owner(),
                ticket.generation(),
                entry.tracks,
                spec.state_base,
                spec.state_bytes / 4,
                state.start,
                state.end,
                spec.reset_sw,
                pc(spec.text.start),
                pc(spec.text.end),
                0,
                0,
                0,
                0,
                0,
            ],
            process_sw: spec.process_sw,
        });
        extents[index] = Some(state);
    }
    Ok(Plan { rows })
}

#[cfg(test)]
mod tests;
