/** Fit simultaneous sine voices against one absolute sample clock. Including
 * released/absent voices in the basis prevents their energy leaking into the
 * held-note measurements. Compare coefficients across windows, never phases
 * relative to the start of each window. */
export function chordWindow(raw: Buffer, start: number, frames: number, notes = [60, 64, 67]) {
  if (
    raw.length % 8 ||
    !Number.isInteger(start) ||
    !Number.isInteger(frames) ||
    start < 0 ||
    frames < 2048 ||
    (start + frames) * 8 > raw.length
  ) {
    throw new Error('invalid stereo PCM window');
  }
  const frequencies = notes.map((note) => 440 * 2 ** ((note - 69) / 12));
  const size = 1 + notes.length * 2;
  return [0, 1].map((channel) => {
    const matrix = Array.from({ length: size }, () => Array<number>(size + 1).fill(0));
    for (let frame = start; frame < start + frames; frame++) {
      const sample = raw.readFloatLE(frame * 8 + channel * 4);
      if (!Number.isFinite(sample)) throw new Error('non-finite PCM');
      const basis = [
        1,
        ...frequencies.flatMap((frequency) => {
          const angle = (2 * Math.PI * frequency * frame) / 48000;
          return [Math.cos(angle), Math.sin(angle)];
        }),
      ];
      for (let row = 0; row < size; row++) {
        for (let col = 0; col < size; col++) matrix[row]![col]! += basis[row]! * basis[col]!;
        matrix[row]![size]! += basis[row]! * sample;
      }
    }
    for (let col = 0; col < size; col++) {
      let pivot = col;
      for (let row = col + 1; row < size; row++)
        if (Math.abs(matrix[row]![col]!) > Math.abs(matrix[pivot]![col]!)) pivot = row;
      [matrix[col], matrix[pivot]] = [matrix[pivot]!, matrix[col]!];
      const divisor = matrix[col]![col]!;
      if (Math.abs(divisor) < 1e-10) throw new Error('singular tone basis');
      for (let j = col; j <= size; j++) matrix[col]![j]! /= divisor;
      for (let row = 0; row < size; row++) {
        if (row === col) continue;
        const factor = matrix[row]![col]!;
        for (let j = col; j <= size; j++) matrix[row]![j]! -= factor * matrix[col]![j]!;
      }
    }
    return notes.map((note, i) => {
      const cosine = matrix[1 + i * 2]![size]!;
      const sine = matrix[2 + i * 2]![size]!;
      return { note, cosine, sine, amplitude: Math.hypot(cosine, sine) };
    });
  });
}

export function voiceChange(
  before: { cosine: number; sine: number; amplitude: number },
  after: { cosine: number; sine: number },
) {
  if (!(before.amplitude > 1e-6)) throw new Error('reference voice is silent');
  return Math.hypot(after.cosine - before.cosine, after.sine - before.sine) / before.amplitude;
}
