import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { installModule } from './install.ts';
import { liveProtocol } from './protocol.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('reset-proof image boots and its explicit diagnostic loads without resetting DSP', async () => {
  const image = path.join(repoRoot(), 'out/patch/overloop-reset-proof-dt2.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  const hash = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(hash);
  const { session } = h;
  const loaded = await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  expect(loaded.syx_sha256).toBe(hash);
  await session.client.call('run.mode', { mode: 'free' });
  const dk = liveProtocol(session);
  onTestFinished(() => dk.cancelPending());
  await dk.waitForBoot();
  expect(await session.client.memPeek(planSymbol(plan, 'dl_reset_hw'))).toBe(0);
  await installModule(dk, 'test/dsp-reset-probe', path.join(repoRoot(), 'out/patch/dsp-reset-probe-dt2.dkm'));
  const reply = await dk.request(0x3f);
  expect(reply.status).toBe(0);
  expect(reply.body.length).toBe(96);
  expect((await dk.request(0x3f, Buffer.from('bad'))).status).toBe(-77);
  expect((await dk.request(0x3f, Buffer.from('RLSE'))).status).toBe(-81);
  expect(await session.client.memPeek(planSymbol(plan, 'dl_reset_hw'))).toBe(0);
  expect(await session.client.memPeek(planSymbol(plan, 'dl_link') + 84)).toBe(0);
  await session.expectNoFault('reset diagnostic boot and load');
}, 240_000);
