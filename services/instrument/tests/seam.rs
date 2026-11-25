//! The check and copy of the image table `instrument_image` (`dkrt_svc_instrument::seam`).
use dkrt_svc_instrument::seam::{copy, usable, MAGIC, REQUIRED, VERSION, WORDS};

/// A table as the image writes it: every word set, so the required ones are non-zero.
fn table() -> Vec<u32> {
    let mut t: Vec<u32> = (0..WORDS as u32).map(|i| 0x4000_0000 + 4 * i).collect();
    t[0] = MAGIC;
    t[1] = VERSION;
    t[2] = WORDS as u32;
    t
}

#[test]
fn a_table_of_this_layout_is_usable() {
    let t = table();
    assert!(unsafe { usable(t.as_ptr()) });
}

#[test]
fn no_table_or_another_layout_is_refused() {
    assert!(!unsafe { usable(core::ptr::null()) });
    for (word, value) in [(0, MAGIC ^ 1), (1, VERSION + 1), (2, WORDS as u32 - 1)] {
        let mut t = table();
        t[word] = value;
        assert!(!unsafe { usable(t.as_ptr()) }, "word {word}");
    }
}

#[test]
fn a_missing_function_of_the_adapter_is_refused_and_a_missing_optional_one_is_not() {
    for &i in REQUIRED.iter() {
        let mut t = table();
        t[i] = 0;
        assert!(!unsafe { usable(t.as_ptr()) }, "required word {i}");
    }
    let optional: Vec<usize> = (3..WORDS).filter(|i| !REQUIRED.contains(i)).collect();
    assert!(!optional.is_empty());
    let mut t = table();
    for &i in &optional {
        t[i] = 0;
    }
    assert!(unsafe { usable(t.as_ptr()) });
}

#[test]
fn the_copy_is_the_table_word_for_word() {
    let t = table();
    let mut d = vec![0u32; WORDS];
    unsafe { copy(t.as_ptr(), d.as_mut_ptr()) };
    assert_eq!(d, t);
}
