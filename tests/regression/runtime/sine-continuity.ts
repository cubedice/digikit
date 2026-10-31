/** Predict a settled sine from a source-only prefix. Never refit at a transition:
 * a dropped sample or phase restart must remain visible in the destination. */
export function sineContinuity(raw: Buffer, trainingFrames = 8192) {
  const frames = raw.length / 8;
  if (!Number.isInteger(frames) || trainingFrames < 8 || frames <= trainingFrames) {
    throw new Error('expected stereo f32 PCM with a training prefix and destination samples');
  }
  return [0, 1].map((channel) => {
    const x = Array.from({ length: frames }, (_, i) => raw.readFloatLE(i * 8 + channel * 4));
    if (x.some((sample) => !Number.isFinite(sample))) throw new Error('non-finite PCM');
    let energy = 0;
    let correlation = 0;
    for (let i = 1; i < trainingFrames - 1; i++) {
      energy += x[i]! ** 2;
      correlation += x[i]! * (x[i - 1]! + x[i + 1]!);
    }
    const coefficient = correlation / energy;
    const omega = Math.acos(coefficient / 2);
    if (!(energy > 0) || !Number.isFinite(omega) || omega <= 0 || omega >= Math.PI) {
      throw new Error('training prefix is not an oscillating signal');
    }
    let cc = 0,
      ss = 0,
      cs = 0,
      xc = 0,
      xs = 0;
    for (let i = 0; i < trainingFrames; i++) {
      const c = Math.cos(omega * i),
        s = Math.sin(omega * i);
      cc += c * c;
      ss += s * s;
      cs += c * s;
      xc += x[i]! * c;
      xs += x[i]! * s;
    }
    const determinant = cc * ss - cs * cs;
    const cosine = (xc * ss - xs * cs) / determinant;
    const sine = (xs * cc - xc * cs) / determinant;
    const amplitude = Math.hypot(cosine, sine);
    let maxError = 0,
      maxErrorFrame = 0,
      maxResidual = 0;
    for (let i = 0; i < frames; i++) {
      const error = Math.abs(x[i]! - cosine * Math.cos(omega * i) - sine * Math.sin(omega * i));
      if (error > maxError) {
        maxError = error;
        maxErrorFrame = i;
      }
      if (i > 0 && i + 1 < frames) {
        maxResidual = Math.max(maxResidual, Math.abs(x[i + 1]! - coefficient * x[i]! + x[i - 1]!));
      }
    }
    return {
      channel,
      frames,
      trainingFrames,
      frequency: (omega * 48000) / (2 * Math.PI),
      amplitude,
      maxErrorFrame,
      relativeError: maxError / amplitude,
      relativeResidual: maxResidual / amplitude,
    };
  });
}
