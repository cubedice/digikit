/**
 * WAV files of PRISM's engine for listening: the engine on the SHARC-only rig (engine-rig.ts) with a bank as the device
 * leases it, driven by short scripts of control changes. Nothing here is a test: the numbers are in engine.rig.test.ts.
 *
 *     pnpm nx:pilot run-many -t build,cli -p sharc-rig patchkit          # the two binaries, once
 *     node modules/fx/prism/tests/render-audio.ts OUTDIR [name ...]      # every case, or the named ones
 *
 * Each case gives OUTDIR/<name>.wav, the input plus the wet signal as a send effect returns it (both at half level, so
 * the sum cannot clip), and OUTDIR/wet/<name>.wav, the wet signal alone. 48 kHz, 16 bit, stereo. OUTDIR/README.txt lists
 * every file with its input, its controls and their changes, and the peak of the wet signal. Audio is never committed.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { BLOCK, DEFAULTS, DEVICE_FRAMES, FS, PrismRig, peak, zoneTime, type Controls, type Signal } from './engine-rig.ts';

interface Case {
  name: string;
  about: string;
  input: string;
  signal: Signal;
  seconds: number;
  /** the controls at the start (over DEFAULTS with MIX 1) */
  start: Partial<Controls>;
  /** changes: [seconds, controls] */
  moves?: [number, Partial<Controls>][];
}

const sec = (s: number): number => Math.round(s * FS);
/** A plucked note: a tone with its octave, a short noise at the attack, an exponential decay. */
function pluck(hz: number, amp: number, at: number, decay = 0.12): Signal {
  let seed = (at * 2654435761) >>> 0;
  const hiss: number[] = [];
  for (let k = 0; k < 480; k++) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    hiss.push(seed / 2 ** 31 - 1);
  }
  return (n) => {
    const p = n - at;
    if (p < 0 || p > FS) return 0;
    const t = p / FS;
    const env = Math.min(1, p / 48) * Math.exp(-t / decay);
    const tone = Math.sin(2 * Math.PI * hz * t) + 0.3 * Math.sin(4 * Math.PI * hz * t);
    return amp * (env * tone + (p < 480 ? 0.4 * Math.exp(-p / 120) * hiss[p]! : 0));
  };
}
/** A chord of three tones from `at` for `seconds`, with edges of 20 ms. */
function chord(amp: number, at: number, seconds: number): Signal {
  const frames = sec(seconds);
  return (n) => {
    const p = n - at;
    if (p < 0 || p >= frames) return 0;
    const edge = Math.min(1, p / 960, (frames - p) / 960);
    const t = p / FS;
    return (amp / 3) * edge * (Math.sin(2 * Math.PI * 220 * t) + Math.sin(2 * Math.PI * 277.18 * t) + Math.sin(2 * Math.PI * 329.63 * t));
  };
}
/** Eighth notes at 120 BPM from `at`, a bar of [A3 - C#4 E4 - A4 E4 -] for `bars` bars. */
function phrase(at: number, bars: number, amp = 0.3): Signal {
  const notes = [220, 0, 277.18, 329.63, 0, 440, 329.63, 0];
  const plucks: Signal[] = [];
  for (let bar = 0; bar < bars; bar++)
    notes.forEach((hz, k) => {
      if (hz) plucks.push(pluck(hz, amp, at + (bar * 8 + k) * 12000));
    });
  return (n) => {
    let v = 0;
    for (const p of plucks) v += p(n) as number;
    return v;
  };
}

const ZONE_NAMES = ['KARP', 'FLNG', 'CHOR', 'SLAP', 'ECHO', 'LONG', 'PHRS', 'LOOP'];
const cases: Case[] = [];
for (let zone = 1; zone <= 8; zone++) {
  const time = zoneTime(zone, 0.5) / FS;
  const seconds = Math.max(3, Math.min(5 * time + 2.5, 30));
  const where = `zone ${zone} (${ZONE_NAMES[zone - 1]}), RATE 0.5 = ${(time * 1000).toFixed(0)} ms, RPTS 0.5`;
  cases.push({
    name: `zone${zone}-burst`,
    about: `One plucked note through ${where}.`,
    input: 'a plucked A3 at 0.2 s',
    signal: pluck(220, 0.4, sec(0.2)),
    seconds,
    start: { zone, rate: 0.5, rpts: 0.5 },
  });
  cases.push({
    name: `zone${zone}-tone`,
    about: `A sustained chord through ${where}.`,
    input: 'an A major chord from 0.2 s for 1 s',
    signal: chord(0.35, sec(0.2), 1),
    seconds,
    start: { zone, rate: 0.5, rpts: 0.5 },
  });
}
for (const zone of [1, 2]) {
  cases.push({
    name: `zone${zone}-high-repeats`,
    about: `The short zones as a resonator: zone ${zone} with RPTS 0.72, RATE moved from 0.2 to 0.8 and back.`,
    input: 'the phrase, two bars',
    signal: phrase(sec(0.2), 2),
    seconds: 6,
    start: { zone, rate: 0.2, rpts: 0.72 },
    moves: [...Array.from({ length: 30 }, (_, k): [number, Partial<Controls>] => [1 + k * 0.05, { rate: 0.2 + (0.6 * (k + 1)) / 30 }]), ...Array.from({ length: 30 }, (_, k): [number, Partial<Controls>] => [3 + k * 0.05, { rate: 0.8 - (0.6 * (k + 1)) / 30 }])],
  });
}
const sweep = (from: number, to: number, start: number, seconds: number): [number, Partial<Controls>][] =>
  Array.from({ length: 60 }, (_, k) => [start + (k * seconds) / 60, { rate: from + ((to - from) * (k + 1)) / 60 }]);
for (const zone of [4, 5]) {
  cases.push({
    name: `rate-free-zone${zone}`,
    about: `A free RATE move in zone ${zone}: 0.3 to 0.7 from 2 s to 4 s, back from 5 s to 5.5 s, then a jump to 0.9 at 7 s. The pitch of the repeats bends while the time moves.`,
    input: 'the phrase, five bars',
    signal: phrase(sec(0.2), 5),
    seconds: 12,
    start: { zone, rate: 0.3, rpts: 0.55 },
    moves: [...sweep(0.3, 0.7, 2, 2), ...sweep(0.7, 0.3, 5, 0.5), [7, { rate: 0.9 }]],
  });
}
cases.push({
  name: 'rate-sync-steps',
  about: 'SYNC at 120 BPM, a new note length every 2 s: 1/8, 1/16, 1/8 dotted, 1/4, 1/8 triplet, 1/8. Each step is a crossfade: no bend.',
  input: 'the phrase, six bars',
  signal: phrase(sec(0.2), 6),
  seconds: 14,
  start: { sync: true, syncTime: 12000, rpts: 0.55 },
  moves: [
    [2, { syncTime: 6000 }],
    [4, { syncTime: 18000 }],
    [6, { syncTime: 24000 }],
    [8, { syncTime: 8000 }],
    [10, { syncTime: 12000 }],
  ],
});
cases.push({
  name: 'zone-changes',
  about: 'Zone changes while the phrase plays, every 2 s: zone 5, 4, 3, 6, 5 at RATE 0.4. Each is a crossfade to the new time: no bend.',
  input: 'the phrase, six bars',
  signal: phrase(sec(0.2), 6),
  seconds: 14,
  start: { zone: 5, rate: 0.4, rpts: 0.55 },
  moves: [
    [2, { zone: 4 }],
    [4, { zone: 3 }],
    [6, { zone: 6 }],
    [8, { zone: 5 }],
  ],
});
for (const rpts of [0, 0.25, 0.5, 0.75, 0.9, 1]) {
  cases.push({
    name: `rpts-${rpts.toFixed(2).replace('.', '')}`,
    about: `RPTS ${rpts}${rpts === 0.75 ? ' (unity: the repeats do not fall)' : rpts > 0.75 ? ' (above unity: the repeats grow into the limiter)' : rpts === 0 ? ' (one echo)' : ''}, a quarter note at 120 BPM.`,
    input: 'one bar of the phrase, then nothing',
    signal: phrase(sec(0.2), 1),
    seconds: 12,
    start: { sync: true, syncTime: 24000, rpts },
  });
}
cases.push({
  name: 'rpts-100-tone',
  about: 'RPTS 1 with a sustained chord of 4 s, then 6 s without input: what the limiter leaves of a tone at the top of RPTS.',
  input: 'an A major chord from 0.2 s for 4 s',
  signal: chord(0.35, sec(0.2), 4),
  seconds: 10,
  start: { sync: true, syncTime: 12000, rpts: 1 },
});
for (const colr of [-1, -0.6, -0.3, 0, 0.3, 0.6, 1]) {
  cases.push({
    name: `colr-${colr < 0 ? 'dark' : colr > 0 ? 'bright' : 'centre'}${colr ? `-${Math.abs(colr * 100).toFixed(0)}` : ''}`,
    about: `COLR ${colr}, RPTS 0.7, a quarter note at 120 BPM.`,
    input: 'one bar of the phrase, then nothing',
    signal: phrase(sec(0.2), 1),
    seconds: 10,
    start: { sync: true, syncTime: 24000, rpts: 0.7, colr },
  });
}
for (const halo of [0, 0.25, 0.5, 0.75, 1]) {
  cases.push({
    name: `halo-${(halo * 100).toFixed(0).padStart(3, '0')}`,
    about: `HALO ${halo}, RPTS 0.7, a quarter note at 120 BPM.`,
    input: 'one bar of the phrase, then nothing',
    signal: phrase(sec(0.2), 1),
    seconds: 10,
    start: { sync: true, syncTime: 24000, rpts: 0.7, halo },
  });
}
cases.push({
  name: 'halo-100-unity',
  about: 'HALO 1 with RPTS 0.75 (unity): the repeats turn into a wash that does not fall.',
  input: 'one bar of the phrase, then nothing',
  signal: phrase(sec(0.2), 1),
  seconds: 16,
  start: { sync: true, syncTime: 24000, rpts: 0.75, halo: 1 },
});
cases.push({
  name: 'halo-steps',
  about: 'HALO turned up while the phrase plays: 0, then 0.25 at 2 s, 0.5 at 4 s, 0.75 at 6 s, 1 at 8 s, 0 at 10 s. RPTS 0.6, an eighth note.',
  input: 'the phrase, six bars',
  signal: phrase(sec(0.2), 6),
  seconds: 14,
  start: { sync: true, syncTime: 12000, rpts: 0.6 },
  moves: [
    [2, { halo: 0.25 }],
    [4, { halo: 0.5 }],
    [6, { halo: 0.75 }],
    [8, { halo: 1 }],
    [10, { halo: 0 }],
  ],
});
for (const skew of [-1, -0.5, 0.5, 1]) {
  cases.push({
    name: `skew-${skew < 0 ? 'left-longer' : 'right-longer'}-${Math.abs(skew * 100).toFixed(0)}`,
    about: `SKEW ${skew} at a quarter note (24000 frames): the two sides at ${skew > 0 ? 'time / ' : 'time * '}${(1 + Math.abs(skew) / 2).toFixed(2)} (left) and ${skew > 0 ? 'time * ' : 'time / '}${(1 + Math.abs(skew) / 2).toFixed(2)} (right). RPTS 0.6.`,
    input: 'one bar of the phrase, then nothing',
    signal: phrase(sec(0.2), 1),
    seconds: 10,
    start: { sync: true, syncTime: 24000, rpts: 0.6, skew },
  });
}
cases.push({
  name: 'skew-move',
  about: 'SKEW turned while the phrase plays: 0 to 1 from 2 s to 4 s, to -1 from 5 s to 8 s, to 0 at 9 s. The two sides bend against each other. RPTS 0.55, an eighth note.',
  input: 'the phrase, six bars',
  signal: phrase(sec(0.2), 6),
  seconds: 14,
  start: { sync: true, syncTime: 12000, rpts: 0.55 },
  moves: [
    ...Array.from({ length: 40 }, (_, k): [number, Partial<Controls>] => [2 + k * 0.05, { skew: (k + 1) / 40 }]),
    ...Array.from({ length: 60 }, (_, k): [number, Partial<Controls>] => [5 + k * 0.05, { skew: 1 - (2 * (k + 1)) / 60 }]),
    [9, { skew: 0 }],
  ],
});
for (const rpts of [0, 0.6]) {
  cases.push({
    name: `flip-rpts-${(rpts * 100).toFixed(0).padStart(2, '0')}`,
    about: `FLIP on, RPTS ${rpts}, a quarter note at 120 BPM: each stretch of the time plays backwards${rpts ? '; a repeat of a repeat is forwards again' : ''}.`,
    input: 'two bars of the phrase, then nothing',
    signal: phrase(sec(0.2), 2),
    seconds: 10,
    start: { sync: true, syncTime: 24000, rpts, flip: true },
  });
}
cases.push({
  name: 'flip-toggle',
  about: 'FLIP turned on at 2 s, off at 4 s, on at 6 s, off at 8 s while the phrase plays. RPTS 0.5, a quarter note.',
  input: 'the phrase, five bars',
  signal: phrase(sec(0.2), 5),
  seconds: 12,
  start: { sync: true, syncTime: 24000, rpts: 0.5 },
  moves: [
    [2, { flip: true }],
    [4, { flip: false }],
    [6, { flip: true }],
    [8, { flip: false }],
  ],
});
cases.push({
  name: 'hold-moves',
  about:
    'HOLD taken at 4 s with the time at one bar (96000 frames) and released at 22 s; the phrase goes on all the time and is not recorded while held. Held: 1/4 note at 8 s, 1/8 at 10 s, 1/16 at 12 s, a bar again at 14 s, free zone 3 RATE 0.5 at 16 s, FLIP on at 18 s and off at 20 s with the bar again. RPTS 0.5.',
  input: 'the phrase, thirteen bars',
  signal: phrase(sec(0.2), 13),
  seconds: 28,
  start: { sync: true, syncTime: 96000, rpts: 0.5 },
  moves: [
    [4, { hold: true }],
    [8, { syncTime: 24000 }],
    [10, { syncTime: 12000 }],
    [12, { syncTime: 6000 }],
    [14, { syncTime: 96000 }],
    [16, { sync: false, zone: 3, rate: 0.5 }],
    [18, { flip: true, sync: true, syncTime: 96000 }],
    [20, { flip: false }],
    [22, { hold: false }],
  ],
});
cases.push({
  name: 'hold-colour-halo',
  about: 'HOLD taken at 3 s (a half note, 48000 frames), then COLR and HALO on the held loop: COLR -1 at 6 s, 1 at 9 s, 0 at 12 s, HALO 1 at 14 s, 0 at 18 s. The loop itself does not change: at 20 s it is as at 4 s.',
  input: 'two bars of the phrase, then nothing',
  signal: phrase(sec(0.2), 2),
  seconds: 24,
  start: { sync: true, syncTime: 48000, rpts: 0.5 },
  moves: [
    [3, { hold: true }],
    [6, { colr: -1 }],
    [9, { colr: 1 }],
    [12, { colr: 0 }],
    [14, { halo: 1 }],
    [18, { halo: 0 }],
  ],
});

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

const show = (c: Partial<Controls>): string =>
  Object.entries(c)
    .map(([k, v]) => `${k} ${typeof v === 'number' ? Number(v.toFixed(3)) : v}`)
    .join(', ');

async function render(c: Case, outdir: string): Promise<string> {
  const rig = await PrismRig.open({ frames: DEVICE_FRAMES, poison: false, friendly: true });
  const blocks = Math.ceil((c.seconds * FS) / BLOCK);
  const left = new Float32Array(blocks * BLOCK);
  const right = new Float32Array(blocks * BLOCK);
  const mixLeft = new Float32Array(blocks * BLOCK);
  const mixRight = new Float32Array(blocks * BLOCK);
  const moves = [...(c.moves ?? [])].sort((a, b) => a[0] - b[0]);
  rig.set({ ...DEFAULTS, mix: 1, ...c.start });
  let done = 0;
  let worst = 0;
  while (done < blocks) {
    while (moves.length && sec(moves[0]![0]) <= done * BLOCK) rig.set(moves.shift()![1]);
    const until = moves.length ? Math.min(blocks, Math.ceil(sec(moves[0]![0]) / BLOCK)) : blocks;
    const take = await rig.run(Math.max(1, until - done), c.signal);
    left.set(take.left, done * BLOCK);
    right.set(take.right, done * BLOCK);
    for (let n = 0; n < take.left.length; n++) {
      mixLeft[done * BLOCK + n] = 0.5 * (take.dryLeft[n]! + take.left[n]!);
      mixRight[done * BLOCK + n] = 0.5 * (take.dryRight[n]! + take.right[n]!);
    }
    worst = Math.max(worst, ...take.instructions);
    done += take.left.length / BLOCK;
  }
  await rig.close();
  writeFileSync(path.join(outdir, `${c.name}.wav`), wav(mixLeft, mixRight));
  for (let n = 0; n < left.length; n++) {
    left[n] = left[n]! * 0.5;
    right[n] = right[n]! * 0.5;
  }
  writeFileSync(path.join(outdir, 'wet', `${c.name}.wav`), wav(left, right));
  const changes = (c.moves ?? []).length > 12 ? `${c.moves!.length} changes (see the description)` : (c.moves ?? []).map(([t, m]) => `${t} s: ${show(m)}`).join('; ');
  return [
    `${c.name}.wav  (${c.seconds.toFixed(1)} s)`,
    `    ${c.about}`,
    `    input: ${c.input}`,
    `    controls: ${show({ ...DEFAULTS, mix: 1, ...c.start })}`,
    ...(changes ? [`    changes: ${changes}`] : []),
    `    wet peak ${(2 * Math.max(peak(left), peak(right))).toFixed(3)} of full scale before the halving; heaviest block ${worst} instructions`,
  ].join('\n');
}

const [outdir, ...names] = process.argv.slice(2);
if (!outdir) throw new Error('usage: node modules/fx/prism/tests/render-audio.ts OUTDIR [name ...]');
mkdirSync(path.join(outdir, 'wet'), { recursive: true });
const chosen = names.length ? cases.filter((c) => names.includes(c.name)) : cases;
const jobs = Math.max(1, Number(process.env.DIGI_JOBS ?? 4));
const entries = new Array<string>(chosen.length);
let next = 0;
await Promise.all(
  Array.from({ length: jobs }, async () => {
    while (next < chosen.length) {
      const k = next++;
      const started = Date.now();
      entries[k] = await render(chosen[k]!, outdir);
      console.log(`${chosen[k]!.name}: ${chosen[k]!.seconds.toFixed(1)} s of audio in ${((Date.now() - started) / 1000).toFixed(0)} s`);
    }
  }),
);
if (!names.length)
  writeFileSync(
    path.join(outdir, 'README.txt'),
    [
      "PRISM repeater engine: renders for listening (docs/findings/358-prism-repeater-engine.md).",
      '',
      'Made by modules/fx/prism/tests/render-audio.ts: the assembled engine on the SHARC-only rig behind the effect adapter,',
      'with a bank of 1,032,192 frames as the device leases it. Not a recording of the device.',
      '',
      '<name>.wav      the input plus the wet signal (as a send effect returns it), each at half level',
      'wet/<name>.wav  the wet signal alone, at half level',
      '48 kHz, 16 bit, stereo. The input is the same on both sides.',
      '',
      'Inputs. "The phrase": plucked eighth notes at 120 BPM, a bar of A3 - C#4 E4 - A4 E4 - (a dash is a rest), from 0.2 s.',
      'A plucked note is a tone with its octave, 10 ms of noise at the attack and a decay of 120 ms.',
      '',
      `Controls not named in a line are: ${show({ ...DEFAULTS, mix: 1 })}. zone 1..8, rate 0..1 inside the zone, syncTime in`,
      'frames (24000 is a quarter note at 120 BPM), rpts 0..1 (0.75 is unity), colr -1 dark .. 1 bright, halo 0..1, skew -1..1.',
      '',
      ...entries.flatMap((e) => [e, '']),
    ].join('\n'),
  );
