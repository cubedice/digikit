//! Two-pass image preparation. Imports are resolved once, fixups checked before
//! any output write, and calculations always read the immutable source image.
use super::*;

#[derive(Debug, PartialEq, Eq)]
pub enum RelocateError {
    Placement,
    OutputSize,
    MissingImport(u32),
    Overlap,
    Address,
    /// A streamed relocation needs the table in offset order (the packer writes it so).
    Order,
}

/// One entry of the relocation table: the image offset of a 48-bit instruction and its flags word (kind << 28 | class << 24 | import).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Fixup {
    pub at: u32,
    pub flags: u32,
}

impl Fixup {
    /// The entry in its 8 table bytes.
    pub fn from_bytes(entry: &[u8]) -> Self {
        Fixup {
            at: word(entry, 0),
            flags: word(entry, 4),
        }
    }
}

/// What relocating one instruction needs once [`Section::prepare_relocation`] has checked the whole table: the placement base and the resolved imports.
/// With it, any window of the image can be relocated alone (`instrument_stream`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Relocation {
    base: u32,
    imports: [u32; 32],
}

impl Relocation {
    /// The relocated value of `f`, from the six source bytes of its instruction.
    pub fn value(&self, f: Fixup, src: &[u8; 6]) -> Result<u32, RelocateError> {
        let kind = f.flags >> 28;
        let class = (f.flags >> 24) & 15;
        let lo = u16::from_le_bytes([src[4], src[5]]) as u32;
        let encoded = if kind == 0 {
            ((src[2] as u32) << 16) | lo
        } else {
            ((u16::from_le_bytes([src[2], src[3]]) as u32) << 16) | lo
        };
        let result = if class == 2 {
            // a streamed fixup is read from the file again, so an index the section check did not see must not panic
            let address = *self
                .imports
                .get((f.flags & 0xffff) as usize)
                .ok_or(RelocateError::Address)?;
            // abs32 imports encode the signed linker addend; abs24 rejects
            // negative addends during packing and therefore stays unsigned.
            if kind == 1 {
                address.checked_add_signed(encoded as i32)
            } else {
                address.checked_add(encoded)
            }
        } else {
            let shift = if class == 0 {
                (self.base - 0x20000000) / 2
            } else {
                self.base - 0x20000000
            };
            encoded.checked_add(shift)
        }
        .ok_or(RelocateError::Address)?;
        if kind == 0 && result >= 1 << 24 {
            return Err(RelocateError::Address);
        }
        Ok(result)
    }

    /// The six bytes of `f`'s instruction once relocated (`src`: its source bytes).
    pub fn relocated(&self, f: Fixup, src: &[u8; 6]) -> Result<[u8; 6], RelocateError> {
        let v = self.value(f, src)?;
        let mut out = *src;
        if f.flags >> 28 == 0 {
            out[2] = (v >> 16) as u8;
        } else {
            out[2..4].copy_from_slice(&((v >> 16) as u16).to_le_bytes());
        }
        out[4..6].copy_from_slice(&(v as u16).to_le_bytes());
        Ok(out)
    }
}

impl Section<'_> {
    /// The six source bytes of the instruction a fixup names (the section is validated: every fixup is inside the image).
    fn instruction(&self, at: u32) -> [u8; 6] {
        self.image[at as usize..at as usize + 6].try_into().unwrap()
    }

    /// Check the placement and every fixup, and resolve the imports, without writing anything. `sorted`: the table must be in strictly increasing
    /// offset order with no two instructions overlapping, which a streamed relocation needs (and which makes the overlap check linear); else
    /// any order without overlaps is taken. Resolve names from the trusted firmware export table, not from arbitrary module input.
    pub fn prepare_relocation(
        &self,
        base: u32,
        mut resolve: impl FnMut(&[u8]) -> Option<u32>,
        sorted: bool,
    ) -> Result<Relocation, RelocateError> {
        let image_bss = (self.image.len() as u32)
            .checked_add(self.bss)
            .ok_or(RelocateError::Placement)?;
        let extent = image_bss
            .checked_add(7)
            .map(|n| n & !7)
            .and_then(|n| n.checked_add(self.state_bytes))
            .and_then(|n| n.checked_add(63))
            .map(|n| n & !63)
            .ok_or(RelocateError::Placement)?;
        if base < 0x20000000
            || base % self.align != 0
            || base.checked_add(extent).is_none_or(|end| end > 0x20080000)
        {
            return Err(RelocateError::Placement);
        }
        let mut imports = [0u32; 32];
        for (i, padded) in self.imports.chunks_exact(32).enumerate() {
            let end = padded.iter().position(|&b| b == 0).unwrap(); // validated section
            imports[i] = resolve(&padded[..end]).ok_or(RelocateError::MissingImport(i as u32))?;
        }
        let rel = Relocation { base, imports };
        let mut previous: Option<u32> = None;
        for (i, entry) in self.relocations.chunks_exact(8).enumerate() {
            let f = Fixup::from_bytes(entry);
            if sorted {
                if previous.is_some_and(|p| f.at < p + 6) {
                    return Err(RelocateError::Order);
                }
                previous = Some(f.at);
            } else if self.relocations[..i * 8].chunks_exact(8).any(|earlier| {
                // Reject duplicate/overlapping instructions: each fixup must have a
                // unique destination, independent of record order and partial writes.
                let other = word(earlier, 0);
                f.at < other + 6 && other < f.at + 6
            }) {
                return Err(RelocateError::Overlap);
            }
            rel.value(f, &self.instruction(f.at))?;
        }
        Ok(rel)
    }

    /// Prepare a relocated copy without allocation or transport. All errors leave output unchanged. The loader owns exclusive destination
    /// storage and must retain it until all transfer reads have drained.
    pub fn relocate_into(
        &self,
        base: u32,
        output: &mut [u8],
        resolve: impl FnMut(&[u8]) -> Option<u32>,
    ) -> Result<(), RelocateError> {
        if output.len() != self.image.len() {
            return Err(RelocateError::OutputSize);
        }
        let rel = self.prepare_relocation(base, resolve, false)?;
        output.copy_from_slice(self.image);
        for entry in self.relocations.chunks_exact(8) {
            let f = Fixup::from_bytes(entry);
            let at = f.at as usize;
            // same immutable inputs as the check above
            output[at..at + 6].copy_from_slice(&rel.relocated(f, &self.instruction(f.at)).unwrap());
        }
        Ok(())
    }
}
