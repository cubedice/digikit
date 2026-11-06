//! Allocation-free V2 accounting. No registry or DSP dispatch is enabled here.
use super::{Bytes, Error, inside, word};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct InstrumentNeeds {
    /// Distinct persistent instrument types, separate from V1 voice rows.
    pub rows: u32,
    /// Persistent track instances across all types.
    pub tracks: u32,
    /// Sum of tracks * align8(state_bytes), before final section rounding.
    pub state_bytes: u32,
}

const HEADER: u32 = 0x50;
const RECORD: u32 = 16;
const DESC: u32 = 48;
const MAX_ROWS: u32 = 8;

pub(super) fn read<B: Bytes>(src: &mut B, off: u32, size: u32) -> Result<InstrumentNeeds, Error> {
    let v8 = word(src, off + 4)? == 8;
    if size < HEADER
        || word(src, off + 0x3c)? != 0
        || (!v8 && (word(src, off + 0x48)? != 0 || word(src, off + 0x4c)? != 0))
    {
        return Err(Error::Dsp);
    }
    let image_off = word(src, off + 0x18)?;
    let image_size = word(src, off + 0x0c)?;
    let table = word(src, off + 0x40)?;
    let count = word(src, off + 0x44)?;
    if count == 0 || count > MAX_ROWS || !inside(table, count * RECORD, size) {
        return Err(Error::Dsp);
    }
    // Canonical V7 table order. Counts have already been bounded by dsp().
    let tables = [
        (0x24, 0x28, 32),
        (0x1c, 0x20, 8),
        (0x2c, 0x30, 64),
        (0x34, 0x38, 64),
        (0x40, 0x44, RECORD),
    ];
    let mut end = HEADER;
    for (offset, length, stride) in tables {
        if word(src, off + offset)? != end {
            return Err(Error::Dsp);
        }
        end = end
            .checked_add(word(src, off + length)? * stride)
            .ok_or(Error::Dsp)?;
    }
    let mut text = [(0u32, 0u32); 32];
    let mut text_count = 0;
    if v8 {
        text_count = word(src, off + 0x4c)?;
        if text_count == 0
            || text_count > 32
            || word(src, off + 0x48)? != end
            || !inside(end, text_count * 8, size)
        {
            return Err(Error::Dsp);
        }
        let mut previous = 0;
        for i in 0..text_count {
            let start = word(src, off + end + i * 8)?;
            let stop = word(src, off + end + i * 8 + 4)?;
            if start & 1 != 0
                || stop & 1 != 0
                || stop <= start
                || stop > image_size
                || stop - start < 6
                || start < previous
            {
                return Err(Error::Dsp);
            }
            text[i as usize] = (start, stop);
            previous = stop;
        }
        end += text_count * 8;
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
    let mut types = [0u32; MAX_ROWS as usize];
    let mut i = 0;
    while i < count {
        let at = off + table + i * RECORD;
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
            if entry & 1 != 0
                || !inside(entry, 6, image_size)
                || (v8
                    && !text[..text_count as usize].iter().any(|&(start, stop)| {
                        entry >= start && entry.checked_add(6).is_some_and(|end| end <= stop)
                    }))
            {
                return Err(Error::Dsp);
            }
        }
        let desc = word(src, at + 12)?;
        if desc & 3 != 0 || !inside(desc, DESC, image_size) {
            return Err(Error::Dsp);
        }
        if v8
            && text[..text_count as usize]
                .iter()
                .any(|&(start, stop)| desc < stop && start < desc + DESC)
        {
            return Err(Error::Dsp);
        }
        // Descriptor words are little endian within the SHARC image.
        let mut d = [0u32; 12];
        j = 0;
        while j < 12 {
            d[j as usize] = word(src, off + image_off + desc + j * 4)?.swap_bytes();
            j += 1;
        }
        if d[0] != 0x444b4954
            || d[1] != 2
            || d[2] != DESC
            || d[3] != 0
            || d[4] == 0
            || d[4] > 16
            || d[5] == 0
            || d[5] & 3 != 0
            || d[6] == 0
            || d[6] > 8
            || d[7] == 0
            || d[7] > 32
            || d[8] == 0
            || d[9] == 0
            || d[11] & 3 != 0
        {
            return Err(Error::Dsp);
        }
        let stride = d[5].checked_add(7).ok_or(Error::Dsp)? & !7;
        let allocation = stride.checked_mul(d[4]).ok_or(Error::Dsp)?;
        needs.state_bytes = needs
            .state_bytes
            .checked_add(allocation)
            .ok_or(Error::Dsp)?;
        needs.tracks += d[4]; // at most eight records of sixteen tracks
        i += 1;
    }
    Ok(needs)
}
