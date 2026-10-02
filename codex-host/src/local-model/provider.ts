import { z } from 'zod';

import { CodexRuntimeFailure } from '../runtime/errors.js';

export const LOCAL_OLLAMA_PROVIDER_ID = 'wowai_ollama' as const;
const DEFAULT_PROBE_TIMEOUT_MS = 2_000;

const modelNameSchema = z
  .string()
  .min(1)
  .max(128)
  .regex(/^[a-z0-9][a-z0-9._:/-]*$/iu, 'model contains unsupported characters');
const versionResponseSchema = z.object({ version: z.string().min(1).max(64) }).strict();
const tagsResponseSchema = z
  .object({
    models: z.array(
      z.looseObject({
        name: z.string().min(1),
        model: z.string().min(1).optional(),
      }),
    ),
  })
  .strict();
const showResponseSchema = z.looseObject({
  capabilities: z.array(z.string().min(1)).default([]),
});

export interface LocalModelConfiguration {
  provider: 'local-ollama';
  appServerProviderId: typeof LOCAL_OLLAMA_PROVIDER_ID;
  endpoint: URL;
  model: string;
  probeTimeoutMs: number;
}

export interface LocalModelCapabilities {
  provider: 'local-ollama';
  model: string;
  version: string;
  text: true;
  vision: boolean;
  tools: boolean;
}

export function localModelConfigurationFromEnvironment(
  environment: NodeJS.ProcessEnv,
): LocalModelConfiguration | undefined {
  const provider = environment.WOWAI_MODEL_PROVIDER;
  const endpoint = environment.WOWAI_LOCAL_MODEL_ENDPOINT;
  const model = environment.WOWAI_LOCAL_MODEL;
  if (provider === undefined && endpoint === undefined && model === undefined) return undefined;
  if (provider !== 'local-ollama') {
    throw configurationFailure('WOWAI_MODEL_PROVIDER must be local-ollama for STEP-011');
  }
  if (endpoint === undefined || model === undefined) {
    throw configurationFailure(
      'WOWAI_MODEL_PROVIDER, WOWAI_LOCAL_MODEL_ENDPOINT, and WOWAI_LOCAL_MODEL must be configured together',
    );
  }

  let parsedEndpoint: URL;
  try {
    parsedEndpoint = new URL(endpoint);
  } catch {
    throw configurationFailure('WOWAI_LOCAL_MODEL_ENDPOINT is not a valid URL');
  }
  validateLoopbackEndpoint(parsedEndpoint);
  const parsedModel = modelNameSchema.safeParse(model);
  if (!parsedModel.success) {
    throw configurationFailure('WOWAI_LOCAL_MODEL is not a valid explicit model name');
  }
  const timeoutText = environment.WOWAI_LOCAL_MODEL_PROBE_TIMEOUT_MS;
  const timeout = timeoutText === undefined ? DEFAULT_PROBE_TIMEOUT_MS : Number(timeoutText);
  if (!Number.isInteger(timeout) || timeout < 250 || timeout > 10_000) {
    throw configurationFailure('WOWAI_LOCAL_MODEL_PROBE_TIMEOUT_MS must be 250..10000');
  }

  return {
    provider,
    appServerProviderId: LOCAL_OLLAMA_PROVIDER_ID,
    endpoint: parsedEndpoint,
    model: parsedModel.data,
    probeTimeoutMs: timeout,
  };
}

export async function probeLocalModel(
  configuration: LocalModelConfiguration,
  fetchImplementation: typeof fetch = fetch,
): Promise<LocalModelCapabilities> {
  const signal = AbortSignal.timeout(configuration.probeTimeoutMs);
  try {
    const [versionValue, tagsValue, showValue] = await Promise.all([
      readJson(fetchImplementation, endpoint(configuration, 'api/version'), { signal }),
      readJson(fetchImplementation, endpoint(configuration, 'api/tags'), { signal }),
      readJson(fetchImplementation, endpoint(configuration, 'api/show'), {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ model: configuration.model, verbose: false }),
        signal,
      }),
    ]);
    const version = versionResponseSchema.parse(versionValue);
    const tags = tagsResponseSchema.parse(tagsValue);
    const installed = tags.models.some(
      (candidate) =>
        candidate.name === configuration.model || candidate.model === configuration.model,
    );
    if (!installed) {
      throw new Error(`configured model is not installed: ${configuration.model}`);
    }
    const show = showResponseSchema.parse(showValue);
    const capabilities = new Set(show.capabilities.map((value) => value.toLowerCase()));
    return {
      provider: configuration.provider,
      model: configuration.model,
      version: version.version,
      text: true,
      vision: capabilities.has('vision'),
      tools: capabilities.has('tools'),
    };
  } catch (error) {
    if (error instanceof CodexRuntimeFailure) throw error;
    throw new CodexRuntimeFailure({
      code: 'MODEL_PROVIDER_UNAVAILABLE',
      message: `The configured local Ollama model is unavailable: ${safeMessage(error)}`,
      retryable: true,
    });
  }
}

export function appServerProviderArguments(configuration: LocalModelConfiguration): string[] {
  const baseUrl = new URL('v1/', configuration.endpoint).href;
  return [
    '-c',
    `model_provider="${configuration.appServerProviderId}"`,
    '-c',
    `model="${configuration.model}"`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.name="Ollama"`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.base_url="${baseUrl}"`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.wire_api="responses"`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.requires_openai_auth=false`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.request_max_retries=0`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.stream_max_retries=0`,
    '-c',
    `model_providers.${configuration.appServerProviderId}.supports_websockets=false`,
  ];
}

function validateLoopbackEndpoint(endpointUrl: URL): void {
  const host = endpointUrl.hostname.toLowerCase();
  const loopback = host === 'localhost' || host === '127.0.0.1' || host === '[::1]';
  if (
    endpointUrl.protocol !== 'http:' ||
    !loopback ||
    endpointUrl.username !== '' ||
    endpointUrl.password !== '' ||
    endpointUrl.search !== '' ||
    endpointUrl.hash !== '' ||
    (endpointUrl.pathname !== '/' && endpointUrl.pathname !== '')
  ) {
    throw configurationFailure(
      'WOWAI_LOCAL_MODEL_ENDPOINT must be a credential-free HTTP loopback origin',
    );
  }
}

function endpoint(configuration: LocalModelConfiguration, path: string): URL {
  return new URL(path, configuration.endpoint);
}

async function readJson(
  fetchImplementation: typeof fetch,
  url: URL,
  init: RequestInit,
): Promise<unknown> {
  const response = await fetchImplementation(url, init);
  if (!response.ok) throw new Error(`HTTP ${String(response.status)} from ${url.pathname}`);
  return await response.json();
}

function configurationFailure(message: string): CodexRuntimeFailure {
  return new CodexRuntimeFailure({
    code: 'MODEL_PROVIDER_UNAVAILABLE',
    message,
    retryable: false,
  });
}

function safeMessage(error: unknown): string {
  if (error instanceof z.ZodError) return 'provider returned an invalid capability response';
  if (error instanceof Error && error.name === 'TimeoutError') return 'capability probe timed out';
  if (error instanceof Error) return error.message.slice(0, 256);
  return 'unknown provider error';
}
