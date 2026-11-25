//! A V2 image streamed from its file and relocated one chunk at a time: the [`Source`] of `Transfer::begin_streamed`, so the loader keeps
//! no relocated copy of the image. The section was parsed and admitted from the same file (`Section::prepare_relocation` with `sorted`);
//! the stream reads the image and the relocation table from the file again and checks them against what was admitted: a hash of each,
//! the fixups in increasing order inside the image, and a fixup that crosses a chunk boundary read the same from both chunks. A file that
//! changed in between fails the transfer (`complete`) before the BSS is cleared or anything is initialized.
use crate::instrument_image::Source;
use crate::instrument_section::{Fixup, Relocation, Section};

/// The section's bytes, where they live (the store file on the card).
pub trait Reader {
    /// The `out.len()` bytes at `offset` in the section.
    fn read(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()>;
}

/// FNV-1a: catches a changed file, cheaply (a multiply per byte); the file's own hash was checked when it was staged for admission.
const FNV: u32 = 0x811c_9dc5;
fn fnv(mut h: u32, bytes: &[u8]) -> u32 {
    for &b in bytes {
        h = (h ^ b as u32).wrapping_mul(0x0100_0193);
    }
    h
}

pub struct Stream<R> {
    reader: R,
    rel: Relocation,
    image_at: u32,
    image_len: u32,
    relocs_at: u32,
    relocs: u32,
    /// The hashes of the admitted image and relocation table, and of what was read so far.
    expected: (u32, u32),
    image_hash: u32,
    table_hash: u32,
    /// The image bytes given so far.
    streamed: u32,
    /// The next fixup to apply, and the end of the last one applied (the next starts at or after it).
    next: u32,
    after: u32,
    /// The source bytes of a fixup that crosses the end of the last chunk (its first bytes are that chunk's).
    carry: Option<(u32, [u8; 6])>,
    failed: bool,
}

impl<R: Reader> Stream<R> {
    /// For `section`, read again through `reader`; `rel` from `section.prepare_relocation(base, resolve, true)`.
    pub fn new(section: &Section<'_>, rel: Relocation, reader: R) -> Self {
        let (image, image_at, relocations, relocs_at) = section.stream_parts();
        Self {
            reader,
            rel,
            image_at,
            image_len: image.len() as u32,
            relocs_at,
            relocs: (relocations.len() / 8) as u32,
            expected: (fnv(FNV, image), fnv(FNV, relocations)),
            image_hash: FNV,
            table_hash: FNV,
            streamed: 0,
            next: 0,
            after: 0,
            carry: None,
            failed: false,
        }
    }

    /// The image's size (what `begin_streamed` takes).
    pub fn image_len(&self) -> u32 {
        self.image_len
    }

    /// The next fixup's entry: in order, and inside the image.
    fn fixup(&mut self) -> Result<(Fixup, [u8; 8]), ()> {
        let mut entry = [0u8; 8];
        self.reader
            .read(self.relocs_at + self.next * 8, &mut entry)?;
        let f = Fixup::from_bytes(&entry);
        if f.at < self.after || f.at.checked_add(6).is_none_or(|end| end > self.image_len) {
            return Err(());
        }
        Ok((f, entry))
    }

    fn chunk(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()> {
        if offset != self.streamed || out.len() as u32 > self.image_len - offset {
            return Err(());
        }
        let end = offset + out.len() as u32;
        self.reader.read(self.image_at + offset, out)?;
        self.image_hash = fnv(self.image_hash, out);
        while self.next < self.relocs {
            let (f, entry) = self.fixup()?;
            if f.at >= end {
                break;
            }
            // the six source bytes of the instruction: the part in this chunk is `out`'s, which was hashed
            let src: [u8; 6] = if f.at < offset {
                // began in the last chunk: the carry, whose bytes in this chunk must be the ones just read
                // (the same fixup as the carry's, so it starts 1 to 5 bytes before this chunk)
                let (at, src) = self.carry.take().ok_or(())?;
                if at != f.at {
                    return Err(());
                }
                let k = (offset - f.at) as usize;
                let n = ((f.at + 6).min(end) - offset) as usize;
                if out[..n] != src[k..k + n] {
                    return Err(());
                }
                src
            } else {
                let i = (f.at - offset) as usize;
                let n = (out.len() - i).min(6);
                let mut src = [0u8; 6];
                src[..n].copy_from_slice(&out[i..i + n]);
                if n < 6 {
                    // crosses the end of this chunk: the rest from the file now, checked against the next chunk
                    self.reader.read(self.image_at + end, &mut src[n..])?;
                }
                src
            };
            let new = self.rel.relocated(f, &src).map_err(|_| ())?;
            for b in (f.at + 2).max(offset)..(f.at + 6).min(end) {
                out[(b - offset) as usize] = new[(b - f.at) as usize];
            }
            if f.at + 6 > end {
                self.carry = Some((f.at, src));
                break;
            }
            self.table_hash = fnv(self.table_hash, &entry);
            self.after = f.at + 6;
            self.next += 1;
        }
        self.streamed = end;
        Ok(())
    }
}

impl<R: Reader> Source for Stream<R> {
    fn fill(&mut self, offset: u32, out: &mut [u8]) -> Result<(), ()> {
        if self.failed || self.chunk(offset, out).is_err() {
            self.failed = true;
            return Err(());
        }
        Ok(())
    }
    fn complete(&mut self) -> bool {
        !self.failed
            && self.streamed == self.image_len
            && self.next == self.relocs
            && self.carry.is_none()
            && (self.image_hash, self.table_hash) == self.expected
    }
}

#[cfg(test)]
mod tests;
