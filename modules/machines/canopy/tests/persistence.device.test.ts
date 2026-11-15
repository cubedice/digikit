import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot, Session } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { cardAfter, overlayRecords } from '@digi/regression/runtime/write-safety.ts';
import { bootInfo, currentRecord, recordsIn } from '@digi/regression/runtime/card-kit.ts';

// EXPERIMENTAL, NOT REGISTERED until the cold-boot and production fix pass.
// A stock project on a disposable DTII card. All edits and saves are native
// panel actions; the test reads requested values but never fabricates state.
vi.setConfig({ hookTimeout: 600_000 });
const enabled = process.env.DIGI_CANOPY_PERSIST_TEST === '1' || process.env.DIGI_CANOPY_SOUND_PROOF === '1';
if (enabled) {
const options = liveOptions('dt2', { fresh: true });
// Opt-in emulator-only experiment; the default remains the production baseline.
const useUnsafeSoundProof = process.env.DIGI_CANOPY_SOUND_PROOF === '1';
const useUnsafeCopyProof = useUnsafeSoundProof && process.env.DIGI_CANOPY_COPY_PROOF === '1';
const useUnsafeLoadGuard = useUnsafeSoundProof && process.env.DIGI_CANOPY_LOAD_GUARD === '1';
const checkWorkingBoot = process.env.DIGI_CANOPY_WORKING_BOOT === '1';
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-persist-card-'));
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
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-persist-card-'))
    throw new Error('Refusing to remove an unowned card fixture');
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

test(checkWorkingBoot ? 'working-state restart restores each CANOPY track SRC sound' :
  'native project save and reboot restore each CANOPY track SRC sound', { timeout: 900_000 }, async () => {
  let session = h.session;
  let coldSession: Session | undefined;
  onTestFinished(async () => { await coldSession?.close(); });
  const artifacts = path.join(repoRoot(), 'out/canopy-persistence', path.basename(session.runDir));
  fs.mkdirSync(artifacts, { recursive: true });
  console.info('CANOPY persistence evidence:', artifacts);
  const log: Record<string, unknown> = {};
  onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(log, null, 2)));
  const { syx, plan } = await prepareRuntimePlanOffline(
    session.options,
    session.runDir,
    artifacts,
    ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_src_debug', 'src_state', ...(useUnsafeSoundProof ? ['canopy_sound_proof'] : []), ...(useUnsafeLoadGuard ? ['canopy_load_guard'] : []), ...(checkWorkingBoot ? ['DKRT_SAVE_MODE'] : [])],
    { modules: [useUnsafeSoundProof ? 'test/instrument-play-proof' : 'core/instrument-play', 'core/digikit-menu', 'core/health', ...(useUnsafeSoundProof ? ['test/canopy-sound-proof'] : []), ...(useUnsafeCopyProof ? ['test/canopy-copy-handoff'] : []), ...(useUnsafeLoadGuard ? ['test/canopy-load-preflight'] : [])] },
  );
  expect(createHash('sha256').update(fs.readFileSync(syx)).digest('hex')).toBe(plan.output?.syx_sha256);
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
  const firstEpoch = Number((await session.status()).epoch);
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const cf = async (name: string, index = 0) => {
    const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
    if (value === null) throw new Error(`Unreadable ${name}[${index}]`);
    return value >>> 0;
  };
  await waitInstrumentReady(cf);
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: firstEpoch, timeout: 180_000 });
  const { tap: key, edge, turn } = acknowledgedPanel(session);
  const track = async (n: number) => {
    await edge('TRK', true);
    try { await key(`TRIG ${n}`); } finally { await edge('TRK', false); }
  };
  const chooseCanopy = async (n: number) => {
    await track(n);
    await edge('FUNC', true);
    try { await key('SRC'); } finally { await edge('FUNC', false); }
    for (let i = 0; i < 20; i++) await key('DOWN');
    await key('YES');
    await key('YES');
    await expect.poll(async () => {
      const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
      return tracks.includes(n - 1);
    }, { timeout: 60_000 }).toBe(true);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(n - 1);
    for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== 0; i++) await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 1), { timeout: 30_000 }).toBe(0);
    await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
  };
  const sound = async (trackNo: number) => {
    const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
    const instance = tracks.indexOf(trackNo - 1);
    if (instance < 0) throw new Error(`Track ${trackNo} is not admitted: ${tracks}`);
    // CsState is 192 bytes. Control IDs 0/1 are CHRD/VOIC and 4 is STRUM;
    // metadata's mode distinguishes PLAY from the first ROOT chord.
    const base = instance * 48;
    const [metadata, first, strum] = await Promise.all([
      cf('src_state', base + 6), cf('src_state', base + 7), cf('src_state', base + 9),
    ]);
    return { mode: metadata & 3, chord: first >>> 16, voicing: first & 0xffff, strum: strum >>> 16 };
  };
  await chooseCanopy(1);
  await turn(2, 20); // CHRD
  await turn(3, 12); // VOIC
  await turn(4, 16); // STRUM
  await expect.poll(() => sound(1), { timeout: 30_000 }).toMatchObject({ mode: 1, voicing: 3 });
  const first = await sound(1);
  log.track1Edited = first;
  expect(first.chord).toBeGreaterThan(0);
  expect(first.strum).toBeGreaterThan(0);
  await chooseCanopy(2);
  await turn(2, 12);
  await turn(3, 8);
  await turn(4, 8);
  await expect.poll(() => sound(2), { timeout: 30_000 }).toMatchObject({ mode: 1, voicing: 2 });
  const second = await sound(2);
  log.track2Edited = second;
  expect(second.chord).toBeGreaterThan(0);
  expect(second.strum).toBeGreaterThan(0);
  expect(second).not.toEqual(first);
  expect(await sound(1), 'track 2 edits do not replace track 1').toEqual(first);
  log.before = [first, second];
  // FUNC+SETTINGS is the stock SAVE PROJECT shortcut (roundtrip.device.test).
  await edge('FUNC', true);
  try { await key('SETTINGS'); } finally { await edge('FUNC', false); }
  await waitEmulated(session, 300);
  await key('YES'); // slot 001
  await waitEmulated(session, 3500); // allow the stock card write to finish
  if (useUnsafeSoundProof)
    log.proofAfterSave = await Promise.all(Array.from({ length: 8 }, (_, i) => cf('canopy_sound_proof', i)));
  if (useUnsafeLoadGuard) {
    // Deliberately make working state different from the saved project; force
    // the guard to refuse before entering the card loader and check that the
    // current sounds are still present. This tests one stock caller only.
    await track(1);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
    await turn(2, 12);
    const working = [await sound(1), await sound(2)];
    expect(working[0]).not.toEqual(first);
    // Fail the second slab allocation so the first must be freed by the
    // firmware deallocator before the guard refuses the load.
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_load_guard'), value: 2 });
    await key('SETTINGS');
    await waitEmulated(session, 300);
    for (let i = 0; i < 3; ++i) { await key('YES'); await waitEmulated(session, 500); }
    await key('YES');
    await waitEmulated(session, 6000);
    log.guardRefusal = await Promise.all(Array.from({ length: 11 }, (_, i) => cf('canopy_load_guard', i)));
    expect((log.guardRefusal as number[])[1]).toBeGreaterThan(0);
    expect((log.guardRefusal as number[])[2]).toBeGreaterThan(0);
    expect((log.guardRefusal as number[])[9], 'one allocation was deliberately failed').toBe(1);
    expect((log.guardRefusal as number[])[10], 'the new slab was freed').toBe(1);
    expect([await sound(1), await sound(2)], 'refused load leaves edited working sounds intact').toEqual(working);
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_load_guard'), value: 0 });
  }
  if (checkWorkingBoot) {
    // Experimental positive control: ask DigiKit's restart to invoke the
    // stock working-state serializer and boot-record writer. This is NOT its
    // default and does not model an ordinary physical power-off by itself.
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'DKRT_SAVE_MODE'), value: 3 });
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);
    await waitInstrumentReady(cf);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, timeout: 180_000 });
    await expect.poll(async () => {
      const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
      log.workingBootTracks = tracks;
      return tracks.includes(0) && tracks.includes(1);
    }, { timeout: 60_000, message: 'working boot restores both edited CANOPY tracks' }).toBe(true);
    log.workingBootValues = [await sound(1), await sound(2)];
    expect(log.workingBootValues, 'working boot restores each track SRC').toEqual(log.before);
    await session.expectNoFault('CANOPY working-state restart');
    return;
  }
  // Export the guest's actual card writes and start a fresh firmware instance
  // on a separate card image. This models a power cycle instead of relying on
  // live RAM or on the restart command to flush a custom state record.
  const overlay = path.join(artifacts, 'saved-card.overlay.bin');
  await session.client.call('card.overlay', { path: overlay });
  const written = Array.from(overlayRecords(fs.readFileSync(overlay)), (record) => record.sector);
  log.projectWriteSectors = written.filter((sector) => sector >= 0x58000 && sector < 0x60000);
  expect((log.projectWriteSectors as number[]).length, 'stock project save reached the card').toBeGreaterThan(0);
  if (!session.cardPath) throw new Error('No source card for the simulated power cycle');
  const savedCard = cardAfter(session.cardPath, overlay, path.join(session.runDir, 'persist-saved-card'));
  log.savedSelection = currentRecord(recordsIn(savedCard, 'dt2'));
  expect((log.savedSelection as { paths?: string[] } | null)?.paths, 'the saved card selects CANOPY')
    .toEqual(['modules/canopy.dkm']);
  // A saved DTII project no longer performs the fresh-card filesystem check.
  // Match the separate relaxed daemon used for later boots in card-rig while
  // retaining the normal, unrelaxed first boot and panel setup.
  coldSession = await Session.start({ ...options, card: undefined, dsp: 'sharc', env: { DIGI_COLD_RELAX_FS: '1' } });
  session = coldSession;
  const savedEpoch = Number((await session.status()).epoch);
  await session.client.firmwareLoad({
    syx,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
    card: savedCard,
  });
  await session.client.call('run.mode', { mode: 'free' });
  await waitInstrumentReady(cf);
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: savedEpoch, timeout: 180_000 });
  await expect.poll(async () => {
    log.coldBoot = await bootInfo(session, plan);
    const boot = log.coldBoot as { count: number; results: number[] };
    return { count: boot.count, results: boot.results };
  }, { timeout: 60_000, message: 'the selected CANOPY module loads successfully after power cycle' })
    .toEqual({ count: 1, results: [0] });
  const coldModules = await liveProtocol(session).modules();
  log.coldModules = coldModules;
  expect(coldModules.map((module) => module.name)).toContain('digikit/canopy');
  await waitEmulated(session, 1000); // allow admission of any project restored by the normal boot
  log.tracksBeforeProjectLoad = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
  if ((log.tracksBeforeProjectLoad as number[]).includes(0) && (log.tracksBeforeProjectLoad as number[]).includes(1))
    log.valuesBeforeProjectLoad = [await sound(1), await sound(2)];
  // Explicitly load the saved slot using the same stock panel sequence as
  // dynreg's round-trip test.
  // This proves the project was loaded from the saved card before admission.
  const coldPanel = acknowledgedPanel(session);
  await coldPanel.tap('SETTINGS');
  await waitEmulated(session, 300);
  for (let i = 0; i < 3; i++) {
    await coldPanel.tap('YES');
    await waitEmulated(session, 500);
  }
  await coldPanel.tap('YES');
  await waitEmulated(session, 6000);
  await session.expectNoFault('saved CANOPY project loaded');
  await expect.poll(async () => {
    const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
    return tracks.includes(0) && tracks.includes(1);
  }, { timeout: 60_000 }).toBe(true);
  log.tracksAfterPowerCycle = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
  if (useUnsafeSoundProof)
    log.proofAfterLoad = await Promise.all(Array.from({ length: 8 }, (_, i) => cf('canopy_sound_proof', i)));
  if (useUnsafeLoadGuard) {
    log.guardAfterLoad = await Promise.all(Array.from({ length: 9 }, (_, i) => cf('canopy_load_guard', i)));
    const guard = log.guardAfterLoad as number[];
    expect(guard[1], 'card guard entered').toBeGreaterThan(0);
    expect(guard[2], 'card guard did not refuse').toBe(0);
    expect(guard[3], 'loaded snapshots used reserved handles').toBeGreaterThan(0);
    expect(guard[4], 'card guard completed').toBeGreaterThan(0);
    expect(guard[7], 'unused reservations were returned').toBeGreaterThan(0);
    expect(guard[8], 'tail cancellation succeeded').toBe(0);
    expect(guard[6], 'only published snapshots consumed handles').toBe(guard[5]! + guard[3]!);
  }
  log.after = [await sound(1), await sound(2)];
  expect(log.after, 'saved per-track CHRD, VOIC and STRUM survive reboot').toEqual(log.before);
  await session.expectNoFault('CANOPY native project reboot');
});
} else {
  test.skip('CANOPY persistence experiment is opt-in', () => {});
}
