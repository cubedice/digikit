import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('runtime binds one DSP session and releases the physical link without enabling admission', async () => {
  const image = path.join(repoRoot(), 'out/patch/canopy-live-session.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.modules).toContain('test/instrument-session@0.1.0');
  const sha = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(sha);
  const { session } = h;
  const loaded = await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  expect(loaded.syx_sha256).toBe(sha);
  await session.client.call('run.mode', { mode: 'free' });
  const binding = planSymbol(plan, 'DKRT_INSTRUMENT_BINDING');
  await expect.poll(() => session.client.memPeek(binding + 8), { timeout: 30_000, interval: 100 }).toBe(1);
  expect(await session.client.memPeek(binding + 4)).toBe(63);
  const status = planSymbol(plan, 'DKRT_INSTRUMENT_SESSION');
  expect(await session.client.memPeek(status)).toBe(0);
  const dspWord = async (symbol: string) => {
    const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
      addr: planSymbol(plan, symbol),
      len: 4,
    });
    expect(result.unknown).toBe(0);
    return Buffer.from(result.hex, 'hex').readUInt32LE(0);
  };
  const request = async (token: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(token);
    await session.client.call('mem.cf.write', {
      addr: planSymbol(plan, 'isp_request'),
      hex: bytes.toString('hex'),
    });
  };
  expect(await dspWord('_instrument_session')).toBe(0);
  await request(0x12345678);
  try {
    const deadline = Date.now() + 30_000;
    while (Date.now() < deadline) {
      await session.expectNoFault('bootstrap progress');
      if ((await session.client.memPeek(status)) === 4) break;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    expect(await session.client.memPeek(status)).toBe(4);
  } catch (error) {
    const link = planSymbol(plan, 'dl_link');
    const state = [];
    for (let i = 0; i < 21; i++) state.push(await session.client.memPeek(link + i * 4));
    console.error('bootstrap diagnostics', {
      state,
      machine: await session.status(),
      faults: await session.faults(),
      ticks: await session.client.memPeek(planSymbol(plan, 'DKRT_STATE') + 8),
      started: await session.client.memPeek(status + 8),
      frames: await session.client.memPeek(planSymbol(plan, 'frame_block')),
      guard: await session.client.memPeek(planSymbol(plan, 'registry_guard') + 12),
      session: await dspWord('_instrument_session'),
      heartbeat: await dspWord('_mi_heartbeat'),
      mailbox: await dspWord('_dl_irows'),
      ack: await dspWord('_instrument_registry_reply'),
    });
    throw error;
  }
  expect(await session.client.memPeek(status + 4)).toBe(0x12345678);
  expect(await dspWord('_instrument_session')).toBe(0x12345678);
  expect(await dspWord('_ir_lifecycle_ready')).toBe(0);
  // DlLink.lease is word 19; release succeeds only after physical END drainage.
  expect(await session.client.memPeek(planSymbol(plan, 'dl_link') + 76)).toBe(0);
  await request(0x87654321);
  // Observe multiple UI iterations; changing the trigger cannot rearm this boot.
  for (let i = 0; i < 10; i++) {
    await new Promise((resolve) => setTimeout(resolve, 100));
    expect(await session.client.memPeek(status)).toBe(4);
    expect(await dspWord('_instrument_session')).toBe(0x12345678);
  }
  await session.expectNoFault('one-shot live instrument session');
}, 90_000);
