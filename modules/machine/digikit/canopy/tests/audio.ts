// The analysis of a CANOPY capture (`pcm.tap`: f32le stereo at 48 kHz) shared by the device tests: its level envelope and its
// strongest spectral peaks.

/** The sample rate of a capture. */
export const RATE = 48000;

/** The mono envelope of a capture in 10 ms frames, in dB (floor -100). */
export function envelope(raw: Buffer): number[] {
  const frames = Math.floor(raw.length / 8);
  const hop = RATE / 100;
  const out: number[] = [];
  for (let at = 0; at + hop <= frames; at += hop) {
    let sum = 0;
    for (let i = at; i < at + hop; i++) {
      const m = (raw.readFloatLE(8 * i) + raw.readFloatLE(8 * i + 4)) / 2;
      sum += m * m;
    }
    out.push(Math.max(-100, 10 * Math.log10(sum / hop + 1e-12)));
  }
  return out;
}

/** The strongest spectral peaks of `n` (a power of two) mono samples from frame `from`, as MIDI note numbers (rounded to 0.1). */
export function pitches(raw: Buffer, from: number, n = 16384, count = 6): number[] {
  const re = new Float64Array(n),
    im = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const at = from + i;
    const m = 8 * at + 8 <= raw.length ? (raw.readFloatLE(8 * at) + raw.readFloatLE(8 * at + 4)) / 2 : 0;
    re[i] = m * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1)));
  }
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j]!, re[i]!];
      [im[i], im[j]] = [im[j]!, im[i]!];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    for (let i = 0; i < n; i += len)
      for (let k = 0; k < len / 2; k++) {
        const wr = Math.cos(ang * k),
          wi = Math.sin(ang * k);
        const ur = re[i + k]!,
          ui = im[i + k]!;
        const vr = re[i + k + len / 2]! * wr - im[i + k + len / 2]! * wi,
          vi = re[i + k + len / 2]! * wi + im[i + k + len / 2]! * wr;
        re[i + k] = ur + vr;
        im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;
        im[i + k + len / 2] = ui - vi;
      }
  }
  const mag = Array.from({ length: n / 2 }, (_, i) => Math.hypot(re[i]!, im[i]!));
  const peaks: { bin: number; m: number }[] = [];
  for (let i = 2; i < mag.length - 1; i++)
    if (mag[i]! > mag[i - 1]! && mag[i]! >= mag[i + 1]! && (i * RATE) / n > 40) peaks.push({ bin: i, m: mag[i]! });
  peaks.sort((a, b) => b.m - a.m);
  return peaks
    .slice(0, count)
    .map((p) => Math.round((69 + 12 * Math.log2((p.bin * RATE) / n / 440)) * 10) / 10)
    .sort((a, b) => a - b);
}
