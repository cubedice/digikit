import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/session-handle.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { dspWords } from '@digi/regression/runtime/dsp-state.ts';
import { rpc } from '@digi/regression/runtime/guest-access.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { liveSampleFolder, seededCard } from '@digi/regression/runtime/live-card.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { instrumentSymbols } from '@digi/regression/runtime/instrument-symbols.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/module-artifact.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { canopyPanel } from './canopy-panel.ts';
import { envelope } from './audio.ts';

const hex8 = (value: number) => (value >>> 0).toString(16).padStart(8, '0');

describeOn('dt2', 'CANOPY under the DSP load budget', () => {
  // The adapter compares the cost of the last DSP block with its limit (`_iv_budget_limit`, core/instrument-play, finding 377) and hands
  // CANOPY a pressure; CANOPY sheds voices as it rises and takes them back as it falls, and the stock tracks play as before. The firmware
  // shows it: the cost word is the stock block handler's own count in its reply record, and the pressure travels in the input the
  // admitted callback gets. The emulator's count stays far under the limit (it counts instructions), so the test lowers the limit.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-load-budget-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(liveSampleFolder('dt2')!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines', 'instrument', 'track-keyboard'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-load-budget-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, cardImage: () => seededCard('dt2', options.firmware, cardSeed!), dsp: 'sharc', restart: false });

  test('blocks over the limit silence CANOPY without touching a stock track, and CANOPY returns', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-load-budget', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanListedOffline(session.options, session.runDir, artifacts, ['DKRT_BOOT'], {
      modules: ['core/instrument-play', 'core/track-keyboard', 'core/health'],
    });
    await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true, auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const sym = await instrumentSymbols(session, plan);
    const panel = canopyPanel(session, plan, sym);
    await waitInstrumentReady(panel.cf);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
    await panel.chooseCanopy(1);
    const lanes = async () => {
      const addr = sym('track_keyboard_lanes');
      const value = await session.client.memPeek((addr & ~3) >>> 0);
      return value === null ? null : (value >>> (24 - 8 * (addr & 3))) & 255;
    };
    await expect.poll(lanes, { timeout: 30_000, message: 'the track keyboard plays track 1' }).not.toBe(0);
    const { edge } = acknowledgedPanel(session);
    const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });

    const dsp = (name: string) => planSymbol(plan, name);
    const word = async (name: string) => (await dspWords(session, dsp(name), 1))[0]! >>> 0;
    const poke = async (name: string, value: number) => {
      await session.client.call('run.mode', { mode: 'paused' });
      try {
        const old = (await dspWords(session, dsp(name), 1))[0]!;
        await rpc(session, 'dsp.patch', { text: `BASE ${'0'.repeat(64)}\nDM ${dsp(name).toString(16)} ${hex8(value)} was ${hex8(old)}\n` });
      } finally {
        await session.client.call('run.mode', { mode: 'free' });
      }
    };
    const capture = async (name: string, ms: number, play?: () => Promise<void>) => {
      const file = path.join(artifacts, `${name}.f32le`);
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      try {
        await play?.();
        await waitEmulated(session, ms);
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
      }
      return envelope(fs.readFileSync(file));
    };
    const peak = (env: number[]) => Math.max(...env);
    // the envelope from its onset (the first 10 ms frame above -60 dB)
    const fromOnset = (env: number[], count: number) => {
      const at = env.findIndex((level) => level > -60);
      expect(at, 'the press sounds').toBeGreaterThanOrEqual(0);
      return env.slice(at, at + count);
    };
    // the notes CANOPY's instance of track 1 owns: the tokens of its six pad and tail slots and of its arp group
    const owners = async () => {
      const state = (await dspWords(session, dsp('_ir_rows'), 5))[4]!;
      const bank = await dspWords(session, state, 48);
      const arp = await dspWords(session, state + 544, 1);
      return [0, 8, 16, 24, 32, 40].map((i) => bank[i]!).concat(arp).filter((token) => token !== 0).length;
    };
    const stockPress = async () => {
      await edge('TRIG 2', true);
      try {
        await waitEmulated(session, 300);
      } finally {
        await edge('TRIG 2', false);
      }
    };
    const starve = async () => {
      await poke('_iv_budget_limit', 1);
      await waitEmulated(session, 150);
      expect(await word('_iv_pressure'), 'every block is over the limit: the pressure is at its top').toBe(7);
    };
    const feed = async () => {
      // the pressure falls a level every 150 calm blocks (100 ms)
      await poke('_iv_budget_limit', limit);
      await waitEmulated(session, 1200);
      expect(await word('_iv_pressure'), 'calm blocks take the pressure back').toBe(0);
    };

    const limit = await word('_iv_budget_limit');
    evidence.limit = limit;
    evidence.cost = await word('_iv_budget_cost');
    expect(limit, "the adapter's default limit").toBe(615000);
    expect(evidence.cost as number, 'the adapter reads the cost of the last block').toBeGreaterThan(0);
    expect(evidence.cost as number, "the emulator's count is under the limit").toBeLessThan(limit);
    expect(await word('_iv_pressure'), 'no pressure under the limit').toBe(0);

    // The stock reference: TRIG 2 auditions track 2's sample.
    const stock = fromOnset(await capture('stock', 900, stockPress), 25);
    await waitEmulated(session, 1500);

    // CANOPY: notes held on channel 1, which plays track 1 in the default MIDI configuration.
    const notes = [57, 60, 64];
    try {
      for (const note of notes) await midi([0x90, note, 100]);
      await waitEmulated(session, 400);
      const held = await capture('held', 400);
      evidence.held = peak(held);
      evidence.owners = await owners();
      expect(peak(held), 'the held notes sound').toBeGreaterThan(-50);
      expect(evidence.owners, 'CANOPY owns them').toBeGreaterThan(0);
      expect(await word('_iv_pressure')).toBe(0);

      // A limit no block meets: the pressure goes to its top and CANOPY ramps out; it keeps the notes it owns.
      await starve();
      expect(await word('_iv_budget_overs'), 'the blocks over the limit are counted').toBeGreaterThan(7);
      const starved = await capture('starved', 400);
      evidence.starved = peak(starved);
      evidence.ownersStarved = await owners();
      expect(peak(starved), 'CANOPY is silent under full pressure').toBeLessThan(-90);
      expect(evidence.ownersStarved, 'and still owns its notes').toBe(evidence.owners);

      // The limit back: the held notes fade back in, without a new note-on.
      await feed();
      const returned = await capture('returned', 400);
      evidence.returned = peak(returned);
      expect(peak(returned), 'the held notes sound again').toBeGreaterThan((evidence.held as number) - 6);
    } finally {
      for (const note of notes) await midi([0x80, note, 0]);
    }
    await waitEmulated(session, 4500); // CANOPY's release tail

    // A note that starts under full pressure is taken and stays silent, and the stock track plays as it did: the same envelope
    // from its onset.
    await starve();
    try {
      await midi([0x90, 57, 100]);
      const masked = await capture('masked', 400);
      evidence.masked = peak(masked);
      expect(peak(masked), 'a new note under full pressure is silent').toBeLessThan(-90);
      expect(await owners(), 'and owned').toBeGreaterThan(0);
      const stockStarved = fromOnset(await capture('stock-starved', 900, stockPress), 25);
      evidence.stock = stock;
      evidence.stockStarved = stockStarved;
      expect(stockStarved.length).toBe(stock.length);
      expect(peak(stockStarved), 'the stock press sounds').toBeGreaterThan(-50);
      for (let i = 1; i < stock.length - 1; i++)
        expect(Math.abs(stockStarved[i]! - stock[i]!), `the stock track's level in frame ${i} after its onset`).toBeLessThan(1);
    } finally {
      await midi([0x80, 57, 0]);
    }
    await feed();
    await session.expectNoFault('CANOPY under the DSP load budget');
  });
});
