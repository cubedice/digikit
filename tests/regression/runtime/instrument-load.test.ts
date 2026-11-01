import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('bound instrument uploads, initializes and retires before memory release', async () => {
  const root = repoRoot();
  const image = path.join(root, 'out/patch/canopy-live-load.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.modules).toContain('test/instrument-load@0.1.0');
  const sha = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(sha);
  const { session } = h;
  const loaded = await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  expect(loaded.syx_sha256).toBe(sha);
  await session.client.call('run.mode', { mode: 'free' });
  const write = async (addr: number, bytes: Buffer) =>
    session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  const writeWord = async (addr: number, value: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await write(addr, bytes);
  };
  const binding = planSymbol(plan, 'DKRT_INSTRUMENT_BINDING');
  await expect.poll(() => session.client.memPeek(binding + 8), { timeout: 30_000 }).toBe(1);
  await writeWord(planSymbol(plan, 'isp_request'), 0x12345678);
  await expect
    .poll(() => session.client.memPeek(planSymbol(plan, 'DKRT_INSTRUMENT_SESSION')), { timeout: 30_000 })
    .toBe(4);

  // Use assembler-produced, relocation-free callback bytes. Never guess opcode encodings.
  const object = JSON.parse(
    fs.readFileSync(path.join(root, 'out/cache/dko/test/instrument-load/build/probe-dsp.dko.json'), 'utf8'),
  );
  expect(object.relocs).toEqual([]);
  const text = object.sections.find((s: { name: string }) => s.name === 'seg_pmco');
  const code = Buffer.from(text.hex, 'hex');
  expect(code.length).toBeLessThanOrEqual(32);
  const section = Buffer.alloc(192); // 112-byte header/tables/padding + 80-byte image
  const fields: [number, number][] = [
    [0, 0x444b4453],
    [4, 6],
    [8, 1],
    [12, 80],
    [20, 16],
    [24, 112],
    [28, 80],
    [36, 80],
    [44, 80],
    [52, 80],
    [64, 80],
    [68, 1],
    [72, 96],
    [76, 1],
    [80, 20],
    [84, 0],
    [88, 0],
    [92, 32],
    [96, 0],
    [100, code.length],
  ];
  for (const [at, value] of fields) section.writeUInt32BE(value, at);
  code.copy(section, 112);
  [0x444b4954, 2, 48, 0, 1, 8, 8, 1, 20, 20, 2, 32].forEach((value, i) => section.writeUInt32LE(value, 144 + 4 * i));
  const input = planSymbol(plan, 'ilp_input');
  await write(input + 12, section);
  await writeWord(input + 4, section.length);
  await writeWord(input, 1);
  const status = planSymbol(plan, 'DKRT_INSTRUMENT_LOAD');
  const word = (n: number) => session.client.memPeek(status + 4 * n);
  const waitPhase = async (phase: number) =>
    expect
      .poll(
        async () => {
          await session.expectNoFault('instrument lifecycle');
          const state = await word(0);
          expect(state, 'probe must not fail closed').not.toBe(9);
          return state;
        },
        { timeout: 30_000, interval: 100 },
      )
      .toBe(phase);
  try {
    await waitPhase(3);
  } catch (error) {
    const words = [];
    for (let i = 0; i < 6; i++) words.push(await word(i));
    const registry: Record<string, unknown> = {};
    for (const name of ['_instrument_registry_reply', '_dl_irows', '_ir_rows', '_ir_snapshot']) {
      registry[name] = await session.client.call('dsp.peek', { addr: planSymbol(plan, name), len: 64 });
    }
    console.error('load diagnostics', { words, registry, status: await session.status() });

    throw error;
  }
  expect(await word(4)).toBe(1);
  expect(await word(5)).toBe(128);
  const base = (await word(1))!;
  const state = (await word(2))!;
  expect(state).toBe(base + 80);
  const peek = async (addr: number, len: number) => {
    const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(result.unknown).toBe(0);
    return Buffer.from(result.hex, 'hex');
  };
  expect(await peek(base, 80)).toEqual(section.subarray(112));
  expect((await peek(state, 4)).readUInt32LE()).toBe(1); // CREATE callback executed from uploaded code
  await writeWord(input, 2);
  await waitPhase(6);
  expect(await word(4)).toBe(0);
  expect(await word(5)).toBe(0);
  expect((await peek(state, 4)).readUInt32LE()).toBe(4); // DETACH callback completed before release
  expect((await peek(planSymbol(plan, '_ir_rows'), 4)).readUInt32LE()).toBe(6);
  expect(await session.client.memPeek(planSymbol(plan, 'dl_link') + 76)).toBe(0);
  await session.expectNoFault('completed instrument lifecycle');
}, 150_000);
