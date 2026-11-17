import { expect, test } from 'vitest';
import { chordWindow, voiceChange } from '../runtime/chord-continuity.ts';

function capture(fault = '') {
  const raw = Buffer.alloc(24576 * 8);
  for (let frame = 0; frame < 24576; frame++)
    for (let channel = 0; channel < 2; channel++) {
      let sample = 0;
      for (const note of [60, 64, 67]) {
        if ((note === 67 && frame < 8192) || (note === 64 && frame >= 16384)) continue;
        const broken = channel === 1 && note === 60 && frame >= 16384;
        const t = broken && fault === 'restart' ? frame - 16384 : frame;
        const gain = broken && fault === 'gain' ? 0.5 : broken && fault === 'cutoff' ? 0 : 1;
        sample += gain * 0.05 * Math.sin((2 * Math.PI * 440 * 2 ** ((note - 69) / 12) * t) / 48000 + note);
      }
      raw.writeFloatLE(sample, frame * 8 + channel * 4);
    }
  return raw;
}

test('separates held voices from a newly added and released voice', () => {
  const raw = capture();
  const a = chordWindow(raw, 0, 8192),
    b = chordWindow(raw, 8192, 8192),
    c = chordWindow(raw, 16384, 8192);
  for (const channel of [0, 1]) {
    expect(voiceChange(a[channel]![0]!, b[channel]![0]!)).toBeLessThan(1e-6);
    expect(voiceChange(b[channel]![0]!, c[channel]![0]!)).toBeLessThan(1e-6);
    expect(voiceChange(b[channel]![2]!, c[channel]![2]!)).toBeLessThan(1e-6);
    expect(c[channel]![1]!.amplitude).toBeLessThan(1e-8);
  }
});
test.each(['restart', 'gain', 'cutoff'])('detects held-note %s in the right channel', (fault) => {
  const raw = capture(fault),
    a = chordWindow(raw, 8192, 8192),
    b = chordWindow(raw, 16384, 8192);
  expect(voiceChange(a[0]![0]!, b[0]![0]!)).toBeLessThan(1e-6);
  expect(voiceChange(a[1]![0]!, b[1]![0]!)).toBeGreaterThan(0.02);
});
