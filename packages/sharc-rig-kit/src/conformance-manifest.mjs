// Discovery shared by the rig suite and the Nx projects that run its fixed cases.
export const CONFORMANCE_ROOTS = ['modules', 'runtime', 'services', 'image-modules', 'tests/fixtures/modules'];

/** @type {Record<string, [string, string, string, string, string]>} */
export const NEGATIVE_VARIANTS = {
  'test/dyn-guard-nan#overrun': [
    'src/dsp/overrun.s',
    '_ov_render',
    '_ov_note_on',
    '_ov_desc',
    'wrote memory it does not own',
  ],
};

export const dspSourcePaths = (manifest, device) =>
  (manifest.object ?? [])
    .filter((object) => !object.devices || object.devices.includes(device))
    .flatMap((object) =>
      (object.sources ?? []).filter((source) => source.endsWith('.s') && source.split('/').includes('dsp')),
    );

/**
 * A module the rig does not run: `[conformance] rig_skip = "<why>"` (its DSP reads data the rig does not place; its own tests run it) or a
 * persistent V2 instrument (`instrument = true` on a machine: no voice). spec.ts gives no spec for either, so no fixed conformance
 * target is made for them (it would find no module).
 */
function rigSkips(manifest) {
  const why = manifest.conformance?.rig_skip;
  if (why !== undefined && (typeof why !== 'string' || !why.trim()))
    throw new Error(`${manifest.module.id}: [conformance] rig_skip names why the rig does not run the module`);
  return why !== undefined || !!manifest.machine?.some((machine) => machine.instrument === true);
}

/** The two rig suites already exclude these runtime fault and commit fixtures. */
export function conformanceKind(manifest) {
  const id = manifest.module.id;
  if (id.startsWith('core/') || rigSkips(manifest)) return null;
  if (
    id !== 'test/commit-demo' &&
    ((manifest.module.type === 'machine' && manifest.machine?.length) ||
      manifest.entry?.some((entry) => entry.registry === 'voices' && typeof entry.pc === 'string'))
  )
    return 'machine';
  if (id !== 'test/dyn-fx-hang' && manifest.module.type === 'fx' && manifest.fx?.length) return 'fx';
  return null;
}

export const negativeVariantsOf = (id) =>
  Object.keys(NEGATIVE_VARIANTS).filter((variant) => variant.startsWith(id + '#'));
