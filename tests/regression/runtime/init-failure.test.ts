import fs from 'node:fs';
import { LOG_KIND } from '@digi/dk-session/gen_proto';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { moduleArtifact } from './install.ts';
import { waitEmulated } from './waits.ts';

// CF publication and command ownership need the real loader, but no DSP code.
const handle = withSession({ ...liveOptions('dt2'), dsp: 'zero', runtime: {}, restart: false });

test('failed initialization removes service handlers before freeing the module', async () => {
  const session = handle.session;
  const dk = liveProtocol(session);
  await waitEmulated(session, 12_000);
  const bytes = fs.readFileSync(moduleArtifact(session.options.device, 'test/svc-init-fail'));
  const storePath = 'modules/svc-init-fail.dkm';
  await dk.put(storePath, bytes);
  expect(await dk.verifyFile(storePath, bytes)).toBe(true);
  await dk.select([storePath]);
  await dk.restart();
  await dk.waitForBoot();
  const events = await dk.readLog();
  expect(
    events.some((event) => event.kind === LOG_KIND.MOD_LOAD && event.args[1] === 0xfffffff7),
    'boot reports E_INIT',
  ).toBe(true);
  expect((await dk.modules()).map((module) => module.name)).not.toContain('test/svc-init-fail');
  expect((await dk.request(0x3e)).status, 'failed init leaves no callable handler').toBe(-60);
  await session.expectNoFault('failed initialization cleanup');
}, 360_000);
