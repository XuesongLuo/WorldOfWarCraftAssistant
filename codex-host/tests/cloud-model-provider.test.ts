import { describe, expect, it } from 'vitest';

import {
  cloudAppServerProviderArguments,
  cloudModelConfigurationFromEnvironment,
} from '../src/cloud-model/provider.js';

describe('cloud provider configuration', () => {
  it('requires explicit provider, model, upload consent, and API key readiness', () => {
    expect(
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openai',
        WOWAI_CLOUD_MODEL: 'vision-model-fixture',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
        OPENAI_API_KEY: 'test-only-not-a-real-key',
      }),
    ).toEqual({
      provider: 'openai',
      appServerProviderId: 'openai',
      displayName: 'OpenAI',
      model: 'vision-model-fixture',
      credentialEnvironmentKey: 'OPENAI_API_KEY',
      vision: true,
    });
  });

  it('registers DeepSeek as a fixed Responses provider without returning its credential', () => {
    const configuration = cloudModelConfigurationFromEnvironment({
      WOWAI_MODEL_PROVIDER: 'deepseek',
      WOWAI_CLOUD_MODEL: 'deepseek-flash',
      WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      DEEPSEEK_API_KEY: 'test-only-not-a-real-key',
    });
    expect(configuration).toMatchObject({
      provider: 'deepseek',
      appServerProviderId: 'wowai_deepseek',
      displayName: 'DeepSeek',
      model: 'deepseek-flash',
      credentialEnvironmentKey: 'DEEPSEEK_API_KEY',
      vision: true,
    });
    if (configuration === undefined) throw new Error('expected DeepSeek configuration');
    expect(configuration).not.toHaveProperty('apiKey');
    expect(cloudAppServerProviderArguments(configuration)).toEqual(
      expect.arrayContaining([
        'model_provider="wowai_deepseek"',
        'model_providers.wowai_deepseek.base_url="https://api.deepseek.com"',
        'model_providers.wowai_deepseek.env_key="DEEPSEEK_API_KEY"',
        'model_providers.wowai_deepseek.wire_api="responses"',
      ]),
    );
  });

  it('does not infer vision support for an unregistered DeepSeek model', () => {
    expect(
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'deepseek',
        WOWAI_CLOUD_MODEL: 'deepseek-v4-pro',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
        DEEPSEEK_API_KEY: 'test-only-not-a-real-key',
      }),
    ).toMatchObject({ vision: false });
  });

  it('fails closed without every explicit cloud gate and never returns the credential', () => {
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openai',
        WOWAI_CLOUD_MODEL: 'vision-model-fixture',
        WOWAI_CLOUD_UPLOAD_CONSENT: '0',
        OPENAI_API_KEY: 'test-only-not-a-real-key',
      }),
    ).toThrow(/CONSENT/u);
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'openai',
        WOWAI_CLOUD_MODEL: 'vision-model-fixture',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      }),
    ).toThrow(/OPENAI_API_KEY/u);
    expect(() =>
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'deepseek',
        WOWAI_CLOUD_MODEL: 'deepseek-flash',
        WOWAI_CLOUD_UPLOAD_CONSENT: '1',
      }),
    ).toThrow(/DEEPSEEK_API_KEY/u);
    expect(
      cloudModelConfigurationFromEnvironment({
        WOWAI_MODEL_PROVIDER: 'local-ollama',
        OPENAI_API_KEY: 'unrelated-existing-user-environment',
      }),
    ).toBeUndefined();
  });
});
