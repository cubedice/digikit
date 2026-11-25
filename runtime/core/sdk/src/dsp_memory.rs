//! Shared first-fit search over the live runtime's DSP allocation ledger.
//! Offsets/lengths include every module (voices, effects, and future V2 states).
//! The owning loader must record the returned range before another search; this
//! function neither reserves memory nor permits freeing an in-flight allocation.

/// Lowest offset whose absolute byte address meets `align`, or None for no fit
/// or malformed bookkeeping. Empty slots ignore their offset. All arithmetic is
/// checked so corrupt metadata cannot turn into an overlapping allocation.
pub fn first_fit(
    base: u32,
    total: u32,
    len: u32,
    align: u32,
    offsets: &[u32],
    lengths: &[u32],
) -> Option<u32> {
    if len == 0 || len > total || !align.is_power_of_two() || offsets.len() != lengths.len() {
        return None;
    }
    base.checked_add(total)?;
    for (i, (&off, &size)) in offsets.iter().zip(lengths).enumerate() {
        if size == 0 {
            continue;
        }
        let end = off.checked_add(size)?;
        if end > total {
            return None;
        }
        for (&other, &count) in offsets[..i].iter().zip(&lengths[..i]) {
            if count != 0 && off < other.checked_add(count)? && other < end {
                return None;
            }
        }
    }
    let mut cursor = 0u32;
    loop {
        let address = base.checked_add(cursor)?;
        let padding = address.wrapping_neg() & (align - 1);
        let candidate = cursor.checked_add(padding)?;
        let end = candidate.checked_add(len)?;
        if end > total {
            return None;
        }
        let mut next = candidate;
        for (&off, &size) in offsets.iter().zip(lengths) {
            if size != 0 && candidate < off + size && off < end {
                next = next.max(off + size);
            }
        }
        if next == candidate {
            return Some(candidate);
        }
        cursor = next;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dsp_memory_reuses_lowest_gap_across_unsorted_owners() {
        let offsets = [128, 0, 64, u32::MAX];
        let mut lengths = [64, 64, 64, 0];
        assert_eq!(
            first_fit(0x20000000, 256, 64, 64, &offsets, &lengths),
            Some(192)
        );
        lengths[2] = 0; // only a completed detach may release this slot
        assert_eq!(
            first_fit(0x20000000, 256, 64, 64, &offsets, &lengths),
            Some(64)
        );
        lengths[1] = 0;
        assert_eq!(
            first_fit(0x20000000, 256, 128, 64, &offsets, &lengths),
            Some(0)
        );
    }

    #[test]
    fn dsp_memory_aligns_addresses_not_offsets_and_keeps_exact_fits() {
        assert_eq!(first_fit(0x20000002, 30, 16, 16, &[], &[]), Some(14));
        assert_eq!(first_fit(0x20000002, 29, 16, 16, &[], &[]), None);
        assert_eq!(first_fit(0x20000002, 62, 16, 16, &[14], &[16]), Some(30));
        assert_eq!(
            first_fit(0, u32::MAX, 1, 1 << 31, &[0], &[1]),
            Some(1 << 31)
        );
    }

    #[test]
    fn dsp_memory_rejects_invalid_requests_and_ledger() {
        for (base, total, len, align) in [
            (0, 64, 0, 8),
            (0, 64, 65, 8),
            (0, 64, 8, 0),
            (0, 64, 8, 3),
            (u32::MAX - 2, 4, 1, 1),
            (u32::MAX - 2, 2, 1, 8),
        ] {
            assert_eq!(first_fit(base, total, len, align, &[], &[]), None);
        }
        assert_eq!(first_fit(0, 64, 8, 8, &[0], &[]), None);
        for (offsets, lengths) in [
            ([0, 60], [8, 8]),
            ([0, u32::MAX], [8, 2]),
            ([0, 4], [8, 8]),
            ([0, 0], [8, 8]),
        ] {
            assert_eq!(first_fit(0, 64, 8, 8, &offsets, &lengths), None);
        }
    }

    #[test]
    fn dsp_memory_matches_byte_search_for_all_small_fragmented_pools() {
        // Enumerate occupied cells, request sizes and unaligned pool bases.
        // The oracle tests each byte independently, not the search algorithm.
        for mask in 0u32..256 {
            let offsets = [14, 12, 10, 8, 6, 4, 2, 0];
            let lengths =
                core::array::from_fn::<_, 8, _>(|i| if mask & (1 << i) != 0 { 2 } else { 0 });
            for base in 0..8 {
                for align in [1, 2, 4, 8] {
                    for len in 1..=16 {
                        let expected = (0..=16 - len).find(|&off| {
                            (base + off) % align == 0
                                && offsets.iter().zip(lengths).all(|(&used, size)| {
                                    size == 0 || off + len <= used || used + size <= off
                                })
                        });
                        assert_eq!(
                            first_fit(base, 16, len, align, &offsets, &lengths),
                            expected
                        );
                    }
                }
            }
        }
    }
}
