import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { CMD } from '@digi/dk-session/gen_proto';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { settleUi } from './waits.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('CANOPY native SOUND and MOVEMENT pages route controls to the audible bank', async () => {
  const root = repoRoot(),
    image = path.join(root, 'out/patch/canopy-stage5-native.syx');
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
    file = fs.readFileSync(path.join(root, 'out/patch/canopy-stage5-native.dkm'));
  await dk.put('modules/canopy.dkm', file);
  expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
  await dk.select(['modules/canopy.dkm']);
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
    for (const [name, n] of [
      ['DKRT_BOOT', 20],
      ['DKRT_INSTRUMENT_FILE', 4],
      ['DKRT_INSTRUMENT_SESSION', 4],
      ['mt_guard_fault', 5],
      ['ip_control', 8],
      ['ip_src_debug', 12],
      ['ip_src_refresh', 8],
      ['src_state', 21],
      ['src_status', 160],
    ] as const)
      values[name] = await Promise.all(Array.from({ length: n }, (_, i) => cf(name, i)));
    for (const name of [
      '_ir_rows',
      '_ir_process_rows',
      '_iv_result',
      '_mi_heartbeat',
      '_iv_input',
      '_pattern_position_info',
    ])
      values[name] = await session.client.call('dsp.peek', {
        addr: planSymbol(plan, name),
        len: name === '_ir_rows' ? 64 : name === '_iv_input' || name === '_pattern_position_info' ? 80 : 32,
      });
    fs.writeFileSync(path.join(root, 'out/canopy-stage5-native-state.json'), JSON.stringify(values, null, 2));
    return values;
  };
  await expect
    .poll(
      async () => {
        const phase = await cf('DKRT_INSTRUMENT_FILE');
        if (phase === 9) throw new Error(JSON.stringify(await diagnostic()));
        return phase;
      },
      { timeout: 60_000 },
    )
    .toBe(3);

  expect((await dk.modules()).map((m) => m.name)).toContain('digikit/canopy');
  expect(await cf('ip_control', 1)).toBe(0); // routes always start disabled
  const peek = async (addr: number, len: number) => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(r.unknown).toBe(0);
    return Buffer.from(r.hex, 'hex');
  };
  const row = await peek(planSymbol(plan, '_ir_rows'), 64);
  expect(row.readUInt32LE(0)).toBe(3);
  const state = row.readUInt32LE(16);
  onTestFinished(async () => {
    await diagnostic();
    await captureRemoteOled(session, 'canopy-stage5-native-finish');
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage5-native-instance.json'),
      JSON.stringify(await session.client.call('dsp.peek', { addr: state, len: 800 })),
    );
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
  const key = async (name: string, settle = 500) => {
    await session.client.inputButton(button(name), true);
    await delay(200);
    await session.client.inputButton(button(name), false);
    await delay(settle);
  };
  await settleUi(session);
  // Select the admitted machine using the stock machine chooser.
  await session.client.inputButton(button('FUNC'), true);
  try {
    await key('SRC');
  } finally {
    await session.client.inputButton(button('FUNC'), false);
  }
  for (let i = 0; i < 20; i++) await key('DOWN', 100);
  fs.writeFileSync(
    path.join(root, 'out/canopy-stage5-native-chooser.json'),
    JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
  );
  await key('YES');
  await key('YES');
  await openDigikitMenu(key);
  await key('YES'); // KEYBOARD is the first optional setup row
  await key('DOWN'); // skip the track heading
  await key('YES'); // ENABLE
  await expect.poll(() => cf('ip_control', 1), { timeout: 10_000 }).toBe(1);
  await expect.poll(() => cf('ip_control'), { timeout: 30_000 }).toBe(3);
  for (let i = 0; i < 3; i++) await key('NO');
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
  const bank = async () => {
    const b = await peek(planSymbol(plan, '_iv_controls'), 128);
    return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
  };
  await key('SRC', 1500);
  await expect.poll(() => cf('ip_src_debug', 3), { timeout: 15000 }).toBeGreaterThan(0);
  if (await cf('ip_src_debug', 1)) await key('SRC', 1000);
  expect(await cf('ip_src_debug', 1)).toBe(0);
  await captureRemoteOled(session, 'canopy-stage5-sound');
  const start = await bank();
  for (const encoder of [2, 3, 6]) {
    await session.client.inputTurn(encoder, 8);
    await delay(700);
  }
  const readonly = await bank();
  expect(readonly.slice(16, 18)).toEqual(start.slice(16, 18));
  expect(readonly[20]).toBe(start[20]);
  await expect
    .poll(
      async () => {
        await session.client.inputTurn(4, 4);
        await delay(300);
        return (await bank())[18];
      },
      { timeout: 20000 },
    )
    .toBeGreaterThan(start[18]!);
  await key('SRC', 1500);
  expect(await cf('ip_src_debug', 1)).toBe(1);
  await captureRemoteOled(session, 'canopy-stage5-movement');
  const movement = await bank();
  await expect
    .poll(
      async () => {
        await session.client.inputTurn(3, 2);
        await delay(300);
        return (await bank())[24];
      },
      { timeout: 20000 },
    )
    .toBeGreaterThan(movement[24]!);
  // Remaining editable PLAY controls, including every MOVEMENT encoder.
  for (const [encoder, index] of [
    [1, 22],
    [2, 23],
    [4, 25],
    [5, 26],
    [6, 27],
    [7, 28],
  ] as const) {
    const before = (await bank())[index]!;
    await session.client.inputTurn(encoder, 1);
    await expect.poll(async () => (await bank())[index], { timeout: 15000 }).toBe(before + 1);
  }
  const source = await bank();
  await session.client.inputTurn(8, 4);
  await delay(700);
  expect((await bank())[29]).toBe(source[29]);
  await key('AMP');
  await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(1);
  await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(0);
  for (const [encoder, index] of [
    [5, 19],
    [7, 21],
  ] as const) {
    const before = (await bank())[index]!;
    await session.client.inputTurn(encoder, 2);
    await expect.poll(async () => (await bank())[index], { timeout: 15000 }).toBe(before + 2);
  }
  await expect.poll(() => cf('src_state', 4), { timeout: 20000 }).toBe(0);
  const idlePaints = await cf('ip_src_debug', 3);
  await expect.poll(() => cf('ip_src_debug', 3), { timeout: 15000 }).toBeGreaterThan(idlePaints);
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
    await session.client.inputButton(button('TRK'), true);
    try {
      await key(`TRIG ${n}`);
    } finally {
      await session.client.inputButton(button('TRK'), false);
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
  await key('SRC');
  for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x90, note, 100] });
  await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(3);
  await delay(1500);
  await captureRemoteOled(session, 'canopy-stage5-notes');
  expect(await cf('ip_src_debug', 5)).toBeGreaterThan(0);
  for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
  const menuRow = async (row: number) => {
    for (let i = 0; i < 40; i++) await key('UP', 50);
    for (let i = 0; i < row; i++) await key('DOWN', 100);
  };
  await openDigikitMenu(key);
  await key('YES');
  await menuRow(13);
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
  await menuRow(34);
  await key('YES'); // PRESETS
  const savedBank = await bank();
  await menuRow(2);
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
  expect(preset?.length).toBe(64);
  expect(Array.from({ length: 16 }, (_, i) => preset!.readUInt16BE(8 + 2 * i))).toEqual(savedBank.slice(16));
  for (let i = 0; i < 4; i++) await key('NO');
  if (await cf('ip_src_debug', 1)) await key('SRC');
  await session.client.inputTurn(4, 1);
  await expect.poll(async () => (await bank())[18], { timeout: 15000 }).toBe(savedBank[18]! + 1);
  await openDigikitMenu(key);
  await key('YES');
  await menuRow(34);
  await key('YES');
  await menuRow(3);
  await key('YES'); // LOAD SAVED SOUND
  await expect.poll(async () => (await bank()).slice(16), { timeout: 30000 }).toEqual(savedBank.slice(16));
  for (let i = 0; i < 4; i++) await key('NO');
  if (await cf('ip_src_debug', 1)) await key('SRC');
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
  for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
}, 900000);
