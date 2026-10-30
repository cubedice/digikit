import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { settleUi } from './live.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('running pattern switch preserves the source until the committed boundary', async () => {
  const root = repoRoot();
  const image = path.join(root, 'out/patch/canopy-live-carrier.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.modules).toContain('test/canopy-carrier@0.1.0');
  expect(plan.base.syx_sha256).toBe('26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5');
  const sha = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(sha);
  const { session } = h;
  await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  await session.client.call('run.mode', { mode: 'free' });
  const status = planSymbol(plan, 'cp_transport_proof');
  const admission = planSymbol(plan, 'cp_admission');
  const word = (i: number) => session.client.memPeek(status + 4 * i);
  const info = (i: number) => session.client.memPeek(admission + 4 * i);
  const readVoices = async () => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
      addr: planSymbol(plan, '_cp_banks'),
      len: 256,
    });
    expect(r.unknown).toBe(0);
    const b = Buffer.from(r.hex, 'hex');
    return Array.from({ length: 4 }, (_, i) => ({ token: b.readUInt32LE(i * 32), gate: b.readUInt32LE(i * 32 + 4) }))
      .filter((v) => v.token !== 0)
      .sort((a, b) => a.token - b.token);
  };
  const key = async (name: string) => {
    await session.client.inputButton(button(name), true);
    await delay(300);
    await session.client.inputButton(button(name), false);
    await delay(1200);
  };
  const modified = async (modifier: string, name: string) => {
    await session.client.inputButton(button(modifier), true);
    try {
      await key(name);
    } finally {
      await session.client.inputButton(button(modifier), false);
    }
  };
  const shot = async (name: string) =>
    fs.writeFileSync(
      path.join(root, `out/canopy-pattern-${name}.json`),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
  await expect.poll(() => word(0), { timeout: 30_000 }).toBe(1);
  await settleUi(session);
  for (const [addr, value] of [
    [0x429368ca, 3],
    [0x42936872, 0],
  ] as const) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: b.toString('hex') });
  }
  await modified('FUNC', 'SRC');
  await key('DOWN');
  await key('YES');
  await key('YES');
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  const selectPattern = (n: number) => modified('PTN', `TRIG ${n}`);
  await selectPattern(2);
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(1);
  await modified('FUNC', 'SRC');
  await key('DOWN');
  await key('YES');
  await key('YES');
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  await selectPattern(1);
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  const sourcePattern = await info(3);

  const position = planSymbol(plan, 'pp_position');
  const pos = (n: number) => session.client.memPeek(position + 4 * n);
  await key('PLAY');
  await expect.poll(() => pos(5), { timeout: 20_000 }).toBe(3);
  await expect.poll(() => pos(6), { timeout: 20_000 }).toBe(sourcePattern);
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  const generation = (await word(10))!;
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 1, gate: 1 }]);
  const trace: {
    selected: number | null;
    committed: number | null;
    generation: number | null;
    boundary: number | null;
    flags: number | null;
  }[] = [];
  await selectPattern(2);
  const deadline = Date.now() + 90_000;
  while (Date.now() < deadline) {
    const before = await pos(3);
    const row = {
      selected: await session.client.memPeek(0x4316501c),
      committed: await pos(6),
      generation: await word(10),
      boundary: await pos(3),
      flags: await pos(5),
    };
    if (before === row.boundary) trace.push(row);
    if (row.committed !== sourcePattern && row.flags === 3) break;
    await delay(20);
  }
  fs.writeFileSync(
    path.join(root, 'out/canopy-pattern-trace.json'),
    JSON.stringify({ sourcePattern, generation, trace }),
  );
  await shot('destination');
  expect(
    trace.some((r) => r.selected !== sourcePattern && r.committed === sourcePattern),
    'observe scheduled lookahead',
  ).toBe(true);
  expect(
    trace.filter((r) => r.committed === sourcePattern).every((r) => r.generation === generation),
    'old ownership persists until commit',
  ).toBe(true);
  expect(trace.at(-1)?.committed).not.toBe(sourcePattern);
  await expect.poll(() => word(10), { timeout: 15_000 }).toBe(generation + 1);
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([]);
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  await key('STOP');
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100, 0x80, 60, 0, 0x90, 67, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x80, 67, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY running pattern switch');
}, 240_000);
