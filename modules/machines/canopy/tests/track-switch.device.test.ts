import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { SETTLE } from '@digi/regression/runtime/fx-live.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';

describeOn('dt2', 'CANOPY on two tracks of the DT2', () => {
  // The DSP reports the status of one instance, the one on the selected track (finding 357). After a track is selected
  // the status of its instance is absent until the focus has moved. That moment is not a lost link: the SRC page must
  // not show the LINK LOST toast, and the status of the new track must arrive.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-track-switch-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-track-switch-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  test('selecting the other CANOPY track shows no fault and brings its status', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-track-switch', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanListedOffline(
      session.options,
      session.runDir,
      artifacts,
      ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'ip_src_fault_toasts', 'src_status'],
      { modules: ['core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    await session.client.firmwareLoad({
      syx,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const word = async (address: number) => {
      const value = await session.client.memPeek(address);
      if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
      return value >>> 0;
    };
    const cf = (name: string, index = 0) => word(planSymbol(plan, name) + 4 * index);
    await waitInstrumentReady(cf);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
    const { tap: key, edge } = acknowledgedPanel(session);
    const selectTrack = async (n: number) => {
      await edge('TRK', true);
      try {
        await key(`TRIG ${n}`);
      } finally {
        await edge('TRK', false);
      }
    };
    const instanceOf = async (track: number) => {
      const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
      return tracks.indexOf(track - 1);
    };
    const chooseCanopy = async (n: number) => {
      await selectTrack(n);
      await edge('FUNC', true);
      try {
        await key('SRC');
      } finally {
        await edge('FUNC', false);
      }
      for (let i = 0; i < 20; i++) await key('DOWN');
      await key('YES');
      await key('YES');
      await expect.poll(() => instanceOf(n), { timeout: 60_000 }).toBeGreaterThanOrEqual(0);
    };
    // IsStatus is 638 bytes on the ColdFire: its track at +4, the reported track in the low bits of the fast status at
    // +20, the block count of that status at +16 and the validity byte of the fast status at +632.
    const statusOf = async (instance: number) => {
      const base = planSymbol(plan, 'src_status') + instance * 638;
      return {
        track: await word(base + 4),
        reportedTrack: (await word(base + 20)) & 15,
        valid: (await word(base + 632)) >>> 24,
      };
    };
    const phase = (instance: number) => cf('ip_control', instance * 8);
    const transportFaults = () => cf('ip_control', 4);

    await chooseCanopy(11);
    await chooseCanopy(12);
    const first = await instanceOf(11),
      second = await instanceOf(12);
    expect(new Set([first, second]).size, 'two instances').toBe(2);
    await expect.poll(async () => [await phase(first), await phase(second)], { timeout: 60_000 }).toEqual([3, 3]);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(11);
    await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
    await sessionRt(session).ui.wait({ ms: 1000, why: SETTLE });

    const toastsBefore = await cf('ip_src_fault_toasts');
    const switches: unknown[] = [];
    evidence.switches = switches;
    for (const track of [11, 12, 11, 12, 11, 12]) {
      const draws = await cf('ip_src_debug', 3);
      await selectTrack(track);
      const instance = track === 11 ? first : second;
      await expect
        .poll(() => statusOf(instance), { timeout: 30_000 })
        .toEqual({ track: track - 1, reportedTrack: track - 1, valid: 1 });
      await sessionRt(session).ui.wait({ ms: 400, why: SETTLE });
      const row = {
        track,
        status: await statusOf(instance),
        draws: (await cf('ip_src_debug', 3)) - draws,
        srcTrack: await cf('ip_src_debug', 0),
        toasts: (await cf('ip_src_fault_toasts')) - toastsBefore,
        transportFaults: await transportFaults(),
      };
      switches.push(row);
      expect(row.srcTrack, 'the SRC page follows the selection').toBe(track - 1);
      expect(row.draws, 'the SRC page was drawn after the switch').toBeGreaterThan(0);
      expect(row.status, `track ${track} still reports`).toEqual({ track: track - 1, reportedTrack: track - 1, valid: 1 });
      expect(row.transportFaults, 'the note transport did not fault').toBe(0);
    }
    expect((await cf('ip_src_fault_toasts')) - toastsBefore, 'no fault toast for a track switch').toBe(0);
    await session.expectNoFault('CANOPY track switch');
  });
});
