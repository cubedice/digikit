import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { button, encoder } from '@digi/test-support';
import { expect } from 'vitest';
import { installModule, liveArtifacts, moduleArtifact, runtimeTests, settleUi, tap } from './live.ts';
import { planSymbol, prepareRuntimePlan } from './runtime-plan.ts';

runtimeTests(
  'Overloop display follows capture, live window edits, and page visibility',
  async ({ session, dk }) => {
    await settleUi(session);
    const artifacts = liveArtifacts(session, 'overloop-display');
    const plan = await prepareRuntimePlan(session, artifacts, ['loop_ui_snapshot', 'loop_ui_paints']);
    await installModule(dk, 'digikit/overloop', moduleArtifact('dt2', 'digikit/overloop'));
    await delay(3000);
    const base = planSymbol(plan, 'loop_ui_snapshot');
    const word = async (index: number) => (await session.client.memPeek(base + index * 4))!;
    const paints = () => session.client.memPeek(planSymbol(plan, 'loop_ui_paints'));
    const key = (name: string) => tap((down) => session.client.inputButton(button(name), down), 150, 400);
    const shot = async (name: string) => {
      const frame = await session.client.framebufferGet({ hash: true, data: true });
      fs.writeFileSync(path.join(artifacts, `${name}.json`), JSON.stringify(frame));
      const snapshot = [];
      for (let i = 0; i < 16; i++) snapshot.push(await word(i));
      fs.writeFileSync(path.join(artifacts, `${name}-status.json`), JSON.stringify(snapshot));
      return frame.sha256;
    };
    const turnUntil = async (name: string, delta: number, reached: () => Promise<boolean>) => {
      for (let i = 0; i < 40 && !(await reached()); i++) {
        await session.client.inputTurn(encoder(name), delta);
        await delay(200);
      }
      if (!(await reached())) await shot(`${name}-timeout`);
      expect(await reached(), `${name} reaches its DSP parameter`).toBe(true);
    };
    await key('STOP');
    await session.client.inputButton(button('FUNC'), true);
    try {
      await key('FX');
    } finally {
      await session.client.inputButton(button('FUNC'), false);
    }
    for (let i = 0; i < 4; i++) await key('UP');
    for (let i = 0; i < 3; i++) await key('DOWN');
    await expect.poll(paints, { timeout: 10_000 }).toBeGreaterThan(0);
    expect(await word(0)).toBe(0x4f564c31);
    expect(await word(2)).toBe(0);
    const empty = await shot('empty');
    // A short phrase exercises the UI without spending several
    // minutes interpreting the stock fixture's 128-step cycle.
    await turnUntil('ENC C', 8, async () => (await word(15)) >= 8);
    await turnUntil('ENC A', 8, async () => (await word(2)) === 3);
    expect(await shot('armed')).not.toBe(empty);
    await key('PLAY');
    await expect.poll(() => word(2), { timeout: 10_000, interval: 100 }).toBe(1);
    await shot('recording');
    await expect.poll(() => word(2), { timeout: 120_000, interval: 200 }).toBe(2);
    await key('STOP');
    expect(await word(3)).toBeGreaterThan(32);
    const frames = await word(3);
    const recorded = await shot('recorded');
    await turnUntil('ENC C', -4, async () => (await word(15)) <= 2);
    if (!(await word(15))) await turnUntil('ENC C', 1, async () => (await word(15)) > 0);
    await expect.poll(() => word(13), { timeout: 5000 }).toBeLessThan(frames);
    await turnUntil('ENC B', 1, async () => (await word(14)) > 0);
    await expect.poll(() => word(12), { timeout: 5000 }).toBeGreaterThan(0);
    await turnUntil('ENC D', 8, async () => (await word(6)) >= 0x3f7f0000);
    expect(await shot('window-loop-side')).not.toBe(recorded);
    const moving = await shot('cursor-1');
    await expect.poll(() => shot('cursor-2'), { timeout: 5000, interval: 130 }).not.toBe(moving);
    await key('UP');
    const hidden = await paints();
    await delay(1200);
    expect(await paints()).toBe(hidden);
    await key('DOWN');
    await expect.poll(paints, { timeout: 5000 }).toBeGreaterThan(hidden!);
    await session.expectNoFault('Overloop performance display');
  },
  { devices: ['dt2'], patchSet: 'overloop', timeout: 360_000 },
);
