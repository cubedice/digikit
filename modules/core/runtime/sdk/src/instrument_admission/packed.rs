//! Compose bounded decoding with admission; no allocation or DSP writes.
use super::*;
use crate::instrument_descriptor as descriptor;

/// Slices bounded by a separately validated DKDS6 header. The image must be the
/// exact immutable source covered by the allocation grant. The loader resolves
/// keys and rejects builtin/V1 conflicts before supplying types in record order.
pub struct Packed<'a> {
    pub image: &'a [u8],
    pub records: &'a [u8],
    pub relocations: &'a [u8],
    pub executable: &'a [u8],
    pub resolved_types: &'a [u32],
}

/// Derive row inputs from the packed metadata and allocated image/state bases.
/// Whole-section validation, import resolution and instruction-boundary checking
/// are still loader responsibilities. Failure leaves ticket and grants untouched.
pub fn prepare_packed(
    loads: &Loads,
    ticket: Ticket,
    grant: &Grant,
    packed: &Packed<'_>,
    limits: &Limits,
) -> Result<Plan, Error> {
    let entries = loads.admission_entries(ticket).map_err(Error::Load)?;
    let image_bytes = u32::try_from(packed.image.len()).map_err(|_| Error::Grant)?;
    if !grant.image.valid()
        || !grant.state.valid()
        || grant.image.end - grant.image.start != image_bytes
    {
        return Err(Error::Grant);
    }
    let records = descriptor::records(
        packed.records,
        packed.image,
        packed.relocations,
        descriptor::Limits {
            state_bytes: grant.state.end - grant.state.start,
            cycles_process: limits.process_total,
            cycles_reset: limits.reset_frame,
            call_depth: limits.call_depth,
            stack_bytes: limits.stack_bytes,
        },
    )
    .map_err(Error::Descriptor)?;
    let ranges =
        descriptor::executable_ranges(packed.executable, image_bytes).map_err(Error::Descriptor)?;
    descriptor::validate_executables(packed.records, &ranges).map_err(Error::Descriptor)?;
    let count = records.iter().flatten().count();
    if packed.resolved_types.len() != count {
        return Err(Error::Types);
    }
    let extent = |offset: u32| -> Result<Range, Error> {
        let r = ranges
            .iter()
            .flatten()
            .find(|r| offset >= r.start && offset.checked_add(6).is_some_and(|end| end <= r.end))
            .ok_or(Error::Executable)?;
        Ok(Range {
            start: grant
                .image
                .start
                .checked_add(r.start)
                .ok_or(Error::Executable)?,
            end: grant
                .image
                .start
                .checked_add(r.end)
                .ok_or(Error::Executable)?,
        })
    };
    let make = |record: descriptor::Record, type_no: u32, state_base: u32| -> Result<Spec, Error> {
        let d = record.descriptor;
        let entry = entries
            .iter()
            .flatten()
            .find(|e| e.type_no == type_no)
            .ok_or(Error::Types)?;
        if entry.tracks != d.tracks {
            return Err(Error::Types);
        }
        Ok(Spec {
            type_no,
            state_base,
            state_bytes: d.state_bytes,
            text: extent(record.reset_offset)?,
            process_text: extent(record.process_offset)?,
            reset_sw: pc(grant
                .image
                .start
                .checked_add(record.reset_offset)
                .ok_or(Error::Executable)?),
            process_sw: pc(grant
                .image
                .start
                .checked_add(record.process_offset)
                .ok_or(Error::Executable)?),
            reset_instructions: d.cycles_reset,
            process_instructions: d.cycles_process,
            call_depth: d.call_depth,
            stack_bytes: d.stack_bytes,
            max_events: d.max_events,
            max_controls: d.max_controls,
            flags: d.flags,
        })
    };
    // The decoder guarantees at least one record; a fixed array avoids heap use.
    let first = make(
        records[0].ok_or(Error::Types)?,
        packed.resolved_types[0],
        grant.state.start,
    )?;
    let mut specs = [first; MAX_ROWS];
    let mut state = grant.state.start;
    for (i, record) in records.iter().flatten().enumerate() {
        specs[i] = make(*record, packed.resolved_types[i], state)?;
        state = state
            .checked_add(record.descriptor.allocation_bytes)
            .ok_or(Error::State)?;
    }
    prepare(loads, ticket, grant, &specs[..count], limits)
}

#[cfg(test)]
mod tests;
