import fs from 'node:fs';
import path from 'node:path';
import type { DkSession } from '@digi/dk-session';
import type { Device } from '@digi/test-support';
import { moduleArtifact } from './install.ts';
import { boundService, neededServices, servicePath, servicesSidecar, type CardService } from './service-files.ts';

/**
 * A packed module for a card: the file, and the services it needs when the file is not an Nx pack output (a package a proof was given
 * by path has no `<leaf>.services.json` beside it). A pack output's own answer wins.
 */
export type SeedModule = { file: string; needs?: string[] };

/** The service module of a service name (`modules/core/services/<name>/dks`). */
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

/** The bound service file (the Nx target `<service id>:bind` writes it). */
const bound = (device: Device, service: CardService) =>
  boundService(device, service, moduleArtifact(device, service.moduleId));

/**
 * Put packed modules in a card seed folder (`<seed>/digikit/modules/<file name>`) with the service files they need
 * (`<seed>/digikit/services/<name>.dks`, bound to the stock firmware): the runtime finds a service by its name at boot, and a module
 * with a machine or an effect is refused without its service. Returns the names of the services on the card.
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
  if (needed.length) {
    const serviceDir = path.join(seed, 'digikit', 'services');
    fs.mkdirSync(serviceDir, { recursive: true });
    for (const service of needed)
      fs.copyFileSync(await bound(device, service), path.join(serviceDir, `${service.service}.dks`));
  }
  return needed.map((service) => service.service);
}

/**
 * The same for a card written over the protocol: upload the bound services `needs` (`services/<name>.dks`) and read each back. Call it
 * before the selection is written.
 */
export async function putServices(
  dk: Pick<DkSession, 'put' | 'verifyFile'>,
  device: Device,
  needs: string[],
): Promise<void> {
  for (const service of needs.map(serviceOf)) {
    const bytes = fs.readFileSync(await bound(device, service));
    await dk.put(servicePath(service.service), bytes);
    if (!(await dk.verifyFile(servicePath(service.service), bytes)))
      throw new Error(`the card's ${servicePath(service.service)} differs from the bound file`);
  }
}
