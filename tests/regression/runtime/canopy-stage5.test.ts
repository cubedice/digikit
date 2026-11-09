import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { repoRoot, withinRuntimeDeadline } from '@digi/test-support';
import { CMD } from '@digi/dk-session/gen_proto';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { waitEmulated } from './waits.ts';
import { expectRuntimeUi } from './ready.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';
import { waitInstrumentReady } from './instrument-ready.ts';
import { acknowledgedPanel } from './panel-ack.ts';
import { menuSelection } from './menu-selection.ts';
import { encoderValue } from './encoder-value.ts';
import { canopyControls } from './canopy-controls.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test(
  'CANOPY native SOUND, ARP and TONE pages route controls to the audible bank',
  async () => {
    const root = repoRoot(),
      image = path.resolve(root, process.env.CANOPY_UI_IMAGE ?? 'out/patch/canopy-native-ui.syx');
    const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
    expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
    expect(plan.modules.some((m) => m.startsWith('test/canopy'))).toBe(false);
    const { session } = h;
    await session.client.firmwareLoad({
      syx: image,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session),
      file = fs.readFileSync(path.resolve(root, process.env.CANOPY_UI_PACKAGE ?? 'out/patch/canopy-native-ui.dkm'));
    await dk.put('modules/canopy.dkm', file);
    expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const beforeRestart = (await session.status()).epoch;
    if (typeof beforeRestart !== 'number') throw new Error('Missing pre-restart epoch');
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);
    const word = async (address: number) => {
      const value = await session.client.memPeek(address);
      if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
      return value;
    };
    const cf = (name: string, i = 0) => word(planSymbol(plan, name) + 4 * i);
    const diagnostic = async () => {
      const values: Record<string, unknown> = {};
      const deadline = Date.now() + 20_000;
      const capture = async (name: string, read: () => Promise<unknown>) => {
        try {
          values[name] = await withinRuntimeDeadline(deadline, read);
        } catch (error) {
          values[name] = { error: String(error) };
        }
      };
      await Promise.all([
        // Queue the compact liveness/DSP reads before the individual CF words.
        capture('status', () => session.status()),
        capture('audio_report', () => session.client.call('audio.report')),
        capture('diagnostics', () => session.client.call('diagnostics')),
        ...(
          [
            ['_ir_rows', 64],
            ['_ir_process_rows', 32],
            ['_iv_result', 32],
            ['_mi_heartbeat', 32],
            ['_iv_input', 80],
            ['_pattern_position_info', 80],
            ['_instrument_reply', 64],
          ] as const
        ).map(([name, len]) =>
          capture(name, () =>
            session.client.call('dsp.peek', {
              addr: planSymbol(plan, name),
              len,
            }),
          ),
        ),
        ...(
          [
            ['DKRT_BOOT', 20],
            ['DKRT_INSTRUMENT_FILE', 4],
            ['DKRT_INSTRUMENT_SESSION', 4],
            ['dl_link', 24],
            ['mt_guard_fault', 5],
            ['registry_guard', 8],
            ['frame_block', 1],
            ['ip_control', 8],
            ['ip_src_debug', 12],
            ['ip_src_refresh', 8],
            ['ip_panel_debug', 8],
            ['ip_keyboard_status', 12],
            ['src_state', 28],
            ['src_status', 160],
          ] as const
        ).map(([name, n]) => capture(name, () => Promise.all(Array.from({ length: n }, (_, i) => cf(name, i))))),
      ]);
      fs.writeFileSync(path.join(root, 'out/canopy-stage5-native-state.json'), JSON.stringify(values, null, 2));
      return values;
    };
    try {
      await waitInstrumentReady(cf);
    } catch (error) {
      await diagnostic();
      throw error;
    }

    expect((await dk.modules()).map((m) => m.name)).toContain('digikit/canopy');
    expect(await cf('ip_control', 1)).toBe(0); // external MIDI starts disabled
    const peek = async (addr: number, len: number) => {
      const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
      expect(r.unknown).toBe(0);
      return Buffer.from(r.hex, 'hex');
    };
    const row = await peek(planSymbol(plan, '_ir_rows'), 64);
    expect(row.readUInt32LE(0)).toBe(3);
    const state = row.readUInt32LE(16);
    onTestFinished(async () => {
      // Evidence is independent and best effort: a failed screenshot must neither
      // skip the instance dump nor replace the failure that triggered cleanup.
      const captures = [
        [
          'instance',
          async () => {
            const instance = await withinRuntimeDeadline(Date.now() + 20_000, () =>
              session.client.call('dsp.peek', { addr: state, len: 1184 }),
            ).catch((error: unknown) => ({ error: String(error) }));
            fs.writeFileSync(path.join(root, 'out/canopy-stage5-native-instance.json'), JSON.stringify(instance));
          },
        ],
        ['state', diagnostic],
        ['OLED', () => captureRemoteOled(session, 'canopy-stage5-native-finish')],
      ] as const;
      const results = await Promise.allSettled(captures.map(([, capture]) => capture()));
      results.forEach((result, index) => {
        if (result.status === 'rejected')
          console.warn(`CANOPY final ${captures[index]![0]} capture failed:`, result.reason);
      });
    });
    const voices = async () => {
      const b = await peek(state, 192);
      return Array.from({ length: 6 }, (_, i) => ({
        slot: i,
        token: b.readUInt32LE(i * 32),
        gate: b.readUInt32LE(i * 32 + 4),
      }))
        .filter((v) => v.token !== 0 && !(v.gate & 4))
        .sort((a, b) => a.token - b.token);
    };
    const { tap: key, edge, turn } = acknowledgedPanel(session);
    const readyArtifacts = path.join(root, 'out/design/canopy-native-ui');
    fs.mkdirSync(readyArtifacts, { recursive: true });
    await expectRuntimeUi(session, plan, readyArtifacts, {
      afterEpoch: beforeRestart,
      timeout: 180_000,
    });
    console.info('CANOPY: restarted runtime UI ready');
    // Select the admitted machine using the stock machine chooser.
    await edge('FUNC', true);
    try {
      await key('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; i++) await key('DOWN');
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage5-native-chooser.json'),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
    await key('YES');
    await key('YES');
    // Local audition does not depend on enabling an external MIDI route.
    await expect.poll(() => cf('ip_control'), { timeout: 60_000 }).toBe(3);
    await key('KEYBOARD');
    const heldPanel = async () => (await voices()).filter((v) => v.gate === 1);
    try {
      let presses = await cf('ip_panel_debug');
      // Chromatic mode's lower row is the white keys. Upper-row 1/4/8
      // are intentional gaps and must retain the stock no-note behavior.
      for (const n of [9, 11, 13]) {
        await edge(`TRIG ${n}`, true);
        await expect.poll(() => cf('ip_panel_debug'), { timeout: 30_000 }).toBe(++presses);
      }
      await expect.poll(() => cf('ip_panel_debug'), { timeout: 30_000 }).toBeGreaterThanOrEqual(3);
      await expect.poll(async () => (await heldPanel()).length, { timeout: 30_000 }).toBe(3);
      const before = await heldPanel();
      await edge('TRIG 11', false);
      await expect.poll(async () => (await heldPanel()).length, { timeout: 30_000 }).toBe(2);
      expect((await heldPanel()).every((v) => before.some((p) => p.token === v.token))).toBe(true);
      await key('KEYBOARD'); // leaving keyboard mode releases only its own notes
      await expect.poll(async () => (await heldPanel()).length, { timeout: 30_000 }).toBe(0);
    } finally {
      for (const n of [9, 11, 13]) await edge(`TRIG ${n}`, false);
    }
    console.info('CANOPY: panel chord, independent release and keyboard exit passed');
    await openDigikitMenu(key);
    await key('YES'); // CANOPY setup is the first optional setup row
    await key('YES'); // INPUT
    await key('YES'); // ENABLE
    await expect.poll(() => cf('ip_control', 1), { timeout: 10_000 }).toBe(1);
    await expect.poll(() => cf('ip_control'), { timeout: 30_000 }).toBe(3);
    for (let i = 0; i < 4; i++) await key('NO');
    expect(await cf('ip_admission', 5)).toBe(await cf('ip_control', 7));
    // Stock receive-port settings are independent of the instrument route.
    // Set the emulator fixture to receive both ports, as in the carrier proofs.
    for (const [addr, value] of [
      [0x429368ca, 3],
      [0x42936872, 0],
      [0x429368d6, 1],
    ] as const) {
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(value);
      await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
    }
    const rawBank = async () => {
      const b = await peek(planSymbol(plan, '_iv_controls'), 128);
      return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
    };
    const bank = async () => (await rawBank()).map((v, i) => (i < 16 ? v : v & 65535));
    const toneBank = async () => (await rawBank()).slice(16, 28).map((v) => v >>> 16);
    const { settled } = canopyControls(cf, rawBank);
    const setControl = (encoder: number, control: number, target: number) =>
      encoderValue(
        (delta) => turn(encoder, delta * ([0, 1, 6, 7, 8, 11, 12, 16, 20, 21].includes(control) ? 4 : 1)),
        () => settled(control),
        target,
      );
    const chordChoice = async () => {
      const chord = await settled(0);
      return ((await rawBank())[0]! >>> 8) & 255 ? chord + 1 : 0;
    };
    const setChord = (choice: number) => encoderValue((delta) => turn(2, delta * 4), chordChoice, choice);
    const waveChoice = async () => {
      const table = await settled(16),
        family = (await rawBank())[1]!;
      return family < 2 ? family : table + 2;
    };
    const setWave = (choice: number) => encoderValue((delta) => turn(2, delta * 4), waveChoice, choice);
    const srcPage = async (page: number) => {
      // The remembered page alone does not prove the native SRC view is bound
      // after a modal closes. A nonzero published token proves visible ownership.
      if (!(await cf('ip_src_debug', 2))) await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30000 }).toBeGreaterThan(0);
      for (let i = 0; i < 3 && (await cf('ip_src_debug', 1)) !== page; i++) await key('SRC');
      expect(await cf('ip_src_debug', 1)).toBe(page);
      expect(await cf('ip_src_debug')).toBe(0);
      expect(await cf('ip_src_debug', 2)).toBeGreaterThan(0);
    };
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 3), { timeout: 15000 }).toBeGreaterThan(0);
    await srcPage(0);
    await captureRemoteOled(session, 'canopy-stage5-sound');
    const heldValues = async (name: string) => {
      await edge('SRC', true);
      try {
        await waitEmulated(session, 800);
        await captureRemoteOled(session, name);
      } finally {
        await edge('SRC', false);
      }
      await waitEmulated(session, 100);
    };
    await heldValues('canopy-sound-held-values');
    expect(await cf('ip_src_debug', 1)).toBe(0);
    const start = await bank();
    for (const encoder of [3, 6]) {
      await session.client.inputTurn(encoder, 8);
      await waitEmulated(session, 50);
    }
    const readonly = await bank();
    expect(readonly.slice(16, 18)).toEqual(start.slice(16, 18));
    expect(readonly[20]).toBe(start[20]);
    await setControl(4, 2, start[18]! + 4);
    expect((await bank())[18]).toBeGreaterThan(start[18]!);
    // A tiny turn leaves PLAY selected. Four raw ticks select the next item;
    // changing mode is available on the track page and owns a reset boundary.
    await turn(2, 1);
    expect(await chordChoice()).toBe(0);
    await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
    await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 15000 }).toBe(1);
    const playGeneration = await cf('src_state');
    await setChord(1); // MAJ, generated from a root
    expect(await cf('ip_keyboard_status')).toBe(1);
    expect(await cf('src_state')).toBeGreaterThan(playGeneration);
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(0);
    await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x90, 60, 100] });
    await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 15000 }).toBe(3);
    const rootGeneration = await cf('src_state');
    await setChord(0); // PLAY, direct keyboard notes again
    expect(await cf('ip_keyboard_status')).toBe(0);
    expect(await cf('src_state')).toBeGreaterThan(rootGeneration);
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(0);
    await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x90, 62, 100] });
    await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 15000 }).toBe(1);
    await session.client.call('midi.usb.in', { bytes: [0x80, 62, 0, 0xb0, 120, 0] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(0);
    await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(1);
    await captureRemoteOled(session, 'canopy-stage5-movement');
    await heldValues('canopy-arp-held-values');
    expect(await cf('ip_src_debug', 1)).toBe(1);
    const movement = await bank();
    await setControl(3, 8, movement[24]! + 2);
    expect((await bank())[24]).toBeGreaterThan(movement[24]!);
    // One signed spread control: no transposition of the pad or bidirectional
    // spread. The 0..6 wire value represents the visible -3..+3 range.
    const tuneBeforeSpread = (await bank())[0]! >>> 16;
    const setSpread = (encoded: number) => setControl(3, 8, encoded);
    for (const [encoded, name] of [
      [0, 'down'],
      [3, 'zero'],
      [6, 'up'],
    ] as const) {
      await setSpread(encoded);
      expect((await bank())[11]).toBe(encoded);
      expect((await bank())[0]! >>> 16).toBe(tuneBeforeSpread);
      await captureRemoteOled(session, `canopy-spread-${name}`);
    }
    await setSpread(3);
    // Remaining editable PLAY controls, including every ARP encoder.
    for (const [encoder, index] of [
      [1, 22],
      [2, 23],
      [4, 25],
      [5, 26],
      [6, 27],
      [7, 28],
    ] as const) {
      const before = (await bank())[index]!;
      await setControl(encoder, index - 16, before + 1);
      expect((await bank())[index]).toBe(before + 1);
    }
    const source = await bank();
    await setControl(8, 5, source[21]! + 4);
    expect((await bank())[21]).toBe(source[21]! + 4);
    expect((await bank())[29]).toBe(source[29]);
    await key('AMP');
    await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(1);
    await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(2);
    await captureRemoteOled(session, 'canopy-tone-legacy');
    await heldValues('canopy-tone-legacy-held-values');
    expect(await cf('ip_src_debug', 1)).toBe(2);
    const legacyTone = await rawBank();
    for (const encoder of [3, 4, 5, 6, 7]) await turn(encoder, 4);
    expect(await rawBank()).toEqual(legacyTone); // WAVE stays available; only C-G require a wavetable.
    await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(0);
    for (const [encoder, index] of [
      [5, 19],
      [7, 21],
    ] as const) {
      const before = (await bank())[index]!;
      await setControl(encoder, index - 16, before + 2);
      expect((await bank())[index]).toBe(before + 2);
    }
    await expect.poll(() => cf('src_state', 4), { timeout: 20000 }).toBe(0);
    const idlePaints = await cf('ip_src_debug', 3);
    await expect.poll(() => cf('ip_src_debug', 3), { timeout: 15000 }).toBeGreaterThan(idlePaints);
    // Focus expires after 750 device milliseconds; allow the next 40 ms paint.
    // Wall time only bounds a stalled poll, not the lifetime of a UI value.
    await waitEmulated(session, 800);
    // The last byte of the bounded presentation is the visible focus flag.
    await expect.poll(async () => (await cf('display', 47)) & 255, { timeout: 15000 }).toBe(0);
    // Applied status revision must agree with the requested bank, not just an ACK.
    expect(await cf('src_status', 7)).toBe(await cf('src_state', 3));
    await key('DOWN');
    expect(await cf('ip_src_debug', 1)).toBe(1);
    await key('UP');
    expect(await cf('ip_src_debug', 1)).toBe(0);
    await key('FLTR');
    await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(0);
    await key('SRC');
    const selectedTrack = async (n: number) => {
      await edge('TRK', true);
      try {
        await key(`TRIG ${n}`);
      } finally {
        await edge('TRK', false);
      }
      await delay(1000);
    };
    const beforeStock = await bank();
    await selectedTrack(2);
    expect(await cf('ip_src_debug', 2)).toBe(0);
    const paints = await cf('ip_src_debug', 3);
    await session.client.inputTurn(1, 2);
    await delay(1000);
    expect(await bank()).toEqual(beforeStock);
    expect(await cf('ip_src_debug', 3)).toBe(paints);
    await captureRemoteOled(session, 'canopy-stage5-stock-track');
    await selectedTrack(1);
    expect(await cf('ip_src_debug', 1)).toBe(1);
    await srcPage(0);
    for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x90, note, 100] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(3);
    await delay(1500);
    await captureRemoteOled(session, 'canopy-stage5-notes');
    expect(await cf('ip_src_debug', 5)).toBeGreaterThan(0);
    await openDigikitMenu(key);
    await key('DOWN'); // CANOPY STATUS follows CANOPY setup.
    await key('YES');
    await waitEmulated(session, 300);
    await expect.poll(async () => (await cf('src_status', 158)) >>> 24, { timeout: 15000 }).toBe(1);
    expect(((await cf('src_status', 5)) >>> 20) & 7).toBe(3);
    const statusPaints = await cf('ip_src_debug', 3);
    await captureRemoteOled(session, 'canopy-native-status');
    await waitEmulated(session, 150);
    expect(await cf('ip_src_debug', 3)).toBe(statusPaints);
    await key('NO');
    await key('UP'); // Restore the root's remembered selection to CANOPY.
    await key('NO');
    await key('NO');
    for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
    const selectMenu = menuSelection(session, plan, 'core/instrument-play', key);
    const menuRow = (row: number, menu = 'menu') => selectMenu(menu, row);
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(1); // CHORD LEARN
    await key('YES');
    await menuRow(1, 'shape_menu');
    await key('YES');
    await expect.poll(() => cf('ip_keyboard_status'), { timeout: 15000 }).toBe(2);
    await expect.poll(() => cf('ip_control'), { timeout: 30000 }).toBe(3);
    const hiddenPaints = await cf('ip_src_debug', 3),
      hiddenColor = (await bank())[18];
    await session.client.inputTurn(4, 4);
    await delay(1000);
    expect((await bank())[18]).toBe(hiddenColor);
    expect(await cf('ip_src_debug', 3)).toBe(hiddenPaints);
    for (const note of [60, 67, 71, 76]) await session.client.call('midi.usb.in', { bytes: [0x90, note, 100] });
    await expect.poll(() => cf('ip_keyboard_status', 3), { timeout: 15000 }).toBe(4);
    await captureRemoteOled(session, 'canopy-stage5-learn');
    for (const note of [60, 67, 71, 76]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
    await key('YES');
    await expect.poll(() => cf('ip_keyboard_status'), { timeout: 15000 }).toBe(0);
    await expect.poll(() => cf('ip_keyboard_status', 7), { timeout: 30000 }).toBe(0);
    expect(await cf('ip_keyboard_status', 8)).toBe(0);
    // USER is a chord choice within ROOT, not a read-only mode. Exercise both
    // directions through the native encoder path after learning a real shape.
    for (let i = 0; i < 4; i++) await key('NO');
    await srcPage(0);
    await setChord(1); // ROOT mode through CHRD, without a setup-menu mode switch.
    expect(await cf('ip_keyboard_status')).toBe(1);
    for (const chord of [10, 9, 10, 11]) {
      // The stock idle filter can consume the first packet after a long menu
      // visit (finding 78). Drain each actual input and its configuration before
      // deciding whether another single detent is necessary.
      await setControl(2, 0, chord);
      expect((await bank())[16]).toBe(chord);
      await waitEmulated(session, 100);
    }
    await captureRemoteOled(session, 'canopy-user-chord-exit');
    console.info('CANOPY: USER chord exits in both directions through native encoder B');
    await setChord(0); // restore PLAY directly before the preset/audio checks
    await expect.poll(() => cf('ip_keyboard_status'), { timeout: 15000 }).toBe(0);
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(4);
    await key('YES'); // PRESETS
    const savedBank = await bank();
    await menuRow(2, 'preset_menu');
    await key('YES'); // SAVE CURRENT SOUND
    await expect.poll(async () => (await cf('preset_present')) >>> 24, { timeout: 30000 }).toBe(1);
    expect(await cf('preset_result')).toBe(0);
    const saved = await dk.request(CMD.SETTINGS, Uint8Array.of(0, 0, 0, 0, 2, 0));
    expect(saved.status).toBe(0);
    const settings = Buffer.from(saved.body),
      record = settings.subarray(10);
    expect(settings[0]! & 1).toBe(0);
    expect(settings.readUInt32BE(2)).toBeGreaterThan(0);
    let preset: Buffer | undefined;
    for (let at = 18, used = record.readUInt32BE(10); at < used;) {
      const n = record[at++]!,
        name = record.subarray(at, at + n).toString('ascii');
      at += n;
      const size = record.readUInt16BE(at);
      at += 2;
      if (name === 'canopy/preset/1') preset = record.subarray(at, at + size);
      at += size;
    }
    expect(preset?.length).toBe(96);
    expect(preset![4]).toBe(3);
    expect(Array.from({ length: 16 }, (_, i) => preset!.readUInt16BE(8 + 2 * i))).toEqual(savedBank.slice(16));
    expect(Array.from({ length: 12 }, (_, i) => preset!.readUInt16BE(64 + 2 * i))).toEqual(await toneBank());
    for (let i = 0; i < 4; i++) await key('NO');
    await srcPage(0);
    await setControl(4, 2, savedBank[18]! + 1);
    expect((await bank())[18]).toBe(savedBank[18]! + 1);
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(4);
    await key('YES');
    await menuRow(3, 'preset_menu');
    await key('YES'); // LOAD SAVED SOUND
    await expect.poll(async () => (await bank()).slice(16), { timeout: 30000 }).toEqual(savedBank.slice(16));
    for (let i = 0; i < 4; i++) await key('NO');
    await srcPage(0);
    await expect.poll(() => cf('src_state', 4), { timeout: 30000 }).toBe(0);
    await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(1);
    const sound = await cf('ip_admission', 1);
    const nativeTune = async () => (await word(sound + 0x44)) & 65535;
    const beforeTune = await nativeTune();
    const held = (await voices())[0]!;
    const rotation = async () => (await peek(state + held.slot * 32 + 16, 8)).toString('hex');
    const beforeRotation = await rotation();
    await expect
      .poll(
        async () => {
          await session.client.inputTurn(1, 8);
          await delay(500);
          return nativeTune();
        },
        { timeout: 15000, interval: 500 },
      )
      .not.toBe(beforeTune);
    await expect.poll(rotation, { timeout: 15000 }).not.toBe(beforeRotation);
    expect((await voices())[0]!.token).toBe(held.token);
    await expect.poll(() => cf('src_state', 4), { timeout: 30000 }).toBe(0);
    const beforeLevel = (await word(sound + 0x58)) >>> 16;
    await expect
      .poll(
        async () => {
          await session.client.inputTurn(8, -8);
          await delay(500);
          return (await word(sound + 0x58)) >>> 16;
        },
        { timeout: 15000, interval: 500 },
      )
      .toBeLessThan(beforeLevel);
    await captureRemoteOled(session, 'canopy-stage5-native-tune-level');
    console.info('CANOPY: native pages, held values, controls, presets and tuning passed');
    // Observe the final stereo output while the same CANOPY token remains held.
    // Changing track level must not alter the source LEV Sound slot.
    const audioLevel = async (name: string) => {
      const output = path.join(root, `out/canopy-native-level-${name}.f32le`);
      await session.client.call('pcm.tap', { action: 'capture', path: output });
      try {
        await session.client.call('pcm.tap', { action: 'start' });
        await expect
          .poll(
            async () => {
              const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', {
                action: 'report',
              });
              expect(r.non_finite).toBe(0);
              return r.samples;
            },
            { timeout: 60000 },
          )
          .toBeGreaterThanOrEqual(24000);
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
      }
      const raw = fs.readFileSync(output);
      let energy = 0;
      const first = Math.max(0, raw.length - 8192 * 8);
      for (let at = first; at < raw.length; at += 4) energy += raw.readFloatLE(at) ** 2;
      return Math.sqrt(energy / ((raw.length - first) / 4));
    };
    const turnRange = async (encoder: number, direction: 1 | -1) => {
      // Native detents can be fractional; cover the range without assuming
      // that one injected detent equals one displayed level unit.
      for (let i = 0; i < 32; i++) {
        await session.client.inputTurn(encoder, direction * 30);
        await waitEmulated(session, 40);
        if (encoder === 8 && (await word(sound + 0x58)) >>> 16 === (direction < 0 ? 0 : 0x7f00)) break;
      }
    };
    const sourceBeforeTrack = (await word(sound + 0x58)) >>> 16;
    await turnRange(9, 1);
    const full = await audioLevel('track-full');
    expect(full).toBeGreaterThan(1e-5);
    await turnRange(9, -1);
    const zero = await audioLevel('track-zero');
    expect((await word(sound + 0x58)) >>> 16).toBe(sourceBeforeTrack);
    expect(zero).toBeLessThan(full * 0.01);
    await key('SRC'); // Track LEVEL/DATA also edits the native control on ARP.
    await turnRange(9, 1);
    const arpFull = await audioLevel('arp-track-full');
    expect(arpFull).toBeGreaterThan(full * 0.5);
    await key('SRC');
    await turnRange(8, -1);
    expect((await word(sound + 0x58)) >>> 16).toBe(0);
    const sourceZero = await audioLevel('source-zero');
    expect(sourceZero).toBeLessThan(full * 0.01);
    fs.writeFileSync(
      path.join(root, 'out/canopy-native-levels.json'),
      JSON.stringify({ full, zero, arpFull, sourceZero }, null, 2),
    );
    for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
    await session.client.call('midi.usb.in', { bytes: [0xb0, 120, 0] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(0);
    await srcPage(0);
    await turnRange(8, 1);
    await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(1);
    const wavetableHeld = (await voices())[0]!.token;
    const wavetableGeneration = await cf('src_state');
    await srcPage(2);
    for (const choice of [1, 2, 0, 2]) {
      await setWave(choice); // REED, ANALOG, VELVET, ANALOG on the track itself.
      expect(await voices()).toEqual([expect.objectContaining({ token: wavetableHeld, gate: 1 })]);
      expect(await cf('src_state')).toBe(wavetableGeneration);
    }
    await expect.poll(async () => (await bank())[1], { timeout: 15000 }).toBe(2);
    const toneDefaults = await toneBank();
    expect(toneDefaults).toEqual([0, 0, 0, 10, 0, 0, 0, 100, 5, 15, 100, 100]);
    for (const [encoder, index, delta] of [
      [2, 0, 1], // GLASS
      [3, 1, 25], // 1.25
      [4, 2, 20],
      [5, 3, 5],
      [6, 4, 1], // TRI
      [7, 5, 1], // PHRASE
    ] as const) {
      const before = (await toneBank())[index]!;
      await setControl(encoder, 16 + index, before + delta);
      expect((await toneBank())[index]).toBe(before + delta);
    }
    expect((await voices())[0]!.token).toBe(wavetableHeld);
    await expect.poll(() => cf('src_state', 4), { timeout: 30000 }).toBe(0);
    await captureRemoteOled(session, 'canopy-wavetable-tone');
    await heldValues('canopy-wavetable-tone-values');
    expect(await cf('ip_src_debug', 1)).toBe(2);
    expect(await audioLevel('wavetable')).toBeGreaterThan(1e-5);
    await srcPage(0);
    const tonePosition = (await toneBank())[1]!;
    const oldColor = (await bank())[18];
    await setControl(4, 17, tonePosition + 1); // SOUND D aliases TONE C for WAVETABLE.
    expect((await toneBank())[1]).toBe(tonePosition + 1);
    expect((await bank())[18]).toBe(oldColor);
    const savedTone = await toneBank();
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(4);
    await key('YES');
    await menuRow(2, 'preset_menu');
    await key('YES');
    await expect.poll(() => cf('preset_result'), { timeout: 30000 }).toBe(0);
    for (let i = 0; i < 4; i++) await key('NO');
    await srcPage(2);
    await setControl(3, 17, savedTone[1]! + 1);
    expect((await toneBank())[1]).toBe(savedTone[1]! + 1);
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(4);
    await key('YES');
    await menuRow(3, 'preset_menu');
    await key('YES');
    await expect.poll(toneBank, { timeout: 30000 }).toEqual(savedTone);
    expect((await bank())[1]).toBe(2);
    expect(await cf('ip_control', 4)).toBe(0);
    expect(await cf('mt_guard_fault')).toBe(0);
    await session.client.call('midi.usb.in', { bytes: [0xb0, 120, 0] });
    console.info('CANOPY: wavetable engine, held-note edits, TONE values and v3 preset recall passed');
    // Generic-core measurements: ~23 minutes to load and ~0.91 s per acknowledged
    // key edge. Keep room for navigation/audio without relaxing operation guards.
  },
  60 * 60_000,
);
