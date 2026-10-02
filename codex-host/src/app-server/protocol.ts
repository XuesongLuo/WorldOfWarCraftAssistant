import { z } from 'zod';

export const appServerRequestIdSchema = z.union([z.string(), z.number()]);

const appServerErrorSchema = z
  .object({
    code: z.number().int(),
    message: z.string(),
    data: z.unknown().optional(),
  })
  .strict();

export type AppServerRequestId = z.infer<typeof appServerRequestIdSchema>;

export type AppServerResponse =
  | { id: AppServerRequestId; result: unknown }
  | { id: AppServerRequestId; error: z.infer<typeof appServerErrorSchema> };

export interface AppServerRequest {
  id: AppServerRequestId;
  method: string;
  params: unknown;
}

export interface AppServerNotification {
  method: string;
  params: unknown;
  emittedAtMs?: number;
}

export type AppServerMessage = AppServerResponse | AppServerRequest | AppServerNotification;

export class AppServerProtocolError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = 'AppServerProtocolError';
  }
}

export function parseAppServerMessage(line: string): AppServerMessage {
  let value: unknown;
  try {
    value = JSON.parse(line);
  } catch {
    throw new AppServerProtocolError('App Server emitted malformed JSON');
  }
  if (!isRecord(value)) throw new AppServerProtocolError('App Server message must be an object');

  if ('method' in value && typeof value.method === 'string') {
    if ('id' in value) {
      assertExactKeys(value, ['id', 'method', 'params']);
      return {
        id: appServerRequestIdSchema.parse(value.id),
        method: value.method,
        params: value.params,
      };
    }
    assertExactKeys(
      value,
      'emittedAtMs' in value ? ['method', 'params', 'emittedAtMs'] : ['method', 'params'],
    );
    if (
      'emittedAtMs' in value &&
      (!Number.isInteger(value.emittedAtMs) || Number(value.emittedAtMs) < 0)
    ) {
      throw new AppServerProtocolError('App Server notification has an invalid timestamp');
    }
    return {
      method: value.method,
      params: value.params,
      ...('emittedAtMs' in value ? { emittedAtMs: Number(value.emittedAtMs) } : {}),
    };
  }

  if ('id' in value && ('result' in value || 'error' in value)) {
    if ('result' in value && 'error' in value) {
      throw new AppServerProtocolError('App Server response cannot contain result and error');
    }
    const id = appServerRequestIdSchema.parse(value.id);
    if ('error' in value) {
      assertExactKeys(value, ['id', 'error']);
      return { id, error: appServerErrorSchema.parse(value.error) };
    }
    assertExactKeys(value, ['id', 'result']);
    return { id, result: value.result };
  }

  throw new AppServerProtocolError('App Server emitted an unknown message shape');
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

export function assertExactKeys(value: Record<string, unknown>, expected: readonly string[]): void {
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (actual.length !== wanted.length || actual.some((key, index) => key !== wanted[index])) {
    throw new AppServerProtocolError(`unexpected App Server fields: ${actual.join(',')}`);
  }
}
