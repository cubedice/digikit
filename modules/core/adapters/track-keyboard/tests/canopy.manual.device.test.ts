import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';

describeOn('dt2', 'CANOPY shared track keyboard', () => {
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const card = fs.mkdtempSync(path.join(fixtureRoot, 'track-keyboard-canopy-'));
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(card, name));
    await seedModules(card, 'dt2', [{ file: moduleArtifact('dt2', 'digikit/canopy'), needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(card);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('track-keyboard-canopy-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const handle = withSession({ ...options, card, dsp: 'sharc', restart: false });

  test('inherits legacy performance until a TRIG edit and then owns keyboard input', { timeout: 420_000 }, async () => {
    const { session } = handle;
    const output = fs.mkdtempSync(path.join(fixtureRoot, 'tk-canopy-ui-'));
    const { syx, plan } = await prepareRuntimePlanModulesOffline(
      session.options,
      session.runDir,
      output,
      [
        'DKRT_BOOT',
        'ip_control',
        'ip_tracks',
        'ip_admission',
        'ip_src_debug',
        'track_keyboard_src',
        'track_keyboard_lanes',
        'tk_panel_claim_hits',
      ],
      { modules: ['core/track-keyboard', 'core/digikit-menu', 'core/health'], timeoutMs: 90_000 },
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
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const cf = async (name: string, index = 0) => {
      const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
      if (value === null) throw new Error(`Missing ${name}[${index}]`);
      return value >>> 0;
    };
    const byte = async (name: string) => {
      const addr = planSymbol(plan, name);
      const value = await session.client.memPeek(addr & ~3);
      if (value === null) throw new Error(`Missing ${name}`);
      return (value >>> (24 - 8 * (addr & 3))) & 255;
    };
    await waitInstrumentReady(cf);
    await expectRuntimeUi(session, plan, output, { afterEpoch: epoch, emulatedMs: 180_000 });
    const { edge, tap, turn } = acknowledgedPanel(session);
    await edge('FUNC', true);
    try {
      await tap('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; ++i) await tap('DOWN');
    await tap('YES');
    await tap('YES');
    await pollEmulated(
      session,
      () => byte('track_keyboard_src'),
      (n) => n === 1,
      {
        emulatedMs: 15000,
        message: 'CANOPY compact SRC view is registered',
      },
    );
    expect(await byte('track_keyboard_lanes')).toBe(0);
    await tap('SRC');
    await tap('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(0); // Velvet and Reed need only SOURCE.
    fs.copyFileSync(await captureRemoteOled(session, 'tk-canopy-source'), path.join(output, 'source.png'));
    await tap(1);
    await tap(1);
    await tap(1);
    await turn(1, 12);
    await pollEmulated(
      session,
      () => byte('track_keyboard_lanes'),
      (n) => n === 3,
      {
        emulatedMs: 5000,
        message: 'first performance edit hands both lanes to shared keyboard',
      },
    );
    const before = await cf('tk_panel_claim_hits');
    await tap('KEYBOARD');
    await tap('TRIG 9');
    await pollEmulated(
      session,
      () => cf('tk_panel_claim_hits'),
      (n) => n === before + 1,
      {
        emulatedMs: 4000,
        message: 'the shared owner takes the CANOPY panel note',
      },
    );
    await session.expectNoFault('CANOPY track keyboard handoff');
  });
});
