import { z } from 'zod';

export const PROTOCOL_VERSION = '2.0' as const;
export const MAX_MESSAGE_BYTES = 1_048_576;
export const DEFAULT_TIMEOUT_MS = 30_000;
export const MAX_TIMEOUT_MS = 120_000;

const uuidSchema = z
  .string()
  .regex(
    /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu,
    'expected RFC 4122 UUID',
  );
const utcTimestampSchema = z
  .string()
  .regex(/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/u)
  .refine((value) => !Number.isNaN(Date.parse(value)), 'expected ISO-8601 UTC timestamp');
const modeSchema = z.enum(['achievement', 'mount', 'pet', 'gear', 'general', 'coach']);
const providerSchema = z.enum(['local-ollama', 'local-lmstudio', 'openai', 'deepseek', 'mock']);
const cloudProviderSchema = z.enum(['openai', 'deepseek']);
const nullableBoundedString = (maximum: number) => z.string().max(maximum).nullable();
const bridgeFieldSchema = z.enum([
  'class',
  'classId',
  'specialization',
  'specializationId',
  'level',
  'zone',
  'mapId',
  'activity',
  'encounterId',
  'achievementId',
  'criteria',
  'event',
  'skills',
  'talents',
  'actionSlots',
  'keyBindings',
]);

export const assistantErrorCodeSchema = z.enum([
  'WOW_WINDOW_NOT_FOUND',
  'ANCHOR_NOT_FOUND',
  'CAPTURE_DENIED',
  'CAPTURE_EMPTY',
  'PRIVACY_CONFIRM_REQUIRED',
  'CODEX_NOT_INSTALLED',
  'CODEX_VERSION_MISMATCH',
  'CODEX_START_FAILED',
  'CODEX_PROTOCOL_ERROR',
  'CODEX_TOOL_BLOCKED',
  'MODEL_PROVIDER_UNAVAILABLE',
  'MODEL_CAPABILITY_MISSING',
  'AI_CREDENTIALS_MISSING',
  'AI_AUTH_FAILED',
  'AI_MODEL_UNAVAILABLE',
  'AI_NETWORK_UNAVAILABLE',
  'AI_RATE_LIMITED',
  'AI_TIMEOUT',
  'AI_INVALID_RESPONSE',
  'KNOWLEDGE_STALE',
  'POLICY_BLOCKED',
  'BRIDGE_FRAME_INVALID',
  'OBSERVATION_STALE',
  'TEACHING_SESSION_INACTIVE',
]);

export const assistantErrorSchema = z
  .object({
    code: assistantErrorCodeSchema,
    message: z.string().min(1).max(512),
    retryable: z.boolean(),
  })
  .strict();

export const assistantRequestSchema = z
  .object({
    schemaVersion: z.literal(PROTOCOL_VERSION),
    requestId: uuidSchema,
    conversationId: uuidSchema,
    createdAt: utcTimestampSchema,
    mode: modeSchema,
    locale: z.string().min(2).max(35),
    gameFlavor: z.literal('retail'),
    question: z
      .string()
      .trim()
      .refine((value) => Array.from(value).length >= 1 && Array.from(value).length <= 4000),
    character: z
      .object({
        region: z.enum(['cn', 'us', 'eu', 'kr', 'tw']),
        realm: nullableBoundedString(128),
        name: nullableBoundedString(128),
        classId: z.number().int().positive().nullable(),
        specializationId: z.number().int().positive().nullable(),
        level: z.number().int().min(1).max(100).nullable(),
      })
      .strict(),
    images: z
      .array(
        z
          .object({
            id: uuidSchema,
            mimeType: z.literal('image/png'),
            captureScope: z.enum(['wow-window', 'selected-region', 'tooltip']),
            sha256: z.string().regex(/^[0-9a-f]{64}$/iu),
            dataBase64: z
              .string()
              .min(12)
              .max(956_000)
              .regex(/^[A-Za-z0-9+/]+={0,2}$/u),
            privacyMaskApplied: z.boolean(),
            userConfirmed: z.literal(true),
            uploadDestination: cloudProviderSchema,
            uploadPurpose: z.literal('visual-question'),
            uploadConfirmedAt: utcTimestampSchema,
            consentNoticeVersion: z.literal(1),
          })
          .strict(),
      )
      .max(1),
    visualBridge: z
      .object({
        protocolVersion: z.literal(1),
        source: z.literal('plugin-public'),
        sequence: z.number().int().min(0).max(0xffff_ffff),
        capturedAt: utcTimestampSchema,
        confidence: z.number().min(0).max(1),
        allowedFields: z.array(bridgeFieldSchema).max(16),
        unavailableFields: z.array(bridgeFieldSchema).max(16),
      })
      .strict()
      .nullable()
      .optional(),
    observations: z
      .array(
        z
          .object({
            id: uuidSchema,
            source: z.enum(['plugin-public', 'profile-cache', 'screen-observed', 'model-inferred']),
            kind: z.enum([
              'build',
              'game-state',
              'achievement-progress',
              'combat-ui',
              'screen-text',
            ]),
            capturedAt: utcTimestampSchema,
            confidence: z.number().min(0).max(1),
            summary: z.string().min(1).max(4000),
          })
          .strict(),
      )
      .max(32),
    privacy: z
      .object({
        selectedWindowOnly: z.literal(true),
        screenObservationEnabled: z.boolean(),
        rawFramesPersisted: z.literal(false),
      })
      .strict(),
    client: z
      .object({
        addonVersion: nullableBoundedString(64),
        companionVersion: z.string().min(1).max(64),
        uiScale: z.number().min(0.5).max(4).nullable(),
      })
      .strict(),
    runtime: z
      .object({
        engine: z.literal('codex'),
        provider: providerSchema,
        model: z.string().min(1).max(128),
        allowCloudUpload: z.boolean(),
      })
      .strict()
      .refine(
        (value) => !cloudProviderSchema.safeParse(value.provider).success || value.allowCloudUpload,
        {
          message: 'cloud providers require explicit upload consent',
        },
      )
      .refine(
        (value) => cloudProviderSchema.safeParse(value.provider).success || !value.allowCloudUpload,
        {
          message: 'only cloud providers may enable cloud upload',
        },
      ),
  })
  .strict()
  .superRefine((value, context) => {
    if (
      value.images.length > 0 &&
      (!cloudProviderSchema.safeParse(value.runtime.provider).success ||
        value.images[0]?.uploadDestination !== value.runtime.provider)
    ) {
      context.addIssue({
        code: 'custom',
        path: ['runtime', 'provider'],
        message: 'confirmed images require the matching explicitly enabled cloud provider',
      });
    }
  });

const answerSchema = z
  .object({
    summary: z.string().max(8000),
    nextSteps: z.array(z.string().min(1).max(1000)).max(5),
    constraints: z.array(z.string().min(1).max(1000)).max(10),
    uncertainties: z.array(z.string().min(1).max(1000)).max(10),
    followUp: nullableBoundedString(1000),
  })
  .strict();

export const assistantResponseSchema = z
  .object({
    schemaVersion: z.literal(PROTOCOL_VERSION),
    requestId: uuidSchema,
    status: z.enum(['completed', 'needs_context', 'refused', 'failed']),
    mode: modeSchema,
    answer: answerSchema,
    sources: z
      .array(
        z
          .object({
            title: z.string().min(1).max(256),
            url: z.url().startsWith('https://').nullable(),
            dataVersion: nullableBoundedString(128),
          })
          .strict(),
      )
      .max(20),
    provenance: z
      .array(
        z
          .object({
            observationId: uuidSchema,
            source: z.enum(['plugin-public', 'profile-cache', 'screen-observed', 'model-inferred']),
            confidence: z.number().min(0).max(1),
            reason: z.string().min(1).max(1000),
          })
          .strict(),
      )
      .max(32),
    usage: z
      .object({
        imageUsed: z.boolean(),
        knowledgeUsed: z.boolean(),
        screenObservationUsed: z.boolean(),
        addonBridgeUsed: z.boolean(),
        runtime: z.literal('codex'),
        provider: providerSchema,
      })
      .strict(),
    error: assistantErrorSchema.nullable(),
  })
  .strict()
  .superRefine((value, context) => {
    if (value.status === 'failed' && value.error === null) {
      context.addIssue({ code: 'custom', message: 'failed response requires error' });
    }
    if (value.status !== 'failed' && value.error !== null) {
      context.addIssue({ code: 'custom', message: 'non-failed response cannot contain error' });
    }
  });

const envelopeBaseSchema = z
  .object({
    protocolVersion: z.literal(PROTOCOL_VERSION),
    messageId: uuidSchema,
    kind: z.enum(['hello', 'ready', 'request', 'cancel', 'response', 'error']),
    requestId: uuidSchema.nullable(),
    sequence: z.number().int().nonnegative(),
    sentAt: utcTimestampSchema,
    timeoutMs: z.number().int().min(1000).max(MAX_TIMEOUT_MS).nullable(),
    payload: z.unknown(),
  })
  .strict();

export const envelopeSchema = envelopeBaseSchema.superRefine((value, context) => {
  const addPayloadIssue = (message: string): void => {
    context.addIssue({ code: 'custom', path: ['payload'], message });
  };

  if (value.kind === 'hello') {
    if (value.requestId !== null || value.sequence !== 0 || value.timeoutMs !== null) {
      addPayloadIssue('invalid hello envelope metadata');
    }
    const result = z
      .object({
        supportedVersions: z.array(z.literal(PROTOCOL_VERSION)).min(1),
        maxMessageBytes: z.number().int().min(1024).max(MAX_MESSAGE_BYTES),
      })
      .strict()
      .safeParse(value.payload);
    if (!result.success) addPayloadIssue('invalid hello payload');
  } else if (value.kind === 'ready') {
    if (value.requestId !== null || value.sequence !== 1 || value.timeoutMs !== null) {
      addPayloadIssue('invalid ready envelope metadata');
    }
    const result = z
      .object({
        selectedVersion: z.literal(PROTOCOL_VERSION),
        maxMessageBytes: z.number().int().min(1024).max(MAX_MESSAGE_BYTES),
      })
      .strict()
      .safeParse(value.payload);
    if (!result.success) addPayloadIssue('invalid ready payload');
  } else if (value.requestId === null) {
    addPayloadIssue('request-scoped message requires requestId');
  } else if (value.kind === 'request') {
    if (value.sequence !== 0 || value.timeoutMs === null)
      addPayloadIssue('invalid request metadata');
    const result = assistantRequestSchema.safeParse(value.payload);
    if (!result.success || result.data.requestId !== value.requestId) {
      addPayloadIssue('invalid request payload');
    }
  } else if (value.kind === 'response') {
    if (value.timeoutMs !== null) addPayloadIssue('response timeout must be null');
    const result = assistantResponseSchema.safeParse(value.payload);
    if (!result.success || result.data.requestId !== value.requestId) {
      addPayloadIssue('invalid response payload');
    }
  } else if (value.kind === 'error') {
    if (value.timeoutMs !== null || !assistantErrorSchema.safeParse(value.payload).success) {
      addPayloadIssue('invalid error payload');
    }
  } else {
    const result = z
      .object({ reason: z.enum(['user', 'timeout', 'shutdown']) })
      .strict()
      .safeParse(value.payload);
    if (value.timeoutMs !== null || !result.success) addPayloadIssue('invalid cancel payload');
  }
});

export class ProtocolValidationError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = 'ProtocolValidationError';
  }
}

export function parseJsonLine(input: Uint8Array | string) {
  const bytes = typeof input === 'string' ? new TextEncoder().encode(input) : input;
  if (bytes.byteLength > MAX_MESSAGE_BYTES) {
    throw new ProtocolValidationError('message exceeds maximum byte length');
  }
  if (bytes.byteLength >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
    throw new ProtocolValidationError('UTF-8 BOM is not permitted');
  }

  let text: string;
  try {
    text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch {
    throw new ProtocolValidationError('message is not valid UTF-8');
  }
  if (text.includes('\u0000') || text.includes('\r') || text.includes('\n')) {
    throw new ProtocolValidationError('message must contain exactly one JSON line');
  }

  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new ProtocolValidationError('message is not valid JSON');
  }
  const result = envelopeSchema.safeParse(value);
  if (!result.success) {
    throw new ProtocolValidationError(z.prettifyError(result.error));
  }
  return result.data;
}
