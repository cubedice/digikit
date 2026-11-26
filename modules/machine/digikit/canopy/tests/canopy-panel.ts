// Panel steps and readers shared by CANOPY's Sound persistence device tests:
// track and machine selection, the VOICE page edits, and the stock PRESET and
// SYSEX DUMP menus of DTII 1.17 (the menu rows were read from OLED frames).
import type { Session } from '@digi/test-support';
import { expect } from 'vitest';
import { planSymbol, type RuntimePlan } from '@digi/regression/runtime/runtime-plan.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { REDRAW, SETTLE } from '@digi/regression/runtime/panel-waits.ts';

export type Sound = { color: number; drift: number };
// PRESET menu, left column: LOAD (+Drive), SAVE (+Drive), MANAGE, POOL.
export const PRESET_LOAD = 0;
export const PRESET_SAVE = 1;
export const PRESET_POOL = 3;

/** The panel steps and readers of one emulator session. `sym` resolves a name: pass `instrumentSymbols` of the session for the state of
 * services/instrument.dks (the plan has only the image's names). */
export function canopyPanel(session: Session, plan: RuntimePlan, sym: (name: string) => number = (name) => planSymbol(plan, name)) {
  const cf = async (name: string, index = 0) => {
    const value = await session.client.memPeek(sym(name) + 4 * index);
    if (value === null) throw new Error(`Unreadable ${name}[${index}]`);
    return value >>> 0;
  };
  const { tap: key, edge, turn } = acknowledgedPanel(session);
  const admitted = () => Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
  const track = async (n: number) => {
    await edge('TRK', true);
    try {
      await key(`TRIG ${n}`);
    } finally {
      await edge('TRK', false);
    }
  };
  // The VOICE page (SRC page 0 with the track keyboard) of track n's admitted CANOPY instance.
  const voicePage = async (n: number) => {
    await track(n);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(n - 1);
    for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== 0; i++) await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 1), { timeout: 30_000 }).toBe(0);
    await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
  };
  const chooseCanopy = async (n: number) => {
    await track(n);
    await edge('FUNC', true);
    try {
      await key('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; i++) await key('DOWN');
    await key('YES');
    await key('YES');
    await expect.poll(async () => (await admitted()).includes(n - 1), { timeout: 60_000 }).toBe(true);
    await voicePage(n);
  };
  const sound = async (trackNo: number): Promise<Sound> => {
    const instance = (await admitted()).indexOf(trackNo - 1);
    if (instance < 0) throw new Error(`Track ${trackNo} is not admitted`);
    // CsState is 192 bytes (48 words per instance).
    const base = instance * 48;
    // word 8 holds controls 2 and 3: COLOR and DRIFT, VOICE B and H with the track keyboard
    const voice = await cf('src_state', base + 8);
    return { color: voice >>> 16, drift: voice & 0xffff };
  };
  // VOICE page encoders of a Velvet or Reed Sound: B = COLOR, H = DRIFT (signed detents).
  const edit = async (n: number, color: number, drift: number) => {
    const before = await sound(n);
    await voicePage(n);
    if (color) await turn(2, color);
    if (drift) await turn(8, drift);
    await expect.poll(() => sound(n), { timeout: 30_000 }).not.toEqual(before);
    await sessionRt(session).ui.wait({ ms: 300, why: REDRAW });
    return sound(n);
  };
  let shots = 0;
  const shot = (name: string) => captureRemoteOled(session, `${String(++shots).padStart(2, '0')}-${name}`);
  // Open a PRESET menu entry. UP stops at the first row, so the remembered
  // cursor does not matter.
  const presetMenu = async (entry: number, name: string) => {
    await key('PRESET');
    await sessionRt(session).ui.wait({ ms: 300, why: REDRAW });
    for (let i = 0; i < 4; i++) await key('UP');
    for (let i = 0; i < entry; i++) await key('DOWN');
    await shot(`${name}-menu`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
  };
  const listTop = async () => {
    for (let i = 0; i < 8; i++) await key('UP');
  };
  // ENTER PRESET NAME (YES keeps the proposed name), then the tag page (<SAVE>).
  const confirmSave = async (name: string) => {
    await shot(`${name}-name`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await shot(`${name}-tags`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 3500, why: SETTLE }); // a +Drive save writes the card
    await shot(`${name}-saved`);
  };
  const leaveMenus = async () => {
    for (let i = 0; i < 3; i++) {
      await key('NO');
      await sessionRt(session).ui.wait({ ms: 250, why: REDRAW });
    }
  };
  // Track n's Sound into an empty pool slot (SAVE TO HERE is the first action).
  const poolSave = async (n: number, slot = 1) => {
    await track(n);
    await presetMenu(PRESET_POOL, 'pool-save');
    await listTop();
    for (let i = 1; i < slot; i++) await key('DOWN');
    await key('RIGHT');
    await sessionRt(session).ui.wait({ ms: 400, why: REDRAW });
    await shot('pool-save-actions');
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await confirmSave('pool-save');
    await leaveMenus();
  };
  // A filled pool slot onto track n (LOAD TO TRACK is the first action).
  const poolLoad = async (n: number, slot = 1) => {
    await track(n);
    await presetMenu(PRESET_POOL, `pool-load-t${n}`);
    await listTop();
    for (let i = 1; i < slot; i++) await key('DOWN');
    await key('RIGHT');
    await sessionRt(session).ui.wait({ ms: 400, why: REDRAW });
    await shot(`pool-load-t${n}-actions`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await shot(`pool-load-t${n}-confirm`); // LOAD PRESET TO TRACK? CONFIRM Y/N
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 1500, why: SETTLE });
    await shot(`pool-load-t${n}-done`);
    await leaveMenus();
  };
  // Track n's Sound to +Drive slot A:001.
  const driveSave = async (n: number) => {
    await track(n);
    await presetMenu(PRESET_SAVE, 'drive-save');
    await listTop();
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await confirmSave('drive-save');
    await leaveMenus();
  };
  // +Drive slot A:001 onto track n.
  const driveLoad = async (n: number, name = `drive-load-t${n}`) => {
    await track(n);
    await presetMenu(PRESET_LOAD, name);
    await sessionRt(session).ui.wait({ ms: 900, why: REDRAW });
    await listTop();
    await shot(`${name}-list`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 1500, why: SETTLE });
    await shot(`${name}-done`);
    await leaveMenus();
  };
  // SETTINGS > SYSEX DUMP (fifth row) > SYSEX SEND or SYSEX RECEIVE.
  const sysexMenu = async (receive: boolean, name: string) => {
    await key('SETTINGS');
    await sessionRt(session).ui.wait({ ms: 500, why: REDRAW });
    for (let i = 0; i < 8; i++) await key('UP');
    for (let i = 0; i < 4; i++) await key('DOWN');
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await key('UP');
    await key('UP');
    if (receive) await key('DOWN');
    await shot(`${name}-menu`);
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
  };
  // SYSEX SEND > PATTERN > the first pattern (A01). The stock emulator image asks
  // SELECT INTERFACE (USB, USB+MIDI) next; with the CANOPY runtime the firmware
  // sends on USB at once, and the extra YES lands on the SENDING screen.
  const sysexSendPattern = async () => {
    await sysexMenu(false, 'sysex-send');
    await key('DOWN'); // PATTERN
    await key('RIGHT'); // the pattern list
    await listTop();
    await shot('sysex-send-pattern');
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await shot('sysex-send-interface');
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 300, why: REDRAW });
  };
  // SYSEX RECEIVE > PATTERN > the first pattern (A01): waits for the dump.
  const sysexReceivePattern = async () => {
    await sysexMenu(true, 'sysex-receive');
    await key('RIGHT'); // the pattern list (PATTERN is the default kind)
    await listTop();
    await shot('sysex-receive-pattern');
    await key('YES');
    await sessionRt(session).ui.wait({ ms: 600, why: REDRAW });
    await shot('sysex-receive-waiting');
  };
  const persistence = async () => ({
    info: await Promise.all(Array.from({ length: 8 }, (_, i) => cf('ip_sa_info', i))),
    stats: await Promise.all(Array.from({ length: 9 }, (_, i) => cf('ip_sa_stats', i))),
  });
  return {
    cf,
    key,
    shot,
    leaveMenus,
    chooseCanopy,
    voicePage,
    sound,
    edit,
    poolSave,
    poolLoad,
    driveSave,
    driveLoad,
    sysexSendPattern,
    sysexReceivePattern,
    persistence,
  };
}
