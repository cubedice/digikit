use super::*;
use crate::instrument_admission::{prepare, Limits, Spec};

fn request() -> MemoryRequest {
    MemoryRequest {
        image_bytes: 30,
        bss_bytes: 3,
        state_bytes: 32,
        align: 16,
    }
}
fn reserve(loads: &mut Loads, owner: u32) -> Ticket {
    loads
        .reserve(
            owner,
            &[Request {
                type_no: 20 + owner,
                tracks: 2,
            }],
        )
        .unwrap()
}

#[test]
fn instrument_memory_grants_exclude_legacy_ranges_bss_and_padding() {
    let mut loads = Loads::new(16);
    let t = reserve(&mut loads, 2);
    let mut offsets = [0, 192, 0, 0];
    let mut lengths = [64, 64, 0, 0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 512,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    let grant = loads.reserve_memory(t, &mut ledger, &request()).unwrap();
    assert_eq!(
        grant.image,
        Range {
            start: 0x20010040,
            end: 0x2001005e
        }
    );
    assert_eq!(
        grant.state,
        Range {
            start: 0x20010068,
            end: 0x20010088
        }
    );
    assert_eq!(ledger.offsets, &[0, 192, 64, 0]);
    assert_eq!(ledger.lengths, &[64, 64, 128, 0]);
    let spec = Spec {
        type_no: 22,
        state_base: grant.state.start,
        state_bytes: 12,
        text: grant.image,
        reset_sw: 0xb88020,
        process_sw: 0xb88023,
        reset_instructions: 10,
        process_instructions: 10,
        call_depth: 1,
        stack_bytes: 16,
        max_events: 8,
        max_controls: 32,
    };
    let limits = Limits {
        l2: Range {
            start: ledger.base,
            end: ledger.base + ledger.bytes,
        },
        reset_frame: 1000,
        reset_overhead: 500,
        process_total: 100,
        call_depth: 4,
        stack_bytes: 128,
    };
    assert!(prepare(&loads, t, &grant, &[spec], &limits).is_ok());
    assert_eq!(
        first_fit(
            ledger.base,
            ledger.bytes,
            64,
            64,
            ledger.offsets,
            ledger.lengths
        ),
        Some(256)
    );
}

#[test]
fn instrument_memory_timeout_and_detach_retain_memory_until_release() {
    let mut loads = Loads::new(16);
    let t = reserve(&mut loads, 0);
    let mut offsets = [0; 2];
    let mut lengths = [0; 2];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 128,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    loads.reserve_memory(t, &mut ledger, &request()).unwrap();
    loads.start(t).unwrap();
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::Load(Error::WrongPhase))
    );
    loads.timeout(t).unwrap();
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::Load(Error::WrongPhase))
    );
    assert_eq!(
        first_fit(
            ledger.base,
            ledger.bytes,
            8,
            8,
            ledger.offsets,
            ledger.lengths
        ),
        None
    );
    assert_eq!(loads.detached(t), Ok(Command::Release(t)));
    assert_eq!(
        loads.reserve(
            0,
            &[Request {
                type_no: 20,
                tracks: 2
            }]
        ),
        Err(Error::OwnerBusy)
    );
    assert_eq!(loads.timeout(t), Err(Error::WrongPhase));
    assert_eq!(loads.cancel(t), Err(Error::WrongPhase));
    assert_eq!(ledger.lengths[0], 128);
    loads.release_memory(t, &mut ledger).unwrap();
    let replacement = reserve(&mut loads, 0);
    loads
        .reserve_memory(replacement, &mut ledger, &request())
        .unwrap();
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::Load(Error::StaleTicket))
    );
    assert_eq!(ledger.lengths[0], 128);
}

#[test]
fn instrument_memory_pretransfer_cancel_and_tampered_ledger() {
    let mut loads = Loads::new(16);
    let t = reserve(&mut loads, 0);
    let mut offsets = [0; 2];
    let mut lengths = [0; 2];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 256,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    loads.reserve_memory(t, &mut ledger, &request()).unwrap();
    assert_eq!(loads.cancel(t), Ok(Command::Release(t)));
    ledger.offsets[0] = 64;
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::LedgerChanged)
    );
    assert_eq!(ledger.lengths[0], 128);
    ledger.offsets[0] = 0;
    ledger.base += 8;
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::LedgerChanged)
    );
    ledger.base -= 8;
    loads.release_memory(t, &mut ledger).unwrap();
    assert_eq!(ledger.lengths, &[0, 0]);
    assert_eq!(
        loads.release_memory(t, &mut ledger),
        Err(MemoryError::Load(Error::StaleTicket))
    );
}

#[test]
fn instrument_memory_failed_reservations_are_atomic_and_retryable() {
    let mut loads = Loads::new(16);
    let t = reserve(&mut loads, 1);
    let mut offsets = [0, 0];
    let mut lengths = [128, 0];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 128,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    assert!(matches!(
        loads.reserve_memory(t, &mut ledger, &request()),
        Err(MemoryError::NoSpace)
    ));
    assert_eq!(ledger.lengths, &[128, 0]);
    for (image, bss, state, align) in [
        (0, 0, 8, 8),
        (3, 0, 8, 8),
        (4, u32::MAX, 8, 8),
        (4, 0, 0, 8),
        (4, 0, 12, 8),
        (4, 0, 8, 3),
    ] {
        let r = MemoryRequest {
            image_bytes: image,
            bss_bytes: bss,
            state_bytes: state,
            align,
        };
        assert!(matches!(
            loads.reserve_memory(t, &mut ledger, &r),
            Err(MemoryError::Invalid)
        ));
        assert_eq!(ledger.lengths, &[128, 0]);
    }
    ledger.lengths[0] = 0;
    loads.reserve_memory(t, &mut ledger, &request()).unwrap();
    assert!(matches!(
        loads.reserve_memory(t, &mut ledger, &request()),
        Err(MemoryError::Occupied)
    ));
    assert_eq!(ledger.lengths, &[0, 128]);
    loads.start(t).unwrap();
    assert!(matches!(
        loads.reserve_memory(t, &mut ledger, &request()),
        Err(MemoryError::Load(Error::WrongPhase))
    ));
}

#[test]
fn instrument_memory_live_retirement_keeps_other_modules_intact() {
    let mut loads = Loads::new(16);
    let t = reserve(&mut loads, 0);
    let other = reserve(&mut loads, 1);
    let mut offsets = [0; 2];
    let mut lengths = [0; 2];
    let mut ledger = Ledger {
        base: 0x20010000,
        bytes: 256,
        offsets: &mut offsets,
        lengths: &mut lengths,
    };
    loads.reserve_memory(t, &mut ledger, &request()).unwrap();
    loads
        .reserve_memory(other, &mut ledger, &request())
        .unwrap();
    loads.start(t).unwrap();
    loads.image_acked(t).unwrap();
    loads.initialized(t).unwrap();
    assert!(loads.lookup(20).is_some());
    assert_eq!(loads.cancel(t), Ok(Command::Detach(t)));
    assert!(loads.lookup(20).is_none());
    loads.detached(t).unwrap();
    loads.release_memory(t, &mut ledger).unwrap();
    assert_eq!(ledger.offsets, &[0, 128]);
    assert_eq!(ledger.lengths, &[0, 128]);
    assert_eq!(loads.capacity().rows_used, 1);
}
