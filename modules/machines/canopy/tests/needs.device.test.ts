import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { patchkit } from '@digi/regression/runtime/patchkit.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { openDigikitMenu } from '@digi/regression/runtime/flows/menu.ts';

/**
 * What a load of CANOPY takes is what its file says (docs/design/digikit-dkm.md, "Needs are inferred"). CANOPY is a file of persistent
 * V2 instruments: the loader keeps the module, copies of its keys table and DSP section and the relocated image in one arena block.
 * `patchkit needs` (the hosts' reader) and the runtime's scan (the MODULES page) read the same accounting, so the test compares the
 * needs with CAPACITY after the load and reads CANOPY's row of the page: its kind and the memory it takes.
 */
describeOn('dt2', 'CANOPY needs on the DT2', () => {
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-needs-card-'));
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
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-needs-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  test('the load takes what the needs say, and the MODULES page shows them', { timeout: 600_000 }, async () => {
    const { session } = h;
    const artifacts = path.join(repoRoot(), 'out/canopy-needs', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const { syx, plan } = await prepareRuntimePlanListedOffline(
      session.options,
      session.runDir,
      artifacts,
      ['DKRT_BOOT', 'ip_control', 'DK_LOAD', 'DK_LOAD_N'],
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
    const beforeRestart = (await session.status()).epoch;
    if (typeof beforeRestart !== 'number') throw new Error('Missing pre-restart epoch');
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);
    const word = async (address: number) => {
      const value = await session.client.memPeek(address);
      if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
      return value >>> 0;
    };
    await waitInstrumentReady((name, i = 0) => word(planSymbol(plan, name) + 4 * i));
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, emulatedMs: 180_000 });

    // CANOPY is the only module of this boot: the arena holds its one V2 block and its record block, the DSP area its image,
    // bss and track states. Its instruments take instrument rows and tracks (CAPACITY has them as kinds), and no voice row.
    const needs = JSON.parse((await patchkit(['needs', '--json', moduleFile])).stdout)[moduleFile] as Record<string, number>;
    const capacity = await dk.capacity();
    fs.writeFileSync(path.join(artifacts, 'needs.json'), JSON.stringify({ needs, capacity }, null, 2));
    const used = (kind: string) => capacity[kind]!.used;
    const kinds = ['ARENA', 'DSP_AREA', 'MACHINE_ROWS', 'VOICE_ROWS', 'INSTRUMENT_ROWS', 'INSTRUMENT_TRACKS'];
    expect(Object.fromEntries(kinds.map((k) => [k, used(k)])), 'the load takes what the needs say').toEqual({
      ARENA: needs.ARENA,
      DSP_AREA: needs.DSP_AREA,
      MACHINE_ROWS: needs.MACHINE_ROWS,
      VOICE_ROWS: 0,
      INSTRUMENT_ROWS: needs.INSTRUMENT_ROWS,
      INSTRUMENT_TRACKS: needs.INSTRUMENT_TRACKS,
    });
    expect([needs.ARENA_XFER, needs.INSTRUMENT_ROWS, needs.INSTRUMENT_TRACKS]).toEqual([0, 1, 4]);
    // the firmware adapter admits four tracks, and CANOPY takes them; a boot takes one file of instruments, so no row is left to a second
    const rows = capacity.INSTRUMENT_ROWS!;
    expect([capacity.INSTRUMENT_TRACKS!.limit, rows.limit > rows.used, rows.largest]).toEqual([4, true, 0]);

    // SETTINGS > DIGIKIT > MODULES (the last row): the scan gives the file its kind and its memory, not a dash and its file size
    const { tap: key } = acknowledgedPanel(session);
    await openDigikitMenu({ ...sessionRt(session), dk });
    for (let i = 0; i < 8; i++) await key('DOWN');
    await key('YES');
    await expect.poll(() => word(planSymbol(plan, 'DK_LOAD_N')), { timeout: 60_000 }).toBe(1);
    const row = Buffer.alloc(24);
    for (let k = 0; k < 24; k += 4) row.writeUInt32BE(await word(planSymbol(plan, 'DK_LOAD') + k), k);
    // `module_text` of the menu: the name (12 columns and a space), the kind, the memory right aligned to 23 columns
    const memory = `${Math.ceil((needs.ARENA! + needs.DSP_AREA!) / 1024)}K`;
    expect(row.toString('latin1').split('\0')[0], 'the MODULES row of CANOPY').toBe(
      `${'CANOPY'.padEnd(13)}MACH`.padEnd(23 - memory.length) + memory,
    );
    await session.expectNoFault('CANOPY needs');
  });
});
