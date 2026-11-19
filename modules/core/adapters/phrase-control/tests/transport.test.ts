import { execFileSync } from 'node:child_process';
import { beforeAll, expect, test } from 'vitest';

type Op = Record<string, unknown>;
type Request = { transport: true; ops: Op[] };
type Call = { returned: boolean; ctx_bad: string[]; regs_bad: string[]; regs: Record<string, number> };
type Result = { observed: Record<string, number[] | Call>; symbols: Record<string, number> };
const requests: Record<string, Request> = {};
let results: Record<string, Result>;
const rx = 0x20080000,
  record = 0x20082000;
const input = rx + 0x8b4;
const sentinel = 0xdeadbeef;
const carrier = [0x5f4, 0x5f8, 0x648, 0x64c, 0x69c, 0x6a0, 0x6f0, 0x6f4, 0x9e4, 0x9e8, 0xa38, 0xa3c, 0xa8c, 0xa90];
const legacy = carrier.map((_, i) => (i === 0 ? 0x4f564c32 : 0x12000000 + i));
const put = (at: number | string, word: number, offset = 0): Op => ({ op: 'poke', at, word, offset });
const read = (tag: string, at: number | string, count: number): Op => ({ op: 'read', tag, at, count });
const putWords = (at: number | string, words: number[]): Op[] => words.map((word, i) => put(at, word, 4 * i));
const checksum = (words: number[]) => words.slice(0, 63).reduce((check, value) => (check ^ value) >>> 0, 0x73509c21);
function frame(sequence = 1): number[] {
  const data = Array.from({ length: 64 }, (_, i) => (0x13000000 + i * 997) >>> 0);
  data[0] = 0x50484331;
  data[1] = 3; // PH_VERSION
  data[2] = 256;
  data[3] = sequence;
  data[4] = 1;
  data[62] = sequence;
  data[63] = checksum(data);
  return data;
}
const receive = (tag: string): Op => ({
  op: 'call',
  tag,
  entry: '_test_receive',
  regs: { I3: rx },
  // The real position hook pins L0/L1 and clears SIMD/ALUSAT; unrelated DAG
  // contexts remain hostile. MODIFY(...)(NW) must not depend on M5/M6.
  ctx: { L0: 0, L1: 0, L2: 12, L3: 24, L4: 16, L5: 32, M5: 9, M6: 7, MODE1: 0x1011800 },
});
const reply = (tag: string): Op => ({
  op: 'call',
  tag,
  entry: '_test_reply',
  regs: { I4: record },
  // dsp-link guarantees only L2/L3=0, M5=0, M6=1. Preserve the live I4/R1/R2
  // and all inherited length context, especially the previously unpinned L5.
  ctx: { L0: 12, L1: 16, L2: 0, L3: 0, L4: 24, L5: 4, MODE1: 0x1011800 },
});

requests.copy = {
  transport: true,
  ops: [
    ...putWords(input, frame()),
    receive('call'),
    read('copied', '_phrase_control_info', 64),
    read('sequence', '_ph_last_sequence', 1),
  ],
};
const invalids: Record<string, number[]> = {};
for (const name of ['magic', 'version', 'size', 'closing', 'checksum', 'duplicate', 'backward']) {
  const packet = frame(name === 'duplicate' ? 1 : name === 'backward' ? 0 : 2);
  if (name === 'magic') packet[0] = 0;
  if (name === 'version') packet[1] = 2;
  if (name === 'size') packet[2] = 252;
  if (name === 'closing') packet[62] = 3;
  packet[63] = checksum(packet);
  if (name === 'checksum') packet[63] = (packet[63]! ^ 1) >>> 0;
  invalids[name] = packet;
  requests[name] = {
    transport: true,
    ops: [
      ...putWords(input, frame()),
      receive('first'),
      ...putWords(input, packet),
      receive('reject'),
      read('copied', '_phrase_control_info', 64),
      read('sequence', '_ph_last_sequence', 1),
      ...putWords(input, frame(3)),
      receive('recover'),
      read('recovered', '_phrase_control_info', 64),
    ],
  };
}
requests.wrap = {
  transport: true,
  ops: [
    put('_ph_last_sequence', 0xfffffffe),
    ...putWords(input, frame()),
    receive('call'),
    read('copied', '_phrase_control_info', 64),
  ],
};
requests.halfRange = {
  transport: true,
  ops: [
    ...putWords(input, frame(0x80000000)),
    receive('call'),
    read('copied', '_phrase_control_info', 64),
    read('sequence', '_ph_last_sequence', 1),
  ],
};

const statuses = Array.from({ length: 5 }, (_, n) =>
  Array.from({ length: 32 }, (_, i) => 0x20000000 + n * 0x10000 + i * 43),
);
function recordSeed(): Op[] {
  return [
    { op: 'fill', at: record, word: sentinel, count: 704 },
    ...carrier.map((offset, i) => put(record, legacy[i]!, offset)),
  ];
}
const replies: Op[] = [put('_phrase_control_info', 1, 16)];
for (let index = 0; index < 5; index++) {
  replies.push(
    ...recordSeed(),
    ...putWords('_phrase_status', statuses[index]!),
    reply(`call${index}`),
    read(`record${index}`, record, 704),
  );
}
requests.pages = {
  transport: true,
  ops: [...replies, read('snapshot', '_ph_reply_snapshot', 36), read('serial', '_ph_reply_serial', 1)],
};
requests.disabled = {
  transport: true,
  ops: [...recordSeed(), put('_phrase_control_info', 0, 16), reply('call'), read('record', record, 704)],
};

beforeAll(() => {
  results = JSON.parse(
    execFileSync('uv', ['run', 'python', '-m', 'tools.patchkit.pk_phrase_transport_probe'], {
      input: JSON.stringify(requests),
      encoding: 'utf8',
      maxBuffer: 4 * 1024 * 1024,
      timeout: 120_000,
    }),
  ) as Record<string, Result>;
}, 130_000);
function words(name: string, tag: string): number[] {
  return results[name]!.observed[tag] as number[];
}

test('receiver copies all64 words with their original offsets and accepts a fresh sequence', () => {
  expect(words('copy', 'copied')).toEqual(frame());
  expect(words('copy', 'sequence')).toEqual([1]);
});
test.each(Object.keys(invalids))('receiver rejects %s and fails open before accepting a later valid frame', (name) => {
  const expected = frame();
  expected[0] = 0;
  expected[4] = 0;
  expect(words(name, 'copied')).toEqual(expected);
  expect(words(name, 'sequence')).toEqual([1]);
  expect(words(name, 'recovered')).toEqual(frame(3));
});
test('sequence ordering accepts forward wrap and rejects ambiguous half-range jumps', () => {
  expect(words('wrap', 'copied')).toEqual(frame());
  expect(words('halfRange', 'copied')).toEqual(Array<number>(64).fill(0));
  expect(words('halfRange', 'sequence')).toEqual([0]);
});
test('reply publishes three coherent snapshot pages then preserves the complete legacy fourth frame', () => {
  for (let phase = 0; phase < 5; phase++) {
    const actual = words('pages', `record${phase}`);
    const packet = carrier.map((offset) => actual[offset / 4]!);
    if (phase === 3) expect(packet).toEqual(legacy);
    else {
      const page = phase === 4 ? 0 : phase;
      const snapshot = phase === 4 ? statuses[4]! : statuses[0]!;
      const padded = [...snapshot, 0, 0, 0, 0];
      expect(packet).toEqual([
        0x50485231,
        (phase === 4 ? 2 : 1) * 4 + page,
        ...padded.slice(page * 12, page * 12 + 12),
      ]);
    }
    const carrierWords = new Set(carrier.map((offset) => offset / 4));
    actual.forEach((value, index) => {
      if (!carrierWords.has(index)) expect(value, `frame${phase} word${index}`).toBe(sentinel);
    });
  }
  expect(words('pages', 'snapshot')).toEqual([...statuses[4]!, 0, 0, 0, 0]);
  expect(words('pages', 'serial')).toEqual([2]);
});
test('disabled phrase status leaves every native/legacy reply word untouched', () => {
  const actual = words('disabled', 'record');
  const expected = Array<number>(704).fill(sentinel);
  carrier.forEach((offset, index) => {
    expected[offset / 4] = legacy[index]!;
  });
  expect(actual).toEqual(expected);
});
test('transport calls preserve inherited context and reply preserves live I4/R1/R2', () => {
  for (const [name, result] of Object.entries(results))
    for (const [tag, observation] of Object.entries(result.observed)) {
      if (Array.isArray(observation)) continue;
      expect(observation.returned, `${name}/${tag}`).toBe(true);
      expect(observation.ctx_bad, `${name}/${tag}`).toEqual([]);
      expect(observation.regs_bad, `${name}/${tag}`).not.toContain('I4');
      if (name === 'pages' || name === 'disabled') {
        expect(observation.regs.R1).toBe(0x7001);
        expect(observation.regs.R2).toBe(0x7002);
      }
    }
});
