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

test.each([false, true])(
  'live MIDI reaches configured CANOPY banks (session bound: %s)',
  async (bound) => {
    const root = repoRoot();
    const image = path.join(root, `out/patch/canopy-live-transport${bound ? '-session' : ''}.syx`);
    const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
    expect(plan.modules).toContain('test/canopy-transport@0.1.0');
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
    const status = planSymbol(plan, 'cp_transport_proof');
    const word = (index: number) => session.client.memPeek(status + 4 * index);
    const banks = planSymbol(plan, '_cp_banks');
    const readVoices = async () => {
      const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr: banks, len: 256 });
      expect(result.unknown).toBe(0);
      const bytes = Buffer.from(result.hex, 'hex');
      return Array.from({ length: 4 }, (_, index) => ({
        token: bytes.readUInt32LE(index * 32),
        gate: bytes.readUInt32LE(index * 32 + 4),
      }))
        .filter((voice) => voice.token !== 0)
        .sort((a, b) => a.token - b.token);
    };
    if (bound) {
      expect(plan.modules).toContain('test/instrument-session@0.1.0');
      await expect.poll(() => word(2), { timeout: 30_000 }).toBe(0);
      const binding = planSymbol(plan, 'DKRT_INSTRUMENT_BINDING');
      await expect.poll(() => session.client.memPeek(binding + 8), { timeout: 30_000 }).toBe(1);
      expect(await word(0)).toBe(0);
      expect(await word(5)).toBe(0);
      expect(await readVoices()).toEqual([]);
      const request = Buffer.alloc(4);
      request.writeUInt32BE(0x12345678);
      await session.client.call('mem.cf.write', {
        addr: planSymbol(plan, 'isp_request'),
        hex: request.toString('hex'),
      });
      const bootstrap = planSymbol(plan, 'DKRT_INSTRUMENT_SESSION');
      await expect.poll(() => session.client.memPeek(bootstrap), { timeout: 30_000 }).toBe(4);
      expect(await session.client.memPeek(bootstrap + 4)).toBe(0x12345678);
    }
    await expect.poll(() => word(0), { timeout: 30_000, interval: 100 }).toBe(3);
    expect(await word(1)).toBe(1);
    expect(await word(4)).toBe(0);
    expect(await word(5)).toBe(1);
    expect(await word(10)).toBe(2);
    expect(await word(11)).toBe(bound ? 2 : 1);
    expect(await readVoices()).toEqual([]);
    const write = async (addr: number, value: number) => {
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(value);
      await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
    };
    await write(0x429368ca, 3); // enable both receive ports in this emulator only
    await write(0x42936872, 0); // channel 1 is auto
    const send = async (port: 'usb' | 'din', bytes: number[], expected: { token: number; gate: number }[]) => {
      const before = (await word(8))!;
      await session.client.call(port === 'usb' ? 'midi.usb.in' : 'midi.in', { bytes });
      await expect.poll(readVoices, { timeout: 10_000, interval: 100 }).toEqual(expected);
      await expect.poll(() => word(8), { timeout: 10_000, interval: 100 }).toBeGreaterThan(before);
      await expect.poll(() => word(7), { timeout: 10_000, interval: 100 }).toBe(0);
      expect(await word(3)).toBe(0);
      await session.expectNoFault(`${port} CANOPY event ACK`);
    };
    await send(
      'usb',
      [0x90, 60, 20, 0x90, 64, 100],
      [
        { token: 1, gate: 1 },
        { token: 2, gate: 1 },
      ],
    );
    await send(
      'usb',
      [0x80, 60, 0],
      [
        { token: 1, gate: 0 },
        { token: 2, gate: 1 },
      ],
    );
    await send(
      'din',
      [0x90, 67, 127],
      [
        { token: 1, gate: 0 },
        { token: 2, gate: 1 },
        { token: 3, gate: 1 },
      ],
    );
    await send(
      'usb',
      [0x90, 64, 0],
      [
        { token: 1, gate: 0 },
        { token: 2, gate: 0 },
        { token: 3, gate: 1 },
      ],
    );
    await send(
      'din',
      [0x80, 67, 0],
      [
        { token: 1, gate: 0 },
        { token: 2, gate: 0 },
        { token: 3, gate: 0 },
      ],
    );
    expect(await word(9)).toBe(4);
  },
  90_000,
);
