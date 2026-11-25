// Temporary rehearsal of the hardware install (not committed): the DT2 hardware image with RELAY (`canopy-overloop-prism-relay`) and the
// owner's selection (CANOPY, Overloop, the remote, PRISM, RELAY) with every service they and the image need, booted from the card as the
// device will boot it. Every wait is in emulated time; the runtime log is written out whatever happens.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { LOG_KIND } from '@digi/dk-session/gen_proto';
import { afterAll, beforeAll, expect, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { liveSampleFolder, seededCard } from '@digi/regression/runtime/live-card.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { seedModules, seedServices } from '@digi/regression/runtime/card-seed.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';

const HW_SET = (process.env.HW_SET ?? 'core/health,core/input,core/instrument-play,core/loop-storage,core/pattern-position,core/loop-display,core/phrase-ui,core/track-keyboard,core/relay-track').split(',');
const MODULES = [
  { id: 'digikit/canopy', name: 'canopy.dkm', needs: ['machines', 'instrument'] },
  { id: 'digikit/overloop', name: 'overloop.dkm', needs: ['fx'] },
  { id: 'digikit/remote', name: 'remote.dkm', needs: [] },
  { id: 'digikit/prism', name: 'prism.dkm', needs: ['fx'] },
  { id: 'digikit/relay-machine', name: 'relay-machine.dkm', needs: ['machines', 'instrument'] },
];
const SELECTION = MODULES.map((m) => `modules/${m.name}`);
const KIND = Object.fromEntries(Object.entries(LOG_KIND).map(([k, v]) => [v, k]));

describeOn('dt2', 'hardware install rehearsal', () => {
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const root = fs.realpathSync(path.join(repoRoot(), 'out'));
  const seed = fs.mkdtempSync(path.join(root, 'hw-rehearsal-card-'));
  beforeAll(async () => {
    fs.cpSync(liveSampleFolder('dt2')!, seed, { recursive: true });
    await seedModules(seed, 'dt2', MODULES.map((m) => ({ file: moduleArtifact('dt2', m.id), name: m.name, needs: m.needs })));
    await seedServices(seed, 'dt2', ['track-keyboard']);
  });
  afterAll(() => fs.rmSync(seed, { recursive: true }));
  const h = withSession({
    ...options,
    cardImage: () => seededCard('dt2', options.firmware, seed, { selection: SELECTION }),
    dsp: 'sharc',
    restart: false,
  });

  test('the hardware image boots the owner selection', { timeout: 1_800_000 }, async () => {
    const { session } = h;
    const out = path.join(root, 'hw-rehearsal', path.basename(session.runDir));
    fs.mkdirSync(out, { recursive: true });
    const { syx, plan } = await prepareRuntimePlanModulesOffline(session.options, session.runDir, out, ['DKRT_BOOT', 'DKRT_INSTRUMENT_FILE'], {
      modules: HW_SET,
      timeoutMs: 300_000,
    });
    fs.copyFileSync(syx, path.join(out, 'hardware.syx'));
    fs.writeFileSync(path.join(out, 'hardware.plan.json'), JSON.stringify(plan, null, 2));
    const dk = liveProtocol(session);
    const report: Record<string, unknown> = {
      syx_sha256: createHash('sha256').update(fs.readFileSync(syx)).digest('hex'),
      plan_output: plan.output,
      selection: SELECTION,
    };
    const word = async (name: string, i = 0) => ((await session.client.memPeek(planSymbol(plan, name) + 4 * i)) ?? 0) >>> 0;
    const results = () => Promise.all(SELECTION.map(async (_, i) => (await word('DKRT_BOOT', 10 + i)) | 0));
    try {
      await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true, auto_no: true, card: session.cardPath });
      await session.client.call('run.mode', { mode: 'free' });
      await expectRuntimeUi(session, plan, out, { emulatedMs: 300_000 });
      report.ui = true;
      // the load results (1 while a DSP part still loads) and CANOPY's instrument transaction (phase 3: ready)
      await pollEmulated(
        session,
        async () => ({ results: await results(), instrument: await Promise.all([0, 1, 2, 3].map((i) => word('DKRT_INSTRUMENT_FILE', i))) }),
        (s) => s.results.every((r) => r !== 1) && s.instrument[0] === 3,
        { emulatedMs: 240_000, intervalMs: 1000, message: 'every load done and the instrument ready' },
      );
      report.results = await results();
      report.instrument_file = await Promise.all([0, 1, 2, 3].map((i) => word('DKRT_INSTRUMENT_FILE', i)));
      report.modules = await dk.modules();
      const capacity = await dk.capacity();
      report.capacity = Object.fromEntries(['ARENA', 'DSP_AREA', 'PACKAGE', 'STAGE', 'MACHINE_ROWS', 'VOICE_ROWS', 'INSTRUMENT_ROWS'].map((k) => [k, capacity[k]]));
      await captureRemoteOled(session, 'rehearsal-main').then((png) => fs.copyFileSync(png, path.join(out, 'main.png')));
      expect(report.results, 'every module of the selection loaded').toEqual(SELECTION.map(() => 0));
      await session.expectNoFault('hardware install rehearsal');
      report.ok = true;
    } finally {
      try {
        report.results_at_end = await results();
        report.log = (await dk.readLog()).map((e) => `${KIND[e.kind] ?? e.kind} ${e.args.map((a: number) => (a >>> 0).toString(16)).join(' ')}`);
      } catch (e) {
        report.log_error = String(e);
      }
      fs.writeFileSync(path.join(out, 'rehearsal.json'), JSON.stringify(report, null, 2));
      console.info('REHEARSAL', out, JSON.stringify({ ok: report.ok, results: report.results_at_end, capacity: report.capacity }));
    }
  });
});
