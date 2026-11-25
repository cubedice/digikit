//! The module arena: first fit over an address-ordered free list with coalescing on free. Blocks are 16-byte aligned
//! (the ColdFire cache line). A block starts with a 16-byte header: word 0 the block size (header included, a multiple
//! of 16), word 1 the next free block while free or `USED` while allocated. The arena hands out and takes back device
//! addresses (`DevAddr`); the words of the list hold device addresses too, and the walk reads and writes them as plain
//! words through `DevAddr::ptr` and `of_ptr` (plain, not `mem::read32`: the arena is ordinary RAM that only this module
//! touches, and a volatile read would keep the compiler from combining the free list's reads).

use crate::{platform::st, state::DKRT_ARENA};
use dkrt_sdk::ARENA_BYTES;
use core::ptr::{addr_of_mut, null_mut};
use dkrt_abi::dev_addr::DevAddr;

const HDR: u32 = dkneeds::ARENA_HDR;
const USED: u32 = 0xa110_c8ed;

/// The block header at the device address `a` (null for 0).
#[inline(always)]
unsafe fn block(a: u32) -> *mut u32 {
    DevAddr::new(a).ptr()
}

/// The device address of the block header `b` (0 for null).
#[inline(always)]
unsafe fn addr(b: *mut u32) -> u32 {
    DevAddr::of_ptr(b).get()
}

pub unsafe fn init() {
    let s = st();
    let base = DevAddr::of_ptr(addr_of_mut!(DKRT_ARENA)).get();
    let lo = (base + 15) & !15;
    let hi = (base + ARENA_BYTES as u32) & !15;
    s.arena.lo = lo;
    s.arena.hi = hi;
    let b = block(lo);
    *b = hi - lo;
    *b.add(1) = 0;
    s.arena.free_head = lo;
}

/// A block of at least `n` bytes, 16-byte aligned; `NULL` when none fits.
pub unsafe fn alloc(n: u32) -> DevAddr {
    // dk_alloc runs in any task: the free list walk is one critical section (the mask nests).
    let sr = crate::platform::irq_mask();
    let a = alloc_walk(n);
    crate::platform::irq_restore(sr);
    DevAddr::of_ptr(a)
}

unsafe fn alloc_walk(n: u32) -> *mut u8 {
    let s = st();
    if n == 0 || n > ARENA_BYTES as u32 {
        return null_mut();
    }
    let mut need = ((n + 15) & !15) + HDR;
    let mut prev: *mut u32 = null_mut();
    let mut cur = block(s.arena.free_head);
    while !cur.is_null() {
        let sz = *cur;
        let next = *cur.add(1);
        if sz >= need {
            let rem = sz - need;
            let replacement = if rem >= 2 * HDR {
                let nb = block(addr(cur) + need);
                *nb = rem;
                *nb.add(1) = next;
                addr(nb)
            } else {
                need = sz;
                next
            };
            if prev.is_null() {
                s.arena.free_head = replacement;
            } else {
                *prev.add(1) = replacement;
            }
            *cur = need;
            *cur.add(1) = USED;
            return (cur as *mut u8).add(HDR as usize);
        }
        prev = cur;
        cur = block(next);
    }
    null_mut()
}

/// As `alloc`, from the top of the arena: the free block at the highest address that fits gives its upper end. For a block that is freed again soon (the copy of a
/// module's DSP image while the link sends it): above the blocks that stay, it leaves no hole between them when it goes, so a later module finds the free bytes in one
/// piece (the fork: the hardware image's arena is full to a few KB, docs/plans/upstream-merge-1124).
pub unsafe fn alloc_top(n: u32) -> DevAddr {
    let sr = crate::platform::irq_mask();
    let a = alloc_top_walk(n);
    crate::platform::irq_restore(sr);
    DevAddr::of_ptr(a)
}

unsafe fn alloc_top_walk(n: u32) -> *mut u8 {
    let s = st();
    if n == 0 || n > ARENA_BYTES as u32 {
        return null_mut();
    }
    let need = ((n + 15) & !15) + HDR;
    // the last free block that fits (the list is in address order) and the block before it in the list
    let mut prev: *mut u32 = null_mut();
    let mut cur = block(s.arena.free_head);
    let mut fit: *mut u32 = null_mut();
    let mut fit_prev: *mut u32 = null_mut();
    while !cur.is_null() {
        if *cur >= need {
            fit = cur;
            fit_prev = prev;
        }
        prev = cur;
        cur = block(*cur.add(1));
    }
    if fit.is_null() {
        return null_mut();
    }
    let rem = *fit - need;
    let b = if rem >= 2 * HDR {
        // the free block keeps its lower part and its place in the list; the new block is its upper end
        *fit = rem;
        let b = block(addr(fit) + rem);
        *b = need;
        b
    } else {
        // the whole block leaves the list (its size word stays)
        let next = *fit.add(1);
        if fit_prev.is_null() {
            s.arena.free_head = next;
        } else {
            *fit_prev.add(1) = next;
        }
        fit
    };
    *b.add(1) = USED;
    (b as *mut u8).add(HDR as usize)
}

/// Return a block. An address that is not a live block of this arena is ignored; returns whether it was freed.
pub unsafe fn free(p: DevAddr) -> bool {
    let sr = crate::platform::irq_mask();
    let freed = free_walk(p.get());
    crate::platform::irq_restore(sr);
    freed
}

unsafe fn free_walk(a: u32) -> bool {
    let s = st();
    if a < s.arena.lo + HDR || a >= s.arena.hi || a & 15 != 0 {
        return false;
    }
    let h = block(a - HDR);
    if *h.add(1) != USED {
        return false;
    }
    let size = *h;
    let mut prev: *mut u32 = null_mut();
    let mut cur = block(s.arena.free_head);
    while !cur.is_null() && addr(cur) < addr(h) {
        prev = cur;
        cur = block(*cur.add(1));
    }
    *h.add(1) = addr(cur);
    // merge with the next block
    if !cur.is_null() && addr(h) + size == addr(cur) {
        *h = size + *cur;
        *h.add(1) = *cur.add(1);
    }
    // merge with the previous block
    if !prev.is_null() && addr(prev) + *prev == addr(h) {
        *prev += *h;
        *prev.add(1) = *h.add(1);
    } else if prev.is_null() {
        s.arena.free_head = addr(h);
    } else {
        *prev.add(1) = addr(h);
    }
    true
}

#[cfg(test)]
mod top_tests {
    use super::*;
    use crate::fake_platform::test_arena::TestArena;

    #[test]
    fn a_block_from_the_top_leaves_no_hole_when_it_goes() {
        let _a = TestArena::lock();
        unsafe {
            init();
            let (free0, _) = stats();
            let a = alloc(1000);
            let t = alloc_top(4000);
            let b = alloc(1000);
            assert!(!a.is_null() && !t.is_null() && !b.is_null());
            // the transient block is above the two that stay
            assert!(t.get() > b.get() && b.get() > a.get());
            assert!(free(t));
            let (free1, largest) = stats();
            assert_eq!(largest + HDR, free1, "the free bytes are one piece");
            assert!(free(a) && free(b));
            assert_eq!(stats().0, free0, "everything is back");
        }
    }

    #[test]
    fn a_block_from_the_top_takes_a_whole_block_that_has_no_room_to_split() {
        let _a = TestArena::lock();
        unsafe {
            init();
            let (free0, _) = stats();
            let whole = alloc_top(free0 - HDR);
            assert!(!whole.is_null(), "the whole arena in one block");
            assert_eq!(stats().0, 0);
            assert!(alloc_top(16).is_null(), "nothing is left");
            assert!(free(whole));
            assert_eq!(stats().0, free0);
        }
    }
}

/// Whether `p` is a live (allocated) block of this arena.
#[cfg(test)]
pub(crate) unsafe fn is_live(p: DevAddr) -> bool {
    let s = st();
    let a = p.get();
    a >= s.arena.lo + HDR && a < s.arena.hi && *block(a - HDR).add(1) == USED
}

/// (total free bytes, largest free block) in payload bytes.
pub unsafe fn stats() -> (u32, u32) {
    let s = st();
    let mut total = 0;
    let mut largest = 0;
    let mut cur = block(s.arena.free_head);
    while !cur.is_null() {
        let sz = *cur;
        total += sz;
        if sz > largest {
            largest = sz;
        }
        cur = block(*cur.add(1));
    }
    (total, if largest > HDR { largest - HDR } else { 0 })
}
