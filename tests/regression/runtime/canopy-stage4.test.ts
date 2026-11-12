import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { settleUi } from './waits.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('CANOPY movement through MIDI, setup, master transport and final audio', async () => {
  const root = repoRoot(),
    image = path.join(root, 'out/patch/canopy-stage4-final.syx');
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
    file = fs.readFileSync(path.join(root, 'out/patch/canopy-stage4.dkm'));
  await dk.put('modules/canopy.dkm', file);
  expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
  await dk.select(['modules/canopy.dkm']);
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const cf = (name: string, i = 0) => session.client.memPeek(planSymbol(plan, name) + 4 * i);
  const diagnostic = async () => {
    const values: Record<string, unknown> = {};
    for (const [name, n] of [
      ['DKRT_BOOT', 20],
      ['DKRT_INSTRUMENT_FILE', 4],
      ['DKRT_INSTRUMENT_SESSION', 4],
      ['mt_guard_fault', 5],
      ['ip_control', 8],
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
    fs.writeFileSync(path.join(root, 'out/canopy-stage4-state.json'), JSON.stringify(values, null, 2));
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
    await captureRemoteOled(session, 'canopy-stage4-finish');
    await diagnostic();
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage4-instance.json'),
      JSON.stringify(await session.client.call('dsp.peek', { addr: state, len: 768 })),
    );
  });
  const voices = async () => {
    const b = await peek(state, 192);
    return Array.from({ length: 6 }, (_, i) => ({ token: b.readUInt32LE(i * 32), gate: b.readUInt32LE(i * 32 + 4) }))
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
    path.join(root, 'out/canopy-stage4-chooser.json'),
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
  const sound = async () => {
    const b = await peek(planSymbol(plan, '_iv_controls'), 64);
    return Array.from({ length: 16 }, (_, i) => b.readUInt32LE(i * 4));
  };
  const menuRow = async (row: number) => {
    for (let i = 0; i < 36; i++) await key('UP', 30);
    for (let i = 0; i < row; i++) await key('DOWN', 50);
  };
  const turn = async (enc: number, delta: number, count = 1) => {
    for (let i = 0; i < count; i++) {
      await session.client.inputTurn(enc, delta);
      await delay(250);
    }
  };
  const adjust = async (index: number, target: number) => {
    if (index === 15) {
      // The stock encoder filter may consume the first packet after idle.
      // Keep turning until the applied value reaches the panel's ms target.
      await expect
        .poll(
          async () => {
            const value = (await sound())[index]!;
            if (value !== target) {
              const difference = Math.ceil((target * 2) / 3) - Math.ceil((value * 2) / 3);
              await turn(1, Math.max(-64, Math.min(64, 2 * difference)));
            }
            return (await sound())[index];
          },
          { timeout: 90000, interval: 350 },
        )
        .toBe(target);
      return;
    }
    await expect
      .poll(
        async () => {
          const value = (await sound())[index]!;
          if (value !== target) await turn(1, 2 * (target - value));
          return (await sound())[index];
        },
        { timeout: 90000, interval: 350 },
      )
      .toBe(target);
  };
  const choose = async (row: number, index: number) => {
    await menuRow(row);
    await key('YES');
    for (let i = 0; i < 8; i++) await key('UP', 30);
    for (let i = 0; i < index; i++) await key('DOWN', 50);
    await key('YES');
  };
  const sample = async (name: string) => {
    const capture = path.join(root, `out/canopy-stage4-${name}.f32le`);
    await session.client.call('pcm.tap', { action: 'capture', path: capture });
    await session.client.call('pcm.tap', { action: 'start' });
    await expect
      .poll(
        async () => {
          const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', { action: 'report' });
          expect(r.non_finite).toBe(0);
          return r.samples;
        },
        { timeout: 45000 },
      )
      .toBeGreaterThanOrEqual(24000);
    await session.client.call('pcm.tap', { action: 'capture_stop' });
    const raw = fs.readFileSync(capture);
    const n = Math.min(8192, raw.length / 8),
      start = raw.length / 8 - n;
    let energy = 0,
      peak = 0;
    for (let i = start * 2; i < raw.length / 4; i++) {
      const v = raw.readFloatLE(i * 4);
      expect(Number.isFinite(v)).toBe(true);
      energy += v * v;
      peak = Math.max(peak, Math.abs(v));
    }
    return { rms: Math.sqrt(energy / (n * 2)), peak };
  };
  onTestFinished(async () => {
    await session.client.call('pcm.tap', { action: 'capture_stop' });
  });
  expect(await sound()).toEqual([5, 0, 64, 0, 5, 15, 0, 120, 4, 50, 0, 1, 0, 0, 0, 0]);
  if (!process.env.CANOPY_STRUM_ONLY) {
    for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x90, note, 127] });
    await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(3);
    const ownership = await voices();
    const arp = async () => {
      const b = await peek(state + 544, 208);
      return {
        token: b.readUInt32LE(0),
        serial: b.readUInt32LE(52),
        note: b.readUInt32LE(60),
        mix: b.readFloatLE(32),
        period: b.readUInt32LE(120),
        status: b.readUInt32LE(136),
      };
    };
    let master: Awaited<ReturnType<typeof arp>>;
    let masterAudio: Awaited<ReturnType<typeof sample>>;
    for (const clock of process.env.CANOPY_FINE_ONLY ? [2] : [1, 2]) {
      await openDigikitMenu(key);
      await key('YES');
      await menuRow(24);
      // A stopped master source remains quiet despite held keyboard notes.
      await choose(31, clock);
      await expect.poll(async () => (await sound())[13], { timeout: 15000 }).toBe(clock);
      if (clock === 2) {
        await menuRow(26);
        await adjust(8, 3); // synchronized triplets
        await menuRow(30);
        await adjust(12, 50);
      }
      await menuRow(24);
      await adjust(6, 127);
      await expect.poll(async () => (await arp()).token, { timeout: 15000 }).toBe(0);
      for (let i = 0; i < 3; i++) await key('NO');
      await key('PLAY');
      await expect.poll(async () => (await arp()).period, { timeout: 90000 }).toBeGreaterThan(0);
      master = await arp();
      await expect.poll(async () => (await arp()).serial, { timeout: 60000 }).toBeGreaterThan(master.serial);
      masterAudio = await sample(`master-${clock}`);
      expect(masterAudio.rms).toBeGreaterThan(0.00001);
      expect(masterAudio.peak).toBeLessThan(1);
      await key('STOP');
      await expect.poll(async () => (await arp()).token, { timeout: 30000 }).toBe(0);
      expect(await voices()).toEqual(ownership);
      const stoppedSerial = (await arp()).serial;
      await delay(3000);
      expect((await arp()).serial).toBe(stoppedSerial);
      await key('PLAY');
      await expect.poll(async () => (await arp()).serial, { timeout: 90000 }).toBeGreaterThan(stoppedSerial);
      await key('STOP');
      console.log(`master clock ${clock}: start, gate, audio, stop and restart accepted`);
      if (clock === 2) {
        const serial = (await arp()).serial;
        await session.client.call('midi.usb.in', {
          bytes: [0xfa, ...Array(96).fill(0xf8)],
          chunk: 1,
          period: 1833333,
        });
        await expect.poll(async () => (await arp()).serial, { timeout: 120000 }).toBeGreaterThan(serial);
        await expect.poll(async () => (await arp()).status, { timeout: 180000 }).toBe(3);
        await expect.poll(async () => (await arp()).token, { timeout: 30000 }).toBe(0);
        expect(await voices()).toEqual(ownership);
        const lostSerial = (await arp()).serial;
        await delay(3000);
        expect((await arp()).serial).toBe(lostSerial);
        await session.client.call('midi.usb.in', {
          bytes: [...Array(96).fill(0xf8), 0xfc],
          chunk: 1,
          period: 2750000,
        });
        await expect.poll(async () => (await arp()).serial, { timeout: 120000 }).toBeGreaterThan(lostSerial);
        await expect.poll(async () => (await arp()).period, { timeout: 120000 }).toBeGreaterThanOrEqual(185);
        await expect.poll(async () => (await arp()).token, { timeout: 180000 }).toBe(0);
        expect(await voices()).toEqual(ownership);
        console.log('master phase USB clock loss, slower recovery and STOP accepted');
      }
    }
    if (process.env.CANOPY_FINE_ONLY) return;
    await openDigikitMenu(key);
    await key('YES');
    await choose(31, 0);
    await menuRow(24);
    await adjust(6, 127);
    const notes = new Set<number>();
    await expect
      .poll(
        async () => {
          const a = await arp();
          if (a.token) notes.add(a.note);
          return notes.size;
        },
        { timeout: 30000, interval: 75 },
      )
      .toBe(3);
    expect([...notes].sort((a, b) => a - b)).toEqual([60, 64, 67]);
    expect(await voices()).toEqual(ownership);
    await expect.poll(async () => (await arp()).mix, { timeout: 10000 }).toBe(1);
    const audio = await sample('arp');
    expect(audio.rms).toBeGreaterThan(0.00001);
    expect(audio.peak).toBeLessThan(1);
    console.log('local UP audio accepted');
    await adjust(6, 0);
    await expect.poll(async () => (await arp()).token, { timeout: 15000 }).toBe(0);
    expect(await voices()).toEqual(ownership);
    // Applied enum controls and octave expansion are exercised through panel UI.
    await choose(28, 1); // DOWN
    await expect.poll(async () => (await sound())[10], { timeout: 15000 }).toBe(1);
    await menuRow(29);
    await adjust(11, 2);
    await menuRow(24);
    await adjust(6, 127);
    const expanded = new Set<number>();
    await expect
      .poll(
        async () => {
          const a = await arp();
          if (a.token) expanded.add(a.note);
          return [...expanded].some((n) => n > 67);
        },
        { timeout: 60000 },
      )
      .toBe(true);
    await adjust(6, 0);
    console.log('DOWN and octave control accepted');
    for (let i = 0; i < 3; i++) await key('NO');

    fs.writeFileSync(
      path.join(root, 'out/canopy-stage4-audio.json'),
      JSON.stringify(
        { notes: [...notes], expanded: [...expanded], audio, master: master!, masterAudio: masterAudio! },
        null,
        2,
      ),
    );
  }

  for (const note of [60, 64, 67]) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
  await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(0);
  await openDigikitMenu(key);
  await key('YES');
  await menuRow(24);
  await adjust(6, 0);
  await choose(6, 1); // ROOT
  await menuRow(33);
  await captureRemoteOled(session, 'canopy-stage4-strum-before');
  await adjust(15, 150); // 100 ms on the panel, encoded as 150 audio blocks
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 127] });
  await expect.poll(async () => (await voices()).some((v) => v.gate === 9), { timeout: 30000 }).toBe(true);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0] });
  await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(0);
  await delay(3000);
  expect(await voices()).toEqual([]);
  expect(await cf('ip_control')).toBe(3);
  console.log('generated strum and release cancellation accepted');
}, 1500000);
