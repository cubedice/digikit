//! Persistent V2 instruments in a DSP section (versions 7 and 8): the rules of the format, defined once.
//!
//! Two readers use them. [`read`] works out what a file takes: it streams the section through [`Bytes`] (the host tools, and the runtime's scan from
//! store sector reads). The runtime's loader validates the section it holds in memory before it admits it (`dkrt_sdk::instrument_section`). Both take the
//! header layout, the limits, the rule of a descriptor ([`descriptor`]), of a text range ([`text_range_ok`]) and of a callback ([`callback_ok`]) from
//! here, so a file cannot pass one and fail the other on these. What only the loader judges: the device word, imports, relocations and the admission
//! limits of the device. What only this reader takes: a version 7 section (no text table) and V1 voices or effects beside the instruments; the loader
//! refuses both until it can admit them (`instrument_section::Error::Header`, `Mixed`).
//!
//! No allocation and no `match` on dense integers (a jump table is not ColdFire code). The rules take decoded words: each reader decodes its own bytes.
use super::{inside, word, Bytes, Error};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct InstrumentNeeds {
    /// Distinct persistent instrument types, separate from V1 voice rows.
    pub rows: u32,
    /// Persistent track instances across all types.
    pub tracks: u32,
    /// Sum of tracks * align8(state_bytes), before final section rounding.
    pub state_bytes: u32,
}

/// The header of a version 7 or 8 section, in bytes.
pub const SECTION_HEADER: u32 = 0x50;
/// One instrument record: key, reset entry, process entry, descriptor offset (big endian words).
pub const RECORD_BYTES: u32 = 16;
/// One descriptor in the DSP image: twelve little endian words.
pub const DESCRIPTOR_BYTES: u32 = 48;
/// Instrument records of one section.
pub const MAX_INSTRUMENTS: u32 = 8;
/// Text ranges of a version 8 section.
pub const MAX_TEXT_RANGES: u32 = 32;
/// One text range: start and end offset in the image (big endian words).
pub const TEXT_RANGE_BYTES: u32 = 8;
/// A callback or a relocated instruction: one 48-bit instruction.
pub const INSTRUCTION_BYTES: u32 = 6;

/// Header fields beyond those of `gen_limits` (`DSP_H_*`): the instrument table, then the text table of version 8 (reserved, zero, in version 7).
pub const H_RESERVED: u32 = 0x3c;
pub const H_INSTRUMENT_OFF: u32 = 0x40;
pub const H_INSTRUMENT_N: u32 = 0x44;
pub const H_TEXT_OFF: u32 = 0x48;
pub const H_TEXT_N: u32 = 0x4c;

/// The tables after the header, in file order, each directly after the one before: (offset field, count field, entry bytes). Imports, relocations,
/// V1 voices, effects, instrument records; the text table of version 8 follows them (`H_TEXT_OFF`, `H_TEXT_N`, `TEXT_RANGE_BYTES`), then zero
/// padding to 16 and the image, which ends the section.
pub const TABLES: [(u32, u32, u32); 5] = [
    (0x24, 0x28, 32),
    (0x1c, 0x20, 8),
    (0x2c, 0x30, 64),
    (0x34, 0x38, 64),
    (H_INSTRUMENT_OFF, H_INSTRUMENT_N, RECORD_BYTES),
];

/// Whether a section of this version holds persistent V2 instruments (the loader's V2 path takes it).
pub const fn is_instrument_version(version: u32) -> bool {
    version == 7 || version == 8
}

pub const DESCRIPTOR_MAGIC: u32 = 0x444b_4954;
pub const DESCRIPTOR_VERSION: u32 = 2;
/// Descriptor flag: the borrowed status-output extension.
pub const STATUS_OUTPUT: u32 = 1;
/// Descriptor flag: the native AMP snapshot. Requires `STATUS_OUTPUT`.
pub const NATIVE_AMP: u32 = 2;
/// Descriptor flag: gains at the stock AMP position. Requires `STATUS_OUTPUT | NATIVE_AMP`.
pub const POST_AMP: u32 = 4;

/// The descriptor's flag word: each level on top of the one before; any other value is refused.
pub const fn valid_flags(flags: u32) -> bool {
    flags == 0
        || flags == STATUS_OUTPUT
        || flags == (STATUS_OUTPUT | NATIVE_AMP)
        || flags == (STATUS_OUTPUT | NATIVE_AMP | POST_AMP)
}

/// Why [`descriptor`] refuses twelve words.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DescriptorError {
    /// Magic, version or size.
    Header,
    Flags,
    /// Tracks (1 to 16), events (1 to 8) or controls (1 to 32).
    Counts,
    /// No state, or state or stack bytes that are not a multiple of 4.
    Alignment,
    /// The state of all tracks overflows.
    Allocation,
}

/// What a descriptor declares. `allocation_bytes` is the state of all its tracks, each rounded to 8.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Shape {
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

impl Shape {
    /// A descriptor declares the cost of its process and reset callbacks (a file without them is refused).
    pub const fn declares_budget(&self) -> bool {
        self.cycles_process != 0 && self.cycles_reset != 0
    }
}

/// The twelve decoded words of a descriptor. The checks run in this order, so both readers give the same refusal for the same words.
pub fn descriptor(w: &[u32; 12]) -> Result<Shape, DescriptorError> {
    if w[0] != DESCRIPTOR_MAGIC || w[1] != DESCRIPTOR_VERSION || w[2] != DESCRIPTOR_BYTES {
        return Err(DescriptorError::Header);
    }
    if !valid_flags(w[3]) {
        return Err(DescriptorError::Flags);
    }
    if w[4] == 0 || w[4] > 16 || w[6] == 0 || w[6] > 8 || w[7] == 0 || w[7] > 32 {
        return Err(DescriptorError::Counts);
    }
    if w[5] == 0 || w[5] & 3 != 0 || w[11] & 3 != 0 {
        return Err(DescriptorError::Alignment);
    }
    let Some(stride) = w[5].checked_add(7) else {
        return Err(DescriptorError::Allocation);
    };
    let Some(allocation_bytes) = (stride & !7).checked_mul(w[4]) else {
        return Err(DescriptorError::Allocation);
    };
    Ok(Shape {
        flags: w[3],
        tracks: w[4],
        state_bytes: w[5],
        max_events: w[6],
        max_controls: w[7],
        cycles_process: w[8],
        cycles_reset: w[9],
        call_depth: w[10],
        stack_bytes: w[11],
        allocation_bytes,
    })
}

/// A text range of an image of `image_bytes`: even bounds, at least one instruction, inside the image, not before the end of the range before it.
pub const fn text_range_ok(start: u32, end: u32, image_bytes: u32, previous_end: u32) -> bool {
    start & 1 == 0
        && end & 1 == 0
        && end > start
        && end <= image_bytes
        && end - start >= INSTRUCTION_BYTES
        && start >= previous_end
}

/// A callback entry: an even offset with one whole instruction inside the image.
pub const fn callback_ok(entry: u32, image_bytes: u32) -> bool {
    entry & 1 == 0 && entry <= image_bytes && image_bytes - entry >= INSTRUCTION_BYTES
}

/// One whole instruction at `entry` lies in the range.
pub const fn in_text(entry: u32, start: u32, end: u32) -> bool {
    entry >= start && entry <= end && end - entry >= INSTRUCTION_BYTES
}

/// The instrument needs of the version 7 or 8 section at `off`, `size` bytes long; the caller (`dsp`) bounded the counts of the V1 tables.
pub(super) fn read<B: Bytes>(src: &mut B, off: u32, size: u32) -> Result<InstrumentNeeds, Error> {
    let v8 = word(src, off + 4)? == 8;
    if size < SECTION_HEADER
        || word(src, off + H_RESERVED)? != 0
        || (!v8 && (word(src, off + H_TEXT_OFF)? != 0 || word(src, off + H_TEXT_N)? != 0))
    {
        return Err(Error::Dsp);
    }
    let image_off = word(src, off + 0x18)?;
    let image_size = word(src, off + 0x0c)?;
    let table = word(src, off + H_INSTRUMENT_OFF)?;
    let count = word(src, off + H_INSTRUMENT_N)?;
    if count == 0 || count > MAX_INSTRUMENTS || !inside(table, count * RECORD_BYTES, size) {
        return Err(Error::Dsp);
    }
    let mut end = SECTION_HEADER;
    for (offset, length, stride) in TABLES {
        if word(src, off + offset)? != end {
            return Err(Error::Dsp);
        }
        end = end
            .checked_add(word(src, off + length)? * stride)
            .ok_or(Error::Dsp)?;
    }
    let mut text = [(0u32, 0u32); MAX_TEXT_RANGES as usize];
    let mut text_count = 0;
    if v8 {
        text_count = word(src, off + H_TEXT_N)?;
        if text_count == 0
            || text_count > MAX_TEXT_RANGES
            || word(src, off + H_TEXT_OFF)? != end
            || !inside(end, text_count * TEXT_RANGE_BYTES, size)
        {
            return Err(Error::Dsp);
        }
        let mut previous = 0;
        for i in 0..text_count {
            let start = word(src, off + end + i * TEXT_RANGE_BYTES)?;
            let stop = word(src, off + end + i * TEXT_RANGE_BYTES + 4)?;
            if !text_range_ok(start, stop, image_size, previous) {
                return Err(Error::Dsp);
            }
            text[i as usize] = (start, stop);
            previous = stop;
        }
        end += text_count * TEXT_RANGE_BYTES;
    }
    let padded = end.checked_add(15).ok_or(Error::Dsp)? & !15;
    if image_off != padded || image_off.checked_add(image_size) != Some(size) {
        return Err(Error::Dsp);
    }
    while end < padded {
        if word(src, off + end)? != 0 {
            return Err(Error::Dsp);
        }
        end += 4;
    }
    let mut needs = InstrumentNeeds {
        rows: count,
        ..InstrumentNeeds::default()
    };
    let voice_off = word(src, off + 0x2c)?;
    let voices = word(src, off + 0x30)?;
    let mut types = [0u32; MAX_INSTRUMENTS as usize];
    let mut i = 0;
    while i < count {
        let at = off + table + i * RECORD_BYTES;
        let kind = word(src, at)?;
        let mut j = 0;
        while j < i {
            if types[j as usize] == kind {
                return Err(Error::Dsp);
            }
            j += 1;
        }
        types[i as usize] = kind;
        j = 0;
        while j < voices {
            if word(src, off + voice_off + j * 64)? == kind {
                return Err(Error::Dsp);
            }
            j += 1;
        }
        for entry in [word(src, at + 4)?, word(src, at + 8)?] {
            if !callback_ok(entry, image_size)
                || (v8
                    && !text[..text_count as usize]
                        .iter()
                        .any(|&(start, stop)| in_text(entry, start, stop)))
            {
                return Err(Error::Dsp);
            }
        }
        let desc = word(src, at + 12)?;
        if desc & 3 != 0 || !inside(desc, DESCRIPTOR_BYTES, image_size) {
            return Err(Error::Dsp);
        }
        if v8
            && text[..text_count as usize]
                .iter()
                .any(|&(start, stop)| desc < stop && start < desc + DESCRIPTOR_BYTES)
        {
            return Err(Error::Dsp);
        }
        // Descriptor words are little endian within the SHARC image.
        let mut d = [0u32; 12];
        j = 0;
        while j < 12 {
            let mut b = [0u8; 4];
            if !src.read(off + image_off + desc + j * 4, &mut b) {
                return Err(Error::Unreadable);
            }
            d[j as usize] = u32::from_le_bytes(b);
            j += 1;
        }
        let shape = descriptor(&d).map_err(|_| Error::Dsp)?;
        if !shape.declares_budget() {
            return Err(Error::Dsp);
        }
        needs.state_bytes = needs
            .state_bytes
            .checked_add(shape.allocation_bytes)
            .ok_or(Error::Dsp)?;
        needs.tracks += shape.tracks; // at most eight records of sixteen tracks
        i += 1;
    }
    Ok(needs)
}
