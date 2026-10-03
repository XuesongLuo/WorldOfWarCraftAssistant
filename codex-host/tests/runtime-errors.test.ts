import { describe, expect, it } from 'vitest';

import { AppServerRequestError, AppServerTurnError } from '../src/app-server/client.js';
import {
  classifyCloudModelError,
  redactSensitiveText,
  safeDiagnosticMessage,
} from '../src/runtime/errors.js';

describe('cloud error classification and redaction', () => {
  it.each([
    [
      new AppServerTurnError('failed', { message: 'No API key configured' }),
      'AI_CREDENTIALS_MISSING',
      false,
    ],
    [
      new AppServerTurnError('failed', { status: 401, message: 'Unauthorized' }),
      'AI_AUTH_FAILED',
      false,
    ],
    [
      new AppServerTurnError('failed', {
        status: 404,
        message: 'model deepseek-missing not found',
      }),
      'AI_MODEL_UNAVAILABLE',
      false,
    ],
    [
      new AppServerRequestError('turn/start', {
        code: -32_000,
        message: '429 too many requests',
      }),
      'AI_RATE_LIMITED',
      true,
    ],
    [new AppServerTurnError('failed', { code: 'ENOTFOUND' }), 'AI_NETWORK_UNAVAILABLE', true],
    [new AppServerTurnError('failed', { message: 'request timed out' }), 'AI_TIMEOUT', true],
  ] as const)('classifies %s as %s', (error, code, retryable) => {
    expect(classifyCloudModelError(error)).toMatchObject({ code, retryable });
  });

  it('does not expose credentials, authorization headers, or provider response text in diagnostics', () => {
    const secret = 'sk-test-secret-value';
    const source = `Authorization: Bearer ${secret}\napi_key=${secret}\n${secret}`;
    const redacted = redactSensitiveText(source, [secret]);
    expect(redacted).not.toContain(secret);
    expect(redacted).toContain('[REDACTED]');

    const remote = new AppServerRequestError('turn/start', {
      code: 401,
      message: `Unauthorized bearer ${secret}`,
      data: { authorization: secret },
    });
    expect(remote.message).not.toContain(secret);
    expect(safeDiagnosticMessage(remote)).toBe('The Codex App Server rejected a request.');
  });

  it('returns no guess for an unrecognized provider failure', () => {
    expect(
      classifyCloudModelError(new Error('provider returned an unspecified failure')),
    ).toBeUndefined();
  });
});
