export type CloudModelProvider = 'openai' | 'deepseek';

export interface CloudModelConfiguration {
  provider: CloudModelProvider;
  appServerProviderId: string;
  displayName: string;
  model: string;
  baseUrl?: URL;
  credentialEnvironmentKey: 'OPENAI_API_KEY' | 'DEEPSEEK_API_KEY';
  vision: boolean;
}

type Environment = Record<string, string | undefined>;

interface ProviderDescriptor {
  appServerProviderId: string;
  displayName: string;
  baseUrl?: string;
  credentialEnvironmentKey: CloudModelConfiguration['credentialEnvironmentKey'];
  supportsVision(model: string): boolean;
}

const providers: Record<CloudModelProvider, ProviderDescriptor> = {
  openai: {
    appServerProviderId: 'openai',
    displayName: 'OpenAI',
    credentialEnvironmentKey: 'OPENAI_API_KEY',
    supportsVision: () => true,
  },
  deepseek: {
    appServerProviderId: 'wowai_deepseek',
    displayName: 'DeepSeek',
    baseUrl: 'https://api.deepseek.com',
    credentialEnvironmentKey: 'DEEPSEEK_API_KEY',
    supportsVision: (model) => model === 'deepseek-flash',
  },
};

export function cloudModelConfigurationFromEnvironment(
  environment: Environment,
): CloudModelConfiguration | undefined {
  const provider = environment.WOWAI_MODEL_PROVIDER;
  const model = environment.WOWAI_CLOUD_MODEL;
  const consent = environment.WOWAI_CLOUD_UPLOAD_CONSENT;
  if (provider !== 'openai' && provider !== 'deepseek') {
    if (model !== undefined || consent !== undefined) {
      throw new Error('cloud model and consent require a supported WOWAI_MODEL_PROVIDER');
    }
    return undefined;
  }
  if (model === undefined || model.length === 0 || model.length > 128) {
    throw new Error('WOWAI_CLOUD_MODEL must name one explicit cloud model');
  }
  if (consent !== '1') {
    throw new Error('WOWAI_CLOUD_UPLOAD_CONSENT=1 is required for a cloud provider');
  }
  const descriptor = providers[provider];
  if ((environment[descriptor.credentialEnvironmentKey] ?? '').length === 0) {
    throw new Error(
      `${descriptor.credentialEnvironmentKey} is required for ${descriptor.displayName}`,
    );
  }
  return {
    provider,
    appServerProviderId: descriptor.appServerProviderId,
    displayName: descriptor.displayName,
    model,
    ...(descriptor.baseUrl === undefined ? {} : { baseUrl: new URL(descriptor.baseUrl) }),
    credentialEnvironmentKey: descriptor.credentialEnvironmentKey,
    vision: descriptor.supportsVision(model),
  };
}

export function cloudAppServerProviderArguments(configuration: CloudModelConfiguration): string[] {
  if (configuration.baseUrl === undefined) return [];
  const id = configuration.appServerProviderId;
  return [
    '-c',
    `model_provider="${id}"`,
    '-c',
    `model="${configuration.model}"`,
    '-c',
    `model_providers.${id}.name="${configuration.displayName}"`,
    '-c',
    `model_providers.${id}.base_url="${configuration.baseUrl.href.replace(/\/$/u, '')}"`,
    '-c',
    `model_providers.${id}.env_key="${configuration.credentialEnvironmentKey}"`,
    '-c',
    `model_providers.${id}.wire_api="responses"`,
    '-c',
    `model_providers.${id}.requires_openai_auth=false`,
    '-c',
    `model_providers.${id}.request_max_retries=0`,
    '-c',
    `model_providers.${id}.stream_max_retries=0`,
    '-c',
    `model_providers.${id}.supports_websockets=false`,
  ];
}
