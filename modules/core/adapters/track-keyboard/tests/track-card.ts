import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll } from 'vitest';
import { seedServices } from '@digi/regression/runtime/card-seed.ts';

/**
 * The card of a track keyboard proof: the files of the card `options` names (the DT2 sample card) and `services/instrument.dks`. The track
 * keyboard's keyboard and MIDI paths run through the code of core/instrument-play in that service file, which the image asks the boot for
 * itself (`boot_need_instrument`, docs/plans/fork-image-diet.md section 9): no module of these proofs needs it. Call it at describe level,
 * before `withSession`, whose session then starts on the seed.
 */
export function trackKeyboardCard<T extends { card?: string }>(options: T): T {
  const root = fs.realpathSync(path.join(repoRoot(), 'out'));
  const seed = fs.mkdtempSync(path.join(root, 'track-keyboard-card-'));
  beforeAll(async () => {
    if (options.card) fs.cpSync(options.card, seed, { recursive: true });
    await seedServices(seed, 'dt2', ['instrument']);
  });
  afterAll(() => {
    const owned = fs.realpathSync(seed);
    if (path.dirname(owned) !== root || !path.basename(owned).startsWith('track-keyboard-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  return { ...options, card: seed };
}
