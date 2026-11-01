//! Shared no-std V2 descriptor validation used by packing and future admission.
pub use dkrt_sdk::instrument_descriptor::{DESC_BYTES, Descriptor, Limits, MAGIC, VERSION};

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
