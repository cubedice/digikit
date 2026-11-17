import { expect, test } from 'vitest';
import { sineContinuity } from '../runtime/sine-continuity.ts';

const boundary = 16000;
const omega = (2 * Math.PI * 261.625565) / 48000;
function capture(edit: (i: number, value: number) => number = (_, value) => value) {
  const raw = Buffer.alloc(48000 * 8);
  for (let i = 0; i < 48000; i++) {
    raw.writeFloatLE(0.04 * Math.sin(omega * i + 0.7), i * 8);
    // Mutate only the right channel, so a left-only check cannot pass.
    raw.writeFloatLE(edit(i, 0.03 * Math.sin(omega * i + 0.4)), i * 8 + 4);
  }
  return raw;
}

test('predicts unbroken stereo PCM beyond the source calibration', () => {
  for (const channel of sineContinuity(capture())) {
    expect(channel.frequency).toBeCloseTo(261.625565, 3);
    expect(channel.relativeError).toBeLessThan(0.001);
    expect(channel.relativeResidual).toBeLessThan(0.00001);
  }
});

test.each(['drop', 'duplicate', 'restart', 'gain', 'gap'] as const)('detects %s after the source prefix', (fault) => {
  const raw = capture((i, value) => {
    if (i < boundary) return value;
    if (fault === 'gain') return value * 0.95;
    if (fault === 'gap') return i < boundary + 32 ? 0 : value;
    const position = fault === 'drop' ? i + 1 : fault === 'duplicate' ? i - 1 : i - boundary;
    return 0.03 * Math.sin(omega * position + 0.4);
  });
  const [left, right] = sineContinuity(raw);
  expect(left!.relativeError).toBeLessThan(0.001);
  expect(right!.relativeError).toBeGreaterThan(0.001);
  expect(right!.maxErrorFrame).toBeGreaterThanOrEqual(boundary);
});

test('rejects silence, nonfinite data and incomplete stereo frames', () => {
  expect(() => sineContinuity(Buffer.alloc(48000 * 8))).toThrow();
  const raw = capture();
  raw.writeFloatLE(NaN, boundary * 8);
  expect(() => sineContinuity(raw)).toThrow('non-finite');
  expect(() => sineContinuity(capture().subarray(1))).toThrow('stereo');
});
