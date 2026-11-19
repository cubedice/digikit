# The loop's controls (`core/loop-control`)

The one home of the loop effect's controls on the Digitakt II (OS 1.17): the crossfader, ARM and DROP, and the engine's
status as the DSP returns it. Any page may write and read them through `sdk/include/dk/loop.h`; today Overloop's page
(`core/loop-display`) does. The values are **not kit values**: they live in this module's memory, are the same in every
kit, do not change at a pattern change and are gone after a restart (crossfader at LIVE, nothing armed, no loop).

## The interface

```c
uint32_t dkrt_loop_get(uint32_t index);   /* a raw value: LP_XFAD 0..127, the counters LP_ARM, LP_DROP, LP_SEEN */
void     dkrt_loop_xfad(uint32_t value);  /* the crossfader: 0 LIVE .. 127 LOOP */
void     dkrt_loop_arm(void);             /* one press of ARM: arms, or takes the arm back */
void     dkrt_loop_drop(void);            /* drop the loop */
uint32_t dkrt_loop_status(uint32_t *out); /* the engine's status (LS_* words); 1 while it is fresh */
uint32_t dkrt_loop_page(void);            /* the native FX page of the loop effect, 0 while none is loaded */
```

The indices and the status words are in `sdk/dsp/contract/dk_loop.inc`, the one file the home, the page and the engine
share.

## How the values reach the engine

The engine is an ordinary effect of the effect framework: its controls are the parameter words of its row. The fx service
normally fills those from the kit. For an effect whose values an image adapter holds it asks the adapter instead:

```c
int fx_held_values(uint32_t effect_id, uint32_t native_page, uint16_t raw[8]);
```

This module publishes the function as an `[[info]]` entry of that name; the fx service finds it by the name's hash
(`fxpage.rs`, `HELD_HASH`) and calls it once a UI pass for every loaded effect. For the loop effect (effect ID `0xca1b`:
Overloop's module id and the effect id `loop`) it returns 1 and the values on the kit's scale; for any other effect 0,
and the kit is read as before. For a held effect the kit is never read, and the row's MIX is the record's default. The
call also tells this module the effect's page code, which is how the page adapter knows its page.

ARM and DROP are counters, not levels: the engine acts on a change of the word. A press therefore cannot be lost between
two blocks or repeated.

## How the status comes back

The engine publishes sixteen words in `_loop_ui_status` at the end of a block (this module owns the mailbox on the DSP
side). `core/dsp-link` returns them in fourteen spare words of the reply (the `OVL2` carrier, finding 340). PRISM's reply
(`core/phrase-control`) has the same cells in three blocks of four, so a status read once a UI pass came only when the pass
met the fourth block, at irregular times. `core/dsp-link` therefore calls `loop_reply_frame(rec)` once a block, in the block
interrupt, with the reply copy, as it calls `phrase_write_frame`: it reads the fourteen cells and keeps the newest `OVL2`
carrier (its magic, and its packed word 7 zero). It only reads; what the other adapters get is unchanged.
`loop_ui_tick`, once a UI pass, copies the newest carrier with the interrupt masked for that bounded copy and takes it
when the block interrupt found a new one ([finding 363](../../../../docs/findings/363-loop-link-jitter.md)). A status without
a new heartbeat for 60 passes is not fresh.

When the status shows a loop the home has not seen (the count in the status differs from `LP_SEEN`), the home sets the
crossfader to LOOP and `LP_SEEN` to the count. Until the engine reads that count back it holds its own crossfader at LOOP,
so an old crossfader value cannot be heard between the capture and the home's answer.

## Two pages

Overloop's page (`core/loop-display`) and PRISM's (`core/phrase-ui`) both set the crossfader and press ARM and DROP through
this home; neither keeps a copy, so a change on one is what the other shows. The raw values are exported as
`loop_control_raw` for device tests. The tap and the wet mailbox between the two effects are `core/loop-storage`'s.

## Checks

```bash
pnpm nx:pilot run core/loop-control:test-host    # the home: values, counters, the held-values answer, the page code, a new loop,
                                                 # the per-block hook (the newest carrier kept, the phrase reply and bad carriers refused)
pnpm nx:pilot run dkrt-svc-fx:rust-tests         # the fx service: held values never come from the kit
pnpm nx:pilot run core/runtime:test-assembly     # the info entry is named as the service looks it up
```

An engine that starts anew (a load, a fault, a lease that moved) goes on counting loops from the count its status mailbox
shows, so its next loop always has a count the home has not seen (finding 361: this closes the case finding 360 left open).
