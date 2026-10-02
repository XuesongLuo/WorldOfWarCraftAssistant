import { describe, expect, it } from 'vitest';

import {
  appServerProviderArguments,
  localModelConfigurationFromEnvironment,
  probeLocalModel,
} from '../src/local-model/provider.js';

const environment = {
  WOWAI_MODEL_PROVIDER: 'local-ollama',
  WOWAI_LOCAL_MODEL_ENDPOINT: 'http://127.0.0.1:11434',
  WOWAI_LOCAL_MODEL: 'qwen3:8b',
};

describe('local Ollama provider', () => {
  it('requires one explicit loopback origin and model', () => {
    const configuration = localModelConfigurationFromEnvironment(environment);
    expect(configuration).toMatchObject({
      provider: 'local-ollama',
      appServerProviderId: 'wowai_ollama',
      model: 'qwen3:8b',
      probeTimeoutMs: 2_000,
    });
    expect(configuration?.endpoint.href).toBe('http://127.0.0.1:11434/');

    for (const endpoint of [
      'https://127.0.0.1:11434',
      'http://192.168.1.5:11434',
      'http://user:secret@127.0.0.1:11434',
      'http://127.0.0.1:11434/proxy',
      'http://127.0.0.1:11434/?cloud=true',
    ]) {
      expect(() =>
        localModelConfigurationFromEnvironment({
          ...environment,
          WOWAI_LOCAL_MODEL_ENDPOINT: endpoint,
        }),
      ).toThrow(/loopback origin/u);
    }
  });

  it('builds a no-auth Responses provider with retries disabled', () => {
    const argumentsList = appServerProviderArguments(configuration());
    expect(argumentsList).toEqual(
      expect.arrayContaining([
        'model_provider="wowai_ollama"',
        'model="qwen3:8b"',
        'model_providers.wowai_ollama.base_url="http://127.0.0.1:11434/v1/"',
        'model_providers.wowai_ollama.request_max_retries=0',
      ]),
    );
  });

  it('probes installed model capabilities without downloading anything', async () => {
    const configuredModel = configuration();
    const requested: string[] = [];
    const fakeFetch = ((input: string | URL | Request) => {
      const url = new URL(input instanceof Request ? input.url : input.toString());
      requested.push(url.pathname);
      if (url.pathname === '/api/version') return Promise.resolve(json({ version: '0.12.3' }));
      if (url.pathname === '/api/tags') {
        return Promise.resolve(json({ models: [{ name: 'qwen3:8b', model: 'qwen3:8b' }] }));
      }
      return Promise.resolve(json({ capabilities: ['completion', 'tools', 'vision'] }));
    }) as typeof fetch;

    await expect(probeLocalModel(configuredModel, fakeFetch)).resolves.toEqual({
      provider: 'local-ollama',
      model: 'qwen3:8b',
      version: '0.12.3',
      text: true,
      vision: true,
      tools: true,
    });
    expect(requested.sort()).toEqual(['/api/show', '/api/tags', '/api/version']);
  });

  it.each([
    ['missing model', { version: '0.12.3' }, { models: [] }, { capabilities: [] }],
    ['malformed tags', { version: '0.12.3' }, { models: 'bad' }, { capabilities: [] }],
    ['bad status', { version: '0.12.3' }, { models: [] }, null],
  ])('fails closed for %s', async (_name, version, tags, show) => {
    const configuredModel = configuration();
    const fakeFetch = ((input: string | URL | Request) => {
      const path = new URL(input instanceof Request ? input.url : input.toString()).pathname;
      if (path === '/api/version') return Promise.resolve(json(version));
      if (path === '/api/tags') return Promise.resolve(json(tags));
      return Promise.resolve(show === null ? new Response('', { status: 500 }) : json(show));
    }) as typeof fetch;

    await expect(probeLocalModel(configuredModel, fakeFetch)).rejects.toMatchObject({
      assistantError: { code: 'MODEL_PROVIDER_UNAVAILABLE' },
    });
  });
});

function configuration() {
  const result = localModelConfigurationFromEnvironment(environment);
  if (result === undefined) throw new Error('test configuration was unexpectedly absent');
  return result;
}

function json(value: unknown): Response {
  return new Response(JSON.stringify(value), {
    status: 200,
    headers: { 'content-type': 'application/json' },
  });
}
