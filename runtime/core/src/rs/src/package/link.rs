//! The DSP link of the device: the queue of the crate `dkrt-link` over the transport of `core/dsp-link` (docs/plans/runtime-and-services.md, R6).
use core::ptr::addr_of_mut;
pub use dkrt_link::*;
use dkrt_abi::link::*;

// ---- the link of the device ------------------------------------------------------------------------------------------------

/// `core/dsp-link` through the loader's call wrappers.
pub struct DspTransport;

impl Transport for DspTransport {
    unsafe fn send(&mut self, win: u32, dst: u32, buf: *const u8, len: u32) -> Sent {
        // A V2 instrument transaction drains or owns the link (instrument.rs). While it owns the link nothing of the legacy side is sent; the
        // queue keeps it. While it drains, the module loads already queued must finish: they are what the drain waits for (`instrument::tick`
        // takes the link only when the loader is idle with nothing waiting), so their transfers go on. With the loader idle a drain holds
        // every other transfer (a live effect value), as before the queue (`fx_word_start`).
        if legacy_held() {
            return Sent::Refused;
        }
        // What the loader did before the queue: the area send (`dl_send`) answered -2 (retained or a bad destination) with a failure, and every other
        // refusal, a word write included, was tried again until the loader's guard ended it. A retained state therefore holds word writes; it fails area sends.
        if win == 0 {
            let r = crate::package::dsp::call_send(dst, buf, len);
            if r == 0 {
                Sent::Accepted
            } else if r == -1 {
                Sent::Refused
            } else {
                Sent::Hard
            }
        } else if crate::package::dsp::retained() {
            Sent::Refused
        } else if crate::package::dsp::call_send_win(win, dst, buf as *const u32, len) == 0 {
            Sent::Accepted
        } else {
            Sent::Refused
        }
    }
    unsafe fn poll(&mut self) -> Poll {
        // a retained DSP state is never polled again (nothing it owns may change until the reboot)
        if crate::package::dsp::retained() {
            return Poll::Busy;
        }
        let mut s = crate::package::dsp::DlStatus { state: 0, err: 0, nchunks: 0, acked: 0, sent: 0, resent: 0, nerr: 0 };
        crate::package::dsp::call_poll(&mut s);
        if s.state == crate::package::dsp::DL_DONE {
            Poll::Done
        } else if s.state == crate::package::dsp::DL_ERROR {
            crate::package::dsp::ds().last_dl_err = s.err;
            Poll::Error(s.err)
        } else {
            Poll::Busy
        }
    }
    fn event(&mut self, what: u32, a: [u32; 4]) {
        // one flag test without a sink
        if !crate::package::resident::dlog::sink_present() && !crate::package::resident::dlog::sink_on(crate::package::resident::dlog::SINK_LOG) {
            return;
        }
        let flags = if what == EV_ERROR { dkrt_sdk::logk::L_WARN << dkrt_sdk::logk::LEVEL_SHIFT } else { crate::package::resident::dlog::INFO };
        unsafe { crate::package::resident::dlog::ev(dkrt_sdk::logk::DSP_LINK, flags, [what, a[0], a[1], a[2], a[3]]) }
    }
}

static mut LINK: Link<DspTransport> = Link::new(DspTransport);

/// The link of the device.
pub fn lk() -> &'static mut Link<DspTransport> {
    unsafe { &mut *addr_of_mut!(LINK) }
}

/// One UI pass of the link: settle the transfer in flight and start the next. `dkrt_tick` calls it before the loader and the services.
pub unsafe fn pump() {
    lk().pump()
}

/// Whether a V2 instrument transaction holds the legacy side of the DSP link now (instrument.rs): it owns the link (`Pause`), or it drains it and
/// the loader has nothing left that the drain waits for (`Drain` with the loader idle). The transport refuses every send then.
pub unsafe fn legacy_held() -> bool {
    match crate::package::instrument::legacy_policy() {
        dkrt_sdk::instrument_coordinator::LegacyPolicy::Run => false,
        dkrt_sdk::instrument_coordinator::LegacyPolicy::Drain => {
            let d = crate::package::dsp::ds();
            d.phase == crate::package::dsp::PH_IDLE && d.nwait == 0
        }
        dkrt_sdk::instrument_coordinator::LegacyPolicy::Pause => true,
    }
}

/// `Link::run` for the legacy side: false at once while a V2 instrument transaction holds the link (`legacy_held`). `run` waits in the UI task
/// for its result, and the transaction's owner runs later in the same pass (the last tick phase): spinning to the bound there froze the UI loop
/// (upstream's machines service parks a voice row with it at the boot). Every caller takes false as "try again later".
pub unsafe fn run(owner: Owner, seq: &Sequence) -> bool {
    if legacy_held() {
        return false;
    }
    lk().run(owner, seq)
}
