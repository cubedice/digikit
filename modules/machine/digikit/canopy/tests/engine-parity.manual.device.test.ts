// Manual proof (the `.manual` suffix keeps it out of the Nx graph): CANOPY played by the track keyboard, its only note engine, on the
// owner's own Sounds (docs/plans/upstream-merge-1124/README.md, "CANOPY as a voice host"). This file compared it with CANOPY's own engine
// until that engine was removed; the comparison was measured at commit 28ea6d093, and the file now records the track keyboard's side.
//
// It boots an image with core/track-keyboard on a card with CANOPY and its services and the owner's settings record (HW_SETTINGS, the
// settings.bin of a device backup), whose working copy gives track 1 the owner's CANOPY Sound. It plays fixed inputs on track 1 and
// writes out/canopy-parity/shared/: the raw captures (f32le stereo, 48 kHz) and report.json. MIDI reaches track 1 on its channel in the
// Digitakt's MIDI config (channel 1).
import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, test, vi } from 'vitest';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { writeIdFile } from '@digi/regression/runtime/card-cache.ts';
import { withSession } from '@digi/regression/support.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { liveSampleFolder, seededCard } from '@digi/regression/runtime/live-card.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { instrumentSymbols } from '@digi/regression/runtime/instrument-symbols.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { seedModules, seedServices } from '@digi/regression/runtime/card-seed.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { canopyPanel } from './canopy-panel.ts';
import { envelope, pitches, RATE } from './audio.ts';

const SETTINGS = process.env.HW_SETTINGS;
const WRITE_SETTINGS = [
  'import sys',
  'from pathlib import Path',
  'from tools.patchkit import pk_device',
  'img, raw = Path(sys.argv[1]), Path(sys.argv[2]).read_bytes()',
  'st = pk_device.store_of("dt2")',
  'padded = raw.ljust(((len(raw) + 511) // 512) * 512, bytes(1))',
  'assert len(padded) <= st.set_copy_sectors * 512, len(padded)',
  'f = img.open("r+b")',
  'for s in (st.set_a, st.set_b):',
  '    f.seek(s * 512); f.write(bytes(st.set_copy_sectors * 512))',
  'f.seek(st.set_a * 512); f.write(padded); f.close()',
].join(String.fromCharCode(10));

type Mark = { name: string; frame: number };
type Capture = { name: string; file: string; marks: Mark[] };

/** Onsets (frame index of 10 ms frames where the level rises 9 dB above the last 50 ms and is above -60 dB), release tail (10 ms frames
 * from mark `last_off` until the level stays under -60 dB), peak level and the spectral peaks 500 ms after mark `held`. */
function analyse(c: Capture) {
  const raw = fs.readFileSync(c.file);
  const env = envelope(raw);
  const onsets: number[] = [];
  for (let i = 5; i < env.length; i++) {
    const before = Math.max(...env.slice(i - 5, i));
    if (env[i]! > -60 && env[i]! - before > 9 && (!onsets.length || i - onsets[onsets.length - 1]! > 3)) onsets.push(i);
  }
  const mark = (name: string) => c.marks.find((m) => m.name === name)?.frame;
  const off = mark('last_off');
  let tail: number | null = null;
  if (off !== undefined) {
    const start = Math.floor(off / (RATE / 100));
    for (let i = start; i < env.length; i++)
      if (env.slice(i, i + 20).every((v) => v < -60)) {
        tail = i - start;
        break;
      }
  }
  const held = mark('held');
  return {
    name: c.name,
    frames: Math.floor(raw.length / 8),
    peak_db: Math.max(...env),
    onsets_10ms: onsets,
    tail_10ms: tail,
    pitches: held !== undefined ? pitches(raw, held + RATE / 2) : [],
    marks: c.marks,
  };
}

describeOn('dt2', 'CANOPY note engine parity (shared)', () => {
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const root = fs.realpathSync(path.join(repoRoot(), 'out'));
  const out = path.join(root, 'canopy-parity', 'shared');
  const card = fs.mkdtempSync(path.join(root, 'canopy-parity-card-'));
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(liveSampleFolder('dt2')!, name), path.join(card, name));
    await seedModules(card, 'dt2', [{ file: moduleArtifact('dt2', 'digikit/canopy'), needs: ['machines', 'instrument', 'track-keyboard'] }]);
    await seedServices(card, 'dt2', ['track-keyboard']);
  });
  // the settings cards this file made: removed after the session (its own afterAll, registered later, runs first)
  const settingsDirs: string[] = [];
  afterAll(() => {
    fs.rmSync(card, { recursive: true });
    for (const dir of settingsDirs) fs.rmSync(dir, { recursive: true, force: true });
  });
  const handle = withSession({
    ...options,
    cardImage: async () => {
      const base = await seededCard('dt2', options.firmware, card);
      if (!SETTINGS) return base;
      const dir = fs.mkdtempSync(path.join(root, 'canopy-parity-settings-'));
      settingsDirs.push(dir);
      // the card image is sparse (a few hundred KB of its 24 GB are written): copy it as one, or each run costs 24 GB of disk
      for (const f of fs.readdirSync(path.dirname(base))) {
        const from = path.join(path.dirname(base), f);
        if (f === path.basename(base)) execFileSync('cp', ['--sparse=always', from, path.join(dir, f)]);
        else fs.copyFileSync(from, path.join(dir, f));
        fs.chmodSync(path.join(dir, f), 0o644);
      }
      const img = path.join(dir, path.basename(base));
      execFileSync('uv', ['run', '--no-sync', 'python', '-c', WRITE_SETTINGS, img, SETTINGS], {
        cwd: repoRoot(),
        env: { ...process.env, UV_NO_SYNC: '1' },
      });
      // The daemon keys its booted states by the card's content id (`card.key`), not its bytes: the copied id is the base card's, so a boot
      // of the same image with the base card (no settings record) would be restored for this one. The new id is the base id and the
      // record's hash, as `linkCard` does for its writes. PARITY_CARD_KEY=copy keeps the copied id (the reproduction).
      if (process.env.PARITY_CARD_KEY !== 'copy') {
        const id = path.join(dir, 'card.key');
        const text = createHash('sha256')
          .update(`${fs.readFileSync(id, 'utf8').trim()}\0settings\0${createHash('sha256').update(fs.readFileSync(SETTINGS)).digest('hex')}`)
          .digest('hex')
          .slice(0, 32);
        fs.rmSync(id, { force: true });
        writeIdFile(id, text);
      }
      return img;
    },
    dsp: 'sharc',
    restart: false,
  });

  test('plays the same inputs on the owner Sound of track 1', { timeout: 1_800_000 }, async () => {
    expect(SETTINGS, 'HW_SETTINGS names a settings.bin').toBeTruthy();
    const { session } = handle;
    fs.rmSync(out, { recursive: true, force: true });
    fs.mkdirSync(out, { recursive: true });
    const modules = ['core/track-keyboard', 'core/instrument-play', 'core/health'];
    const { syx, plan } = await prepareRuntimePlanModulesOffline(session.options, session.runDir, out, ['DKRT_BOOT'], {
      modules,
      timeoutMs: 120_000,
    });
    const load = (await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true, auto_no: true, card: session.cardPath })) as Record<string, unknown>;
    console.info('LOAD', JSON.stringify({ start_used: load?.start_used, cache_hit: load?.cache_hit, cache_dir: load?.cache_dir, key: process.env.PARITY_CARD_KEY ?? 'derive' }));
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    await expectRuntimeUi(session, plan, out, { emulatedMs: 180_000 });
    // the boot read the owner's record (a cold boot, or a state booted from this same card: `card.key` above)
    const { decodeSettings, settingsMap, settingsRecord } = await import('@digi/regression/runtime/project-kit.ts');
    const keys = Object.keys(settingsMap(decodeSettings((await settingsRecord(dk)).raw)));
    expect(keys, 'the boot loaded the owner settings record').toEqual(expect.arrayContaining(['canopy/work/v1', 'canopy/preset/1']));
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const sym = await instrumentSymbols(session, plan);
    const cf = async (name: string, index = 0) => {
      const value = await session.client.memPeek(sym(name) + 4 * index);
      if (value === null) throw new Error(`Missing ${name}[${index}]`);
      return value >>> 0;
    };
    await waitInstrumentReady(cf);
    await expectRuntimeUi(session, plan, out, { afterEpoch: epoch, emulatedMs: 180_000 });
    const panel = canopyPanel(session, plan, sym);
    const optional = async (name: string, index = 0) => {
      try {
        return await cf(name, index);
      } catch {
        return null;
      }
    };
    const workState = async () => ({
      applied: await cf('ip_work_info', 0),
      window: await optional('work_window'),
      tracks: await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i))),
    });
    const byte = async (name: string, offset: number) => {
      try {
        const addr = sym(name) + offset;
        const value = await session.client.memPeek((addr & ~3) >>> 0);
        return value === null ? null : (value >>> (24 - 8 * (addr & 3))) & 255;
      } catch {
        return null;
      }
    };
    const beforeChoose = await workState();
    await panel.chooseCanopy(1);
    const afterChoose = await workState();
    await waitEmulated(session, 3000);
    const settled = await workState();
    expect(settled.applied, 'the working copy gave track 1 the owner Sound').toBeGreaterThan(0);
    const report: Record<string, unknown> = {
      config: 'shared',
      modules,
      work_copies_applied: settled.applied,
      work_timeline: { beforeChoose, afterChoose, settled },
    };
    const { tap, edge } = acknowledgedPanel(session);
    const channel = 0; // track 1's Digitakt channel (1), zero-based
    const captures: Capture[] = [];
    const ownership: Record<string, string[]> = {};
    const capture = async (name: string, play: (mark: (n: string) => void) => Promise<void>) => {
      const file = path.join(out, `${name}.f32le`);
      const marks: Mark[] = [];
      const at = () => (fs.existsSync(file) ? Math.floor(fs.statSync(file).size / 8) : 0);
      // who plays track 1 during the capture: every change of [claim0, lanes0] with the capture frame it was seen at
      const seen: string[] = [];
      let watching = true;
      const watcher = (async () => {
        let last = '';
        while (watching) {
          const now = JSON.stringify([await byte('track_keyboard_claim', 0), await byte('track_keyboard_lanes', 0)]);
          if (now !== last) seen.push(`${(at() / RATE).toFixed(2)}s ${now}`);
          last = now;
        }
      })();
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      try {
        await waitEmulated(session, 300);
        await play((n) => marks.push({ name: n, frame: at() }));
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
        watching = false;
        await watcher;
      }
      ownership[name] = seen;
      console.info('OWNER', name, '[claim0, lanes0]', seen.join(' '));
      captures.push({ name, file, marks });
    };
    const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });

    // A chord played on a USB keyboard: C4, E4 and G4 80 ms apart, E4 released after 2 s, the rest after one more second.
    await capture('midi-chord', async (mark) => {
      mark('first_on');
      await midi([0x90 | channel, 60, 100]);
      await waitEmulated(session, 80);
      await midi([0x90 | channel, 64, 90]);
      await waitEmulated(session, 80);
      await midi([0x90 | channel, 67, 110]);
      mark('held');
      await waitEmulated(session, 2000);
      await midi([0x80 | channel, 64, 0]);
      await waitEmulated(session, 1000);
      mark('last_off');
      await midi([0x80 | channel, 60, 0, 0x80 | channel, 67, 0]);
      await waitEmulated(session, 3000);
    });

    // The same chord on the panel keyboard (KEYBOARD, TRIG 9, 13, 16 held together for 2 s).
    await tap('KEYBOARD');
    await capture('panel-chord', async (mark) => {
      mark('first_on');
      for (const key of ['TRIG 9', 'TRIG 13', 'TRIG 16']) await edge(key, true);
      mark('held');
      await waitEmulated(session, 2000);
      mark('last_off');
      for (const key of ['TRIG 9', 'TRIG 13', 'TRIG 16']) await edge(key, false);
      await waitEmulated(session, 3000);
    });
    await tap('KEYBOARD');

    // Four stock note trigs (steps 1, 5, 9, 13) played for two bars at the project's tempo.
    await tap('RECORD');
    for (const step of [1, 5, 9, 13]) await tap(`TRIG ${step}`);
    await tap('RECORD');
    await capture('sequencer', async (mark) => {
      mark('first_on');
      await tap('PLAY');
      mark('held');
      await waitEmulated(session, 4000);
      mark('last_off');
      await tap('STOP');
      await waitEmulated(session, 3000);
    });

    report.captures = captures.map(analyse);
    report.ownership = ownership;
    fs.writeFileSync(path.join(out, 'report.json'), JSON.stringify(report, null, 2));
    console.info('PARITY', JSON.stringify(report));
    for (const c of report.captures as ReturnType<typeof analyse>[]) expect(c.frames, `${c.name} was captured`).toBeGreaterThan(RATE);
  });
});
