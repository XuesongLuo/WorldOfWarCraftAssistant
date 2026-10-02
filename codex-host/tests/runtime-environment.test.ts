import { createHash } from 'node:crypto';
import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import { prepareApplicationDirectories } from '../src/host/application-directories.js';
import { loadRuntimeLock, verifyRuntimeBinary } from '../src/runtime/runtime-lock.js';

describe('application-owned Codex environment', () => {
  it('locks the checked-in app-server Schema to the runtime version', async () => {
    const lockPath = new URL('../../eng/codex-runtime-lock.json', import.meta.url);
    const lockFile = fileURLToPath(lockPath);
    const lock = await loadRuntimeLock(lockFile);
    const repositoryRoot = resolve(dirname(lockFile), '..');
    const schema = await readFile(resolve(repositoryRoot, lock.appServerSchema.path));
    const actual = createHash('sha256').update(schema).digest('hex');

    expect(actual).toBe(lock.appServerSchema.sha256);
  });

  it('creates separate config, state, and empty workspace directories', async () => {
    const root = await mkdtemp(join(tmpdir(), 'wowai-host-dirs-'));
    const paths = await prepareApplicationDirectories(root);

    expect(paths).toEqual({
      root,
      config: join(root, 'codex-config'),
      state: join(root, 'codex-state'),
      workspace: join(root, 'codex-workspace'),
    });
    expect(await readFile(join(paths.config, 'config.toml'), 'utf8')).toMatch(/analytics/u);
  });

  it('fails closed on a binary checksum or version mismatch', async () => {
    const root = await mkdtemp(join(tmpdir(), 'wowai-runtime-lock-'));
    const binary = join(root, 'codex.exe');
    await writeFile(binary, 'locked-runtime', 'utf8');
    const sha256 = createHash('sha256').update('locked-runtime').digest('hex');
    const lockPath = join(root, 'lock.json');
    await writeFile(
      lockPath,
      JSON.stringify({
        schemaVersion: 1,
        product: 'codex-cli',
        version: '0.159.2',
        source: {
          package: '@openai/codex',
          version: '0.159.2-win32-x64',
          tarball: 'https://registry.npmjs.org/@openai/codex/-/codex-0.159.2-win32-x64.tgz',
          integrity:
            'sha512-1ZJVTO40/ZaHPUUWc3uCX73jwzJRaDxAjRQEf37dC5Q3h1fp/Z7+1L8oX3bPlXgjhEY6kHW3rm0wz2sdQ5rxNw==',
          license: 'Apache-2.0',
        },
        sha256,
        appServerSchema: { path: 'schema.json', sha256: '0'.repeat(64) },
      }),
      'utf8',
    );
    const lock = await loadRuntimeLock(lockPath);

    await expect(
      verifyRuntimeBinary(binary, lock, () => Promise.resolve('codex-cli 0.159.2')),
    ).resolves.toBeUndefined();
    await expect(
      verifyRuntimeBinary(binary, lock, () => Promise.resolve('codex-cli 9.9.9')),
    ).rejects.toThrow(/version/u);
    await writeFile(binary, 'tampered', 'utf8');
    await expect(
      verifyRuntimeBinary(binary, lock, () => Promise.resolve('codex-cli 0.159.2')),
    ).rejects.toThrow(/checksum/u);
  });
});
