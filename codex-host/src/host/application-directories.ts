import { constants } from 'node:fs';
import { mkdir, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

export interface ApplicationDirectories {
  root: string;
  config: string;
  state: string;
  workspace: string;
  visionTemp: string;
}

const MINIMAL_CONFIG = `[analytics]\nenabled = false\n`;

export async function prepareApplicationDirectories(root: string): Promise<ApplicationDirectories> {
  const paths = {
    root,
    config: join(root, 'codex-config'),
    state: join(root, 'codex-state'),
    workspace: join(root, 'codex-workspace'),
    visionTemp: join(root, 'vision-temp'),
  } satisfies ApplicationDirectories;

  await rm(paths.visionTemp, { recursive: true, force: true });
  await Promise.all([
    mkdir(paths.config, { recursive: true }),
    mkdir(paths.state, { recursive: true }),
    mkdir(paths.workspace, { recursive: true }),
    mkdir(paths.visionTemp, { recursive: true }),
  ]);
  try {
    await writeFile(join(paths.config, 'config.toml'), MINIMAL_CONFIG, {
      encoding: 'utf8',
      flag: constants.O_CREAT | constants.O_EXCL | constants.O_WRONLY,
    });
  } catch (error) {
    if (!isAlreadyExists(error)) throw error;
  }
  return paths;
}

function isAlreadyExists(error: unknown): error is NodeJS.ErrnoException {
  return error instanceof Error && 'code' in error && error.code === 'EEXIST';
}
