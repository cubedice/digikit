use super::*;
use crate::instrument_registry::Request;

fn limits() -> Limits {
    Limits {
        l2: Range {
            start: 0x20000000,
            end: 0x20080000,
        },
        reset_frame: 1000,
        reset_overhead: 500,
        process_total: 1000,
        call_depth: 4,
        stack_bytes: 128,
    }
}
fn spec(type_no: u32, base: u32) -> Spec {
    Spec {
        type_no,
        state_base: base,
        state_bytes: 12,
        text: Range {
            start: 0x20010000,
            end: 0x20010100,
        },
        process_text: Range {
            start: 0x20010000,
            end: 0x20010100,
        },
        reset_sw: 0xb88000,
        process_sw: 0xb88003,
        reset_instructions: 100,
        process_instructions: 100,
        call_depth: 2,
        stack_bytes: 64,
        max_events: 8,
        max_controls: 32,
        flags: 0,
    }
}
fn setup() -> (Loads, Ticket, Grant) {
    let mut loads = Loads::new(16);
    let ticket = loads
        .reserve(
            3,
            &[
                Request {
                    type_no: 17,
                    tracks: 2,
                },
                Request {
                    type_no: 18,
                    tracks: 1,
                },
            ],
        )
        .unwrap();
    let grant = Grant {
        ticket,
        image: Range {
            start: 0x20010000,
            end: 0x20011000,
        },
        state: Range {
            start: 0x20020000,
            end: 0x20021000,
        },
    };
    (loads, ticket, grant)
}
fn failure(loads: &Loads, ticket: Ticket, grant: &Grant, specs: &[Spec], cap: &Limits) -> Error {
    match prepare(loads, ticket, grant, specs, cap) {
        Ok(_) => panic!("unexpected admission"),
        Err(e) => e,
    }
}

#[test]
fn rows_follow_reservations_and_eight_byte_stride_without_publication() {
    let (loads, ticket, grant) = setup();
    let plan = prepare(
        &loads,
        ticket,
        &grant,
        &[spec(18, 0x20020020), spec(17, 0x20020000)],
        &limits(),
    )
    .unwrap();
    let row = plan.rows()[0].unwrap();
    assert_eq!(
        row.words,
        [
            1,
            3,
            ticket.generation(),
            2,
            0x20020000,
            3,
            0x20020000,
            0x20020020,
            0xb88000,
            0xb88000,
            0xb88080,
            0,
            0,
            0,
            0,
            0
        ]
    );
    assert_eq!(
        plan.rows()[1].unwrap().words[6..8],
        [0x20020020, 0x20020030]
    );
    assert!(plan.rows()[2..].iter().all(Option::is_none));
    assert_eq!(loads.lookup(17), None);
    assert_eq!(loads.capacity().rows_used, 2);
}

#[test]
fn whole_batch_rejects_overlap_bounds_alignment_and_overflow() {
    let (loads, ticket, grant) = setup();
    for (base, bytes) in [
        (0x20020018, 12),
        (0x20020ff8, 12),
        (0x20020021, 12),
        (0xfffffff8, 12),
        (0x20020020, 0),
        (0x20020020, 4097),
    ] {
        let mut bad = spec(18, base);
        bad.state_bytes = bytes;
        assert_eq!(
            failure(
                &loads,
                ticket,
                &grant,
                &[spec(17, 0x20020000), bad],
                &limits()
            ),
            Error::State
        );
        assert_eq!(loads.capacity().rows_used, 2);
        assert_eq!(loads.lookup(17), None);
    }
}

#[test]
fn code_data_and_grants_cannot_be_interchanged() {
    let (loads, ticket, mut grant) = setup();
    let specs = [spec(17, 0x20020000), spec(18, 0x20020020)];
    for entry in [0x20010000, 0xb87fff, 0xb8807e, 0xb88080, u32::MAX] {
        let mut bad = specs;
        bad[1].reset_sw = entry;
        assert_eq!(
            failure(&loads, ticket, &grant, &bad, &limits()),
            Error::Executable
        );
    }
    let mut bad = specs;
    bad[1].process_sw = 0xb88100;
    assert_eq!(
        failure(&loads, ticket, &grant, &bad, &limits()),
        Error::Executable
    );
    grant.state = grant.image;
    assert_eq!(
        failure(&loads, ticket, &grant, &specs, &limits()),
        Error::Grant
    );
}

#[test]
fn all_declared_budgets_and_reservation_types_are_checked() {
    let (loads, ticket, grant) = setup();
    let specs = [spec(17, 0x20020000), spec(18, 0x20020020)];
    assert_eq!(
        failure(&loads, ticket, &grant, &specs[..1], &limits()),
        Error::Types
    );
    assert_eq!(
        failure(&loads, ticket, &grant, &[specs[0], specs[0]], &limits()),
        Error::Types
    );
    for index in 0..8 {
        let mut bad = specs;
        match index {
            0 => bad[1].reset_instructions = 492, // 492 + 9 clear + 500 overhead
            1 => bad[1].process_instructions = 801, // 2*100 + 801
            2 => bad[1].process_instructions = 0,
            3 => bad[1].call_depth = 5,
            4 => bad[1].stack_bytes = 129,
            5 => bad[1].max_events = 9,
            6 => bad[1].max_controls = 0,
            _ => bad[1].reset_instructions = u32::MAX,
        }
        assert_eq!(
            failure(&loads, ticket, &grant, &bad, &limits()),
            Error::Budget
        );
    }
}

#[test]
fn stale_ticket_and_started_transfer_cannot_prepare_rows() {
    let (mut loads, ticket, grant) = setup();
    let specs = [spec(17, 0x20020000), spec(18, 0x20020020)];
    loads.start(ticket).unwrap();
    assert_eq!(
        failure(&loads, ticket, &grant, &specs, &limits()),
        Error::Load(LoadError::WrongPhase)
    );
    loads.cancel(ticket).unwrap();
    loads.detached(ticket).unwrap();
    assert_eq!(
        failure(&loads, ticket, &grant, &specs, &limits()),
        Error::Load(LoadError::StaleTicket)
    );
    let other = loads
        .reserve(
            4,
            &[
                Request {
                    type_no: 17,
                    tracks: 2,
                },
                Request {
                    type_no: 18,
                    tracks: 1,
                },
            ],
        )
        .unwrap();
    assert_eq!(
        failure(&loads, other, &grant, &specs, &limits()),
        Error::Grant
    );
}
