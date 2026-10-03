import type { AssistantError } from '../protocol/types.js';

export class CodexRuntimeFailure extends Error {
  public constructor(public readonly assistantError: AssistantError) {
    super(assistantError.message);
    this.name = 'CodexRuntimeFailure';
  }
}

export function classifyCloudModelError(error: unknown): AssistantError | undefined {
  const signal = collectErrorSignal(error).toLowerCase();
  if (signal.length === 0) return undefined;

  if (
    /(?:missing|no|unset|not\s+(?:set|found|configured)).{0,40}(?:api[_ -]?key|credential)|(?:api[_ -]?key|credential).{0,40}(?:missing|unset|not\s+(?:set|found|configured))/u.test(
      signal,
    )
  ) {
    return {
      code: 'AI_CREDENTIALS_MISSING',
      message: 'The selected cloud provider credential is missing. Configure it and retry.',
      retryable: false,
    };
  }
  if (
    /\b401\b|unauthori[sz]ed|authentication\s+(?:failed|required)|invalid[_ -]?(?:api[_ -]?)?key|incorrect[_ -]?(?:api[_ -]?)?key/u.test(
      signal,
    )
  ) {
    return {
      code: 'AI_AUTH_FAILED',
      message: 'The cloud provider rejected the configured credential. Check it and retry.',
      retryable: false,
    };
  }
  if (/\b429\b|rate[_ -]?limit|too many requests|quota[_ -]?(?:exceeded|reached)/u.test(signal)) {
    return {
      code: 'AI_RATE_LIMITED',
      message: 'The cloud provider is rate limiting requests. Wait briefly, then retry.',
      retryable: true,
    };
  }
  if (
    /model[_ -]?(?:not[_ -]?found|unavailable|disabled|unsupported)|(?:unknown|invalid|unsupported)\s+model|\b404\b.{0,80}\bmodel\b|\bmodel\b.{0,80}\b404\b/u.test(
      signal,
    )
  ) {
    return {
      code: 'AI_MODEL_UNAVAILABLE',
      message: 'The configured cloud model is unavailable. Check the exact model name.',
      retryable: false,
    };
  }
  if (/timed?\s*out|timeout|etimedout/u.test(signal)) {
    return {
      code: 'AI_TIMEOUT',
      message: 'The cloud model request timed out. Retry when the connection is stable.',
      retryable: true,
    };
  }
  if (
    /enotfound|econnreset|econnrefused|enetunreach|ehostunreach|fetch failed|network(?:\s+error)?|dns|socket\s+(?:closed|disconnected)|connection\s+(?:closed|reset|refused)|tls|certificate/u.test(
      signal,
    )
  ) {
    return {
      code: 'AI_NETWORK_UNAVAILABLE',
      message: 'The cloud provider could not be reached. Check the network, then retry.',
      retryable: true,
    };
  }
  return undefined;
}

export function retryAfterMilliseconds(error: unknown, now = Date.now()): number {
  const signal = collectErrorSignal(error);
  const seconds = /retry[- ]?after\s*[:=]?\s*(\d{1,6})(?:\s*(?:seconds?|s))?/iu.exec(signal);
  if (seconds?.[1] !== undefined) return Math.min(Number(seconds[1]) * 1_000, 3_600_000);
  const delay = /try again in\s*(\d{1,6})\s*(milliseconds?|ms|seconds?|s|minutes?|m)/iu.exec(
    signal,
  );
  if (delay?.[1] !== undefined && delay[2] !== undefined) {
    const value = Number(delay[1]);
    const unit = delay[2].toLowerCase();
    const multiplier =
      unit.startsWith('m') && unit !== 'ms'
        ? 60_000
        : unit === 'ms' || unit.startsWith('millisecond')
          ? 1
          : 1_000;
    return Math.min(value * multiplier, 3_600_000);
  }
  const date = /retry[- ]?after\s*[:=]\s*([^\r\n]+)/iu.exec(signal)?.[1];
  if (date !== undefined) {
    const parsed = Date.parse(date.trim());
    if (Number.isFinite(parsed)) return Math.min(Math.max(0, parsed - now), 3_600_000);
  }
  return 30_000;
}

export function redactSensitiveText(
  value: string,
  credentials: ReadonlyArray<string | undefined> = [],
): string {
  let result = value;
  for (const credential of credentials) {
    if (credential !== undefined && credential.length >= 4) {
      result = result.split(credential).join('[REDACTED]');
    }
  }
  return result
    .replace(/\bBearer\s+[^\s,;"']+/giu, 'Bearer [REDACTED]')
    .replace(/\b(api[_-]?key|authorization)(\s*[:=]\s*)["']?[^\s,"'}]+/giu, '$1$2[REDACTED]')
    .replace(/\bsk-[A-Za-z0-9_-]{8,}\b/gu, '[REDACTED]');
}

export function safeDiagnosticMessage(error: unknown): string {
  if (error instanceof CodexRuntimeFailure) return error.assistantError.code;
  const message = error instanceof Error ? error.message : 'Unknown error';
  return redactSensitiveText(message, [process.env.OPENAI_API_KEY, process.env.DEEPSEEK_API_KEY]);
}

function collectErrorSignal(value: unknown, depth = 0, seen = new Set<unknown>()): string {
  if (depth > 5 || value === null || value === undefined || seen.has(value)) return '';
  if (typeof value === 'string' || typeof value === 'number') return String(value).slice(0, 4_096);
  if (typeof value !== 'object') return '';
  seen.add(value);
  if (value instanceof Error) {
    const parts = [value.name, value.message];
    for (const property of Object.values(value as unknown as Record<string, unknown>)) {
      parts.push(collectErrorSignal(property, depth + 1, seen));
    }
    return parts.join(' ');
  }
  return Object.entries(value as Record<string, unknown>)
    .flatMap(([key, item]) => [key, collectErrorSignal(item, depth + 1, seen)])
    .join(' ');
}
