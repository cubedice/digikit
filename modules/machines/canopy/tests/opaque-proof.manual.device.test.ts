import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { planSymbol, prepareRuntimePlanOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';

// Never register for hardware. A native project load supplies a valid stock
// Sound record, then four isolated synthetic Sound load/save calls use it.
// This is not a project roundtrip or a complete persistence implementation.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_OPAQUE_PROOF === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const h = withSession({ ...options, dsp: 'sharc', restart: false });
  test('opaque Sound bytes survive stock serialization with CANOPY absent', { timeout: 600_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-opaque-proof', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanOffline(session.options, session.runDir, artifacts,
      ['DKRT_BOOT', 'canopy_opaque_result'],
      { modules: ['core/digikit-menu', 'core/health', 'test/canopy-opaque-proof'] });
    await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
      auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, plan, artifacts, { timeout: 180_000 });
    evidence.modules = await liveProtocol(session).modules();
    expect((evidence.modules as { name: string }[]).map((m) => m.name)).not.toContain('digikit/canopy');
    const word = async (index: number) => {
      const v = await session.client.memPeek(planSymbol(plan, 'canopy_opaque_result') + 4 * index);
      if (v === null) throw new Error(`Unreadable proof word ${index}`);
      return v >>> 0;
    };
    const { tap, edge } = acknowledgedPanel(session);
    // Save a disposable project so the stock load path has an actual record.
    await edge('FUNC', true);
    try { await tap('SETTINGS'); } finally { await edge('FUNC', false); }
    await waitEmulated(session, 300);
    await tap('YES');
    await waitEmulated(session, 3500);
    await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_opaque_result'), value: 1 });
    await tap('SETTINGS');
    await waitEmulated(session, 300);
    for (let i = 0; i < 3; ++i) { await tap('YES'); await waitEmulated(session, 500); }
    await tap('YES');
    await expect.poll(() => word(1), { timeout: 180_000 }).toBe(1);
    evidence.result = await Promise.all(Array.from({ length: 8 }, (_, i) => word(i)));
    expect(evidence.result).toEqual([0, 1, 0, 31, 31, 31, 31, 0]);
    await session.expectNoFault('synthetic opaque Sound roundtrip');
  });
} else {
  test.skip('CANOPY opaque Sound proof is opt-in', () => {});
}
