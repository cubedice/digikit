//! Resolve V2 keys against a captured boot-selection index, never mutable
//! "currently loading" state. This borrows the trusted boot assignment table.
use crate::instrument_section::Section;

pub struct Context<'a> {
    pub selection: u32,
    pub indices: &'a [u32],
    pub keys: &'a [u32],
    pub types: &'a [u32],
    pub first: u32,
    pub last: u32,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    Map,
    Undeclared,
    Ambiguous,
    Unassigned,
    Conflict,
}
#[derive(Debug, PartialEq, Eq)]
pub struct Resolved {
    values: [u32; 8],
    count: usize,
}
impl Resolved {
    pub fn as_slice(&self) -> &[u32] {
        &self.values[..self.count]
    }
}
impl Context<'_> {
    fn resolve(&self, key: u32) -> Result<u32, Error> {
        let mut found = None;
        for i in 0..self.keys.len() {
            if self.indices[i] == self.selection && self.keys[i] == key {
                if found.is_some() {
                    return Err(Error::Ambiguous);
                }
                // Pinned keys still need a declaration in this selection. The
                // legacy planner leaves their assigned-type field at zero.
                found = Some(if key >> 8 == 0 { key } else { self.types[i] });
            }
        }
        let value = found.ok_or(Error::Undeclared)?;
        if !(self.first..=self.last).contains(&value) {
            return Err(Error::Unassigned);
        }
        Ok(value)
    }
}
impl Section<'_> {
    /// Resolve every record before returning a batch. occupied checks the live
    /// CF machine registry and legacy DSP rows, including unpublished owners.
    /// Caller serializes this snapshot with reservation on the UI task. V2's
    /// own reserved/live/retiring conflicts are checked again by admission.
    pub fn resolve_types(
        &self,
        context: &Context<'_>,
        mut occupied: impl FnMut(u32) -> bool,
    ) -> Result<Resolved, Error> {
        if context.first == 0
            || context.first > context.last
            || context.last > 255
            || context.keys.len() > 64
            || context.keys.len() != context.indices.len()
            || context.keys.len() != context.types.len()
        {
            return Err(Error::Map);
        }
        let mut result = Resolved {
            values: [0; 8],
            count: 0,
        };
        for record in self.records().iter().flatten() {
            let value = context.resolve(record.key)?;
            if result.as_slice().contains(&value) || occupied(value) {
                return Err(Error::Conflict);
            }
            result.values[result.count] = value;
            result.count += 1;
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::instrument_section::tests::{fixture, limits};

    #[test]
    fn hashed_assignment_uses_saved_selection_not_another_modules_entry() {
        let mut bytes = fixture();
        bytes[120..124].copy_from_slice(&0xabc000u32.to_be_bytes());
        let section = Section::parse(&bytes, 1, limits()).unwrap();
        let mut context = Context {
            selection: 3,
            indices: &[1, 3],
            keys: &[0xabc000, 0xabc000],
            types: &[16, 21],
            first: 16,
            last: 23,
        };
        assert_eq!(
            section
                .resolve_types(&context, |_| false)
                .unwrap()
                .as_slice(),
            &[21]
        );
        context.selection = 1;
        assert_eq!(
            section
                .resolve_types(&context, |_| false)
                .unwrap()
                .as_slice(),
            &[16]
        );
        context.selection = 2;
        assert_eq!(
            section.resolve_types(&context, |_| false),
            Err(Error::Undeclared)
        );
    }
    #[test]
    fn pinned_keys_require_a_declaration_and_an_unoccupied_dynamic_row() {
        let bytes = fixture();
        let section = Section::parse(&bytes, 1, limits()).unwrap();
        let mut context = Context {
            selection: 3,
            indices: &[3],
            keys: &[20],
            types: &[0],
            first: 16,
            last: 23,
        };
        assert_eq!(
            section
                .resolve_types(&context, |_| false)
                .unwrap()
                .as_slice(),
            &[20]
        );
        assert_eq!(
            section.resolve_types(&context, |t| t == 20),
            Err(Error::Conflict)
        );
        context.selection = 4;
        assert_eq!(
            section.resolve_types(&context, |_| false),
            Err(Error::Undeclared)
        );
        context.selection = 3;
        context.first = 21;
        assert_eq!(
            section.resolve_types(&context, |_| false),
            Err(Error::Unassigned)
        );
    }
    #[test]
    fn malformed_and_ambiguous_maps_never_choose_a_first_match() {
        let bytes = fixture();
        let section = Section::parse(&bytes, 1, limits()).unwrap();
        let mut context = Context {
            selection: 3,
            indices: &[3, 3],
            keys: &[20, 20],
            types: &[0, 0],
            first: 16,
            last: 23,
        };
        assert_eq!(
            section.resolve_types(&context, |_| false),
            Err(Error::Ambiguous)
        );
        context.indices = &[3];
        assert_eq!(
            section.resolve_types(&context, |_| panic!("invalid map")),
            Err(Error::Map)
        );
        context.indices = &[3, 3];
        context.last = 256;
        assert_eq!(section.resolve_types(&context, |_| false), Err(Error::Map));
    }
    #[test]
    fn exhausted_or_stock_assignments_are_rejected_before_occupancy_checks() {
        let mut bytes = fixture();
        bytes[120..124].copy_from_slice(&0xabc000u32.to_be_bytes());
        let section = Section::parse(&bytes, 1, limits()).unwrap();
        for value in [0, 1, 15, 24, u32::MAX] {
            let values = [value];
            let context = Context {
                selection: 3,
                indices: &[3],
                keys: &[0xabc000],
                types: &values,
                first: 16,
                last: 23,
            };
            assert_eq!(
                section.resolve_types(&context, |_| panic!("out of range")),
                Err(Error::Unassigned)
            );
        }
    }
    #[test]
    fn different_keys_cannot_resolve_to_the_same_type_in_one_batch() {
        let old = fixture();
        let mut bytes = [0u8; 224];
        bytes[..136].copy_from_slice(&old[..136]);
        bytes[136..152].copy_from_slice(&old[120..136]);
        bytes[152..].copy_from_slice(&old[136..]);
        for (at, value) in [
            (0x18, 160u32),
            (0x44, 2),
            (0x48, 152),
            (120, 0xabc000),
            (136, 0xdef000),
        ] {
            bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
        }
        let section = Section::parse(&bytes, 1, limits()).unwrap();
        let context = Context {
            selection: 3,
            indices: &[3, 3],
            keys: &[0xabc000, 0xdef000],
            types: &[20, 20],
            first: 16,
            last: 23,
        };
        assert_eq!(
            section.resolve_types(&context, |_| false),
            Err(Error::Conflict)
        );
    }
}
