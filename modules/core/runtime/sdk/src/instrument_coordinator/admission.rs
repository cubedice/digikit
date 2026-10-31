//! Prepare an inert V2 load using the owner's bookkeeping and shared ledger.
use super::*;
use crate::{
    instrument_admission::{self, Limits},
    instrument_registry::{self, Request},
    instrument_section::{RelocateError, Section},
};

#[derive(Debug, PartialEq, Eq)]
pub enum AdmissionError {
    Coordinator(Error),
    Types,
    Budget,
    Unaccounted,
    Registry(instrument_registry::Error),
    Memory(MemoryError),
    Plan(instrument_admission::Error),
    Relocate(RelocateError),
    Cleanup(MemoryError),
}

#[derive(Clone, Copy)]
pub(super) struct Cost {
    ticket: Ticket,
    process: u32,
    tracks: u32,
}

impl Cost {
    pub(super) fn matches(&self, plan: &Plan) -> bool {
        self.ticket == plan.ticket() && self.process == plan.process_cost
    }
}

impl<T: Transport> Coordinator<'_, T> {
    /// Prepare without sending or publishing anything. The caller supplies the
    /// actual shared DSP ledger, resolved type numbers checked against builtin
    /// and V1 types, callback budgets after legacy reservations and trusted imports.
    /// process_total is the total V2 allowance: existing V2 costs are subtracted
    /// here. Prepared, live and retiring modules all retain their reservation.
    /// Section parsing must precede this call. Callback instruction boundaries
    /// and actual resource usage remain the trusted packer/loader's obligation.
    /// Output is written only after all planning checks pass; relocation errors
    /// leave it unchanged. Keep successful output alive through begin and drain.
    /// A failed attempt may consume a generation, but releases its reservations.
    pub fn admit(
        &mut self,
        owner: u32,
        section: &Section<'_>,
        types: &[u32],
        limits: &Limits,
        ledger: &mut Ledger<'_>,
        output: &mut [u8],
        resolve: impl FnMut(&[u8]) -> Option<u32>,
    ) -> Result<Plan, AdmissionError> {
        self.admission_idle()?;
        let (used, tracks) = self
            .costs
            .iter()
            .flatten()
            .try_fold((0u32, 0u32), |(cost, tracks), c| {
                Some((cost.checked_add(c.process)?, tracks.checked_add(c.tracks)?))
            })
            .ok_or(AdmissionError::Budget)?;
        // An externally adopted ledger lacks trusted per-plan costs. Do not
        // silently treat its existing modules as free; finish/retire them first.
        if tracks != self.loads.capacity().tracks_used {
            return Err(AdmissionError::Unaccounted);
        }
        let remaining = limits
            .process_total
            .checked_sub(used)
            .ok_or(AdmissionError::Budget)?;
        let available = Limits {
            process_total: remaining,
            l2: limits.l2,
            reset_frame: limits.reset_frame,
            reset_overhead: limits.reset_overhead,
            call_depth: limits.call_depth,
            stack_bytes: limits.stack_bytes,
        };
        let count = section.records().iter().flatten().count();
        if types.len() != count {
            return Err(AdmissionError::Types);
        }
        let mut requests = [Request {
            type_no: 0,
            tracks: 0,
        }; 8];
        for (i, record) in section.records().iter().flatten().enumerate() {
            requests[i] = Request {
                type_no: types[i],
                tracks: record.descriptor.tracks,
            };
        }
        let ticket = self
            .loads
            .reserve(owner, &requests[..count])
            .map_err(AdmissionError::Registry)?;
        let grant = match self
            .loads
            .reserve_memory(ticket, ledger, &section.memory_request())
        {
            Ok(grant) => grant,
            Err(error) => {
                self.loads
                    .cancel(ticket)
                    .map_err(AdmissionError::Registry)?;
                return Err(AdmissionError::Memory(error));
            }
        };
        let prepared = instrument_admission::prepare_packed(
            &self.loads,
            ticket,
            &grant,
            &section.packed(types),
            &available,
        )
        .map_err(AdmissionError::Plan)
        .and_then(|plan| {
            section
                .relocate_into(grant.image.start, output, resolve)
                .map_err(AdmissionError::Relocate)?;
            Ok(plan)
        });
        match &prepared {
            Ok(plan) => {
                self.costs[owner as usize] = Some(Cost {
                    ticket,
                    process: plan.process_cost,
                    tracks: requests[..count].iter().map(|r| r.tracks).sum(),
                });
            }
            Err(_) => self.cancel_admission(ticket, ledger)?,
        }
        prepared
    }

    /// Cancel only a prepared, never-started admission. This cannot retire a
    /// live image or bypass a required detach. No transport callbacks occur.
    pub fn cancel_admission(
        &mut self,
        ticket: Ticket,
        ledger: &mut Ledger<'_>,
    ) -> Result<(), AdmissionError> {
        self.admission_idle()?;
        self.loads
            .admission_entries(ticket)
            .map_err(AdmissionError::Registry)?;
        self.loads
            .cancel(ticket)
            .map_err(AdmissionError::Registry)?;
        if let Err(error) = self.loads.release_memory(ticket, ledger) {
            self.blocked = Some(Failure::Protocol);
            return Err(AdmissionError::Cleanup(error));
        }
        self.costs[ticket.owner() as usize] = None;
        Ok(())
    }

    /// Reserved V2 processing cost, including prepared and retiring loads.
    pub fn process_reserved(&self) -> u32 {
        self.costs.iter().flatten().map(|c| c.process).sum()
    }

    fn admission_idle(&self) -> Result<(), AdmissionError> {
        if self.blocked.is_some() {
            return Err(AdmissionError::Coordinator(Error::Blocked));
        }
        if self.job.is_some()
            || !self.driver.is_idle()
            || matches!(
                self.handoff,
                Handoff::Acquire { .. } | Handoff::Release { .. }
            )
        {
            return Err(AdmissionError::Coordinator(Error::Busy));
        }
        Ok(())
    }
}
