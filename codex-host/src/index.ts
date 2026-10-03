import { z } from 'zod';
import { pathToFileURL } from 'node:url';

export * from './host/application-directories.js';
export * from './host/json-line-reader.js';
export * from './host/server.js';
export * from './local-model/provider.js';
export * from './cloud-model/provider.js';
export * from './app-server/client.js';
export * from './app-server/process.js';
export * from './app-server/protocol.js';
export * from './app-server/tool-policy.js';
export * from './protocol/session.js';
export * from './protocol/types.js';
export * from './protocol/validation.js';
export * from './runtime/mock-runtime.js';
export * from './runtime/app-server-runtime.js';
export * from './runtime/errors.js';
export * from './runtime/runtime-lock.js';

export const hostBuildInfoSchema = z.object({
  component: z.literal('codex-host'),
  version: z.string().min(1),
});

export const hostBuildInfo = hostBuildInfoSchema.parse({
  component: 'codex-host',
  version: '0.1.0-dev',
});

async function main(): Promise<void> {
  const { CodexHostServer } = await import('./host/server.js');
  const { DeterministicMockRuntime } = await import('./runtime/mock-runtime.js');
  const { startAppServerRuntimeFromEnvironment } = await import('./app-server/process.js');
  const runtime =
    (await startAppServerRuntimeFromEnvironment(process.stderr)) ?? new DeterministicMockRuntime();
  const server = new CodexHostServer({
    input: process.stdin,
    output: process.stdout,
    errors: process.stderr,
    runtime,
  });
  await server.run();
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) {
  void main().catch(() => {
    process.exitCode = 1;
  });
}
