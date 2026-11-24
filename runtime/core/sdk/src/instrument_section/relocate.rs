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
}

impl Section<'_> {
    /// Prepare a relocated copy without allocation or transport. Resolve names
    /// from the trusted firmware export table, not from arbitrary module input.
    /// All errors leave output unchanged. The loader owns exclusive destination
    /// storage and must retain it until all transfer reads have drained.
    pub fn relocate_into(
        &self,
        base: u32,
        output: &mut [u8],
        mut resolve: impl FnMut(&[u8]) -> Option<u32>,
    ) -> Result<(), RelocateError> {
        if output.len() != self.image.len() {
            return Err(RelocateError::OutputSize);
        }
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
        let value = |reloc: &[u8]| -> Result<u32, RelocateError> {
            let at = word(reloc, 0) as usize;
            let flags = word(reloc, 4);
            let kind = flags >> 28;
            let class = (flags >> 24) & 15;
            let lo = u16::from_le_bytes([self.image[at + 4], self.image[at + 5]]) as u32;
            let encoded = if kind == 0 {
                ((self.image[at + 2] as u32) << 16) | lo
            } else {
                ((u16::from_le_bytes([self.image[at + 2], self.image[at + 3]]) as u32) << 16) | lo
            };
            let result = if class == 2 {
                let address = imports[(flags & 0xffff) as usize];
                // abs32 imports encode the signed linker addend; abs24 rejects
                // negative addends during packing and therefore stays unsigned.
                if kind == 1 {
                    address.checked_add_signed(encoded as i32)
                } else {
                    address.checked_add(encoded)
                }
            } else {
                let shift = if class == 0 {
                    (base - 0x20000000) / 2
                } else {
                    base - 0x20000000
                };
                encoded.checked_add(shift)
            }
            .ok_or(RelocateError::Address)?;
            if kind == 0 && result >= 1 << 24 {
                return Err(RelocateError::Address);
            }
            Ok(result)
        };
        for (i, reloc) in self.relocations.chunks_exact(8).enumerate() {
            let start = word(reloc, 0);
            // Reject duplicate/overlapping instructions: each fixup must have a
            // unique destination, independent of record order and partial writes.
            if self.relocations[..i * 8].chunks_exact(8).any(|previous| {
                let other = word(previous, 0);
                start < other + 6 && other < start + 6
            }) {
                return Err(RelocateError::Overlap);
            }
            value(reloc)?;
        }
        output.copy_from_slice(self.image);
        for reloc in self.relocations.chunks_exact(8) {
            let at = word(reloc, 0) as usize;
            let v = value(reloc).unwrap(); // same immutable inputs as preflight
            if word(reloc, 4) >> 28 == 0 {
                output[at + 2] = (v >> 16) as u8;
            } else {
                output[at + 2..at + 4].copy_from_slice(&((v >> 16) as u16).to_le_bytes());
            }
            output[at + 4..at + 6].copy_from_slice(&(v as u16).to_le_bytes());
        }
        Ok(())
    }
}
