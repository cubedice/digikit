import { withinRuntimeDeadline, type Session } from '@digi/test-support';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

/** Navigate an explicitly named core/ui menu through real acknowledged keys.
 * dk_ui_menu.obj is word 1 (core/ui/src/ui.c). The native Menu selection and
 * item vector are shared by VerticalMenuView and CompactVerticalMenuView;
 * View manager/closing match dk_ui_is_open (finding 71 and dk_fw_menu.h).
 * This reads guest state only; it never sets the selection or dispatches actions. */
export function menuSelection(
  session: { client: Pick<Session['client'], 'memPeek'> },
  plan: RuntimePlan,
  owner: string,
  tap: (name: string) => Promise<void>,
) {
  const ownerPlan = {
    ...plan,
    symbols: Object.fromEntries(Object.entries(plan.symbols).filter(([name]) => name.startsWith(`${owner}@`))),
  };

  return async (symbol: string, target: number): Promise<void> => {
    const pointer = planSymbol(ownerPlan, symbol);
    const read = async () => {
      const deadline = Date.now() + 30_000;
      const word = async (address: number) => {
        const value = await withinRuntimeDeadline(deadline, () => session.client.memPeek(address));
        if (value === null) throw new Error(`Menu ${symbol}: unmapped memory at ${address.toString(16)}`);
        return value >>> 0;
      };
      const menu = await word(pointer);
      if (!menu || menu % 4) throw new Error(`Menu ${symbol} has not been created`);
      const object = await word(menu + 4);
      if (!object || object % 4) throw new Error(`Menu ${symbol} has no native object`);
      const [begin, end, selected, manager, flags] = await Promise.all(
        [4, 8, 0x10, 0x38 + 0x2c, 0x38 + 0x30].map((offset) => word(object + offset)),
      );
      const count = (end! - begin!) / 8; // shared_ptr<MenuItem> is two words.
      if (!manager || flags! >>> 24) throw new Error(`Menu ${symbol} is not open`);
      if (!Number.isInteger(count) || count < 1 || count > 4096 || selected! >= count)
        throw new Error(`Menu ${symbol} has invalid rows: ${JSON.stringify({ begin, end, selected })}`);
      return { object, selected: selected!, count };
    };
    let current = await read();
    if (!Number.isInteger(target) || target < 0 || target >= current.count)
      throw new Error(`Menu ${symbol}: row ${target} is outside 0..${current.count - 1}`);
    while (current.selected !== target) {
      const next = current.selected + (target > current.selected ? 1 : -1);
      await tap(next > current.selected ? 'DOWN' : 'UP');
      const after = await read();
      if (after.object !== current.object || after.selected !== next)
        throw new Error(`Menu ${symbol}: expected row ${next}, observed ${after.selected}`);
      current = after;
    }
  };
}
