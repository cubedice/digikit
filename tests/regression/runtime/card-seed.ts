import fs from 'node:fs';
import path from 'node:path';
import type { DkSession } from '@digi/dk-session';
import type { Device } from '@digi/test-support';
import { moduleArtifact } from './module-artifact.ts';
import { neededServices, portFile, servicePath, servicesSidecar, type CardService } from './service-files.ts';

/**
 * A packed module for a card: the file, and the services it needs when the file is not an Nx pack output (a package a proof was given
 * by path has no `<leaf>.services.json` beside it). A pack output's own answer wins.
 */
export type SeedModule = { file: string; needs?: string[] };

/** The service module of a service name (`services/<name>/dks`). */
const serviceOf = (service: string): CardService => ({ service, moduleId: `core/services/${service}` });

/** The services the modules need: the answer of the pack target (`service-files.ts`), or `needs` for a file without one. */
function servicesOf(modules: SeedModule[]): CardService[] {
  const found = new Map<string, CardService>();
  for (const { file, needs } of modules) {
    const packed = fs.existsSync(servicesSidecar(file));
    if (!packed && !needs) throw new Error(`${file} has no ${path.basename(servicesSidecar(file))}: name its services`);
    for (const service of packed ? neededServices([file]) : needs!.map(serviceOf)) found.set(service.service, service);
  }
  return [...found.values()].sort((x, y) => x.service.localeCompare(y.service));
}

/**
 * The service files `services` (`<seed>/digikit/services/<name>.dks`, as packed: the Nx target `<service id>:pack` writes them) and the port file the
 * runtime resolves their firmware names from (`<seed>/digikit/ports/<device>-<version>.dkport`, `patchkit:ports`; docs/plans/port-file.md) in a card seed folder.
 */
function seedServiceFiles(seed: string, device: Device, services: CardService[]): void {
  if (!services.length) return;
  const root = path.join(seed, 'digikit');
  for (const service of services) {
    const to = path.join(root, servicePath(service.service));
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.copyFileSync(moduleArtifact(device, service.moduleId), to);
  }
  const port = portFile(device);
  fs.mkdirSync(path.join(root, path.dirname(port.name)), { recursive: true });
  fs.copyFileSync(port.file, path.join(root, port.name));
}

/**
 * Put packed modules in a card seed folder (`<seed>/digikit/modules/<file name>`) with the service files they need and the port file
 * (`seedServiceFiles`): the runtime finds a service by its name at boot, and a module with a machine or an effect is refused without its service.
 * Returns the names of the services on the card.
 */
export async function seedModules(
  seed: string,
  device: Device,
  modules: (SeedModule & { name?: string })[],
): Promise<string[]> {
  const moduleDir = path.join(seed, 'digikit', 'modules');
  fs.mkdirSync(moduleDir, { recursive: true });
  for (const { file, name } of modules) fs.copyFileSync(file, path.join(moduleDir, name ?? path.basename(file)));
  const needed = servicesOf(modules);
  seedServiceFiles(seed, device, needed);
  return needed.map((service) => service.service);
}

/**
 * Service files in a card seed folder with no module that needs them, with the port file: for an image that asks the boot for a service itself
 * (an adapter's info entry `boot_need_<name>`, docs/plans/fork-image-diet.md section 9.2: the track keyboard).
 */
export async function seedServices(seed: string, device: Device, names: string[]): Promise<void> {
  seedServiceFiles(seed, device, names.map(serviceOf));
}

/**
 * The same for a card written over the protocol: upload the port file and the packed services `needs` (`services/<name>.dks`) and read each back.
 * Call it before the selection is written.
 */
export async function putServices(
  dk: Pick<DkSession, 'put' | 'verifyFile'>,
  device: Device,
  needs: string[],
): Promise<void> {
  if (!needs.length) return;
  const port = portFile(device);
  const portBytes = fs.readFileSync(port.file);
  await dk.put(port.name, portBytes);
  if (!(await dk.verifyFile(port.name, portBytes)))
    throw new Error(`the card's ${port.name} differs from the port file`);
  for (const service of needs.map(serviceOf)) {
    const bytes = fs.readFileSync(moduleArtifact(device, service.moduleId));
    await dk.put(servicePath(service.service), bytes);
    if (!(await dk.verifyFile(servicePath(service.service), bytes)))
      throw new Error(`the card's ${servicePath(service.service)} differs from the packed file`);
  }
}
