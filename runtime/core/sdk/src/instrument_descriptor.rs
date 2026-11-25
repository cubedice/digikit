//! Bounded decoding of V2 descriptors and DKDS7 instrument records. No addresses
//! are dereferenced, allocations made, or runtime capabilities enabled. Decoded
//! callbacks are image offsets, NOT proof of executable instruction boundaries.
//!
//! The format rules (the descriptor's words, a text range, a callback, the limits) are `dkneeds::instrument`'s: the host tools and the
//! runtime's scan read a file with the same ones. This module adds the admission limits of the device and the refusal of each fault.
use dkneeds::instrument as format;

pub const MAGIC: u32 = format::DESCRIPTOR_MAGIC;
pub const VERSION: u32 = format::DESCRIPTOR_VERSION;
pub const DESC_BYTES: usize = format::DESCRIPTOR_BYTES as usize;
/// Opt in to the V5 borrowed status-output extension, the V6 native AMP snapshot (requires STATUS_OUTPUT) and V7 gains at the stock AMP
/// position (requires both). Unknown bits fail closed (`valid_flags`).
pub use format::{valid_flags, NATIVE_AMP, POST_AMP, STATUS_OUTPUT};

const RECORD: usize = format::RECORD_BYTES as usize;
const MAX_RECORDS: usize = format::MAX_INSTRUMENTS as usize;
const TEXT_RANGE: usize = format::TEXT_RANGE_BYTES as usize;
const MAX_TEXT: usize = format::MAX_TEXT_RANGES as usize;

#[derive(Clone, Copy, Debug)]
pub struct Limits {
    pub state_bytes: u32,
    pub cycles_process: u32,
    pub cycles_reset: u32,
    pub call_depth: u32,
    pub stack_bytes: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Descriptor {
    pub flags: u32,
    pub tracks: u32,
    pub state_bytes: u32,
    pub max_events: u32,
    pub max_controls: u32,
    pub cycles_process: u32,
    pub cycles_reset: u32,
    pub call_depth: u32,
    pub stack_bytes: u32,
    pub allocation_bytes: u32,
}

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Size,
    Header,
    Flags,
    Counts,
    Alignment,
    Allocation,
    Budget,
    Records,
    Callback,
    DescriptorRange,
    Relocation,
    Executable,
}
impl core::fmt::Display for Error {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(match self {
            Self::Executable => "instrument V2 executable ranges or callback placement are invalid",
            Self::Size => "instrument V2 descriptor must be exactly 48 bytes",
            Self::Header => "instrument V2 descriptor magic, version or size is incompatible",
            Self::Flags => "instrument V2 descriptor has unsupported flags",
            Self::Counts => {
                "instrument V2 track/event/control count is outside its supported range"
            }
            Self::Alignment => {
                "instrument V2 needs nonzero state and four-byte state/stack alignment"
            }
            Self::Allocation => {
                "instrument V2 state allocation overflows or exceeds admission limits"
            }
            Self::Budget => {
                "instrument V2 budget exceeds admission limits or a callback budget is zero"
            }
            Self::Records => {
                "instrument V2 record table has invalid size or duplicate/invalid keys"
            }
            Self::Callback => "instrument V2 callback is unaligned or outside the image",
            Self::DescriptorRange => "instrument V2 descriptor is unaligned or outside the image",
            Self::Relocation => "instrument V2 relocation is invalid or overlaps a descriptor",
        })
    }
}

impl Descriptor {
    /// Exactly one little-endian DSP descriptor; checked eight-byte instance
    /// stride and aggregate allocation. Limits are admission ceilings, not proof
    /// that the callback obeys its declared budget.
    pub fn parse(bytes: &[u8], limits: Limits) -> Result<Self, Error> {
        if bytes.len() != DESC_BYTES {
            return Err(Error::Size);
        }
        // Volatile byte reads prevent ColdFire codegen from folding this into
        // the toolchain's recursively lowered __bswapsi2 helper. The length
        // check above bounds every descriptor word read below.
        let w = |i: usize| {
            let mut word = 0u32;
            for byte in 0..4 {
                word |= (unsafe { core::ptr::read_volatile(bytes.as_ptr().add(i * 4 + byte)) }
                    as u32)
                    << (8 * byte);
            }
            word
        };
        let mut words = [0u32; 12];
        let mut i = 0;
        while i < 12 {
            words[i] = w(i);
            i += 1;
        }
        // the format's rule of the twelve words, then the admission limits of the device
        let shape = match format::descriptor(&words) {
            Ok(shape) => shape,
            Err(e) => {
                // if-chain, not a `match`: a dense match becomes a jump table, which is not ColdFire code
                return Err(if e == format::DescriptorError::Header {
                    Error::Header
                } else if e == format::DescriptorError::Flags {
                    Error::Flags
                } else if e == format::DescriptorError::Counts {
                    Error::Counts
                } else if e == format::DescriptorError::Alignment {
                    Error::Alignment
                } else {
                    Error::Allocation
                });
            }
        };
        if shape.allocation_bytes > limits.state_bytes {
            return Err(Error::Allocation);
        }
        if !shape.declares_budget()
            || shape.cycles_process > limits.cycles_process
            || shape.cycles_reset > limits.cycles_reset
            || shape.call_depth > limits.call_depth
            || shape.stack_bytes > limits.stack_bytes
        {
            return Err(Error::Budget);
        }
        Ok(Self {
            flags: shape.flags,
            tracks: shape.tracks,
            state_bytes: shape.state_bytes,
            max_events: shape.max_events,
            max_controls: shape.max_controls,
            cycles_process: shape.cycles_process,
            cycles_reset: shape.cycles_reset,
            call_depth: shape.call_depth,
            stack_bytes: shape.stack_bytes,
            allocation_bytes: shape.allocation_bytes,
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Record {
    /// Unresolved machine key/pinned number. Loader must resolve and exclude
    /// conflicts with builtins, legacy voices and other V2 reservations.
    pub key: u32,
    pub reset_offset: u32,
    pub process_offset: u32,
    pub descriptor: Descriptor,
}

/// Decode an exact BE record table and LE image descriptors, using checked
/// slices from a separately validated DKDS7 header. Relocations are BE 8-byte
/// entries. This validates their bounds and keeps descriptors immutable; the
/// caller still validates relocation classes/import indices and section layout.
/// Returned records occupy a fixed eight-slot array in file order.
pub fn records(
    table: &[u8],
    image: &[u8],
    relocations: &[u8],
    limits: Limits,
) -> Result<[Option<Record>; 8], Error> {
    if table.is_empty() || table.len() % RECORD != 0 || table.len() > MAX_RECORDS * RECORD {
        return Err(Error::Records);
    }
    if relocations.len() % 8 != 0 || relocations.len() > 4096 * 8 {
        return Err(Error::Relocation);
    }
    let be = |bytes: &[u8], at: usize| u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap());
    let mut result: [Option<Record>; 8] = [None; MAX_RECORDS];
    let mut allocation = 0u32;
    for (i, row) in table.chunks_exact(RECORD).enumerate() {
        let key = be(row, 0);
        if key == u32::MAX || result.iter().flatten().any(|r| r.key == key) {
            return Err(Error::Records);
        }
        let reset = be(row, 4);
        let process = be(row, 8);
        for offset in [reset, process] {
            if !u32::try_from(image.len()).is_ok_and(|bytes| format::callback_ok(offset, bytes)) {
                return Err(Error::Callback);
            }
        }
        let desc = be(row, 12) as usize;
        let end = desc.checked_add(DESC_BYTES).ok_or(Error::DescriptorRange)?;
        if desc % 4 != 0 {
            return Err(Error::DescriptorRange);
        }
        let bytes = image.get(desc..end).ok_or(Error::DescriptorRange)?;
        for relocation in relocations.chunks_exact(8) {
            let start = be(relocation, 0) as usize;
            let stop = start.checked_add(6).ok_or(Error::Relocation)?;
            if stop > image.len() || (start < end && desc < stop) {
                return Err(Error::Relocation);
            }
        }
        let descriptor = Descriptor::parse(bytes, limits)?;
        allocation = allocation
            .checked_add(descriptor.allocation_bytes)
            .ok_or(Error::Allocation)?;
        if allocation > limits.state_bytes {
            return Err(Error::Allocation);
        }
        result[i] = Some(Record {
            key,
            reset_offset: reset,
            process_offset: process,
            descriptor,
        });
    }
    Ok(result)
}

/// Executable section offsets relative to the DSP image, end exclusive.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Executable {
    pub start: u32,
    pub end: u32,
}

/// DKDS8 text ranges, sorted and nonoverlapping. These are section boundaries,
/// not a disassembly or a proof of control flow / declared callback budgets.
pub fn executable_ranges(
    table: &[u8],
    image_bytes: u32,
) -> Result<[Option<Executable>; 32], Error> {
    if table.is_empty() || table.len() % TEXT_RANGE != 0 || table.len() > MAX_TEXT * TEXT_RANGE {
        return Err(Error::Executable);
    }
    let mut ranges = [None; MAX_TEXT];
    let mut previous_end = 0;
    for (i, bytes) in table.chunks_exact(TEXT_RANGE).enumerate() {
        let start = u32::from_be_bytes(bytes[..4].try_into().unwrap());
        let end = u32::from_be_bytes(bytes[4..].try_into().unwrap());
        if !format::text_range_ok(start, end, image_bytes, previous_end) {
            return Err(Error::Executable);
        }
        ranges[i] = Some(Executable { start, end });
        previous_end = end;
    }
    Ok(ranges)
}

/// Require complete callback instructions inside text, and descriptors outside
/// text. Call after `records` validated the same record table and image.
pub fn validate_executables(
    records: &[u8],
    ranges: &[Option<Executable>; 32],
) -> Result<(), Error> {
    if records.is_empty() || records.len() % RECORD != 0 || records.len() > MAX_RECORDS * RECORD {
        return Err(Error::Records);
    }
    for row in records.chunks_exact(RECORD) {
        let word = |at: usize| u32::from_be_bytes(row[at..at + 4].try_into().unwrap());
        for offset in [word(4), word(8)] {
            if offset % 2 != 0
                || !ranges
                    .iter()
                    .flatten()
                    .any(|r| format::in_text(offset, r.start, r.end))
            {
                return Err(Error::Executable);
            }
        }
        let desc = word(12);
        let end = desc
            .checked_add(DESC_BYTES as u32)
            .ok_or(Error::DescriptorRange)?;
        if ranges
            .iter()
            .flatten()
            .any(|r| desc < r.end && r.start < end)
        {
            return Err(Error::Executable);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests;
