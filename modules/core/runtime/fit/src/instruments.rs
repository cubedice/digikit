//! V2 fit planning before the runtime capacity protocol is extended. Missing
//! instrument capacity is unsupported, never unlimited. This is not admission:
//! the loader must still check machine contract, descriptors and DSP readiness.
use super::{fits_now, take as take_legacy, Short};
use dkneeds::V2Needs;
use dkrt_sdk::{instrument_registry::Capacity, DkrtCap};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Failure {
    Unsupported,
    Rows { need: u32, free: u32 },
    Tracks { need: u32, free: u32 },
    Legacy(Short),
}

pub fn fits(n: &V2Needs, caps: &[DkrtCap], instruments: Option<&Capacity>) -> Result<(), Failure> {
    if n.instruments.rows != 0 || n.instruments.tracks != 0 || n.instruments.state_bytes != 0 {
        let c = instruments.ok_or(Failure::Unsupported)?;
        let free = c.rows_limit.saturating_sub(c.rows_used);
        if n.instruments.rows > free {
            return Err(Failure::Rows {
                need: n.instruments.rows,
                free,
            });
        }
        let free = c.tracks_limit.saturating_sub(c.tracks_used);
        if n.instruments.tracks > free {
            return Err(Failure::Tracks {
                need: n.instruments.tracks,
                free,
            });
        }
    }
    if let Some(short) = fits_now(&n.base, caps) {
        return Err(Failure::Legacy(short));
    }
    Ok(())
}

/// Consume a planning snapshot only if every resource fits. This does not reserve
/// the live registry: the loader must call Registry::reserve in its owning task.
pub fn take(
    n: &V2Needs,
    caps: &mut [DkrtCap],
    instruments: Option<&mut Capacity>,
) -> Result<(), Failure> {
    fits(n, caps, instruments.as_deref())?;
    take_legacy(&n.base, caps);
    if let Some(c) = instruments {
        c.rows_used += n.instruments.rows;
        c.tracks_used += n.instruments.tracks;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use dkneeds::{InstrumentNeeds, Needs};
    use dkrt_sdk::{
        instrument_registry::{Registry, Request},
        proto::*,
    };

    fn needs() -> V2Needs {
        V2Needs {
            base: Needs {
                dsp_area: 640,
                dsp_align: 8,
                ..Needs::default()
            },
            instruments: InstrumentNeeds {
                rows: 1,
                tracks: 2,
                state_bytes: 528,
            },
        }
    }
    fn dsp(used: u32) -> DkrtCap {
        DkrtCap {
            kind: CAPK_DSP_AREA as u32,
            limit: 1024,
            used,
            largest: 1024 - used,
        }
    }
    #[test]
    fn unknown_capacity_refuses_v2_but_preserves_legacy_behavior() {
        let n = needs();
        assert_eq!(fits(&n, &[dsp(0)], None), Err(Failure::Unsupported));
        let legacy = V2Needs {
            base: n.base,
            ..V2Needs::default()
        };
        assert_eq!(fits(&legacy, &[dsp(0)], None), Ok(()));
    }
    #[test]
    fn reserves_all_resources_or_changes_neither_snapshot() {
        let n = needs();
        let mut c = Registry::new(4).capacity();
        let mut caps = [dsp(512)];
        let before = c;
        assert!(matches!(
            take(&n, &mut caps, Some(&mut c)),
            Err(Failure::Legacy(_))
        ));
        assert_eq!(c, before);
        assert_eq!(caps[0].used, 512);
        caps[0] = dsp(0);
        take(&n, &mut caps, Some(&mut c)).unwrap();
        assert_eq!((c.rows_used, c.tracks_used, caps[0].used), (1, 2, 640));
        assert_eq!(caps[0].largest, 384);
    }
    #[test]
    fn tracks_and_rows_remain_reserved_during_retirement() {
        let mut r = Registry::new(2);
        let ticket = r
            .reserve(
                0,
                &[Request {
                    type_no: 17,
                    tracks: 2,
                }],
            )
            .unwrap();
        r.publish(ticket).unwrap();
        r.begin_retire(ticket).unwrap();
        let mut c = r.capacity();
        let mut caps = [dsp(0)];
        assert_eq!(
            take(&needs(), &mut caps, Some(&mut c)),
            Err(Failure::Tracks { need: 2, free: 0 })
        );
        assert_eq!(caps[0].used, 0);
        r.finish_retire(ticket).unwrap();
        assert_eq!(fits(&needs(), &caps, Some(&r.capacity())), Ok(()));
        let c = Capacity {
            rows_limit: 8,
            rows_used: 8,
            tracks_limit: 128,
            tracks_used: 8,
        };
        assert_eq!(
            fits(&needs(), &caps, Some(&c)),
            Err(Failure::Rows { need: 1, free: 0 })
        );
    }
}
