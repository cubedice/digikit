import fs from 'node:fs';
import path from 'node:path';
import type { Session } from '@digi/test-support';
import { expect } from 'vitest';
import { acknowledgedPanel } from './panel-ack.ts';
import { memory } from './panel.ts';
import { portAnchor } from './device-data.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { waitEmulated } from './waits.ts';

/** Exercise the stock AMP page and final audio through the compiled adapter. */
export async function proveCanopyNativeAmp(session: Session, plan: RuntimePlan, state: number, root: string) {
  const { tap, turn } = acknowledgedPanel(session);
  const { soundParam } = memory(session);
  const peek = async (addr: number, len: number) => {
    const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(result.unknown).toBe(0);
    return Buffer.from(result.hex, 'hex');
  };
  const words = (bytes: Buffer) => Array.from({ length: bytes.length / 4 }, (_, i) => bytes.readUInt32LE(4 * i));
  const scalar = async (name: string) => (await peek(planSymbol(plan, name), 4)).readUInt32LE();
  await expect.poll(() => scalar('_mi_envelope_records'), { timeout: 30_000 }).toBeGreaterThan(0);
  const recordAddress = await scalar('_mi_envelope_records');
  const table = async (name: string) =>
    words(await peek(portAnchor(session.options.device, session.options.firmware, `dsp.voice.site.${name}`), 512));
  const [attackTable, decayTable, holdTable] = await Promise.all([
    table('amp_attack_table'),
    table('amp_decay_table'),
    table('amp_hold_table'),
  ]);
  const snapshots: Record<string, unknown>[] = [];
  const snapshot = async () => {
    let observed: { record: number[]; input: number[] } | undefined;
    await expect
      .poll(
        async () => {
          const record = words(await peek(recordAddress, 44));
          const input = words(await peek(planSymbol(plan, '_iv_input'), 128));
          const after = words(await peek(recordAddress, 28));
          observed = { record, input };
          if (record.slice(0, 7).some((value, i) => value !== after[i])) return false;
          if (input[0] !== 6 || input[1] !== 128 || ![1, 2].includes(record[6]!)) return false;
          const expected = [
            record[6],
            (attackTable[record[0]!]! ^ 0x80000000) >>> 0,
            record[1] === 127 ? 0xffffffff : holdTable[record[1]!],
            (decayTable[record[2]!]! ^ 0x80000000) >>> 0,
            record[3],
            (decayTable[record[4]!]! ^ 0x80000000) >>> 0,
            Number(record[5] !== 0),
            0,
          ];
          return expected.every((value, i) => input[i + 24] === value);
        },
        { timeout: 30_000, message: 'stock AMP configuration reaches the V6 snapshot intact' },
      )
      .toBe(true);
    return observed!;
  };
  // Native controls may have fractional raw steps. Acknowledge each turn and
  // completed DSP configuration instead of assuming one detent is one index.
  const set = async (encoder: number, read: (record: number[]) => number, target: number, endpoint = false) => {
    const trace: number[] = [];
    for (let attempt = 0; attempt < 96; attempt++) {
      const current = read((await snapshot()).record);
      trace.push(current);
      if (current === target) return;
      const difference = target - current;
      await turn(encoder, endpoint ? 30 * Math.sign(difference) : Math.max(-30, Math.min(30, difference)));
      await waitEmulated(session, 2);
    }
    throw new Error(`Native AMP encoder ${encoder} did not reach ${target}: ${trace.join(', ')}`);
  };
  // MODE has two clamped endpoints. Its stock inactivity filter can suppress
  // repeated tiny turns; a deliberate turn wakes it and crosses the enum step.
  // Read back the actual mode even when the first wake-up turn changes nothing.
  const mode = (target: number) => set(6, (record) => record[6]!, target, true);
  const parameter = (encoder: number, field: number, target: number) =>
    set(encoder, (record) => record[field]!, target);
  // A completed DSP block can briefly pass an integer index while the requested
  // Sound value is still higher. Normal time controls accumulate three detents
  // per integer step; one-detent turns can also be lost to the inactivity filter.
  // Match the actual Sound target (DTII 1.17 release mirror54) as well as DSP.
  const releaseConfig = async () => {
    let observed: Awaited<ReturnType<typeof snapshot>> & { rawRelease: number };
    await expect
      .poll(
        async () => {
          const rawRelease = await soundParam(0, 54);
          observed = { ...(await snapshot()), rawRelease };
          return rawRelease === (await soundParam(0, 54));
        },
        { timeout: 30_000 },
      )
      .toBe(true);
    return observed!;
  };
  const setRelease = async (target: number) => {
    const trace: number[][] = [];
    const rawTarget = target * 256;
    for (let attempt = 0; attempt < 96; attempt++) {
      const current = await releaseConfig();
      trace.push([current.rawRelease, current.record[4]!]);
      if (current.rawRelease === rawTarget && current.record[4] === target) return;
      const difference = rawTarget - current.rawRelease;
      if (difference) {
        const delta = Math.sign(difference) * Math.max(3, Math.min(30, Math.floor(Math.abs(difference) / 256)));
        await turn(4, delta);
      }
      await waitEmulated(session, 2);
    }
    throw new Error(`Native release did not reach ${target}: ${JSON.stringify(trace)}`);
  };
  const sustain = () =>
    set(
      3,
      (record) => {
        const bytes = Buffer.alloc(4);
        bytes.writeUInt32LE(record[3]!);
        return Math.round(bytes.readFloatLE() * 127);
      },
      127,
    );
  const voices = async () => {
    const data = await peek(state, 192);
    return Array.from({ length: 6 }, (_, slot) => ({
      token: data.readUInt32LE(slot * 32),
      slot,
      gate: data.readUInt32LE(slot * 32 + 4),
      amplitude: data.readFloatLE(slot * 32 + 28),
    })).filter((voice) => voice.token !== 0 && !(voice.gate & 4));
  };
  const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });
  const clear = async () => {
    await midi([0xb0, 120, 0]);
    await expect.poll(async () => (await voices()).length, { timeout: 30_000 }).toBe(0);
  };
  const measure = async (name: string, during?: () => Promise<unknown>) => {
    const file = path.join(root, `out/canopy-native-amp-${name}.f32le`);
    await session.client.call('pcm.tap', { action: 'capture', path: file });
    try {
      await session.client.call('pcm.tap', { action: 'start' });
      await during?.();
      await waitEmulated(session, 120);
    } finally {
      await session.client.call('pcm.tap', { action: 'capture_stop' });
    }
    const data = fs.readFileSync(file);
    expect(data.length).toBeGreaterThanOrEqual(4800 * 8);
    let sum = 0,
      peak = 0;
    for (let offset = 0; offset < data.length; offset += 4) {
      const sample = data.readFloatLE(offset);
      expect(Number.isFinite(sample)).toBe(true);
      sum += sample * sample;
      peak = Math.max(peak, Math.abs(sample));
    }
    expect(peak).toBeLessThan(1);
    return { rms: Math.sqrt(sum / (data.length / 4)), peak, samples: data.length / 4 };
  };
  const evidence: Record<string, unknown> = { snapshots };
  try {
    await clear();
    await tap('AMP');
    await mode(2);
    await parameter(1, 0, 0); // ADSR A: attack.
    await parameter(2, 2, 0); // ADSR B: decay.
    await sustain(); // ADSR C: sustain.
    const release = async (name: string, value: number) => {
      await clear();
      await setRelease(value); // ADSR D: release.
      const observe = async (phase: string) => {
        const config = await releaseConfig();
        const notes = await voices();
        const sidecars = words(await peek(state + 716 * 4, 28 * 4));
        snapshots.push({ name, phase, ...config, notes, sidecars });
        expect(config.rawRelease).toBe(value * 256);
        expect(config.record[4]).toBe(value);
        return { config, notes };
      };
      await observe('configured');
      console.info(`CANOPY AMP: ${name} configured through the native page`);
      await midi([0x90, 60, 100]);
      await expect
        .poll(async () => (await voices()).filter((voice) => voice.gate === 1).length, { timeout: 30_000 })
        .toBe(1);
      await waitEmulated(session, 80);
      await observe('held');
      const held = await measure(`${name}-held`);
      expect(held.rms).toBeGreaterThan(1e-5);
      await midi([0x80, 60, 0]);
      await expect
        .poll(async () => (await voices()).every((voice) => voice.gate !== 1), { timeout: 30_000 })
        .toBe(true);
      await waitEmulated(session, 120);
      const beforeTail = await observe('before-tail');
      const tail = await measure(`${name}-tail`);
      const afterTail = await observe('after-tail');
      const result = { held, tail, beforeTail, remaining: afterTail.notes };
      evidence[name] = result;
      return result;
    };
    const short = await release('short-release', 0);
    const long = await release('long-release', 80);
    expect(short.tail.rms).toBeLessThan(short.held.rms * 0.03);
    expect(long.tail.rms).toBeGreaterThan(long.held.rms * 0.2);
    expect(long.tail.rms).toBeGreaterThan(Math.max(1e-5, short.tail.rms * 10));
    expect(short.remaining.length).toBe(0);
    expect(long.remaining.length).toBe(1);
    expect(long.remaining[0]!.amplitude).toBeGreaterThan(0);
    expect(long.remaining[0]!.amplitude).toBeLessThan(long.beforeTail.notes[0]!.amplitude);
    expect(long.beforeTail.config.input[29]).toBeLessThan(0x3f800000);
    console.info('CANOPY AMP: short and long native release produce different audible tails');

    await clear();
    await mode(1);
    await parameter(1, 0, 0); // AHD A/B/C: attack, hold, decay.
    await parameter(2, 1, 0);
    await parameter(3, 2, 30);
    snapshots.push({ name: 'finite-ahd', ...(await snapshot()) });
    console.info('CANOPY AMP: finite AHD configured through the native page');
    const first = await measure('ahd-first', () => midi([0x90, 60, 100]));
    expect(first.peak).toBeGreaterThan(1e-5);
    await expect
      .poll(async () => (await voices()).map((voice) => [voice.gate, voice.amplitude]), {
        timeout: 60_000,
        message: 'finite AHD finishes while its original key remains held',
      })
      .toEqual([[1, 0]]);
    const firstToken = (await voices())[0]!.token;
    const silent = await measure('ahd-held-silent');
    const second = await measure('ahd-second', () => midi([0x90, 67, 100]));
    expect(second.peak).toBeGreaterThan(Math.max(1e-5, silent.peak * 10));
    expect(second.peak).toBeGreaterThan(first.peak * 0.1);
    const held = await voices();
    expect(held.some((voice) => voice.token === firstToken && voice.gate === 1)).toBe(true);
    expect(held.filter((voice) => voice.gate === 1).length).toBe(2);
    evidence.ahd = { first, silent, second, held };
    console.info('CANOPY AMP: new AHD note is audible after the previous held note finishes');
    await midi([0x80, 60, 0, 0x80, 67, 0]);
    await clear();

    // The later coexistence proof needs sustained notes and finite short tails.
    await mode(2);
    await parameter(1, 0, 0);
    await parameter(2, 2, 0);
    await sustain();
    await setRelease(0);
    snapshots.push({ name: 'coexistence-adsr', ...(await snapshot()) });
    await session.expectNoFault('CANOPY stock AMP release and repeated finite AHD');
  } finally {
    await clear();
    fs.writeFileSync(path.join(root, 'out/canopy-native-amp.json'), JSON.stringify(evidence, null, 2));
  }
  return evidence;
}
