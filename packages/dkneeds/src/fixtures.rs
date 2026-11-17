//! The canonical test fixture of a persistent V2 instrument section, for every test layer that needs one (this crate, the runtime SDK's
//! validator and coordinator, patchkit). Behind the `fixtures` feature: no build of the runtime or of a tool holds it.

use crate::instrument::{DESCRIPTOR_MAGIC, DESCRIPTOR_VERSION};

/// The bytes of [`v2_section`].
pub const V2_SECTION_BYTES: usize = 208;
/// The device word of [`v2_section`] (1: Digitakt II).
pub const V2_DEVICE: u32 = 1;
/// Where the DSP image of [`v2_section`] starts, and where its one descriptor starts (image offset 16).
pub const V2_IMAGE_AT: usize = 144;
pub const V2_DESCRIPTOR_AT: usize = 160;
/// The twelve words of the descriptor of [`v2_section`]: no flags, 2 tracks of 12 state bytes (16 each in memory), 8 events, 32 controls,
/// 20 and 10 cycles, call depth 2, 32 stack bytes.
pub const V2_DESCRIPTOR: [u32; 12] = [
    DESCRIPTOR_MAGIC,
    DESCRIPTOR_VERSION,
    48,
    0,
    2,
    12,
    8,
    32,
    20,
    10,
    2,
    32,
];

/// A version 8 section that the loader admits and the needs reader reads: one import (`test`), one relocation (import class, at image offset 0), no
/// V1 voice or effect, one instrument record (key 20, reset at 0, process at 6, descriptor at 16), one text range (0 to 12), and a 64-byte image
/// with 8 bytes of bss, aligned to 16. The header words are big endian, the descriptor's little endian.
pub const fn v2_section() -> [u8; V2_SECTION_BYTES] {
    let mut bytes = [0u8; V2_SECTION_BYTES];
    let header: [(usize, u32); 25] = [
        (0, crate::DSP_MAGIC),
        (4, 8),
        (8, V2_DEVICE),
        (0x0c, 64),
        (0x10, 8),
        (0x14, 16),
        (0x18, V2_IMAGE_AT as u32),
        (0x24, 80),
        (0x28, 1),
        (0x1c, 112),
        (0x20, 1),
        (0x2c, 120),
        (0x34, 120),
        (0x40, 120),
        (0x44, 1),
        (0x48, 136),
        (0x4c, 1),
        (112, 0),
        (116, 0x0200_0000),
        (120, 20),
        (124, 0),
        (128, 6),
        (132, 16),
        (136, 0),
        (140, 12),
    ];
    let mut i = 0;
    while i < header.len() {
        let (at, value) = header[i];
        let b = value.to_be_bytes();
        bytes[at] = b[0];
        bytes[at + 1] = b[1];
        bytes[at + 2] = b[2];
        bytes[at + 3] = b[3];
        i += 1;
    }
    bytes[80] = b't';
    bytes[81] = b'e';
    bytes[82] = b's';
    bytes[83] = b't';
    i = 0;
    while i < 12 {
        let b = V2_DESCRIPTOR[i].to_le_bytes();
        bytes[V2_DESCRIPTOR_AT + i * 4] = b[0];
        bytes[V2_DESCRIPTOR_AT + i * 4 + 1] = b[1];
        bytes[V2_DESCRIPTOR_AT + i * 4 + 2] = b[2];
        bytes[V2_DESCRIPTOR_AT + i * 4 + 3] = b[3];
        i += 1;
    }
    bytes
}
