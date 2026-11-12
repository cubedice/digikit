import { withinRuntimeDeadline } from '@digi/test-support';

export interface InstrumentLoadProgress {
  file: number[];
  link: number[];
}

/** Wait for publication while the image/zero-fill transport makes progress.
 * Firmware owns its UI-tick deadlines; this watchdog detects a stuck
 * interpreter or RPC. The enclosing test supplies the total wall-time bound. */
export async function waitInstrumentReady(
  word: (symbol: string, index: number) => Promise<number>,
  { hangMs = 60_000, pollMs = 1000 } = {},
): Promise<InstrumentLoadProgress> {
  let last: InstrumentLoadProgress | undefined;
  let marker: string | undefined;
  let advanced = Date.now();
  for (;;) {
    if (last && Date.now() - advanced >= hangMs) {
      throw new Error(`Instrument load made no progress for ${hangMs} ms: ${JSON.stringify(last)}`);
    }
    let current: InstrumentLoadProgress;
    try {
      current = await withinRuntimeDeadline(advanced + hangMs, async () => {
        const [file, link] = await Promise.all([
          Promise.all(Array.from({ length: 4 }, (_, i) => word('DKRT_INSTRUMENT_FILE', i))),
          Promise.all(Array.from({ length: 24 }, (_, i) => word('dl_link', i))),
        ]);
        return { file, link };
      });
    } catch (cause) {
      throw new Error(`Instrument load progress read failed: ${JSON.stringify(last)}`, { cause });
    }
    last = current;
    if (current.file[0] === 3) return current;
    if (current.file[0] === 9 || (current.file[0] === 6 && (current.file[2]! | 0) < 0)) {
      throw new Error(`Instrument load failed: ${JSON.stringify(current)}`);
    }
    // DlLink words: accepted request identity, acknowledged chunk count, END
    // frames remaining. Individual 256-byte writes reset their chunk counters.
    const next = JSON.stringify([current.file[0], current.link[18], current.link[8], current.link[15]]);
    if (next !== marker) {
      marker = next;
      advanced = Date.now();
    }
    await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, advanced + hangMs - Date.now())));
  }
}
