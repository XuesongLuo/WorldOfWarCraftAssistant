import { PassThrough } from 'node:stream';

import { describe, expect, it } from 'vitest';

import { CodexHostServer } from '../src/host/server.js';
import type { ICodexRuntime } from '../src/runtime/mock-runtime.js';
import { DeterministicMockRuntime } from '../src/runtime/mock-runtime.js';
import type { AssistantRequest, ProtocolEnvelope } from '../src/protocol/types.js';
import {
  MAX_MESSAGE_BYTES,
  PROTOCOL_VERSION,
  assistantRequestSchema,
} from '../src/protocol/validation.js';

const fixtureRequest = assistantRequestSchema.parse({
  schemaVersion: '2.0',
  requestId: '11111111-1111-4111-8111-111111111111',
  conversationId: '22222222-2222-4222-8222-222222222222',
  createdAt: '2026-10-01T12:00:00Z',
  mode: 'general',
  locale: 'zh-CN',
  gameFlavor: 'retail',
  question: '测试 Host',
  character: {
    region: 'cn',
    realm: null,
    name: null,
    classId: null,
    specializationId: null,
    level: null,
  },
  images: [],
  observations: [],
  privacy: {
    selectedWindowOnly: true,
    screenObservationEnabled: false,
    rawFramesPersisted: false,
  },
  client: { addonVersion: null, companionVersion: '0.1.0-dev', uiScale: null },
  runtime: { engine: 'codex', provider: 'mock', model: 'deterministic', allowCloudUpload: false },
});

function envelope(
  kind: ProtocolEnvelope['kind'],
  sequence: number,
  payload: unknown,
  timeoutMs: number | null = null,
) {
  return JSON.stringify({
    protocolVersion: PROTOCOL_VERSION,
    messageId:
      kind === 'hello'
        ? 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
        : 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb',
    kind,
    requestId: kind === 'hello' || kind === 'ready' ? null : fixtureRequest.requestId,
    sequence,
    sentAt: '2026-10-01T12:00:00Z',
    timeoutMs,
    payload,
  });
}

const hello = envelope('hello', 0, {
  supportedVersions: [PROTOCOL_VERSION],
  maxMessageBytes: MAX_MESSAGE_BYTES,
});
const request = envelope('request', 0, fixtureRequest, 1_000);

async function runHost(
  inputChunks: Array<string | Uint8Array>,
  runtime: ICodexRuntime = new DeterministicMockRuntime(),
) {
  const input = new PassThrough();
  const output = new PassThrough();
  const errors = new PassThrough();
  let stdout = '';
  let stderr = '';
  output.setEncoding('utf8').on('data', (chunk: string) => (stdout += chunk));
  errors.setEncoding('utf8').on('data', (chunk: string) => (stderr += chunk));
  const server = new CodexHostServer({ input, output, errors, runtime });
  const completed = server.run().catch(() => undefined);
  for (const chunk of inputChunks) input.write(chunk);
  input.end();
  await completed;
  return { stdout, stderr };
}

describe('Codex Host stdio server', () => {
  it('negotiates and answers deterministic requests when chunks split arbitrarily', async () => {
    const joined = `${hello}\n${request}\n`;
    const result = await runHost([joined.slice(0, 11), joined.slice(11, 77), joined.slice(77)]);
    const lines = result.stdout
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as ProtocolEnvelope);

    expect(lines.map((line) => line.kind)).toEqual(['ready', 'response']);
    expect(lines[1]?.payload).toMatchObject({
      requestId: fixtureRequest.requestId,
      status: 'completed',
    });
    expect(result.stderr).toBe('');
  });

  it('keeps diagnostics off stdout on malformed UTF-8', async () => {
    const result = await runHost([Uint8Array.from([0xc3, 0x28, 0x0a])]);

    expect(result.stdout).toBe('');
    expect(result.stderr).toMatch(/UTF-8/u);
  });

  it('times out a stalled runtime without emitting a late response', async () => {
    const stalled: ICodexRuntime = {
      answer() {
        return new Promise(() => undefined);
      },
    };
    const result = await runHost([`${hello}\n${request}\n`], stalled);
    const lines = result.stdout
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as ProtocolEnvelope);

    expect(lines.map((line) => line.kind)).toEqual(['ready', 'error']);
    expect(lines[1]?.payload).toMatchObject({ code: 'AI_TIMEOUT', retryable: true });
  });

  it('cancels a pending request and emits no UI update for it', async () => {
    const stalled: ICodexRuntime = {
      answer(...args: [AssistantRequest, AbortSignal]) {
        const signal = args[1];
        return new Promise((_resolve, reject) => {
          signal.addEventListener(
            'abort',
            () => {
              reject(signal.reason instanceof Error ? signal.reason : new Error('request aborted'));
            },
            { once: true },
          );
        });
      },
    };
    const cancel = envelope('cancel', 1, { reason: 'user' });
    const result = await runHost([`${hello}\n${request}\n${cancel}\n`], stalled);
    const lines = result.stdout
      .trim()
      .split('\n')
      .map((line) => JSON.parse(line) as ProtocolEnvelope);

    expect(lines.map((line) => line.kind)).toEqual(['ready']);
  });
});
