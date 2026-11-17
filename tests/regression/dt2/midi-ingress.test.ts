import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';

interface Observer {
  syx_sha256: string;
  symbols: Record<string, number>;
  writes: { addr: number; hex: string }[];
}

const h = withSession({ device: 'dt2', firmware: '1.17', dsp: 'zero' });

test('USB and DIN gathered messages respect receive settings and auto channel', async () => {
  const root = repoRoot();
  const artifact = path.join(root, 'out/canopy-midi-observer.json');
  if (!fs.existsSync(artifact)) {
    throw new Error('Prepare observer with uv run python -m tools.coldfire.midi_ingress_probe');
  }
  const observer: Observer = JSON.parse(fs.readFileSync(artifact, 'utf8'));
  const sha = createHash('sha256')
    .update(fs.readFileSync(path.join(root, 'firmware/Digitakt_II_OS1.17.syx')))
    .digest('hex');
  expect(observer.syx_sha256).toBe(sha);
  const { session } = h;
  expect(session.loadResult.syx_sha256).toBe(sha);
  const symbol = (name: string): number => {
    const value = observer.symbols[name];
    if (!Number.isInteger(value)) throw new Error(`missing observer symbol ${name}`);
    return value!;
  };
  const write = (addr: number, value: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value >>> 0);
    return session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  };
  for (const entry of observer.writes) await session.client.call('mem.cf.write', entry);
  // These settings are changed only in this fresh emulator instance. No files or
  // device settings are saved. The observer always runs the displaced stock call.
  const enableAddress = 0x429368ca;
  const autoAddress = 0x42936872;
  const originalEnable = await session.client.memPeek(enableAddress);
  const originalAuto = await session.client.memPeek(autoAddress);
  expect(originalEnable).not.toBeNull();
  expect(originalAuto).not.toBeNull();
  try {
    await write(autoAddress, 0);
    for (const port of ['usb', 'din'] as const) {
      for (const enabled of [true, false]) {
        await write(enableAddress, enabled ? 3 : port === 'usb' ? 1 : 2);
        await write(symbol('mi_observed_count'), 0);
        await write(symbol('mi_observed_overflow'), 0);
        // Channel 1 is auto, channel 2 ordinary. Send matching releases too.
        const bytes = [0x90, 60, 100, 0x80, 60, 0, 0x91, 64, 90, 0x81, 64, 0];
        await session.client.call(port === 'usb' ? 'midi.usb.in' : 'midi.in', { bytes });
        await session.wait(100);
        expect(await session.client.memPeek(symbol('mi_observed_overflow'))).toBe(0);
        const count = await session.client.memPeek(symbol('mi_observed_count'));
        expect(count, `${port} receive ${enabled}`).toBe(enabled ? 4 : 0);
        if (enabled) {
          const expected = [
            [port === 'usb' ? 4 : 16, 3, 0x903c64],
            [port === 'usb' ? 4 : 16, 3, 0x803c00],
            [port === 'usb' ? 2 : 8, 3, 0x91405a],
            [port === 'usb' ? 2 : 8, 3, 0x814000],
          ];
          for (let i = 0; i < expected.length; ++i) {
            const row = [];
            for (let j = 0; j < 3; ++j) {
              row.push(await session.client.memPeek(symbol('mi_observed') + i * 12 + j * 4));
            }
            expect(row).toEqual(expected[i]);
          }
        }
        await session.expectNoFault(`${port} receive ${enabled}`);
      }
    }
  } finally {
    await write(enableAddress, originalEnable!);
    await write(autoAddress, originalAuto!);
  }
});
