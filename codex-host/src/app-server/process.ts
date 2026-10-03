import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { resolve } from 'node:path';
import type { Writable } from 'node:stream';

import { prepareApplicationDirectories } from '../host/application-directories.js';
import {
  cloudAppServerProviderArguments,
  cloudModelConfigurationFromEnvironment,
} from '../cloud-model/provider.js';
import {
  appServerProviderArguments,
  localModelConfigurationFromEnvironment,
  probeLocalModel,
  type LocalModelCapabilities,
  type LocalModelConfiguration,
} from '../local-model/provider.js';
import { AppServerCodexRuntime } from '../runtime/app-server-runtime.js';
import { redactSensitiveText } from '../runtime/errors.js';
import { loadRuntimeLock, verifyRuntimeBinary } from '../runtime/runtime-lock.js';
import { AppServerClient } from './client.js';

export interface StartAppServerOptions {
  binaryPath: string;
  lockPath: string;
  applicationRoot: string;
  diagnostics: Writable;
}

export async function startAppServerRuntime(
  options: StartAppServerOptions,
): Promise<AppServerCodexRuntime> {
  const lock = await loadRuntimeLock(options.lockPath);
  await verifyRuntimeBinary(options.binaryPath, lock);
  const directories = await prepareApplicationDirectories(options.applicationRoot);
  const cloudModel = cloudModelConfigurationFromEnvironment(process.env);
  const localModel =
    cloudModel === undefined ? localModelConfigurationFromEnvironment(process.env) : undefined;
  const child = spawn(
    options.binaryPath,
    [
      'app-server',
      '--stdio',
      '--strict-config',
      '-c',
      'analytics.enabled=false',
      '-c',
      'web_search="disabled"',
      ...(cloudModel === undefined ? [] : cloudAppServerProviderArguments(cloudModel)),
      ...(localModel === undefined ? [] : appServerProviderArguments(localModel)),
    ],
    {
      cwd: directories.workspace,
      env: { ...process.env, CODEX_HOME: directories.state },
      shell: false,
      stdio: ['pipe', 'pipe', 'pipe'],
      windowsHide: true,
    },
  );
  forwardDiagnostics(child, options.diagnostics);
  const client = new AppServerClient({
    input: child.stdout,
    output: child.stdin,
    diagnostics: options.diagnostics,
  });
  const runtime = new OwnedAppServerRuntime(
    client,
    directories.workspace,
    child,
    localModel,
    localModel === undefined ? undefined : () => probeLocalModel(localModel),
    directories.visionTemp,
    cloudModel,
  );
  try {
    await runtime.start();
    return runtime;
  } catch (error) {
    await runtime.close();
    throw error;
  }
}

export async function startAppServerRuntimeFromEnvironment(
  diagnostics: Writable,
): Promise<AppServerCodexRuntime | undefined> {
  const binaryPath = process.env.WOWAI_CODEX_BINARY;
  const applicationRoot = process.env.WOWAI_CODEX_ROOT;
  const lockPath = process.env.WOWAI_CODEX_LOCK;
  if (binaryPath === undefined && applicationRoot === undefined && lockPath === undefined) {
    return undefined;
  }
  if (binaryPath === undefined || applicationRoot === undefined || lockPath === undefined) {
    throw new Error(
      'WOWAI_CODEX_BINARY, WOWAI_CODEX_ROOT, and WOWAI_CODEX_LOCK must be configured together',
    );
  }
  return await startAppServerRuntime({
    binaryPath: resolve(binaryPath),
    applicationRoot: resolve(applicationRoot),
    lockPath: resolve(lockPath),
    diagnostics,
  });
}

class OwnedAppServerRuntime extends AppServerCodexRuntime {
  public constructor(
    client: AppServerClient,
    workspace: string,
    private readonly child: ChildProcessWithoutNullStreams,
    localModel?: LocalModelConfiguration,
    capabilityProbe?: () => Promise<LocalModelCapabilities>,
    visionTemp?: string,
    cloudModel?: import('../cloud-model/provider.js').CloudModelConfiguration,
  ) {
    super(client, workspace, localModel, capabilityProbe, visionTemp, cloudModel);
  }

  public override async close(): Promise<void> {
    await super.close();
    if (this.child.exitCode !== null) return;
    const exited = new Promise<void>((resolveExit) =>
      this.child.once('exit', () => {
        resolveExit();
      }),
    );
    const timeout = new Promise<void>((resolveTimeout) => setTimeout(resolveTimeout, 2_000));
    await Promise.race([exited, timeout]);
    if (isRunning(this.child)) this.child.kill();
  }
}

function isRunning(child: ChildProcessWithoutNullStreams): boolean {
  return child.exitCode === null && !child.killed;
}

function forwardDiagnostics(child: ChildProcessWithoutNullStreams, destination: Writable): void {
  const maximumBytes = 64 * 1_024;
  let buffered = Buffer.alloc(0);
  child.stderr.on('data', (chunk: Buffer) => {
    if (buffered.byteLength >= maximumBytes) return;
    buffered = Buffer.concat([buffered, chunk.subarray(0, maximumBytes - buffered.byteLength)]);
  });
  child.stderr.once('end', () => {
    if (buffered.byteLength === 0) return;
    destination.write(
      redactSensitiveText(buffered.toString('utf8'), [
        process.env.OPENAI_API_KEY,
        process.env.DEEPSEEK_API_KEY,
      ]),
    );
    buffered = Buffer.alloc(0);
  });
  child.once('error', (error) => {
    destination.write(`[codex-app-server] ${redactSensitiveText(error.name)}\n`);
  });
}
