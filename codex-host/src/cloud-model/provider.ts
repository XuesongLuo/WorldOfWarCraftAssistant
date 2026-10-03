export const cloudModelProviders = [
  'openai',
  'deepseek',
  'xai',
  'openrouter',
  'dashscope',
  'azure-openai',
] as const;

export type CloudModelProvider = (typeof cloudModelProviders)[number];
export type CloudConnectionMode = 'native-responses' | 'compatible-responses' | 'azure-responses';

export interface CloudModelConfiguration {
  provider: CloudModelProvider;
  appServerProviderId: string;
  displayName: string;
  connectionMode: CloudConnectionMode;
  model: string;
  baseUrl: URL;
  credentialEnvironmentKey: 'WOWAI_ACTIVE_API_KEY';
  credentialHeader?: string;
  organization?: string;
  queryParameters?: Readonly<Record<string, string>>;
  vision: boolean;
  destinationHost: string;
}

type Environment = Record<string, string | undefined>;

interface ProviderDescriptor {
  appServerProviderId: string;
  displayName: string;
  connectionMode: CloudConnectionMode;
  baseUrl(environment: Environment): URL;
  credentialHeader?: string;
  supportsVision(model: string): boolean;
}

const fixedUrl =
  (value: string): (() => URL) =>
  () =>
    new URL(value);

const providers: Record<CloudModelProvider, ProviderDescriptor> = {
  openai: {
    appServerProviderId: 'wowai_openai',
    displayName: 'OpenAI',
    connectionMode: 'native-responses',
    baseUrl: fixedUrl('https://api.openai.com/v1'),
    supportsVision: (model) => /^(gpt-(4o|4\.1|5|6)(-|$)|o[134](-|$))/u.test(model),
  },
  deepseek: {
    appServerProviderId: 'wowai_deepseek',
    displayName: 'DeepSeek',
    connectionMode: 'compatible-responses',
    baseUrl: fixedUrl('https://api.deepseek.com'),
    supportsVision: (model) => model === 'deepseek-flash',
  },
  xai: {
    appServerProviderId: 'wowai_xai',
    displayName: 'xAI',
    connectionMode: 'compatible-responses',
    baseUrl: fixedUrl('https://api.x.ai/v1'),
    supportsVision: (model) => /^grok-4(\.|-|$)/u.test(model),
  },
  openrouter: {
    appServerProviderId: 'wowai_openrouter',
    displayName: 'OpenRouter',
    connectionMode: 'compatible-responses',
    baseUrl: fixedUrl('https://openrouter.ai/api/v1'),
    // Capability is route-dependent; screenshots fail closed until a catalog snapshot is pinned.
    supportsVision: () => false,
  },
  dashscope: {
    appServerProviderId: 'wowai_dashscope',
    displayName: 'Alibaba Cloud Model Studio (Qwen)',
    connectionMode: 'compatible-responses',
    baseUrl: dashScopeUrl,
    supportsVision: (model) =>
      /^(qwen3\.(5|6|7|8)-(max|plus|flash)|qwen3-vl-(plus|flash)|qwen-vl-(max|plus))/u.test(model),
  },
  'azure-openai': {
    appServerProviderId: 'wowai_azure_openai',
    displayName: 'Azure OpenAI',
    connectionMode: 'azure-responses',
    baseUrl: azureOpenAiUrl,
    credentialHeader: 'api-key',
    // A deployment name does not prove the underlying model accepts images.
    supportsVision: () => false,
  },
};

export function isCloudModelProvider(value: string | undefined): value is CloudModelProvider {
  return cloudModelProviders.some((candidate) => candidate === value);
}

export function cloudModelConfigurationFromEnvironment(
  environment: Environment,
): CloudModelConfiguration | undefined {
  const provider = environment.WOWAI_MODEL_PROVIDER;
  const model = environment.WOWAI_CLOUD_MODEL;
  const consent = environment.WOWAI_CLOUD_UPLOAD_CONSENT;
  if (!isCloudModelProvider(provider)) {
    if (model !== undefined || consent !== undefined) {
      throw new Error('cloud model and consent require a supported WOWAI_MODEL_PROVIDER');
    }
    return undefined;
  }
  if (model === undefined || !/^[\x20-\x7e]{1,128}$/u.test(model)) {
    throw new Error('WOWAI_CLOUD_MODEL must name one explicit printable cloud model');
  }
  if (consent !== '1')
    throw new Error('WOWAI_CLOUD_UPLOAD_CONSENT=1 is required for a cloud provider');
  if (activeCredential(environment, provider).length === 0) {
    throw new Error('WOWAI_ACTIVE_API_KEY is required for the selected cloud provider');
  }
  const descriptor = providers[provider];
  const baseUrl = descriptor.baseUrl(environment);
  const organization = optionalIdentifier(environment.WOWAI_CLOUD_ORGANIZATION, 'organization');
  const apiVersion =
    provider === 'azure-openai'
      ? optionalIdentifier(environment.WOWAI_AZURE_API_VERSION ?? 'v1', 'api-version')
      : undefined;
  return {
    provider,
    appServerProviderId: descriptor.appServerProviderId,
    displayName: descriptor.displayName,
    connectionMode: descriptor.connectionMode,
    model,
    baseUrl,
    credentialEnvironmentKey: 'WOWAI_ACTIVE_API_KEY',
    ...(descriptor.credentialHeader === undefined
      ? {}
      : { credentialHeader: descriptor.credentialHeader }),
    ...(organization === undefined ? {} : { organization }),
    ...(apiVersion === undefined ? {} : { queryParameters: { 'api-version': apiVersion } }),
    vision: descriptor.supportsVision(model),
    destinationHost: baseUrl.hostname,
  };
}

export function cloudAppServerProviderArguments(configuration: CloudModelConfiguration): string[] {
  const id = configuration.appServerProviderId;
  const result = [
    '-c',
    `model_provider="${id}"`,
    '-c',
    `model="${tomlString(configuration.model)}"`,
    '-c',
    `model_providers.${id}.name="${tomlString(configuration.displayName)}"`,
    '-c',
    `model_providers.${id}.base_url="${tomlString(configuration.baseUrl.href.replace(/\/$/u, ''))}"`,
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
  if (configuration.credentialHeader === undefined) {
    result.push('-c', `model_providers.${id}.env_key="${configuration.credentialEnvironmentKey}"`);
  } else {
    result.push(
      '-c',
      `model_providers.${id}.env_http_headers={"${configuration.credentialHeader}"="${configuration.credentialEnvironmentKey}"}`,
    );
  }
  if (configuration.organization !== undefined) {
    result.push(
      '-c',
      `model_providers.${id}.http_headers={"OpenAI-Organization"="${tomlString(configuration.organization)}"}`,
    );
  }
  if (configuration.queryParameters !== undefined) {
    const pairs = Object.entries(configuration.queryParameters)
      .map(([key, value]) => `"${tomlString(key)}"="${tomlString(value)}"`)
      .join(',');
    result.push('-c', `model_providers.${id}.query_params={${pairs}}`);
  }
  return result;
}

export function sanitizedAppServerEnvironment(
  environment: Environment,
  configuration: CloudModelConfiguration | undefined,
): NodeJS.ProcessEnv {
  const result: NodeJS.ProcessEnv = {};
  for (const [key, value] of Object.entries(environment)) {
    if (value !== undefined && !isProviderCredentialName(key)) result[key] = value;
  }
  if (configuration !== undefined) {
    const secret = activeCredential(environment, configuration.provider);
    if (secret.length === 0) throw new Error('active credential missing');
    result.WOWAI_ACTIVE_API_KEY = secret;
  }
  return result;
}

function activeCredential(environment: Environment, provider: CloudModelProvider): string {
  if (environment.WOWAI_ACTIVE_API_KEY !== undefined) return environment.WOWAI_ACTIVE_API_KEY;
  const legacy: Record<CloudModelProvider, string> = {
    openai: 'OPENAI_API_KEY',
    deepseek: 'DEEPSEEK_API_KEY',
    xai: 'XAI_API_KEY',
    openrouter: 'OPENROUTER_API_KEY',
    dashscope: 'DASHSCOPE_API_KEY',
    'azure-openai': 'AZURE_OPENAI_API_KEY',
  };
  return environment[legacy[provider]] ?? '';
}

function isProviderCredentialName(name: string): boolean {
  return /^(WOWAI_ACTIVE_API_KEY|OPENAI_API_KEY|DEEPSEEK_API_KEY|XAI_API_KEY|OPENROUTER_API_KEY|DASHSCOPE_API_KEY|AZURE_OPENAI_API_KEY|ANTHROPIC_API_KEY|GEMINI_API_KEY|MISTRAL_API_KEY)$/u.test(
    name,
  );
}

function azureOpenAiUrl(environment: Environment): URL {
  const resource = requiredIdentifier(environment.WOWAI_AZURE_RESOURCE, 'Azure resource', 63);
  return new URL(`https://${resource}.openai.azure.com/openai/v1`);
}

function dashScopeUrl(environment: Environment): URL {
  const workspace = requiredIdentifier(environment.WOWAI_DASHSCOPE_WORKSPACE, 'workspace', 64);
  const suffixes: Readonly<Record<string, string>> = {
    beijing: 'cn-beijing.maas.aliyuncs.com',
    hongkong: 'cn-hongkong.maas.aliyuncs.com',
    singapore: 'ap-southeast-1.maas.aliyuncs.com',
    tokyo: 'ap-northeast-1.maas.aliyuncs.com',
    frankfurt: 'eu-central-1.maas.aliyuncs.com',
    virginia: 'us-east-1.maas.aliyuncs.com',
  };
  const region = environment.WOWAI_DASHSCOPE_REGION;
  const suffix = region === undefined ? undefined : suffixes[region];
  if (suffix === undefined) throw new Error('WOWAI_DASHSCOPE_REGION is not allowlisted');
  return new URL(`https://${workspace}.${suffix}/compatible-mode/v1`);
}

function requiredIdentifier(value: string | undefined, label: string, maximum: number): string {
  if (
    value === undefined ||
    value.length > maximum ||
    !/^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/u.test(value)
  ) {
    throw new Error(`${label} must be a lowercase DNS-safe identifier`);
  }
  return value;
}

function optionalIdentifier(value: string | undefined, label: string): string | undefined {
  if (value === undefined || value.length === 0) return undefined;
  if (value.length > 128 || !/^[A-Za-z0-9._-]+$/u.test(value))
    throw new Error(`${label} contains unsupported characters`);
  return value;
}

function tomlString(value: string): string {
  return value.replace(/\\/gu, '\\\\').replace(/"/gu, '\\"');
}
