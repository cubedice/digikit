import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, f2w, w2f } from '@digi/sharc-rig-kit';

it('reconstructs a recorded stereo take at reduced rates through storage and loop boundaries', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const storage = 'modules/core/adapters/relay-storage/src/dsp';
  const tapeDsp = 'modules/core/adapters/relay-track/src/dsp';
  const files = [storage + '/storage.s', storage + '/pool.s', tapeDsp + '/audio.s', tapeDsp + '/engine.s'];
  const external = 0x20110000;
  const command = external + 512;
  const status = command + 256;
  const timing = status + 768;
  const rig = await RawRig.open({
    objects: files.map((file, i) => ({
      text: readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n'),
      includes: [path.join(root, 'sdk/dsp/contract'), path.join(root, tapeDsp)],
      base: 0xb90000 + i * 0x2000,
    })),
    externs: {
      _relay_external: external,
      _relay_main: external + 10240,
      _rtm_command: command,
      _relay_status: status,
      _relay_request: status + 256,
      _relay_response: status + 512,
      _relay_timing: timing,
      _pattern_position_info: status + 1024,
      _rtm_bus: external + 4096,
      _rtm_mod_frame: external + 12288,
      _tk_pool_head_owner: 0,
      _mi_envelope_records: 0,
    },
    returnSw: 0xb9f100,
  });
  const track = 3;
  const tape = rig.data._rtm_tapes! / 4 + track * 128;
  const trackRow = rig.data._rtm_tracks! / 4 + track * 16;
  const frames = 513; // The last cell is alone in its storage group.
  const q16 = 65536;
  const wrap = (frame: number) => ((frame % frames) + frames) % frames;
  const signal = (channel: number, frame: number) =>
    0.6 * Math.sin((2 * Math.PI * (channel === 0 ? 71 : 93) * frame) / frames + channel * 0.35);
  const pcm = (channel: number, frame: number) => Math.round(signal(channel, wrap(frame)) * 32768) / 32768;
  let sequence = 0;
  async function block(op = 0, source = 0, rate = 0, arg = 0, end = 0) {
    [0, ++sequence, op, track, source, rate, arg, end].forEach((v, i) => rig.poke(command / 4 + i, v >>> 0));
    const result = await rig.call('_rtm_block', { M6: 1, M5: 0 }, { maxSteps: 300000, reads: [[tape + 32, 64]] });
    expect(result.halt, `command ${op}, PC ${result.final_pc.toString(16)}`).toBe('breakpoint');
    return result.reads[0]!.map((word) => w2f(word!));
  }
  const setRate = (rate: number) => block(11, (100 << 5) | (100 << 19), rate, q16);
  try {
    // Initialize the input and link mailboxes, including the transport header.
    for (let i = 0; i < 480; ++i) rig.poke(external / 4 + i, 0);
    rig.poke(timing / 4 + 1, frames);
    await setRate(q16);
    for (let start = 0; start < frames; start += 32) {
      for (let i = 0; i < 64; ++i) rig.poke(external / 4 + i, f2w(pcm(i >> 5, start + (i & 31))));
      if (start === 0) await block(1, 0, 0, frames, 16);
      else await block();
    }
    expect(await rig.peek(trackRow, 1)).toEqual([frames]);
    for (let i = 0; i < 64; ++i) rig.poke(external / 4 + i, 0);

    for (const rate of [q16, q16 / 2, -q16 / 2, 45875, -20000, q16 / 32, -q16 / 32]) {
      await setRate(rate);
      let playbackError = 0;
      let linearError = 0;
      for (const boundary of [256, frames]) {
        const nearBoundary = Math.abs(rate) === q16 / 32;
        const start = nearBoundary ? boundary - 0.5 : boundary + (rate < 0 ? 4 : -4) + (rate === q16 ? 0 : 0.5);
        rig.poke(trackRow + 1, wrap(Math.floor(start)));
        rig.poke(tape + 2, (start % 1) * q16); // fractional phase
        rig.poke(tape + 10, 0); // omit the rate-change fade from the comparison
        const audio = await block();
        for (let i = 0; i < audio.length; ++i) {
          const frame = start + ((i & 31) * rate) / q16;
          const index = Math.floor(frame);
          const t = frame - index;
          const [a, b, c, d] = [-1, 0, 1, 2].map((offset) => pcm(i >> 5, index + offset));
          const cubic =
            b! + 0.5 * t * (c! - a! + t * (2 * a! - 5 * b! + 4 * c! - d! + t * (3 * (b! - c!) + d! - a!)));
          expect(audio[i]!, `rate ${rate}, frame ${frame}, channel ${i >> 5}`).toBeCloseTo(cubic, 6);
          const reference = signal(i >> 5, frame);
          playbackError += (audio[i]! - reference) ** 2;
          linearError += (b! + t * (c! - b!) - reference) ** 2;
        }
      }
      if (rate !== q16) expect(Math.sqrt(playbackError / linearError)).toBeLessThan(0.4);
    }
  } finally {
    await rig.close();
  }
});
