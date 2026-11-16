import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot, Session } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { cardAfter, overlayRecords } from '@digi/regression/runtime/write-safety.ts';
import { canopyPanel } from './canopy-panel.ts';

// CANOPY Sound persistence through the stock preset transfers (finding 151):
// a track Sound saved into the project's preset pool and loaded from it onto
// another track and back, and a track Sound saved to the +Drive preset library
// and loaded onto another track, in the same session and after a power cycle.
// Every step is a native panel action on a disposable DTII card; OLED frames of
// each menu step are kept as evidence.
vi.setConfig({ hookTimeout: 600_000 });
const options = liveOptions('dt2', { fresh: true });
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-transfer-card-'));
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
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-transfer-card-'))
    throw new Error('Refusing to remove an unowned card fixture');
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

test('CANOPY settings travel with the Sound through the preset pool and the +Drive library', { timeout: 1_200_000 }, async () => {
  let session = h.session;
  let coldSession: Session | undefined;
  onTestFinished(async () => {
    await coldSession?.close();
  });
  const artifacts = path.join(repoRoot(), 'out/canopy-transfer', path.basename(session.runDir));
  fs.mkdirSync(artifacts, { recursive: true });
  console.info('CANOPY Sound transfer evidence:', artifacts);
  const log: Record<string, unknown> = {};
  onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(log, null, 2)));
  const { syx, plan } = await prepareRuntimePlanOffline(
    session.options,
    session.runDir,
    artifacts,
    ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_src_debug', 'src_state', 'ip_sa_info', 'ip_sa_stats'],
    { modules: ['core/instrument-play', 'core/digikit-menu', 'core/health'] },
  );
  expect(createHash('sha256').update(fs.readFileSync(syx)).digest('hex')).toBe(plan.output?.syx_sha256);
  const boot = async (card: string) => {
    await session.client.firmwareLoad({
      syx,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card,
    });
    await session.client.call('run.mode', { mode: 'free' });
  };
  await boot(session.cardPath!);
  const dk = liveProtocol(session);
  expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
  await dk.select(['modules/canopy.dkm']);
  const firstEpoch = Number((await session.status()).epoch);
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  let p = canopyPanel(session, plan);
  await waitInstrumentReady(p.cf);
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: firstEpoch, timeout: 180_000 });

  // Two CANOPY tracks with different settings.
  await p.chooseCanopy(1);
  const a = await p.edit(1, 20, 12, 16);
  await p.chooseCanopy(2);
  const b = await p.edit(2, 12, 8, 8);
  expect(b).not.toEqual(a);
  expect(await p.sound(1), 'track 2 edits do not replace track 1').toEqual(a);
  log.edited = { a, b };

  // Preset pool: save track 1, change it, load the pool Sound onto track 2 and back onto track 1.
  await p.poolSave(1);
  log.afterPoolSave = await p.persistence();
  const a2 = await p.edit(1, 0, 0, 10);
  expect(a2).not.toEqual(a);
  await p.poolLoad(2);
  await expect.poll(() => p.sound(2), { timeout: 30_000, message: 'pool Sound loaded onto track 2' }).toEqual(a);
  expect(await p.sound(1), 'loading onto track 2 leaves track 1').toEqual(a2);
  await p.poolLoad(1);
  await expect.poll(() => p.sound(1), { timeout: 30_000, message: 'pool Sound loaded back onto track 1' }).toEqual(a);
  log.afterPoolLoads = await p.persistence();
  await session.expectNoFault('CANOPY preset pool save and loads');

  // +Drive library: save track 2 (changed first), change it again, load the saved Sound onto track 1.
  const c = await p.edit(2, 0, 4, 20);
  expect(c).not.toEqual(a);
  await p.driveSave(2);
  log.afterDriveSave = await p.persistence();
  const d = await p.edit(2, 0, 0, 5);
  expect(d).not.toEqual(c);
  await p.driveLoad(1);
  await expect.poll(() => p.sound(1), { timeout: 30_000, message: '+Drive Sound loaded onto track 1' }).toEqual(c);
  expect(await p.sound(2), 'loading onto track 1 leaves track 2').toEqual(d);
  log.afterDriveLoad = await p.persistence();
  log.values = { a2, c, d };
  await session.expectNoFault('CANOPY +Drive save and load');

  // Power cycle: a fresh emulator on the card the guest wrote, then load the
  // +Drive Sound onto a new CANOPY track. Only the saved record can supply it.
  const overlay = path.join(artifacts, 'saved-card.overlay.bin');
  await session.client.call('card.overlay', { path: overlay });
  log.writtenSectors = Array.from(overlayRecords(fs.readFileSync(overlay)), (record) => record.sector).length;
  if (!session.cardPath) throw new Error('No source card for the simulated power cycle');
  const savedCard = cardAfter(session.cardPath, overlay, path.join(session.runDir, 'transfer-saved-card'));
  coldSession = await Session.start({ ...options, card: undefined, dsp: 'sharc', env: { DIGI_COLD_RELAX_FS: '1' } });
  session = coldSession;
  const coldEpoch = Number((await session.status()).epoch);
  await boot(savedCard);
  p = canopyPanel(session, plan);
  await waitInstrumentReady(p.cf);
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: coldEpoch, timeout: 180_000 });
  expect((await liveProtocol(session).modules()).map((m) => m.name)).toContain('digikit/canopy');
  await p.chooseCanopy(3);
  const fresh = await p.sound(3);
  log.coldFresh = fresh;
  expect(fresh, 'a new CANOPY Sound starts from other settings').not.toEqual(c);
  await p.driveLoad(3, 'cold-drive-load-t3');
  await expect
    .poll(() => p.sound(3), { timeout: 30_000, message: '+Drive Sound after a power cycle' })
    .toEqual(c);
  log.afterColdDriveLoad = await p.persistence();
  await session.expectNoFault('CANOPY +Drive load after power cycle');
});
