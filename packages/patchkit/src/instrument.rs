//! Experimental V2 descriptor validation. No loader capability is advertised.
//! Separate magic/layout: this must never flow through the V1 voice-state path.
use crate::error::*;

pub const MAGIC: u32 = 0x444b_4954;
pub const VERSION: u32 = 2;
pub const DESC_BYTES: usize = 48;

/// Device admission ceilings, supplied by the future registration owner.
/// These are limits, not measurements or permission to load V2 today.
#[derive(Clone, Copy, Debug)]
pub struct Limits {
    pub state_bytes: u32,
    pub cycles_process: u32,
    pub cycles_reset: u32,
    pub call_depth: u32,
    pub stack_bytes: u32,
}

#[derive(Debug, PartialEq, Eq)]
pub struct Descriptor {
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

impl Descriptor {
    /// Read exactly one little-endian descriptor, with no allocation or pointer
    /// dereferences. Each instance starts on an eight-byte boundary like V1.
    pub fn parse(bytes: &[u8], limits: Limits) -> Result<Self> {
        if bytes.len() != DESC_BYTES {
            return Err(err!(
                "instrument V2 descriptor: expected {DESC_BYTES} bytes, got {}",
                bytes.len()
            ));
        }
        let w = |i: usize| u32::from_le_bytes(bytes[i * 4..i * 4 + 4].try_into().unwrap());
        if w(0) != MAGIC || w(1) != VERSION || w(2) != DESC_BYTES as u32 {
            return Err(err!(
                "instrument V2 descriptor: incompatible magic, version or size"
            ));
        }
        if w(3) != 0 {
            return Err(err!(
                "instrument V2 descriptor: unsupported flags {:#x}",
                w(3)
            ));
        }
        for (name, value, cap) in [
            ("tracks", w(4), 16),
            ("max_events", w(6), 8),
            ("max_controls", w(7), 32),
        ] {
            if value == 0 || value > cap {
                return Err(err!(
                    "instrument V2 descriptor: {name} {value} outside 1..={cap}"
                ));
            }
        }
        if w(5) == 0 || w(5) % 4 != 0 || w(11) % 4 != 0 {
            return Err(err!(
                "instrument V2 descriptor: nonzero state and stack alignment must be multiples of four"
            ));
        }
        let allocation_bytes = w(5)
            .checked_add(7)
            .map(|v| v & !7)
            .and_then(|stride| stride.checked_mul(w(4)))
            .ok_or_else(|| err!("instrument V2 descriptor: state allocation overflow"))?;
        if allocation_bytes > limits.state_bytes {
            return Err(err!(
                "instrument V2 descriptor: state allocation {allocation_bytes} exceeds {}",
                limits.state_bytes
            ));
        }
        for (name, value, cap, nonzero) in [
            ("process budget", w(8), limits.cycles_process, true),
            ("reset budget", w(9), limits.cycles_reset, true),
            ("call depth", w(10), limits.call_depth, false),
            ("stack bytes", w(11), limits.stack_bytes, false),
        ] {
            if value > cap || (nonzero && value == 0) {
                return Err(err!(
                    "instrument V2 descriptor: {name} {value} exceeds admission limits"
                ));
            }
        }
        Ok(Self {
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

#[cfg(test)]
mod tests {
    use super::*;
    fn words() -> [u32; 12] {
        [
            MAGIC,
            VERSION,
            DESC_BYTES as u32,
            0,
            16,
            260,
            8,
            32,
            20000,
            2000,
            4,
            64,
        ]
    }
    fn limits() -> Limits {
        Limits {
            state_bytes: 8192,
            cycles_process: 20000,
            cycles_reset: 2000,
            call_depth: 4,
            stack_bytes: 64,
        }
    }
    fn bytes(w: &[u32]) -> Vec<u8> {
        w.iter().flat_map(|v| v.to_le_bytes()).collect()
    }
    #[test]
    fn sizes_each_track_independently_and_enforces_limits() {
        let d = Descriptor::parse(&bytes(&words()), limits()).unwrap();
        assert_eq!(d.allocation_bytes, 16 * 264);
        for (index, value) in [
            (0, 0x444b4d41),
            (1, 1),
            (2, 40),
            (3, 1),
            (4, 0),
            (4, 17),
            (5, 0),
            (5, 261),
            (5, 516),
            (5, 0xffff_fffc),
            (6, 0),
            (6, 9),
            (7, 0),
            (7, 33),
            (8, 0),
            (8, 20001),
            (9, 0),
            (9, 2001),
            (10, 5),
            (11, 65),
            (11, 68),
        ] {
            let mut w = words();
            w[index] = value;
            assert!(
                Descriptor::parse(&bytes(&w), limits()).is_err(),
                "field {index} value {value}"
            );
        }
        let b = bytes(&words());
        for len in 0..DESC_BYTES {
            assert!(Descriptor::parse(&b[..len], limits()).is_err());
        }
        let mut oversized = b;
        oversized.extend_from_slice(&[0; 4]);
        assert!(Descriptor::parse(&oversized, limits()).is_err());
    }
    #[test]
    fn abi_constants_match_the_schema_without_enabling_v2_runtime() {
        let text = include_str!("../../../sdk/sdk.toml");
        let spec = crate::sdkgen::instrument(text).unwrap();
        assert_eq!(spec.desc.len() * 4, DESC_BYTES);
        let constant = |name| spec.consts.iter().find(|c| c.name == name).unwrap().value;
        assert_eq!(constant("DK_I_MAGIC"), MAGIC);
        assert_eq!(constant("DK_INSTRUMENT_V2"), VERSION);
        assert_eq!(constant("DK_I_MAX_TRACKS"), 16);
        assert_eq!(constant("DK_I_MAX_EVENTS"), 8);
        assert_eq!(constant("DK_I_MAX_CONTROLS"), 32);
        assert_eq!(crate::sdkgen::embedded().machine, (1, 0));
    }
}
