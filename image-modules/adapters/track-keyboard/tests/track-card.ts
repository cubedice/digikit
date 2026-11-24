import fs from 'node:fs';
import path from 'node:path';
import { repoRoot, type Session } from '@digi/test-support';
import { afterAll, beforeAll } from 'vitest';
import { seedServices } from '@digi/regression/runtime/card-seed.ts';
import { loadedServiceSymbols } from '@digi/regression/runtime/loaded-service-symbols.ts';
import type { RuntimePlan } from '@digi/regression/runtime/runtime-plan.ts';

/**
 * The card of a track keyboard proof: the files of the card `options` names (the DT2 sample card) and `services/track-keyboard.dks`. The
 * track keyboard's player runs from that service file, which the image asks the boot for itself (`boot_need_track_keyboard`,
 * docs/plans/fork-image-diet.md section 9.2): no module of these proofs needs it. Call it at describe level, before `withSession`, whose
 * session then starts on the seed.
 */
export function trackKeyboardCard<T extends { card?: string }>(options: T): T {
  const root = fs.realpathSync(path.join(repoRoot(), 'out'));
  const seed = fs.mkdtempSync(path.join(root, 'track-keyboard-card-'));
  beforeAll(async () => {
    if (options.card) fs.cpSync(options.card, seed, { recursive: true });
    await seedServices(seed, 'dt2', ['track-keyboard']);
  });
  afterAll(() => {
    const owned = fs.realpathSync(seed);
    if (path.dirname(owned) !== root || !path.basename(owned).startsWith('track-keyboard-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  return { ...options, card: seed };
}

/**
 * The name lookup of a track keyboard proof after a boot: the player's state (`tk_native_audio_active`, `tk_sequence_audio_hits`, the
 * statics of the pool admission, ...) is in services/track-keyboard.dks, the pattern storage (`tk_storage_info`) and everything else in the
 * image. Waits until the boot has loaded the service; `also` names further services to look in (CANOPY's `instrument`).
 */
export function trackKeyboardSymbols(
  session: Session,
  plan: RuntimePlan,
  also: string[] = [],
): Promise<(name: string) => number> {
  return loadedServiceSymbols(session, plan, ['track-keyboard', ...also]);
}
