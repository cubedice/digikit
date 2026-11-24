use super::*;

fn request(type_no: u32, tracks: u32) -> Request {
    Request { type_no, tracks }
}

#[test]
fn only_acknowledged_publication_is_visible_and_retirement_keeps_capacity() {
    let mut r = Registry::new(32);
    let t = r.reserve(0, &[request(17, 16), request(18, 4)]).unwrap();
    assert_eq!((r.capacity().rows_used, r.capacity().tracks_used), (2, 20));
    assert_eq!(r.lookup(17), None);
    r.publish(t).unwrap();
    assert_eq!(
        r.lookup(17),
        Some(Entry {
            owner: 0,
            generation: t.generation(),
            type_no: 17,
            tracks: 16
        })
    );
    assert_eq!(r.lookup(18).unwrap().tracks, 4);
    assert_eq!(r.abort(t), Err(Error::WrongPhase));
    r.begin_retire(t).unwrap();
    assert_eq!(r.lookup(17), None);
    assert_eq!(r.lookup(18), None);
    assert_eq!(r.capacity().tracks_used, 20);
    assert_eq!(r.reserve(1, &[request(17, 1)]), Err(Error::DuplicateType));
    assert_eq!(r.publish(t), Err(Error::WrongPhase));
    r.finish_retire(t).unwrap();
    assert_eq!(r.capacity().rows_used, 0);
    assert_eq!(r.capacity().tracks_used, 0);
}

#[test]
fn failed_batch_reservations_change_nothing() {
    let mut r = Registry::new(20);
    let t = r.reserve(7, &[request(17, 16)]).unwrap();
    let before = r.capacity();
    for (owner, reqs, error) in [
        (8, [request(18, 2), request(17, 1)], Error::DuplicateType),
        (8, [request(18, 2), request(18, 1)], Error::DuplicateType),
        (8, [request(18, 3), request(19, 2)], Error::Capacity),
        (8, [request(18, 2), request(19, 0)], Error::Invalid),
        (7, [request(18, 1), request(19, 1)], Error::OwnerBusy),
    ] {
        assert_eq!(r.reserve(owner, &reqs), Err(error));
        assert_eq!(r.capacity(), before);
        assert_eq!(r.lookup(18), None);
    }
    r.abort(t).unwrap();
    let next = r.reserve(8, &[request(18, 16)]).unwrap();
    assert_eq!(next.generation(), t.generation() + 1);
    r.publish(next).unwrap();
}

#[test]
fn stale_acknowledgements_cannot_change_a_reused_module_slot() {
    let mut r = Registry::new(16);
    let old = r.reserve(0, &[request(17, 16)]).unwrap();
    r.publish(old).unwrap();
    r.begin_retire(old).unwrap();
    r.finish_retire(old).unwrap();
    let new = r.reserve(0, &[request(17, 16)]).unwrap();
    assert_ne!(old.generation(), new.generation());
    for operation in [
        Registry::publish,
        Registry::abort,
        Registry::begin_retire,
        Registry::finish_retire,
    ] {
        assert_eq!(operation(&mut r, old), Err(Error::StaleTicket));
        assert_eq!(r.capacity().tracks_used, 16);
    }
    r.publish(new).unwrap();
    assert_eq!(r.lookup(17).unwrap().generation, new.generation());
}

#[test]
fn module_transactions_are_isolated_and_row_capacity_is_bounded() {
    let mut r = Registry::new(128);
    let a = r.reserve(1, &[request(16, 16), request(17, 16)]).unwrap();
    let b = r.reserve(2, &[request(18, 16), request(19, 16)]).unwrap();
    r.publish(a).unwrap();
    r.publish(b).unwrap();
    r.begin_retire(a).unwrap();
    assert_eq!(r.lookup(18).unwrap().owner, 2);
    let c = r
        .reserve(
            3,
            &[
                request(20, 1),
                request(21, 1),
                request(22, 1),
                request(23, 1),
            ],
        )
        .unwrap();
    assert_eq!(r.reserve(4, &[request(24, 1)]), Err(Error::Capacity));
    r.finish_retire(a).unwrap();
    r.abort(c).unwrap();
    assert_eq!(r.capacity().rows_used, 2);
    assert_eq!(r.lookup(19).unwrap().owner, 2);
}

#[test]
fn invalid_requests_and_generation_exhaustion_fail_closed() {
    let mut r = Registry::new(16);
    assert_eq!(r.reserve(0, &[]), Err(Error::Invalid));
    assert_eq!(r.reserve(0, &[request(17, 17)]), Err(Error::Invalid));
    assert_eq!(r.reserve(0, &[request(u32::MAX, 1)]), Err(Error::Invalid));
    assert_eq!(
        Registry::new(0).reserve(0, &[request(17, 1)]),
        Err(Error::Capacity)
    );
    r.last_generation = u32::MAX - 1;
    let last = r.reserve(0, &[request(17, 16)]).unwrap();
    assert_eq!(last.generation(), u32::MAX);
    r.abort(last).unwrap();
    assert_eq!(
        r.reserve(0, &[request(17, 16)]),
        Err(Error::GenerationExhausted)
    );
    assert_eq!(r.capacity().rows_used, 0);
}
