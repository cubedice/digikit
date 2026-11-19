/**
 * WAV files of a whole transition through Overloop for listening: the engine on the SHARC-only rig (loop-rig.ts) with
 * buffers as the device leases them, a sequencer that plays pattern A and then pattern B, ARM, the pattern change, some
 * turns of the loop and the crossfader moved to LIVE. Nothing here is a test: the numbers are in loop.rig.test.ts.
 *
 *     pnpm nx:pilot run-many -t build,cli -p sharc-rig patchkit          # the two binaries, once
 *     node modules/fx/overloop/tests/render-audio.ts OUTDIR              # both cases
 *
 * Each case gives OUTDIR/<name>-in.wav, what the sequencer plays into the effect (pattern A, then pattern B from the
 * change on), and OUTDIR/<name>-out.wav, what is heard. 48 kHz, 16 bit, stereo. OUTDIR/README.txt has the time of every
 * event. Audio is never committed.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { BLOCK, DEVICE_FRAMES, FS, LoopRig, peak, type Pattern, type Take } from './loop-rig.ts';

/** A step of 16 at 120 BPM is 6000 frames, 187.5 blocks: the steps alternate between 187 and 188 blocks, as on the device. */
const STEPS = [5984, 6016] as const;
const BAR = 96000;
const A: Pattern = { id: 0x47000100, steps: 16, stepFrames: STEPS };
const B: Pattern = { id: 0x47000200, steps: 16, stepFrames: STEPS };

/** A plucked note: a tone with its octave, a short noise at the attack, an exponential decay. Added into `out` from `at`. */
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
/** Pattern A: eighth notes, a bar of [A3 - C#4 E4 - A4 E4 -] (the phrase of PRISM's renders). The notes of the eighths in [from, to). */
function patternA(out: Float32Array, from: number, to: number): void {
  const notes = [220, 0, 277.18, 329.63, 0, 440, 329.63, 0];
  for (let at = 0; at < to; at += 12000) if (at >= from && notes[(at / 12000) % 8]) pluck(out, notes[(at / 12000) % 8]!, 0.3, at);
}
/** Pattern B: sixteenth notes an octave and a fifth lower, a bar of [D2 D2 - D2 F2 - D2 - A2 - D2 D2 C3 - A2 -], longer decays. */
function patternB(out: Float32Array, from: number, to: number, origin: number): void {
  const notes = [73.42, 73.42, 0, 73.42, 87.31, 0, 73.42, 0, 110, 0, 73.42, 73.42, 130.81, 0, 110, 0];
  for (let at = origin; at < to; at += 6000)
    if (at >= from && notes[((at - origin) / 6000) % 16]) pluck(out, notes[((at - origin) / 6000) % 16]!, 0.35, at, 0.2);
}

interface Case {
  name: string;
  about: string;
  /** the frame of the pattern change */
  change: number;
  /** a direct change (the new pattern starts at the next step), else a change at the pattern's end */
  direct: boolean;
}
const cases: Case[] = [
  {
    name: 'sequential',
    about: 'Pattern B is chosen during the third bar of A and comes at the end of that bar, as the Digitakt changes patterns by default.',
    change: 3 * BAR,
    direct: false,
  },
  {
    name: 'mid-pattern',
    about: 'A direct change in the middle of the third bar of A: pattern B starts at once, on the ninth step. The loop is the last two seconds heard, so it starts in the middle of a bar and goes on from there.',
    change: 2 * BAR + BAR / 2,
    direct: true,
  },
];

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

const seconds = (frames: number): string => `${(frames / FS).toFixed(2)} s`;
const dir = process.argv[2];
if (!dir) throw new Error('usage: node modules/fx/overloop/tests/render-audio.ts OUTDIR');
mkdirSync(dir, { recursive: true });
const readme: string[] = [
  'A transition through Overloop, rendered by the engine on the SHARC-only rig (not a recording of a device).',
  '',
  '<name>-in.wav   what the sequencer plays into the effect: pattern A, then pattern B from the change on',
  '<name>-out.wav  what is heard',
  '48 kHz, 16 bit, stereo; both sides carry the same signal. Tempo 120 BPM, a bar is 2.00 s.',
  'Pattern A: eighth notes, a bar of A3 - C#4 E4 - A4 E4 - (plucked). Pattern B: sixteenth notes, low D with F, A and C.',
  '',
];
for (const c of cases) {
  const started = Date.now();
  // the loop turns three times, then the crossfader goes to LIVE over two bars, then pattern B alone for two bars
  const fadeFrom = c.change + 3 * BAR;
  const fadeTo = fadeFrom + 2 * BAR;
  const end = fadeTo + 2 * BAR;
  const input = new Float32Array(end + FS);
  patternA(input, 0, c.change);
  patternB(input, c.change, end, c.change);
  const rig = await LoopRig.open({ frames: DEVICE_FRAMES, poison: false });
  const takes: Take[] = [];
  const run = async (toFrame: number): Promise<void> => {
    const blocks = Math.round((toFrame - rig.clock) / BLOCK);
    if (blocks > 0) takes.push(await rig.run(blocks, (n) => input[n] ?? 0));
  };
  try {
    rig.start(A);
    // ARM a second before the change, and choose pattern B
    const armAt = c.change - FS;
    await run(armAt);
    rig.arm();
    if (c.direct) {
      // the change comes with the next step boundary: ask for it inside the step before
      await run(c.change - 3000);
      rig.changeNow(B);
    } else rig.changeAtEnd(B);
    await run(fadeFrom);
    const loop = await rig.status();
    // the crossfader from LOOP to LIVE in 127 steps over two bars
    for (let step = 1; step <= 127; step++) {
      await run(fadeFrom + Math.round((step * (fadeTo - fadeFrom)) / 127));
      rig.setXfad(127 - step);
    }
    await run(end);
    const changed = rig.log.find(([, id]) => id === B.id)![0];
    const n = takes.reduce((sum, t) => sum + t.left.length, 0);
    const outLeft = new Float32Array(n);
    const outRight = new Float32Array(n);
    let at = 0;
    for (const t of takes) {
      outLeft.set(t.left, at);
      outRight.set(t.right, at);
      at += t.left.length;
    }
    writeFileSync(path.join(dir, `${c.name}-in.wav`), wav(input.subarray(0, n), input.subarray(0, n)));
    writeFileSync(path.join(dir, `${c.name}-out.wav`), wav(outLeft, outRight));
    readme.push(
      `${c.name}: ${c.about}`,
      `  0.00 s        pattern A starts`,
      `  ${seconds(armAt)}        ARM`,
      `  ${seconds(changed)}        the pattern change: B plays into the effect, and the loop of ${loop.loopFrames} frames (${seconds(loop.loopFrames)}, ${loop.loopSteps} steps) is heard, with the crossfader at LOOP`,
      `  ${seconds(fadeFrom)}        the crossfader starts to move from LOOP to LIVE, evenly over two bars`,
      `  ${seconds(fadeTo)}       the crossfader is at LIVE: pattern B alone`,
      `  ${seconds(n)}       end. Peak of the output ${peak(outLeft).toFixed(3)}.`,
      '',
    );
    console.info(`${c.name}: ${(n / FS).toFixed(1)} s of audio in ${((Date.now() - started) / 1000).toFixed(0)} s`);
  } finally {
    await rig.close();
  }
}
writeFileSync(path.join(dir, 'README.txt'), `${readme.join('\n')}\n`);
