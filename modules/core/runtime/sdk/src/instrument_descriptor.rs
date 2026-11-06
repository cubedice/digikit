//! Bounded decoding of V2 descriptors and DKDS7 instrument records. No addresses
//! are dereferenced, allocations made, or runtime capabilities enabled. Decoded
//! callbacks are image offsets, NOT proof of executable instruction boundaries.

pub const MAGIC: u32 = 0x444b_4954;
pub const VERSION: u32 = 2;
pub const DESC_BYTES: usize = 48;
/// Opt in to the V5 borrowed status-output extension. Unknown bits fail closed.
pub const STATUS_OUTPUT: u32 = 1;

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
        if w(0) != MAGIC || w(1) != VERSION || w(2) != DESC_BYTES as u32 {
            return Err(Error::Header);
        }
        if w(3) & !STATUS_OUTPUT != 0 {
            return Err(Error::Flags);
        }
        if !(1..=16).contains(&w(4)) || !(1..=8).contains(&w(6)) || !(1..=32).contains(&w(7)) {
            return Err(Error::Counts);
        }
        if w(5) == 0 || w(5) % 4 != 0 || w(11) % 4 != 0 {
            return Err(Error::Alignment);
        }
        let allocation_bytes = w(5)
            .checked_add(7)
            .map(|n| n & !7)
            .and_then(|n| n.checked_mul(w(4)))
            .ok_or(Error::Allocation)?;
        if allocation_bytes > limits.state_bytes {
            return Err(Error::Allocation);
        }
        if w(8) == 0
            || w(8) > limits.cycles_process
            || w(9) == 0
            || w(9) > limits.cycles_reset
            || w(10) > limits.call_depth
            || w(11) > limits.stack_bytes
        {
            return Err(Error::Budget);
        }
        Ok(Self {
            flags: w(3),
            tracks: w(4),
            state_bytes: w(5),
            max_events: w(6),
            max_controls: w(7),
            cycles_process: w(8),
            cycles_reset: w(9),
            call_depth: w(10),
            stack_bytes: w(11),
            allocation_bytes,
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
    if table.is_empty() || table.len() % 16 != 0 || table.len() > 8 * 16 {
        return Err(Error::Records);
    }
    if relocations.len() % 8 != 0 || relocations.len() > 4096 * 8 {
        return Err(Error::Relocation);
    }
    let be = |bytes: &[u8], at: usize| u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap());
    let mut result: [Option<Record>; 8] = [None; 8];
    let mut allocation = 0u32;
    for (i, row) in table.chunks_exact(16).enumerate() {
        let key = be(row, 0);
        if key == u32::MAX || result.iter().flatten().any(|r| r.key == key) {
            return Err(Error::Records);
        }
        let reset = be(row, 4);
        let process = be(row, 8);
        for offset in [reset, process] {
            if offset % 2 != 0
                || (offset as usize)
                    .checked_add(6)
                    .is_none_or(|end| end > image.len())
            {
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
    if table.is_empty() || table.len() % 8 != 0 || table.len() > 32 * 8 {
        return Err(Error::Executable);
    }
    let mut ranges = [None; 32];
    let mut previous_end = 0;
    for (i, bytes) in table.chunks_exact(8).enumerate() {
        let start = u32::from_be_bytes(bytes[..4].try_into().unwrap());
        let end = u32::from_be_bytes(bytes[4..].try_into().unwrap());
        if start % 2 != 0
            || end % 2 != 0
            || end <= start
            || end > image_bytes
            || end - start < 6
            || start < previous_end
        {
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
    if records.is_empty() || records.len() % 16 != 0 || records.len() > 128 {
        return Err(Error::Records);
    }
    for row in records.chunks_exact(16) {
        let word = |at: usize| u32::from_be_bytes(row[at..at + 4].try_into().unwrap());
        for offset in [word(4), word(8)] {
            if offset % 2 != 0
                || !ranges.iter().flatten().any(|r| {
                    offset >= r.start && offset.checked_add(6).is_some_and(|end| end <= r.end)
                })
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
