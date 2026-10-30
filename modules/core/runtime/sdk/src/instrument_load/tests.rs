use super::*;
fn request(type_no: u32) -> Request {
    Request { type_no, tracks: 2 }
}
fn reserve(loads: &mut Loads) -> Ticket {
    loads.reserve(0, &[request(17)]).unwrap()
}

#[test]
fn transfer_then_reset_ack_is_required_before_publication() {
    let mut l = Loads::new(16);
    let t = reserve(&mut l);
    assert_eq!(l.initialized(t), Err(Error::WrongPhase));
    assert_eq!(l.image_acked(t), Err(Error::WrongPhase));
    assert_eq!(l.start(t), Ok(Command::SendImage(t)));
    assert_eq!(l.initialized(t), Err(Error::WrongPhase));
    assert_eq!(l.lookup(17), None);
    assert_eq!(l.image_acked(t), Ok(Command::Initialize(t)));
    assert_eq!(l.image_acked(t), Err(Error::WrongPhase));
    assert_eq!(l.lookup(17), None);
    assert_eq!(l.initialized(t), Ok(Command::Publish(t)));
    assert_eq!(l.lookup(17).unwrap().generation, t.generation());
    assert_eq!(l.initialized(t), Err(Error::WrongPhase));
    assert_eq!(l.cancel(t), Ok(Command::Detach(t)));
    assert_eq!(l.lookup(17), None);
    assert_eq!(l.capacity().tracks_used, 2);
    assert_eq!(l.detached(t), Ok(Command::Release(t)));
    assert_eq!(l.capacity().rows_used, 0);
}

#[test]
fn rollback_requires_detach_once_any_work_may_have_started() {
    for progress in 0..4 {
        let mut l = Loads::new(16);
        let t = reserve(&mut l);
        if progress >= 1 {
            l.start(t).unwrap();
        }
        if progress >= 2 {
            l.image_acked(t).unwrap();
        }
        if progress >= 3 {
            l.initialized(t).unwrap();
        }
        let command = l.cancel(t).unwrap();
        assert_eq!(l.lookup(17), None);
        if progress == 0 {
            assert_eq!(command, Command::Release(t));
            assert_eq!(l.capacity().rows_used, 0);
        } else {
            assert_eq!(command, Command::Detach(t));
            assert_eq!(l.capacity().rows_used, 1);
            assert_eq!(l.image_acked(t), Err(Error::WrongPhase));
            assert_eq!(l.initialized(t), Err(Error::WrongPhase));
            l.detached(t).unwrap();
            assert_eq!(l.capacity().rows_used, 0);
        }
    }
}

#[test]
fn timeouts_quarantine_until_matching_detach_not_late_initialization() {
    for progress in 1..5 {
        let mut l = Loads::new(2);
        let t = reserve(&mut l);
        l.start(t).unwrap();
        if progress >= 2 {
            l.image_acked(t).unwrap();
        }
        if progress >= 3 {
            l.initialized(t).unwrap();
        }
        if progress >= 4 {
            l.cancel(t).unwrap();
        }
        assert_eq!(l.timeout(t), Ok(Command::Detach(t)));
        assert_eq!(l.timeout(t), Ok(Command::Detach(t)));
        assert_eq!(l.lookup(17), None);
        assert_eq!(l.capacity().tracks_used, 2);
        assert_eq!(l.reserve(1, &[request(18)]), Err(Error::Capacity));
        assert_eq!(l.initialized(t), Err(Error::WrongPhase));
        assert_eq!(l.image_acked(t), Err(Error::WrongPhase));
        assert_eq!(l.detached(t), Ok(Command::Release(t)));
        let replacement = reserve(&mut l);
        assert_ne!(replacement.generation(), t.generation());
        assert_eq!(l.detached(t), Err(Error::StaleTicket));
        assert_eq!(l.timeout(t), Err(Error::StaleTicket));
        assert_eq!(l.capacity().tracks_used, 2);
    }
}

#[test]
fn interleaved_module_completions_do_not_publish_or_retire_other_owners() {
    let mut l = Loads::new(16);
    let a = reserve(&mut l);
    let b = l.reserve(1, &[request(18), request(19)]).unwrap();
    l.start(a).unwrap();
    l.start(b).unwrap();
    l.image_acked(b).unwrap();
    l.initialized(b).unwrap();
    l.timeout(a).unwrap();
    assert_eq!(l.lookup(17), None);
    assert_eq!(l.lookup(18).unwrap().owner, 1);
    assert_eq!(l.lookup(19).unwrap().owner, 1);
    l.detached(a).unwrap();
    assert_eq!(l.capacity().tracks_used, 4);
    assert_eq!(l.lookup(19).unwrap().generation, b.generation());
    l.cancel(b).unwrap();
    l.detached(b).unwrap();
    assert_eq!(l.capacity().rows_used, 0);
}
