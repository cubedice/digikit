// Panel steps and readers shared by CANOPY's Sound persistence device tests:
// track and machine selection, the KEYS page edits, and the stock PRESET and
// SYSEX DUMP menus of DTII 1.17 (the menu rows were read from OLED frames).
import type { Session } from '@digi/test-support';
import { expect } from 'vitest';
import { planSymbol, type RuntimePlan } from '@digi/regression/runtime/runtime-plan.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';

export type Sound = { mode: number; chord: number; voicing: number; strum: number };
// PRESET menu, left column: LOAD (+Drive), SAVE (+Drive), MANAGE, POOL.
export const PRESET_LOAD = 0;
export const PRESET_SAVE = 1;
export const PRESET_POOL = 3;

/** The panel steps and readers of one emulator session. */
export function canopyPanel(session: Session, plan: RuntimePlan) {
  const cf = async (name: string, index = 0) => {
    const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
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
  // The KEYS page (SRC page 0) of track n's admitted CANOPY instance.
  const keysPage = async (n: number) => {
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
    await keysPage(n);
  };
  const sound = async (trackNo: number): Promise<Sound> => {
    const instance = (await admitted()).indexOf(trackNo - 1);
    if (instance < 0) throw new Error(`Track ${trackNo} is not admitted`);
    // CsState is 192 bytes. Control IDs 0/1 are CHRD/VOIC and 4 is STRUM;
    // metadata's mode distinguishes PLAY from the first ROOT chord.
    const base = instance * 48;
    const [metadata, first, strum] = await Promise.all([
      cf('src_state', base + 6),
      cf('src_state', base + 7),
      cf('src_state', base + 9),
    ]);
    return { mode: metadata & 3, chord: first >>> 16, voicing: first & 0xffff, strum: strum >>> 16 };
  };
  // KEYS page encoders: B = CHRD, C = VOIC, D = STRUM (4 detents per list step).
  const edit = async (n: number, chord: number, voicing: number, strum: number) => {
    const before = await sound(n);
    await keysPage(n);
    if (chord) await turn(2, chord);
    if (voicing) await turn(3, voicing);
    if (strum) await turn(4, strum);
    await expect.poll(() => sound(n), { timeout: 30_000 }).not.toEqual(before);
    await waitEmulated(session, 300);
    return sound(n);
  };
  let shots = 0;
  const shot = (name: string) => captureRemoteOled(session, `${String(++shots).padStart(2, '0')}-${name}`);
  // Open a PRESET menu entry. UP stops at the first row, so the remembered
  // cursor does not matter.
  const presetMenu = async (entry: number, name: string) => {
    await key('PRESET');
    await waitEmulated(session, 300);
    for (let i = 0; i < 4; i++) await key('UP');
    for (let i = 0; i < entry; i++) await key('DOWN');
    await shot(`${name}-menu`);
    await key('YES');
    await waitEmulated(session, 600);
  };
  const listTop = async () => {
    for (let i = 0; i < 8; i++) await key('UP');
  };
  // ENTER PRESET NAME (YES keeps the proposed name), then the tag page (<SAVE>).
  const confirmSave = async (name: string) => {
    await shot(`${name}-name`);
    await key('YES');
    await waitEmulated(session, 600);
    await shot(`${name}-tags`);
    await key('YES');
    await waitEmulated(session, 3500); // a +Drive save writes the card
    await shot(`${name}-saved`);
  };
  const leaveMenus = async () => {
    for (let i = 0; i < 3; i++) {
      await key('NO');
      await waitEmulated(session, 250);
    }
  };
  // Track n's Sound into pool slot 001 (empty: SAVE TO HERE is the first action).
  const poolSave = async (n: number) => {
    await track(n);
    await presetMenu(PRESET_POOL, 'pool-save');
    await listTop();
    await key('RIGHT');
    await waitEmulated(session, 400);
    await shot('pool-save-actions');
    await key('YES');
    await waitEmulated(session, 600);
    await confirmSave('pool-save');
    await leaveMenus();
  };
  // Pool slot 001 onto track n (filled: LOAD TO TRACK is the first action).
  const poolLoad = async (n: number) => {
    await track(n);
    await presetMenu(PRESET_POOL, `pool-load-t${n}`);
    await listTop();
    await key('RIGHT');
    await waitEmulated(session, 400);
    await shot(`pool-load-t${n}-actions`);
    await key('YES');
    await waitEmulated(session, 600);
    await shot(`pool-load-t${n}-confirm`); // LOAD PRESET TO TRACK? CONFIRM Y/N
    await key('YES');
    await waitEmulated(session, 1500);
    await shot(`pool-load-t${n}-done`);
    await leaveMenus();
  };
  // Track n's Sound to +Drive slot A:001.
  const driveSave = async (n: number) => {
    await track(n);
    await presetMenu(PRESET_SAVE, 'drive-save');
    await listTop();
    await key('YES');
    await waitEmulated(session, 600);
    await confirmSave('drive-save');
    await leaveMenus();
  };
  // +Drive slot A:001 onto track n.
  const driveLoad = async (n: number, name = `drive-load-t${n}`) => {
    await track(n);
    await presetMenu(PRESET_LOAD, name);
    await waitEmulated(session, 900);
    await listTop();
    await shot(`${name}-list`);
    await key('YES');
    await waitEmulated(session, 1500);
    await shot(`${name}-done`);
    await leaveMenus();
  };
  // SETTINGS > SYSEX DUMP (fifth row) > SYSEX SEND or SYSEX RECEIVE.
  const sysexMenu = async (receive: boolean, name: string) => {
    await key('SETTINGS');
    await waitEmulated(session, 500);
    for (let i = 0; i < 8; i++) await key('UP');
    for (let i = 0; i < 4; i++) await key('DOWN');
    await key('YES');
    await waitEmulated(session, 600);
    await key('UP');
    await key('UP');
    if (receive) await key('DOWN');
    await shot(`${name}-menu`);
    await key('YES');
    await waitEmulated(session, 600);
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
    await waitEmulated(session, 600);
    await shot('sysex-send-interface');
    await key('YES');
    await waitEmulated(session, 300);
  };
  // SYSEX RECEIVE > PATTERN > the first pattern (A01): waits for the dump.
  const sysexReceivePattern = async () => {
    await sysexMenu(true, 'sysex-receive');
    await key('RIGHT'); // the pattern list (PATTERN is the default kind)
    await listTop();
    await shot('sysex-receive-pattern');
    await key('YES');
    await waitEmulated(session, 600);
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
