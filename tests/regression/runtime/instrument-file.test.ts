import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions, liveProtocol, moduleArtifact } from './live.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });

test.each([false, true])(
  'normal V2 file completion (failing CF init: %s)',
  async (failInit) => {
    const root = repoRoot();
    const image = path.join(root, 'out/patch/canopy-normal-file.syx');
    const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
    expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
    const diagnostic = planSymbol(plan, 'DKRT_INSTRUMENT_FILE');
    const { session } = h;
    await session.client.firmwareLoad({
      syx: image,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    // Use assembler-produced, relocation-free callback bytes. Never guess opcode encodings.
    const object = JSON.parse(
      fs.readFileSync(path.join(root, 'out/cache/dko/test/instrument-load/build/probe-dsp.dko.json'), 'utf8'),
    );
    expect(object.relocs).toEqual([]);
    const text = object.sections.find((s: { name: string }) => s.name === 'seg_pmco');
    const code = Buffer.from(text.hex, 'hex');
    expect(code.length).toBeLessThanOrEqual(64);
    const section = Buffer.alloc(224); // 112-byte header/tables/padding + 112-byte image
    const fields: [number, number][] = [
      [0, 0x444b4453],
      [4, 6],
      [8, 1],
      [12, 112],
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
      [88, 16],
      [92, 64],
      [96, 0],
      [100, code.length],
    ];
    for (const [at, value] of fields) section.writeUInt32BE(value, at);
    code.copy(section, 112);
    [0x444b4954, 2, 48, 0, 1, 8, 8, 1, 20, 20, 2, 32].forEach((value, i) => section.writeUInt32LE(value, 176 + 4 * i));

    // A real checksummed DKM with an inert CF image, one pinned machine record,
    // and the diagnostic DKDS6. No debugger injection into loader state.
    const registry = Buffer.alloc(124);
    [0x444b5247, 1, 1, 1, 2, 108, 20, 1, 255].forEach((value, i) => registry.writeUInt32BE(value, i * 4));
    registry.write('V2 FILE', 24 + 52);
    registry.write('V2FILE', 24 + 76);
    const original = failInit ? fs.readFileSync(moduleArtifact('dt2', 'test/svc-init-fail')) : undefined;
    const registryAt = original ? (original.length + 15) & ~15 : 176;
    const dspAt = (registryAt + registry.length + 15) & ~15;
    const file = Buffer.alloc(dspAt + section.length);
    const fileFields: [number, number][] = [
      [0, 0x444b4d46],
      [4, 1],
      [8, file.length],
      [12, 160],
      [0x60, 160],
      [0x64, 16],
      [0x6c, 16],
      [0x70, 176],
      [0x78, 176],
      [0x80, 0xffffffff],
      [0x84, 0xffffffff],
      [0x88, 176],
      [0x90, 176],
      [0x94, registry.length],
      [0x98, 304],
      [0x9c, section.length],
    ];
    for (const [at, value] of fileFields) file.writeUInt32BE(value, at);
    if (original) original.copy(file);
    file.writeUInt32BE(file.length, 8);
    file.writeUInt32BE(registryAt, 0x90);
    file.writeUInt32BE(registry.length, 0x94);
    file.writeUInt32BE(dspAt, 0x98);
    file.writeUInt32BE(section.length, 0x9c);
    file.fill(0, 16, 48);
    file.fill(0, 0x40, 0x60);
    file[0x3a] = 2;
    file.write('test/v2-file', 0x40);
    registry.copy(file, registryAt);
    section.copy(file, dspAt);
    createHash('sha256').update(file).digest().copy(file, 16);
    await dk.put('modules/v2-file.dkm', file);
    expect(await dk.verifyFile('modules/v2-file.dkm', file)).toBe(true);
    await dk.select(['modules/v2-file.dkm']);
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);

    await expect
      .poll(async () => [3, 6, 9].includes((await session.client.memPeek(diagnostic)) ?? -1), { timeout: 60_000 })
      .toBe(true);
    const progress = await Promise.all([0, 1, 2, 3].map((i) => session.client.memPeek(diagnostic + 4 * i)));
    if (progress[0] === 9) {
      const diagnostics: Record<string, unknown> = { progress, modules: await dk.modules() };
      for (const [name, count] of [
        ['registry_guard', 8],
        ['mt_guard_fault', 5],
        ['reset_seen', 1],
        ['frame_block', 1],
      ] as const) {
        const at = planSymbol(plan, name);
        diagnostics[name] = await Promise.all(
          Array.from({ length: count }, (_, i) => session.client.memPeek(at + i * 4)),
        );
      }
      diagnostics.dsp = await session.client.call('dsp.peek', { addr: planSymbol(plan, '_mi_heartbeat'), len: 4 });
      throw new Error(JSON.stringify(diagnostics));
    }
    expect(progress.slice(0, 3), `load progress ${JSON.stringify(progress)}`).toEqual([
      failInit ? 6 : 3,
      0,
      failInit ? 0xfffffff7 : 0,
    ]);
    expect(await session.client.memPeek(diagnostic + 8)).toBe(failInit ? 0xfffffff7 : 0);
    if (failInit) {
      expect((await dk.modules()).map((m) => m.name)).not.toContain('test/v2-file');
      expect((await dk.request(0x3e)).status).toBe(-60);
    } else {
      expect((await dk.modules()).map((m) => m.name)).toContain('test/v2-file');
    }
    const peek = async (addr: number, len: number) => {
      const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
      expect(result.unknown).toBe(0);
      return Buffer.from(result.hex, 'hex');
    };
    const row = await peek(planSymbol(plan, '_ir_rows'), 64);
    if (failInit) {
      // DETACH must precede memory reclamation; a failed initializer never leaves
      // a ready DSP row or a callable CF command pointing into released memory.
      expect(row.readUInt32LE(0)).toBe(6);
      await session.expectNoFault('normal V2 failure retirement');
      return;
    }
    expect(row.readUInt32LE(0)).toBe(3);
    expect(row.readUInt32LE(4)).toBe(0);
    const state = row.readUInt32LE(16);
    expect((await peek(state, 4)).readUInt32LE()).toBe(1);
    await expect.poll(async () => (await peek(planSymbol(plan, '_ilp_dispatched'), 4)).readUInt32LE()).toBe(1);
    expect((await peek(state + 4, 4)).readUInt32LE()).toBe(1);
    await session.expectNoFault('normal V2 file admission');
  },
  480_000,
);
