import { describe, expect, it } from 'vitest';

import {
  cloudAppServerProviderArguments,
  cloudModelConfigurationFromEnvironment,
  sanitizedAppServerEnvironment,
} from '../src/cloud-model/provider.js';

const credential = { WOWAI_ACTIVE_API_KEY: 'test-only-not-a-real-key' };

describe('cloud provider configuration', () => {
  it.each([
    ['openai', 'gpt-5', 'https://api.openai.com/v1', 'native-responses'],
    ['deepseek', 'deepseek-flash', 'https://api.deepseek.com', 'compatible-responses'],
    ['xai', 'grok-4.7', 'https://api.x.ai/v1', 'compatible-responses'],
    [
      'openrouter',
      'anthropic/claude-sonnet-4',
      'https://openrouter.ai/api/v1',
      'compatible-responses',
    ],
  ] as const)('registers %s through a fixed Responses endpoint', (provider, model, url, mode) => {
    const configuration = cloudModelConfigurationFromEnvironment({
      WOWAI_MODEL_PROVIDER: provider,
      WOWAI_CLOUD_MODEL: model,
      WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      ...credential,
    });
    expect(configuration).toMatchObject({ provider, model, connectionMode: mode });
    expect(configuration?.baseUrl.href.replace(/\/$/u, '')).toBe(url);
    expect(configuration).not.toHaveProperty('apiKey');
    if (configuration === undefined) throw new Error('expected cloud configuration');
    expect(cloudAppServerProviderArguments(configuration)).toEqual(
      expect.arrayContaining([
        expect.stringMatching(/wire_api="responses"/u),
        expect.stringMatching(/request_max_retries=0/u),
        expect.stringMatching(/env_key="WOWAI_ACTIVE_API_KEY"/u),
      ]),
    );
  });

  it('builds DashScope endpoints only from allowlisted region templates', () => {
    const configuration = cloudModelConfigurationFromEnvironment({
      WOWAI_MODEL_PROVIDER: 'dashscope',
      WOWAI_CLOUD_MODEL: 'qwen3.8-max',
      WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      WOWAI_DASHSCOPE_REGION: 'singapore',
      WOWAI_DASHSCOPE_WORKSPACE: 'workspace-123',
      ...credential,
    });
    expect(configuration?.baseUrl.href).toBe(
      'https://workspace-123.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1',
    );
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'dashscope',
        WOWAI_CLOUD_MODEL: 'qwen3.8-max',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
        WOWAI_DASHSCOPE_REGION: 'attacker.example',
        WOWAI_DASHSCOPE_WORKSPACE: 'workspace-123',
        ...credential,
      }),
    ).toThrow(/allowlisted/u);
  });

  it('builds Azure deployment-style Responses configuration without putting secrets in arguments', () => {
    const configuration = cloudModelConfigurationFromEnvironment({
      WOWAI_MODEL_PROVIDER: 'azure-openai',
      WOWAI_CLOUD_MODEL: 'my-deployment',
      WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      WOWAI_AZURE_RESOURCE: 'wowai-prod',
      WOWAI_AZURE_API_VERSION: 'v1',
      ...credential,
    });
    expect(configuration).toMatchObject({
      connectionMode: 'azure-responses',
      vision: false,
      destinationHost: 'wowai-prod.openai.azure.com',
    });
    if (configuration === undefined) throw new Error('expected Azure configuration');
    const args = cloudAppServerProviderArguments(configuration);
    expect(args).toEqual(
      expect.arrayContaining([
        'model_providers.wowai_azure_openai.env_http_headers={"api-key"="WOWAI_ACTIVE_API_KEY"}',
        'model_providers.wowai_azure_openai.query_params={"api-version"="v1"}',
      ]),
    );
    expect(args.join(' ')).not.toContain(credential.WOWAI_ACTIVE_API_KEY);
  });

  it('fails closed for missing gates and conservative image capability', () => {
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openai',
        WOWAI_CLOUD_MODEL: 'gpt-5',
        WOWAI_CLOUD_UPLOAD_CONSENT: '0',
        ...credential,
      }),
    ).toThrow(/CONSENT/u);
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openai',
        WOWAI_CLOUD_MODEL: 'gpt-5',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      }),
    ).toThrow(/API_KEY/u);
    expect(
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openrouter',
        WOWAI_CLOUD_MODEL: 'some/vision-model',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
        ...credential,
      }),
    ).toMatchObject({ vision: false });
  });

  it('passes only the active credential to App Server and strips unrelated provider keys', () => {
    const configuration = cloudModelConfigurationFromEnvironment({
      WOWAI_MODEL_PROVIDER: 'xai',
      WOWAI_CLOUD_MODEL: 'grok-4.7',
      WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      PATH: 'safe-path',
      OPENAI_API_KEY: 'unrelated',
      ...credential,
    });
    const environment = sanitizedAppServerEnvironment(
      {
        PATH: 'safe-path',
        OPENAI_API_KEY: 'unrelated',
        ANTHROPIC_API_KEY: 'unrelated-2',
        ...credential,
      },
      configuration,
    );
    expect(environment).toEqual({
      PATH: 'safe-path',
      WOWAI_ACTIVE_API_KEY: credential.WOWAI_ACTIVE_API_KEY,
    });
  });
});
