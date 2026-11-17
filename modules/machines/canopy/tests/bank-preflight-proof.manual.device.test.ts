import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';

// Synthetic native bank-loader call on DigiKit's worker; emulator only.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_BANK_PREFLIGHT === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-bank-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-bank-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
  test('bank reservation failure returns before any destination Sound changes', { timeout: 600_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-bank-preflight', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanModulesOffline(session.options, session.runDir, artifacts,
      ['DKRT_BOOT', 'canopy_bank_loads', 'canopy_bank_tick', 'canopy_bank_test'],
      { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health',
        'test/canopy-bank-preflight'] });
    await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
      auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart(); await dk.waitForBoot(undefined, 180_000);
    const word = async (name: string, index = 0) => {
      const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
      if (value === null) throw new Error(`Unreadable ${name}[${index}]`);
      return value >>> 0;
    };
    await waitInstrumentReady(word);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
    await expect.poll(() => word('canopy_bank_loads'), { timeout: 60_000 }).toBeGreaterThan(0);
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_bank_test'), value: 1 });
    await expect.poll(() => word('canopy_bank_test'), { timeout: 180_000 }).toBe(2);
    evidence.test = await Promise.all(Array.from({ length: 19 }, (_, i) => word('canopy_bank_test', i)));
    const result = evidence.test as number[];
    expect(result[1], 'worker accepted').toBeGreaterThan(0);
    expect(result[2], 'worker step completed').toBe(1);
    expect(result[3], 'worker returned success').toBe(0);
    expect(result[4], 'bank loader returned failure').toBe(0);
    expect(result[5], 'full 130-Sound destination hash').not.toBe(0);
    expect(result[6], 'full destination unchanged').toBe(result[5]);
    expect(result[8], 'guard entered once').toBe(result[7]! + 1);
    expect(result[10], 'guard refused once').toBe(result[9]! + 1);
    expect(result[12], 'allocator failure was injected').toBe(result[11]! + 1);
    expect(result[14], 'no inner Sound loader ran').toBe(result[13]);
    expect(result[15], 'fixture setup succeeded').toBe(0);
    expect(result[17], 'pool consumed no handles').toBe(result[16]);
    expect(result[18], 'real destination was allocated').not.toBe(0);
    await session.expectNoFault('synthetic bank preflight refusal');
  });
} else {
  test.skip('CANOPY synthetic bank preflight is opt-in', () => {});
}
