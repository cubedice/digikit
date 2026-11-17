import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { beforeAll, expect, test } from 'vitest';

// Vitest owns scenarios and expectations; the bounded analysis driver only
// assembles our real adapter and executes the concrete SHARC core.
type Address = number | string;
type Op = Record<string, unknown>;
type Request = {
  ops: Op[];
  native?: boolean;
  breaks?: number[];
  hooks?: [number, string][];
  externs?: Record<string, number>;
};
type Call = {
  returned?: boolean;
  instructions?: number;
  ctx_bad?: string[];
  regs_bad?: string[];
  regs: Record<string, number>;
  pc?: number;
  halt: { reason: string };
};
type Result = { observed: Record<string, number[] | Call>; symbols: Record<string, number> };
const requests: Record<string, Request> = {};
let results: Record<string, Result>;
const trackbus = 0x2009f000;
const one = 0x3f800000;
const word = (value: number) => new Uint32Array(new Float32Array([value]).buffer)[0]!;
const float = (value: number) => new Float32Array(new Uint32Array([value]).buffer)[0]!;
const put = (at: Address, value: number | string, offset = 0): Op => ({ op: 'poke', at, word: value, offset });
const fill = (at: Address, value: number, count = 64, offset = 0): Op => ({
  op: 'fill',
  at,
  float: value,
  count,
  offset,
});
const read = (tag: string, at: Address, count: number, offset = 0): Op => ({ op: 'read', tag, at, count, offset });
const pre = (tag: string): Op => ({ op: 'call', tag, entry: '_phr_pre', regs: { R8: '_test_ext_pointers' } });
const track = (index: number, value: number): Op => fill(trackbus + index * 256, value);

function init(mask = 1, fade = 1, held = 1, old = 0, handoff = 1): Op[] {
  return [
    ...[
      [0, 0x50484331],
      [4, 1],
      [8, 256],
      [16, 1],
      [44, mask],
      [80, word(fade)],
      [220, 0],
      [224, 17],
    ].map(([offset, value]) => put('_phrase_control_info', value!, offset!)),
    fill('_phrase_control_info', 1, 18, 148),
    ...[
      [20, held],
      [24, old],
      [28, word(handoff)],
      [32, 1024],
    ].map(([offset, value]) => put('_phrase_status', value!, offset!)),
    put('_test_ext_pointers', '_test_ext_l'),
    put('_test_ext_pointers', '_test_ext_r', 4),
    fill('_test_ext_l', 0.125, 32),
    fill('_test_ext_r', 0.25, 32),
  ];
}

const snapshots = [
  read('input', '_phr_input', 64),
  read('mirror', '_phr_tracks', 1024),
  read('original', trackbus, 1024),
  read('wet', '_phr_wet', 64),
  read('external', '_phr_external', 64),
  read('rawL', '_test_ext_l', 32),
  read('rawR', '_test_ext_r', 32),
  read('actualFade', '_phrase_status', 1, 92),
  read('routeFault', '_phrase_status', 1, 120),
  read('externalPointers', '_phr_ext_pointers', 2),
];
function scenario(name: string, ops: Op[]) {
  requests[name] = { ops: [...ops, ...snapshots] };
}
function words(name: string, tag: string): number[] {
  return results[name]!.observed[tag] as number[];
}
function samples(name: string, tag: string): number[] {
  return words(name, tag).map(float);
}
function near(values: number[], value: number, tolerance = 4e-6) {
  expect(values.length).toBeGreaterThan(0);
  expect(
    values.every((sample) => Math.abs(sample - value) <= tolerance),
    `all samples near ${value}: ${values.slice(0, 4)}`,
  ).toBe(true);
}

scenario('full', [
  ...init(),
  track(0, 0.25),
  track(1, 0.5),
  pre('pre0'),
  pre('pre1'),
  fill('_fxs_return', 0.125),
  { op: 'call', tag: 'post', entry: '_phr_master' },
  read('return', '_fxs_return', 64),
]);
for (const fade of [0, 0.1, 0.5, 0.9, 1])
  scenario(`fade${fade}`, [...init(1, fade), track(0, 1), track(1, 0.75), pre('pre0'), pre('pre1')]);
scenario('pending', [...init(2), track(0, 0.25), track(1, 0.5), pre('pre0'), pre('pre1')]);
for (const mix of [0, 0.25, 0.5, 1])
  scenario(`handoff${mix}`, [
    ...init(6, 1, 6, 3, mix),
    ...[0, 1, 2, 3].map((i) => track(i, 1)),
    pre('pre0'),
    pre('pre1'),
  ]);
for (const mode of [1, 2, 3, 4])
  scenario(`fault${mode}`, [
    ...init(),
    track(0, 0.25),
    pre('pre0'),
    pre('pre1'),
    put('_test_mode', mode),
    pre('pre2'),
    pre('pre3'),
  ]);
for (const [change, op] of Object.entries({
  disabled: put('_phrase_control_info', 0, 16),
  invalid: put('_phrase_control_info', 0),
  unloaded: put('_fx_table', 0xffffffff),
  empty: put('_phrase_status', 0, 32),
}))
  scenario(change, [...init(), track(0, 0.25), pre('pre0'), pre('pre1'), op, pre('pre2'), pre('pre3')]);
scenario('stereo', [...init(0x30000, 1, 0x10000), pre('pre0'), pre('pre1')]);
for (const mask of [0x10000, 0x20000]) scenario(`mono${mask}`, [...init(mask, 1, mask), pre('pre0'), pre('pre1')]);
scenario('gains', [
  ...init(3, 1, 2),
  track(0, 0.25),
  track(1, 0.5),
  put('_phrase_control_info', 0x7fc00001, 148),
  put('_phrase_control_info', word(0.25), 152),
  pre('pre0'),
  pre('pre1'),
]);
scenario('exclude', [
  ...init(0x30001, 1, 0),
  track(0, 0.25),
  put('_phr_main_exclude', 0x30001),
  pre('pre0'),
  pre('pre1'),
]);
scenario('first', [
  ...init(),
  put('_phrase_control_info', 0, 16),
  ...Array.from({ length: 16 }, (_, i) => track(i, (i - 8) / 64)),
  pre('pre0'),
]);
scenario('rateOff', [...init(), track(0, 0.25), put('_test_mode', 5), pre('pre0'), pre('pre1')]);
scenario('all', [
  ...init(0x3ffff, 1, 0x3ffff),
  ...Array.from({ length: 16 }, (_, i) => track(i, 0.001)),
  pre('pre0'),
  pre('pre1'),
]);
for (const recovery of ['none', 'command', 'fade']) {
  const ops = [
    ...init(),
    track(0, 0.25),
    pre('pre0'),
    pre('pre1'),
    put('_test_mode', 1),
    pre('pre2'),
    put('_test_mode', 0),
    pre('pre3'),
    pre('pre4'),
    read('beforeRecovery', '_phr_tracks', 64),
  ];
  if (recovery === 'command') ops.push(put('_phrase_control_info', 1, 20));
  if (recovery === 'fade')
    ops.push(put('_phrase_control_info', 0, 80), pre('pre5'), put('_phrase_control_info', one, 80));
  scenario(`recover${recovery}`, [...ops, pre('pre6'), pre('pre7')]);
}
scenario('faultNewOwner', [
  ...init(),
  track(0, 0.25),
  pre('pre0'),
  put('_test_mode', 1),
  pre('pre1'),
  put('_phrase_control_info', 18, 224),
  put('_phrase_control_info', 0, 16),
  pre('pre2'),
]);
requests.pointers = {
  ops: [
    { op: 'call', tag: 'first', entry: '_phr_first' },
    ...[1, 7, 15].map((i) => ({ op: 'call', tag: `track${i}`, entry: '_phr_track_base', regs: { I0: i * 256 } })),
  ],
};

// Execute original firmware mixer instructions with exactly the production
// reader hooks overlaid. These are real boundaries, not restated DSP formulas.
for (const hooked of [false, true]) {
  const ops: Op[] = [];
  for (let i = 0; i < 16; i++) {
    ops.push(fill(0x252df8 + i * 256, (i + 1) / 256), fill('_phr_tracks', i & 1 ? (i + 1) / 512 : 0, 64, i * 256));
    for (const [now, previous] of [
      [0x252540, 0x25549c],
      [0x2525cc, 0x25511c],
      [0x252658, 0x254d9c],
    ])
      ops.push(put(now! + 8 * i, one), put(previous! + 56 * i, one));
  }
  ops.push(
    { op: 'call', tag: 'native', entry: 0x1c18a6 },
    ...[0x253e78, 0x254178, 0x254478].map((bus) => read(`bus${bus}`, bus, 64)),
    read('original', 0x252df8, 1024),
  );
  requests[`nativeSend${hooked}`] = {
    native: true,
    breaks: [0x1c1a12],
    ops,
    externs: { _phr_first_back: 0x1c1909, _phr_tracks_back: 0x1c19d3 },
    hooks: hooked
      ? [
          [0x1c1906, '_phr_first'],
          [0x1c19d0, '_phr_track_base'],
        ]
      : [],
  };
}
for (const stereo of [0, 1])
  for (const privateInput of [false, true]) {
    const ops: Op[] = [
      fill(0x20080000, privateInput ? 0.125 : 0, 32),
      fill(0x20080100, 0.25, 32),
      fill(0x20080200, 0, 32),
      fill(0x20080300, 0.25, 32),
      put(0x20080400, privateInput ? 0x20080200 : 0x20080000),
      put(0x20080404, privateInput ? 0x20080300 : 0x20080100),
      fill(0x2526f0, 0.5, 10),
      put(0x252718, stereo),
      put(0x2567f8, stereo),
      fill(0x252df8, 0, 1024),
      { op: 'call', tag: 'native', entry: 0x1c18a6, regs: { R8: 0x20080400 } },
      read('originalL', 0x20080000, 32),
      ...[0x254b78, 0x254c78, 0x253e78, 0x254178, 0x254478].map((bus) => read(`bus${bus}`, bus, 64)),
    ];
    requests[`nativeExt${stereo}${privateInput}`] = { native: true, breaks: [0x1c1c9c], ops };
  }
requests.nativeMaster = {
  native: true,
  breaks: [0x1c212d],
  externs: { _fxs_trackbus: 0x252df8, _fxs_return: 0x254778, _phr_master_back: 0x1c20ed },
  hooks: [[0x1c20ea, '_phr_master']],
  ops: [
    put(0x252538, 0xa5a5),
    put(0x252728, 0x124),
    fill(0x252df8, 0.5, 1024),
    ...Array.from({ length: 16 }, (_, i) => fill('_phr_tracks', (i + 1) / 256, 64, i * 256)),
    { op: 'call', tag: 'native', entry: 0x1c20b9 },
    read('comp', 0x254878, 64),
    read('bypass', 0x254a78, 64),
    read('original', 0x252df8, 1024),
  ],
};

// The real USB writer builds its descriptors from the original pointer table,
// after PRISM has produced fully silent dry mirrors. Its packed outputs must
// retain the exact original track samples in both source-selection modes.
for (const mode of [0, 1])
  for (const routed of [false, true]) {
    const ops: Op[] = [
      ...init(0x3ffff, routed ? 1 : 0, 0x3ffff),
      fill('_fx_table', 0, 120),
      put('_fx_table', '_test_phrase'),
      put('_fx_table', '_te_desc', 4),
      put('_fx_table', '_te_state_a', 8),
      put('_fx_table', 1, 12),
      put('_fx_table', one, 52),
      put('_fx_table', 2, 72),
      fill(0x20080000, 0.015625, 64),
      fill(0x20082000, 0, 1024),
      fill(0x20085000, 0, 1024),
      put(0x2009c004, 0x20082000),
      put(0x2009c008, mode),
    ];
    for (let index = 0; index < 16; index++)
      ops.push(
        fill(0x252df8 + index * 256, (index + 1) / 128),
        put(0x252d78 + index * 8, 0x252df8 + index * 256),
        put(0x252d7c + index * 8, 0x252e78 + index * 256),
      );
    ops.push(
      pre('pre0'),
      pre('pre1'),
      {
        op: 'call',
        tag: 'native',
        entry: 0x1c28b5,
        regs: { R4: '_test_ext_pointers', R8: 0x20080000, R12: 0x20085000 },
        max_steps: 30_000,
      },
      read('interleaved', 0x20082000, 512),
      read('packed', 0x20085000, 672),
      read('original', 0x252df8, 1024),
      read('mirror', '_phr_tracks', 1024),
    );
    requests[`nativeUsb${mode}${routed}`] = {
      native: true,
      breaks: [0xb9f000, 0x1c29e0],
      ops,
      externs: { _fxs_trackbus: 0x252df8 },
    };
  }

beforeAll(() => {
  const output = execFileSync('uv', ['run', 'python', '-m', 'tools.patchkit.pk_phrase_route_probe'], {
    input: JSON.stringify(requests),
    encoding: 'utf8',
    maxBuffer: 8 * 1024 * 1024,
    timeout: 240_000,
  });
  results = JSON.parse(output) as Record<string, Result>;
  const instructions = Object.fromEntries(
    Object.entries(results)
      .filter(([name]) => !requests[name]!.native)
      .map(([name, result]) => [
        name,
        Object.fromEntries(
          Object.entries(result.observed)
            .filter(([, value]) => !Array.isArray(value))
            .map(([tag, value]) => [tag, (value as Call).instructions]),
        ),
      ]),
  );
  mkdirSync('out/runs/prism-route', { recursive: true });
  writeFileSync('out/runs/prism-route/instructions.json', `${JSON.stringify(instructions, null, 2)}\n`);
}, 250_000);

test('every adapter call returns and preserves the hostile native register context', () => {
  for (const [name, result] of Object.entries(results)) {
    if (requests[name]!.native) continue;
    for (const [tag, observation] of Object.entries(result.observed)) {
      if (Array.isArray(observation)) continue;
      expect(observation.returned, `${name}/${tag}`).toBe(true);
      expect(observation.ctx_bad, `${name}/${tag}`).toEqual([]);
      const changed = tag === 'post' ? 'I13' : name === 'pointers' ? (tag === 'first' ? 'I4' : 'I0') : 'R8';
      expect(
        observation.regs_bad!.filter((register) => register !== changed),
        `${name}/${tag}`,
      ).toEqual([]);
    }
  }
});
test('full FADE replaces held sources while capture and original USB buffers remain untouched', () => {
  near(samples('full', 'input'), 0.25, 0);
  near(samples('full', 'mirror').slice(0, 64), 0, 0);
  near(samples('full', 'mirror').slice(64, 128), 0.5, 0);
  near(samples('full', 'original').slice(0, 64), 0.25, 0);
  near(samples('full', 'original').slice(64, 128), 0.5, 0);
  near(samples('full', 'return'), 1.125, 0);
});
test.each([0, 0.1, 0.5, 0.9, 1])('MIX %s: the wet rises over the first half, the sources fall over the second', (fade) => {
  near(samples(`fade${fade}`, 'mirror').slice(0, 64), Math.sin((Math.PI * Math.min(1, 2 - 2 * fade)) / 2));
  near(samples(`fade${fade}`, 'wet'), Math.sin((Math.PI * Math.min(1, 2 * fade)) / 2));
  near(samples(`fade${fade}`, 'mirror').slice(64, 128), 0.75, 0);
});
test('pending source selection never takes over the held recording’s dry ownership', () => {
  near(samples('pending', 'input'), 0.5, 0);
  near(samples('pending', 'mirror').slice(0, 64), 0, 0);
  near(samples('pending', 'mirror').slice(64, 128), 0.5, 0);
});
test.each([0, 0.25, 0.5, 1])('handoff %s restores old-only sources and attenuates new-only sources', (mix) => {
  [mix, 0, 1 - mix, 1].forEach((gain, i) => near(samples(`handoff${mix}`, 'mirror').slice(i * 64, (i + 1) * 64), gain));
});
test('without a held phrase the effect stays on: the sources its tape records give way, the wet passes', () => {
  near(samples('empty', 'mirror').slice(0, 64), 0, 0);
  near(samples('empty', 'wet'), 1);
  expect(words('empty', 'actualFade')).toEqual([0x3f800000]);
  expect(words('empty', 'routeFault')).toEqual([0]);
});
test.each(['fault1', 'fault2', 'fault3', 'fault4', 'disabled', 'invalid', 'unloaded'])(
  '%s fails open to LIVE independently of the module',
  (name) => {
    near(samples(name, 'mirror').slice(0, 64), 0.25, 0);
    near(samples(name, 'wet'), 0, 0);
    expect(words(name, 'actualFade')).toEqual([0]);
    expect(words(name, 'routeFault')).toEqual([name.startsWith('fault') ? 1 : 0]);
  },
);
test('external capture keeps stereo before independent monitor attenuation', () => {
  near(samples('stereo', 'input').slice(0, 32), 0.125, 0);
  near(samples('stereo', 'input').slice(32), 0.25, 0);
  near(samples('stereo', 'external').slice(0, 32), 0, 0);
  near(samples('stereo', 'external').slice(32), 0.25, 0);
  near(samples('stereo', 'rawL'), 0.125, 0);
  near(samples('stereo', 'rawR'), 0.25, 0);
  expect(words('stereo', 'externalPointers')).toEqual([
    results.stereo!.symbols._phr_external!,
    results.stereo!.symbols._phr_external! + 128,
  ]);
});
test.each([
  [0x10000, 0.125],
  [0x20000, 0.25],
])('one external source %s captures centered mono', (mask, value) => {
  near(samples(`mono${mask}`, 'input'), value / Math.sqrt(2), 2e-8);
});
test('zero/invalid gain and native track exclusions do not leak into capture', () => {
  near(samples('gains', 'input'), 0.125, 0);
  near(samples('gains', 'mirror').slice(0, 64), 0.25, 0);
  near(samples('gains', 'mirror').slice(64, 128), 0, 0);
  near(samples('exclude', 'input').slice(0, 32), 0.125, 0);
  near(samples('exclude', 'input').slice(32), 0.25, 0);
});
test('the first block without an owner is exactly dry', () => {
  expect(words('first', 'mirror')).toEqual(words('first', 'original'));
  near(samples('first', 'wet'), 0, 0);
});
test('intentional RATE OFF silence keeps the valid held ownership', () => {
  near(samples('rateOff', 'wet'), 0, 0);
  near(samples('rateOff', 'mirror').slice(0, 64), 0, 0);
  expect(words('rateOff', 'actualFade')).toEqual([one]);
});
test('ALL combines sixteen tracks and stereo input while owning only their direct copies', () => {
  near(samples('all', 'input').slice(0, 32), 0.141, 2e-7);
  near(samples('all', 'input').slice(32), 0.266, 2e-7);
  near(samples('all', 'mirror'), 0, 0);
  near(samples('all', 'external'), 0, 0);
  near(samples('all', 'original'), 0.001, 1e-10);
});
test.each(['none', 'command', 'fade'])('a recovered DSP only re-ducks after explicit user action (%s)', (recovery) => {
  const name = `recover${recovery}`;
  near(samples(name, 'beforeRecovery'), 0.25, 0);
  near(samples(name, 'mirror').slice(0, 64), recovery === 'none' ? 0.25 : 0, 0);
  expect(words(name, 'actualFade')).toEqual([recovery === 'none' ? 0 : one]);
  expect(words(name, 'routeFault')).toEqual([recovery === 'none' ? 1 : 0]);
});
test('an unloaded old generation cannot publish a fault against a new owner', () => {
  expect(words('faultNewOwner', 'routeFault')).toEqual([0]);
  expect(words('faultNewOwner', 'actualFade')).toEqual([0]);
});
test('all native source pointer hooks resolve to the private mirror', () => {
  const result = results.pointers!;
  expect((result.observed.first as Call).regs.I4).toBe(result.symbols._phr_tracks);
  for (const i of [1, 7, 15])
    expect((result.observed[`track${i}`] as Call).regs.I0).toBe(result.symbols._phr_tracks! + i * 256);
  expect((results.full!.observed.post as Call).regs.I13).toBe(results.full!.symbols._phr_tracks);
});
test.each([false, true])('real native send mixer reads the intended source buses (hooks=%s)', (hooked) => {
  const name = `nativeSend${hooked}`;
  expect((results[name]!.observed.native as Call).pc).toBe(0x1c1a12);
  let expected = 0;
  for (let i = 0; i < 16; i++) {
    expected += hooked ? (i & 1 ? (i + 1) / 512 : 0) : (i + 1) / 256;
    near(samples(name, 'original').slice(i * 64, (i + 1) * 64), (i + 1) / 256, 0);
  }
  for (const bus of [0x253e78, 0x254178, 0x254478]) near(samples(name, `bus${bus}`), expected, 2e-7);
});
test.each([0, 1])('real external monitor/send path consumes the substituted pointer pair (mode=%s)', (mode) => {
  const control = `nativeExt${mode}false`,
    replacement = `nativeExt${mode}true`;
  expect((results[replacement]!.observed.native as Call).pc).toBe(0x1c1c9c);
  for (const bus of [0x254b78, 0x254c78, 0x253e78, 0x254178, 0x254478])
    expect(words(replacement, `bus${bus}`)).toEqual(words(control, `bus${bus}`));
  for (const bus of [0x253e78, 0x254178, 0x254478])
    expect(samples(replacement, `bus${bus}`).some((value) => value > 0)).toBe(true);
  near(samples(replacement, 'originalL'), 0.125, 0);
});
test('real native master keeps compressor routing and main exclusions', () => {
  expect((results.nativeMaster!.observed.native as Call).pc).toBe(0x1c212d);
  for (const [tag, comp] of [
    ['comp', true],
    ['bypass', false],
  ] as const) {
    let expected = 0;
    for (let i = 0; i < 16; i++)
      if (!(0x124 & (1 << i)) && Boolean(0xa5a5 & (1 << i)) === comp) expected += (i + 1) / 256;
    near(samples('nativeMaster', tag), expected, 0);
  }
  near(samples('nativeMaster', 'original'), 0.5, 0);
});
test.each([0, 1])('native USB packing retains original tracks at full selected FADE (mode=%s)', (mode) => {
  const baseline = `nativeUsb${mode}false`,
    faded = `nativeUsb${mode}true`;
  expect((results[baseline]!.observed.native as Call).pc).toBe(0x1c29e0);
  expect((results[faded]!.observed.native as Call).pc).toBe(0x1c29e0);
  for (const tag of ['interleaved', 'packed']) {
    expect(words(faded, tag)).toEqual(words(baseline, tag));
    expect(words(faded, tag).some((value) => value !== 0)).toBe(true);
  }
  near(samples(faded, 'mirror'), 0, 0);
  expect(words(faded, 'original')).toEqual(words(baseline, 'original'));
});
