import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { afterAll, expect, it } from 'vitest';
import { packSourceInputs } from '../pack-inputs.mjs';

const root = mkdtempSync(join(tmpdir(), 'digikit-pack-inputs-'));
afterAll(() => rmSync(root, { recursive: true, force: true }));
const write = (file: string, text: string) => {
  mkdirSync(dirname(join(root, file)), { recursive: true });
  writeFileSync(join(root, file), text);
};
write('tools/patchkit/toolchain/pins.toml', '[include]\ncf = ["sdk/include"]\nsharc = ["sdk/dsp/contract"]\n');
const file = 'modules/example/module.toml';
const modules = new Map([
  ['example/module', { file, manifest: {}, objects: [{ sources: ['src/Cargo.toml', '../../shared/dsp.s'] }] }],
]);

it('includes Cargo path-dependency project sources and exact unowned object sources', () => {
  write(
    'modules/example/src/Cargo.toml',
    '[package]\nname = "module"\n[dependencies]\nhelper = { path = "../../../library" }\n',
  );
  write('library/Cargo.toml', '[package]\nname = "helper"\n');
  write('library/project.json', '{"name":"helper"}');
  expect(packSourceInputs(root, modules, [file])).toEqual([
    { input: 'production', projects: ['example/module', 'helper'] },
    '{workspaceRoot}/sdk/include/**/*',
    '{workspaceRoot}/shared/dsp.s',
  ]);
});

it('rejects inherited or external Cargo dependency paths instead of omitting their inputs', () => {
  write('modules/example/src/Cargo.toml', '[dependencies]\nhelper = { workspace = true }\n');
  expect(() => packSourceInputs(root, modules, [file])).toThrow('inherits workspace configuration');
  write('modules/example/src/Cargo.toml', '[dependencies]\nhelper = { path = "../../../../external" }\n');
  expect(() => packSourceInputs(root, modules, [file])).toThrow('outside the workspace');
});

it('keeps explicit sources outside module and Cargo owners file-level with private headers', () => {
  write('sdk/project.json', '{"name":"sdk"}');
  const external = new Map([['example/module', { file, objects: [{ sources: ['../../sdk/src/sample/picker.c'] }] }]]);
  expect(packSourceInputs(root, external, [file])).toEqual([
    { input: 'production', projects: ['example/module'] },
    '{workspaceRoot}/sdk/src/**/*',
    '{workspaceRoot}/sdk/src/sample/**/*.{h,hh,hpp,hxx}',
    '{workspaceRoot}/sdk/src/sample/picker.c',
  ]);
});

it('makes the SDK sources an input of a module with C sources, which link them automatically', () => {
  const c = new Map([['example/module', { file, objects: [{ sources: ['src/module.c', 'src/dsp/x.s'] }] }]]);
  expect(packSourceInputs(root, c, [file])).toEqual([
    { input: 'production', projects: ['example/module'] },
    '{workspaceRoot}/sdk/src/**/*',
  ]);
});

it('makes the C and assembler sources of a Rust object and the headers of its include_dirs inputs', () => {
  const dks = 'modules/svc/dks/module.toml';
  write('modules/svc/dks/Cargo.toml', '[package]\nname = "svc-dks"\n');
  write('adapters/play/src/cf/control.c', '#include "play.h"\n#include "local.h"\n');
  write('adapters/play/src/cf/hooks.s', '.include "hooks.inc"\n');
  write('adapters/play/src/cf/local.h', '');
  write('adapters/play/include/play.h', '#include "deep.h"\n');
  write('adapters/play/include/deep.h', '');
  write('adapters/play/include/hooks.inc', '');
  const object = (include_dirs?: string[]) =>
    new Map([
      [
        'core/svc',
        {
          file: dks,
          manifest: { category: 'test' },
          objects: [
            {
              sources: [
                'Cargo.toml',
                '../../../adapters/play/src/cf/control.c',
                '../../../adapters/play/src/cf/hooks.s',
              ],
              ...(include_dirs && { include_dirs }),
            },
          ],
        },
      ],
    ]);
  const sources = [
    { input: 'production', projects: ['core/svc'] },
    '{workspaceRoot}/adapters/play/src/cf/**/*.{h,hh,hpp,hxx}',
    '{workspaceRoot}/adapters/play/src/cf/control.c',
    '{workspaceRoot}/adapters/play/src/cf/hooks.s',
    '{workspaceRoot}/sdk/include/**/*',
  ];
  // the headers of the include directories, and what they include, are inputs; a Rust object links no SDK library (no sdk/src)
  expect(packSourceInputs(root, object(['../../../adapters/play/include']), [dks])).toEqual([
    ...sources,
    '{workspaceRoot}/adapters/play/include/deep.h',
    '{workspaceRoot}/adapters/play/include/hooks.inc',
    '{workspaceRoot}/adapters/play/include/play.h',
    '{workspaceRoot}/adapters/play/src/cf/local.h',
  ]);
  // without the key the build looks in the module's own directories, and so does the scan
  expect(packSourceInputs(root, object(), [dks])).toEqual([...sources, '{workspaceRoot}/adapters/play/src/cf/local.h']);
  expect(() => packSourceInputs(root, object(['../../../../outside']), [dks])).toThrow('outside the workspace');
});

it('adds the projects nested below a Cargo crate directory to the inputs of a module that builds the crate', () => {
  write('modules/example/src/Cargo.toml', '[package]\nname = "module"\n');
  write('modules/example/project.json', '{"name":"example/module"}');
  write('modules/example/src/part/project.json', '{"name":"example/module-part"}');
  write('modules/example/src/part/deeper/project.json', '{"name":"example/module-deeper"}');
  const one = new Map([['example/module', { file, manifest: {}, objects: [{ sources: ['src/Cargo.toml'] }] }]]);
  expect(packSourceInputs(root, one, [file])[0]).toEqual({
    input: 'production',
    projects: ['example/module', 'example/module-deeper', 'example/module-part'],
  });
});
