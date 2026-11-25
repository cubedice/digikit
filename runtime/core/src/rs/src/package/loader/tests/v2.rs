//! A file of persistent V2 instruments over the fakes: the loader places it in one block, hands it to the owner of the instruments and publishes
//! nothing; the owner publishes the CF side later (`publish_v2`). The owner is `FakeImages` (`v2_idle`, `v2_admit`, the `Admit` and `Queue` events).
use super::*;

const SECTION: usize = dkneeds::fixtures::V2_SECTION_BYTES;
/// The fixture module: a 32-byte image without bss, and an empty keys table of 8 bytes.
const IMAGE: u32 = 32;
const KEYS: u32 = 8;
/// The output buffer the fake owner asks for.
const OUTPUT: u32 = 64;
/// Where the loader keeps its copies in the block: each part starts on 16 bytes.
const KEYS_AT: u32 = 32;
const SECTION_AT: u32 = 48;
const OUTPUT_AT: u32 = 256;
const BLOCK: u32 = OUTPUT_AT + OUTPUT;

/// The fixture module with the canonical V2 section (`dkneeds::fixtures`) as its DSP section.
fn v2_file() -> Vec<u8> {
    let mut v = module_file();
    let at = v.len() as u32;
    v.extend_from_slice(&dkneeds::fixtures::v2_section());
    put(&mut v, dkneeds::H_DSP_OFF, at);
    put(&mut v, dkneeds::H_DSP_SIZE, SECTION as u32);
    seal(&mut v);
    v
}

/// A loader over the V2 file whose owner admits it for selection index 3.
unsafe fn v2_loader() -> Fake {
    let mut l = loader_of(Init::Ok, v2_file());
    l.m.v2_admit = Ok(OUTPUT);
    crate::selection::BOOT_IDX = 3;
    l
}

/// The bytes of the live block at `addr`.
unsafe fn block(l: &Fake, addr: u32) -> &[u8] {
    let b = l.m.live.iter().find(|b| b.0 == addr).expect("the block is live");
    core::slice::from_raw_parts(b.1.as_ptr() as *const u8, b.1.len() * 16)
}

#[test]
fn a_v2_file_is_placed_in_one_block_and_handed_to_the_owner_unpublished() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let file = v2_file();
        let mut l = v2_loader();
        assert_eq!(l.load(PATH.as_ptr()), LOADING);
        let base = FIRST_ADDR;
        // the owner saw the whole section, for the selection of this load
        assert!(l.m.log.contains(&Ev::Admit(SECTION, 3)));
        // one block: the image, then the copies of the keys table and of the section, then the output buffer of the relocated DSP image
        let allocs: Vec<_> = l.m.log.iter().filter(|e| matches!(e, Ev::Alloc(..))).copied().collect();
        assert_eq!(allocs, vec![Ev::Alloc(base, BLOCK)]);
        let mem = block(&l, base);
        let keys_off = file.len() - SECTION - KEYS as usize;
        assert_eq!(&mem[KEYS_AT as usize..(KEYS_AT + KEYS) as usize], &file[keys_off..keys_off + KEYS as usize]);
        assert_eq!(&mem[SECTION_AT as usize..SECTION_AT as usize + SECTION], &dkneeds::fixtures::v2_section()[..]);
        // the needs reader (the hosts, the menu's scan) counts this block, and nothing for a transfer
        let needs = dkneeds::needs_of_v2(&mut &file[..]).unwrap();
        assert_eq!((needs.base.arena, needs.base.arena_xfer), (dkneeds::arena_block(BLOCK).unwrap(), 0));
        // the load went to the owner, with the addresses of its parts
        assert_eq!(
            *l.m.log.last().unwrap(),
            Ev::Queue(InstrumentLoad {
                rec: 0,
                selection: 3,
                image_size: IMAGE,
                keys: base + KEYS_AT,
                keys_size: KEYS,
                section: base + SECTION_AT,
                section_size: SECTION as u32,
                output: base + OUTPUT_AT,
                output_size: OUTPUT,
                init: base,
                tick: 0,
                menu: 0,
            })
        );
        // the record is taken and holds the block, but nothing is published: no init, no registry row, no count of a load
        let s = st();
        assert_eq!((s.modules[0].used, s.modules[0].base, s.modules[0].size, s.loads), (2, base, BLOCK, 0));
        assert!(!l.m.log.iter().any(|e| matches!(e, Ev::Init(..))));
        assert!(l.r.log.is_empty());
        crate::selection::BOOT_IDX = 0;
    }
}

#[test]
fn no_load_starts_while_the_instrument_owner_is_busy() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        // any file: the plain module of the other tests
        let mut l = loader(Init::Ok);
        l.m.v2_idle = false;
        assert_eq!(l.load(PATH.as_ptr()), E_DSP_XFER);
        assert!(l.m.log.is_empty(), "the file is not even staged");
        l.m.v2_idle = true;
        assert_eq!(l.load(PATH.as_ptr()), OK);
    }
}

#[test]
fn a_v2_file_the_owner_refuses_takes_nothing() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        // without an owner (the fake's default, as a device without core/instrument-play): E_CONTRACT
        let mut l = loader_of(Init::Ok, v2_file());
        assert_eq!(l.load(PATH.as_ptr()), E_CONTRACT);
        // an owner that does not take the section or its machine keys
        l.m.v2_admit = Err(E_FORMAT);
        assert_eq!(l.load(PATH.as_ptr()), E_FORMAT);
        assert!(!l.m.log.iter().any(|e| matches!(e, Ev::Alloc(..) | Ev::Queue(..))));
        assert!(l.m.live.is_empty());
        assert_eq!(st().modules[0].used, 0);
        // the record is free: the same file loads once the owner admits it
        l.m.v2_admit = Ok(OUTPUT);
        assert_eq!(l.load(PATH.as_ptr()), LOADING);
        assert_eq!(st().modules[0].used, 2);
    }
}

#[test]
fn a_v2_file_that_finds_no_memory_is_not_handed_over() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let mut l = v2_loader();
        l.m.fail_alloc = Some(1);
        assert_eq!(l.load(PATH.as_ptr()), E_NOMEM);
        assert!(!l.m.log.iter().any(|e| matches!(e, Ev::Queue(..))));
        assert_eq!(st().modules[0].used, 0);
        crate::selection::BOOT_IDX = 0;
    }
}

#[test]
fn the_owner_publishes_the_cf_side_for_the_selection_of_the_load() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let mut l = v2_loader();
        assert_eq!(l.load(PATH.as_ptr()), LOADING);
        let base = FIRST_ADDR;
        // later, with another selection index current: the keys come from the copy in the block
        crate::selection::BOOT_IDX = 5;
        let keys = block(&l, base).as_ptr().add(KEYS_AT as usize);
        assert_eq!(l.publish_v2(0, keys, KEYS, base, 0, IMAGE, 3), OK);
        assert_eq!(crate::selection::BOOT_IDX, 5, "the current selection index is put back");
        assert!(l.m.log.contains(&Ev::Init(0, base)));
        assert_eq!(l.r.log, vec![Ev::Add(0)]);
        let s = st();
        assert_eq!((s.modules[0].used, s.loads), (1, 1));
        // the record block of init went back; the module block stays
        assert_eq!(l.m.live.iter().map(|b| b.0).collect::<Vec<_>>(), vec![base]);
        crate::selection::BOOT_IDX = 0;
    }
}

#[test]
fn a_failed_publication_undoes_init_and_leaves_the_block_to_the_owner() {
    let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let mut l = v2_loader();
        assert_eq!(l.load(PATH.as_ptr()), LOADING);
        let base = FIRST_ADDR;
        l.m.init = Init::Fails(E_INIT);
        let keys = block(&l, base).as_ptr().add(KEYS_AT as usize);
        assert_eq!(l.publish_v2(0, keys, KEYS, base, 0, IMAGE, 3), E_INIT);
        assert_eq!(l.r.log, vec![Ev::Remove(0)], "nothing was added, the undo still asks the registry");
        assert_eq!(st().loads, 0);
        // the owner detaches the DSP side first and then gives the block back: the loader frees nothing here
        assert_eq!(l.m.live.iter().map(|b| b.0).collect::<Vec<_>>(), vec![base]);
        assert!(!l.m.log.iter().any(|e| matches!(e, Ev::Free(a) if *a == base)));
        // a keys table that is not one is refused before init runs
        l.m.log.clear();
        assert_ne!(l.publish_v2(0, block(&l, base).as_ptr(), KEYS, base, 0, IMAGE, 3), OK);
        assert!(!l.m.log.iter().any(|e| matches!(e, Ev::Init(..))));
        crate::selection::BOOT_IDX = 0;
    }
}
