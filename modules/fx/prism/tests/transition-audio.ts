/**
 * A WAV file of a whole transition through PRISM and Overloop for listening: both engines on the SHARC-only rig, coupled as the
 * Digitakt II runs them (pair-rig.ts), with plucked patterns at 120 BPM. Nothing here is a test: the numbers are in
 * transition.rig.test.ts.
 *
 *     pnpm nx:pilot run-many -t build,cli -p sharc-rig patchkit          # the two binaries, once
 *     node modules/fx/prism/tests/transition-audio.ts OUTDIR             # about five minutes
 *
 * OUTDIR/transition-in.wav is what the tracks play (pattern A, then pattern B from the change on), OUTDIR/transition-out.wav
 * what is heard; 48 kHz, 16 bit, stereo. OUTDIR/README.txt has the time of every event. Audio is never committed.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import type { Pattern } from '../../overloop/tests/loop-rig.ts';
import { BLOCK, DEVICE_FRAMES, FS, peak } from './engine-rig.ts';
import { PairRig, type PairTake } from './pair-rig.ts';

/** A step of 16 at 120 BPM is 6000 frames, 187.5 blocks: the steps alternate between 187 and 188 blocks, as on the device. */
const STEPS = [5984, 6016] as const;
const BAR = 96000;
const A: Pattern = { id: 0x47000100, steps: 16, stepFrames: STEPS };
const B: Pattern = { id: 0x47000200, steps: 16, stepFrames: STEPS };

/** A plucked note added into `out` from `at`: a tone with its octave, a short noise at the attack, an exponential decay. */
function pluck(out: Float32Array, hz: number, amp: number, at: number, decay = 0.12): void {
  let seed = (at * 2654435761) >>> 0;
  for (let p = 0; p <= FS && at + p < out.length; p++) {
    const t = p / FS;
    const env = Math.min(1, p / 48) * Math.exp(-t / decay);
    let v = env * (Math.sin(2 * Math.PI * hz * t) + 0.3 * Math.sin(4 * Math.PI * hz * t));
    if (p < 480) {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
      v += 0.4 * Math.exp(-p / 120) * (seed / 2 ** 31 - 1);
    }
    out[at + p] += amp * v;
  }
}
/** Pattern A: eighth notes, a bar of A3 - C#4 E4 - A4 E4 - (the phrase of PRISM's renders). */
function patternA(out: Float32Array, from: number, to: number): void {
  const notes = [220, 0, 277.18, 329.63, 0, 440, 329.63, 0];
  for (let at = 0; at < to; at += 12000) if (at >= from && notes[(at / 12000) % 8]) pluck(out, notes[(at / 12000) % 8]!, 0.3, at);
}
/** Pattern B: sixteenth notes an octave and a fifth lower, longer decays. */
function patternB(out: Float32Array, from: number, to: number, origin: number): void {
  const notes = [73.42, 73.42, 0, 73.42, 87.31, 0, 73.42, 0, 110, 0, 73.42, 73.42, 130.81, 0, 110, 0];
  for (let at = origin; at < to; at += 6000)
    if (at >= from && notes[((at - origin) / 6000) % 16]) pluck(out, notes[((at - origin) / 6000) % 16]!, 0.35, at, 0.2);
}

function wav(left: Float32Array, right: Float32Array): Buffer {
  const data = Buffer.alloc(4 * left.length);
  for (let n = 0; n < left.length; n++) {
    data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, left[n]!)) * 32767), 4 * n);
    data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, right[n]!)) * 32767), 4 * n + 2);
  }
  const head = Buffer.alloc(44);
  head.write('RIFF', 0);
  head.writeUInt32LE(36 + data.length, 4);
  head.write('WAVEfmt ', 8);
  head.writeUInt32LE(16, 16);
  head.writeUInt16LE(1, 20);
  head.writeUInt16LE(2, 22);
  head.writeUInt32LE(FS, 24);
  head.writeUInt32LE(FS * 4, 28);
  head.writeUInt16LE(4, 32);
  head.writeUInt16LE(16, 34);
  head.write('data', 36);
  head.writeUInt32LE(data.length, 40);
  return Buffer.concat([head, data]);
}

const dir = process.argv[2];
if (!dir) throw new Error('usage: node modules/fx/prism/tests/transition-audio.ts OUTDIR');
mkdirSync(dir, { recursive: true });
const seconds = (frames: number): string => `${(frames / FS).toFixed(2)} s`;
const change = 3 * BAR;
const armAt = change - FS;
const fadeFrom = change + 2 * BAR;
const fadeTo = fadeFrom + 2 * BAR;
const gAt = fadeTo + BAR;
const end = gAt + 2 * BAR;
const dry = new Float32Array(end + FS);
patternA(dry, 0, change);
patternB(dry, change, end, change);
const started = Date.now();
const pair = await PairRig.open({ prismFrames: DEVICE_FRAMES, loopFrames: DEVICE_FRAMES });
const takes: PairTake[] = [];
const run = async (toFrame: number): Promise<void> => {
  const blocks = Math.round((toFrame - pair.clock) / BLOCK);
  if (blocks > 0) takes.push(await pair.run(blocks, (n) => dry[n] ?? 0));
};
try {
  // PRISM: a dotted eighth in SYNC with a few repeats, MIX most of the way up
  pair.prism.set({ sync: true, syncTime: 18000, rpts: 0.6, mix: 0.8 });
  pair.loop.start(A);
  await run(armAt);
  pair.loop.arm();
  pair.loop.changeAtEnd(B);
  await run(change);
  // the page puts INPT at LOOP a few milliseconds after the change (PRISM has taken the loop by itself already)
  await run(change + 10 * BLOCK);
  pair.prism.set({ input: 1 });
  // PRISM played on the old pattern: more repeats, darker
  await run(change + BAR);
  pair.prism.set({ rpts: 0.72, colr: -0.4 });
  await run(fadeFrom);
  for (let step = 1; step <= 127; step++) {
    await run(fadeFrom + Math.round((step * (fadeTo - fadeFrom)) / 127));
    pair.loop.setXfad(127 - step);
  }
  // PRISM set up for the new pattern, then G
  await run(fadeTo);
  pair.prism.set({ rpts: 0.5, colr: 0 });
  await run(gAt);
  pair.prism.set({ input: 0 });
  await run(end);
} finally {
  await pair.close();
}
const n = takes.reduce((s, t) => s + t.left.length, 0);
const outL = new Float32Array(n);
const outR = new Float32Array(n);
let at = 0;
for (const t of takes) {
  outL.set(t.left, at);
  outR.set(t.right, at);
  at += t.left.length;
}
// both files by one factor, so that the louder one peaks at 0.9 (the 16-bit file does not clip)
const top = Math.max(peak(outL), peak(outR), peak(dry.subarray(0, n)));
const scale = Math.min(1, 0.9 / top);
const scaled = (x: Float32Array): Float32Array => Float32Array.from(x, (v) => v * scale);
writeFileSync(path.join(dir, 'transition-in.wav'), wav(scaled(dry.subarray(0, n)), scaled(dry.subarray(0, n))));
writeFileSync(path.join(dir, 'transition-out.wav'), wav(scaled(outL), scaled(outR)));
writeFileSync(
  path.join(dir, 'README.txt'),
  [
    'A transition through PRISM and Overloop, rendered by both engines on the SHARC-only rig, coupled block by block as the',
    'Digitakt II runs them; the master stage is its linear case. Not a recording of a device.',
    '',
    'transition-in.wav   what the tracks play: pattern A, then pattern B from the change on (every track at full PRISM send)',
    'transition-out.wav  what is heard',
    `48 kHz, 16 bit, stereo; both files scaled by ${scale.toFixed(3)} (${(20 * Math.log10(scale)).toFixed(1)} dB) so that the louder peaks at 0.9.`,
    'Tempo 120 BPM, a bar is 2.00 s. Pattern A: eighth notes, A3 - C#4 E4 - A4 E4 - (plucked).',
    'Pattern B: sixteenth notes, low D with F, A and C. PRISM: a dotted eighth in SYNC, RPTS 0.6, MIX 0.8.',
    '',
    `  0.00 s        pattern A plays, PRISM echoes it (INPT LIVE)`,
    `  ${seconds(armAt)}        ARM`,
    `  ${seconds(change)}        the pattern change: the loop (the last bar of A) takes over at LOOP, PRISM takes the loop`,
    `  ${seconds(change + BAR)}        PRISM played on the old pattern: more repeats (0.72), darker (COLR -0.4)`,
    `  ${seconds(fadeFrom)}       the crossfader moves from LOOP to LIVE, evenly over two bars`,
    `  ${seconds(fadeTo)}       LIVE: B alone and dry, A's echoes dying away; PRISM set back (RPTS 0.5, COLR 0)`,
    `  ${seconds(gAt)}       G: INPT LIVE, PRISM echoes pattern B`,
    `  ${seconds(n)}       end. Peak of the output before the scaling ${top.toFixed(3)}.`,
    '',
  ].join('\n'),
);
console.info(`transition: ${(n / FS).toFixed(1)} s of audio in ${((Date.now() - started) / 1000).toFixed(0)} s`);
