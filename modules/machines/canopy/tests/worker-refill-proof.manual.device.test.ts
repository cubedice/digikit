import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';

// Disposable emulator-only worker scheduler fixture. Never select on hardware.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_WORKER_REFILL === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-worker-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(() => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    const modules = path.join(cardSeed, 'digikit', 'modules');
    fs.mkdirSync(modules, { recursive: true });
    fs.copyFileSync(moduleFile, path.join(modules, 'canopy.dkm'));
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-worker-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
  test('real worker retries failed refill with interrupts enabled', { timeout: 600_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-worker-refill', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, artifacts,
      ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'canopy_sound_proof',
        'canopy_memory_probe_entry', 'worker_refill_tick', 'worker_refill_test'],
      { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health',
        'test/canopy-sound-proof', 'test/canopy-worker-refill'] },
    );
    await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
      auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);
    const word = async (name: string, index = 0) => {
      const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
      if (value === null) throw new Error(`Unreadable ${name}[${index}]`);
      return value >>> 0;
    };
    await waitInstrumentReady(word);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
    await expect.poll(() => word('canopy_sound_proof'), { timeout: 60_000 }).toBeGreaterThan(0);
    const command = planSymbol(plan, 'worker_refill_test');
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_memory_probe_entry'), value: planSymbol(plan, 'worker_refill_tick') });
    await session.client.call('mem.cf.poke', { addr: command, value: 1 });
    await expect.poll(() => word('worker_refill_test', 3), { timeout: 120_000 }).toBe(1);
    const first = await Promise.all(Array.from({ length: 16 }, (_, i) => word('worker_refill_test', i)));
    evidence.first = first;
    expect(first[1]).toBeGreaterThan(0);
    expect(first[1]).toBeLessThan(0x80000000);
    expect(first[2]).toBe(0xffffffff);
    expect(first[5]).toBe(1);
    expect(first[6]).toBe(0);
    expect(first[7]).toBe(0);
    expect(first[8]).toBe(0);
    expect(first[11]).toBe(1);
    expect(first[15]).toBe(0);
    await session.client.call('mem.cf.poke', { addr: command, value: 2 });
    await expect.poll(() => word('worker_refill_test', 3), { timeout: 120_000 }).toBe(2);
    const second = await Promise.all(Array.from({ length: 16 }, (_, i) => word('worker_refill_test', i)));
    evidence.second = second;
    expect(second[1]).toBeGreaterThan(0);
    expect(second[1]).toBeLessThan(0x80000000);
    expect(second[2]).toBe(0);
    expect(second[5]).toBe(2);
    expect(second[6]).toBe(0);
    expect(second[7]).toBe(4);
    expect(second[8]).toBe(4);
    expect(second[9]).not.toBe(0);
    expect(second[10]).not.toBe(0);
    expect(second[10]).not.toBe(second[9]);
    expect(second[11]).toBe(2);
    expect(second[12]).toBe(2);
    expect((second[4]! | second[13]!) & 0x700).toBe(0);
    expect(second[15]).toBe(0);
    await session.expectNoFault('worker refill fixture');
  });
} else {
  test.skip('CANOPY worker refill experiment is opt-in', () => {});
}
