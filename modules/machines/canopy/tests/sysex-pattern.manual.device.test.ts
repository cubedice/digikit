import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { canopyPanel } from './canopy-panel.ts';

describeOn('dt2', 'CANOPY pattern SysEx on the DT2', () => {
  // MANUAL, NOT IN THE GATE: blocked by the emulator (finding 151). CANOPY Sound
  // persistence through a pattern SysEx dump. DTII 1.17 sends a project or a
  // pattern (with its kit), not a single Sound, so the pattern holding two CANOPY
  // tracks is sent over USB with the stock SYSEX SEND, both tracks are changed,
  // and the captured dump is received back into the same pattern with the stock
  // SYSEX RECEIVE. Today the send stops after 4032 bytes because the emulator does
  // not model USB transmit completion; the test fails at the incomplete dump. Run
  // by name once USB transmit is modelled:
  //   pnpm --filter @digi/regression exec vitest run --project runtime ../../modules/machines/canopy/tests/sysex-pattern.manual.device.test.ts
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-sysex-card-'));
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
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-sysex-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  /** The SysEx messages (F0 ... F7) in a captured USB MIDI stream; other MIDI is dropped. */
  function sysexOnly(stream: number[]): Buffer {
    const out: number[] = [];
    let inside = false;
    for (const byte of stream) {
      if (byte === 0xf0) inside = true;
      if (inside) out.push(byte);
      if (byte === 0xf7) inside = false;
    }
    return Buffer.from(out);
  }

  test('CANOPY settings travel with the Sound through a pattern SysEx dump', { timeout: 1_200_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-sysex', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    console.info('CANOPY SysEx evidence:', artifacts);
    const log: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(log, null, 2)));
    const { syx, plan } = await prepareRuntimePlanListedOffline(
      session.options,
      session.runDir,
      artifacts,
      ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_src_debug', 'src_state', 'ip_sa_info', 'ip_sa_stats'],
      { modules: ['core/instrument-play', 'core/digikit-menu', 'core/health'] },
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
    const p = canopyPanel(session, plan);
    await waitInstrumentReady(p.cf);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: firstEpoch, timeout: 180_000 });

    await p.chooseCanopy(1);
    const a = await p.edit(1, 20, 12, 16);
    await p.chooseCanopy(2);
    const b = await p.edit(2, 12, 8, 8);
    expect(b).not.toEqual(a);
    log.sent = { a, b };

    // With this image the firmware sends the dump on USB at once (no SELECT
    // INTERFACE). midi.usb.out drains what it hands its USB transmit function:
    // {bytes: count, hex}; the first call starts the capture. The emulator does not
    // model the USB peripheral, and the send stops after 4032 bytes (finding 151).
    const usbOut = async () =>
      [...Buffer.from(((await session.client.call('midi.usb.out')) as { hex: string }).hex, 'hex')];
    await usbOut();
    await p.sysexSendPattern();
    const stream: number[] = [];
    let quiet = 0;
    for (let i = 0; i < 240 && quiet < 6; i++) {
      await waitEmulated(session, 500);
      const got = await usbOut();
      stream.push(...got);
      quiet = got.length === 0 && stream.length > 0 ? quiet + 1 : 0;
    }
    const dump = sysexOnly(stream);
    fs.writeFileSync(path.join(artifacts, 'pattern-a01.syx'), dump);
    const messages = dump.filter((byte) => byte === 0xf0).length;
    log.dump = {
      streamBytes: stream.length,
      sysexBytes: dump.length,
      messages,
      sha256: createHash('sha256').update(dump).digest('hex'),
    };
    await p.shot('sysex-send-done');
    expect(messages, 'SYSEX SEND produced a pattern dump').toBeGreaterThan(0);
    expect(dump.at(-1), 'the dump ends with a complete message').toBe(0xf7);
    await p.leaveMenus();
    log.afterSend = await p.persistence();

    // Change both tracks, then receive the dump back into pattern A01.
    const a2 = await p.edit(1, 0, 0, 10);
    const b2 = await p.edit(2, 0, 4, 10);
    expect(a2).not.toEqual(a);
    expect(b2).not.toEqual(b);
    log.changed = { a2, b2 };
    await p.sysexReceivePattern();
    await session.client.call('midi.usb.in', { bytes: dump.toString('hex'), chunk: 384, period: 16_500 });
    await waitEmulated(session, 3000);
    await p.shot('sysex-receive-done');
    log.afterReceive = { persistence: await p.persistence(), values: [await p.sound(1), await p.sound(2)] };
    await p.leaveMenus();
    await expect.poll(() => p.sound(1), { timeout: 30_000, message: 'track 1 after receiving the dump' }).toEqual(a);
    await expect.poll(() => p.sound(2), { timeout: 30_000, message: 'track 2 after receiving the dump' }).toEqual(b);
    await session.expectNoFault('CANOPY pattern SysEx send and receive');
  });
});
