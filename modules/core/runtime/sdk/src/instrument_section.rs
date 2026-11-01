//! Allocation-free DKDS6 section validation for instrument-only loads. Mixed
//! legacy voice/effect sections are deliberately rejected until their lifecycle
//! can be admitted atomically with V2. This module enables no device capability.
use crate::instrument_admission::Packed;
use crate::instrument_descriptor::{self as descriptor, Record};
use crate::instrument_load::MemoryRequest;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Header,
    Device,
    Layout,
    Mixed,
    Import,
    Relocation,
    Allocation,
    Descriptor(descriptor::Error),
}

pub struct Section<'a> {
    image: &'a [u8],
    records: &'a [u8],
    relocations: &'a [u8],
    imports: &'a [u8],
    executable: &'a [u8],
    decoded: [Option<Record>; 8],
    bss: u32,
    align: u32,
    state_bytes: u32,
}
fn word(bytes: &[u8], at: usize) -> u32 {
    u32::from_be_bytes(bytes[at..at + 4].try_into().unwrap())
}
impl<'a> Section<'a> {
    /// Validate the entire section before exposing any slices. This establishes
    /// format bounds, not import resolution, instruction boundaries, relocated
    /// address validity, exclusive ownership or callback budget enforcement.
    pub fn parse(bytes: &'a [u8], device: u32, limits: descriptor::Limits) -> Result<Self, Error> {
        if bytes.len() < 0x50
            || word(bytes, 0) != 0x444b4453
            || word(bytes, 4) != 6
            || word(bytes, 0x3c) != 0
        {
            return Err(Error::Header);
        }
        if word(bytes, 8) != device {
            return Err(Error::Device);
        }
        if word(bytes, 0x30) != 0 || word(bytes, 0x38) != 0 {
            return Err(Error::Mixed);
        }
        let size = word(bytes, 0x0c);
        let bss = word(bytes, 0x10);
        let align = word(bytes, 0x14);
        if size < 6
            || size % 4 != 0
            || bss % 4 != 0
            || !(8..=64).contains(&align)
            || !align.is_power_of_two()
        {
            return Err(Error::Header);
        }
        let nimport = word(bytes, 0x28);
        let nreloc = word(bytes, 0x20);
        let nrecord = word(bytes, 0x44);
        let ntext = word(bytes, 0x4c);
        if nimport > 32
            || nreloc > 4096
            || !(1..=8).contains(&nrecord)
            || !(1..=32).contains(&ntext)
        {
            return Err(Error::Layout);
        }
        let mut end = 0x50usize;
        for (at, count, stride) in [
            (0x24, nimport, 32),
            (0x1c, nreloc, 8),
            (0x2c, 0, 64),
            (0x34, 0, 64),
            (0x40, nrecord, 16),
            (0x48, ntext, 8),
        ] {
            if word(bytes, at) as usize != end {
                return Err(Error::Layout);
            }
            end = end
                .checked_add(count as usize * stride)
                .ok_or(Error::Layout)?;
            if end > bytes.len() {
                return Err(Error::Layout);
            }
        }
        let image_at = end.checked_add(15).ok_or(Error::Layout)? & !15;
        if word(bytes, 0x18) as usize != image_at
            || image_at.checked_add(size as usize) != Some(bytes.len())
            || bytes
                .get(end..image_at)
                .is_none_or(|pad| pad.iter().any(|&b| b != 0))
        {
            return Err(Error::Layout);
        }
        let table = |at: usize, count: u32, stride: usize| {
            let start = word(bytes, at) as usize;
            &bytes[start..start + count as usize * stride]
        };
        let imports = table(0x24, nimport, 32);
        let relocations = table(0x1c, nreloc, 8);
        let records = table(0x40, nrecord, 16);
        let executable = table(0x48, ntext, 8);
        let image = &bytes[image_at..];
        for (i, name) in imports.chunks_exact(32).enumerate() {
            let nul = name.iter().position(|&b| b == 0).ok_or(Error::Import)?;
            if nul == 0
                || name[nul..].iter().any(|&b| b != 0)
                || imports[..i * 32]
                    .chunks_exact(32)
                    .any(|previous| previous == name)
            {
                return Err(Error::Import);
            }
        }
        let ranges = descriptor::executable_ranges(executable, size).map_err(Error::Descriptor)?;
        for reloc in relocations.chunks_exact(8) {
            let offset = word(reloc, 0);
            let flags = word(reloc, 4);
            let kind = flags >> 28;
            let class = (flags >> 24) & 15;
            let import = flags & 0xffff;
            if kind > 1
                || class > 2
                || flags & 0x00ff0000 != 0
                || (class == 2 && import >= nimport)
                || (class != 2 && import != 0)
                || (kind == 0 && class == 1)
                || offset % 2 != 0
                || !ranges.iter().flatten().any(|r| {
                    offset >= r.start && offset.checked_add(6).is_some_and(|end| end <= r.end)
                })
            {
                return Err(Error::Relocation);
            }
        }
        let decoded =
            descriptor::records(records, image, relocations, limits).map_err(Error::Descriptor)?;
        descriptor::validate_executables(records, &ranges).map_err(Error::Descriptor)?;
        let mut state_bytes = 0u32;
        for record in decoded.iter().flatten() {
            state_bytes = state_bytes
                .checked_add(record.descriptor.allocation_bytes)
                .ok_or(Error::Allocation)?;
        }
        size.checked_add(bss)
            .and_then(|n| n.checked_add(7))
            .map(|n| n & !7)
            .and_then(|n| n.checked_add(state_bytes))
            .and_then(|n| n.checked_add(63))
            .ok_or(Error::Allocation)?;
        Ok(Self {
            image,
            records,
            relocations,
            imports,
            executable,
            decoded,
            bss,
            align,
            state_bytes,
        })
    }
    pub fn memory_request(&self) -> MemoryRequest {
        MemoryRequest {
            image_bytes: self.image.len() as u32,
            bss_bytes: self.bss,
            state_bytes: self.state_bytes,
            align: self.align,
        }
    }
    pub fn records(&self) -> &[Option<Record>; 8] {
        &self.decoded
    }
    /// Fixed-width NUL-padded names to resolve through the trusted import table.
    pub fn imports(&self) -> &[u8] {
        self.imports
    }
    pub fn packed<'b>(&'b self, resolved_types: &'b [u32]) -> Packed<'b> {
        Packed {
            image: self.image,
            records: self.records,
            relocations: self.relocations,
            executable: self.executable,
            resolved_types,
        }
    }
}

mod relocate;
pub use relocate::RelocateError;

#[cfg(test)]
pub(crate) mod tests;
