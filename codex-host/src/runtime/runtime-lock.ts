import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { createReadStream } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { promisify } from 'node:util';

import { z } from 'zod';

const execFileAsync = promisify(execFile);
const sha256Schema = z.string().regex(/^[0-9a-f]{64}$/u);

export const runtimeLockSchema = z
  .object({
    $schema: z.string().min(1).optional(),
    schemaVersion: z.literal(1),
    product: z.literal('codex-cli'),
    version: z.string().min(1),
    source: z
      .object({
        package: z.literal('@openai/codex'),
        version: z.string().min(1),
        tarball: z.url(),
        integrity: z.string().regex(/^sha512-[A-Za-z0-9+/]+={0,2}$/u),
        license: z.literal('Apache-2.0'),
      })
      .strict(),
    sha256: sha256Schema,
    appServerSchema: z.object({ path: z.string().min(1), sha256: sha256Schema }).strict(),
  })
  .strict();

export type RuntimeLock = z.infer<typeof runtimeLockSchema>;
export type VersionRunner = (binaryPath: string) => Promise<string>;

export async function loadRuntimeLock(path: string): Promise<RuntimeLock> {
  return runtimeLockSchema.parse(JSON.parse(await readFile(path, 'utf8')));
}

export async function verifyRuntimeBinary(
  binaryPath: string,
  lock: RuntimeLock,
  versionRunner: VersionRunner = runVersion,
): Promise<void> {
  const actualHash = await sha256(binaryPath);
  if (actualHash !== lock.sha256) throw new Error('Codex runtime checksum mismatch');
  const version = (await versionRunner(binaryPath)).trim();
  if (version !== `${lock.product} ${lock.version}`) {
    throw new Error(`Codex runtime version mismatch: expected ${lock.version}`);
  }
}

async function sha256(path: string): Promise<string> {
  const digest = createHash('sha256');
  for await (const chunk of createReadStream(path)) digest.update(chunk as Buffer);
  return digest.digest('hex');
}

async function runVersion(binaryPath: string): Promise<string> {
  const result = await execFileAsync(binaryPath, ['--version'], {
    encoding: 'utf8',
    windowsHide: true,
  });
  return result.stdout;
}
