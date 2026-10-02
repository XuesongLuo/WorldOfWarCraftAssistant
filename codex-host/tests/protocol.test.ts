import { readFileSync } from 'node:fs';

import { describe, expect, it } from 'vitest';

import { DeterministicMockHost } from '../src/runtime/mock-runtime.js';
import { ProtocolSequenceTracker } from '../src/protocol/session.js';
import type { AssistantResponse, ProtocolEnvelope } from '../src/protocol/types.js';
import {
  MAX_MESSAGE_BYTES,
  assistantRequestSchema,
  assistantResponseSchema,
  envelopeSchema,
  parseJsonLine,
} from '../src/protocol/validation.js';

interface ContractCase {
  name: string;
  accepted: boolean;
  value: unknown;
}

interface ContractFixtures {
  assistantRequests: ContractCase[];
  assistantResponses: ContractCase[];
}

const fixturePath = new URL('../../contracts/tests/validation-cases.json', import.meta.url);
const fixtures = JSON.parse(readFileSync(fixturePath, 'utf8')) as ContractFixtures;
const validRequest = assistantRequestSchema.parse(
  fixtures.assistantRequests.find((item) => item.accepted)?.value,
);

function envelope(
  kind: ProtocolEnvelope['kind'],
  sequence: number,
  payload: unknown,
): ProtocolEnvelope {
  const requestId = kind === 'hello' || kind === 'ready' ? null : validRequest.requestId;
  return envelopeSchema.parse({
    protocolVersion: '2.0',
    messageId:
      kind === 'hello'
        ? 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
        : 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb',
    kind,
    requestId,
    sequence,
    sentAt: '2026-09-29T12:00:00Z',
    timeoutMs: kind === 'request' ? 30_000 : null,
    payload,
  });
}

const hello = envelope('hello', 0, {
  supportedVersions: ['2.0'],
  maxMessageBytes: MAX_MESSAGE_BYTES,
});
const ready = envelope('ready', 1, {
  selectedVersion: '2.0',
  maxMessageBytes: MAX_MESSAGE_BYTES,
});
const requestEnvelope = envelope('request', 0, validRequest);

describe('shared protocol contract fixtures', () => {
  it.each(fixtures.assistantRequests)('$name', ({ value, accepted }) => {
    expect(assistantRequestSchema.safeParse(value).success).toBe(accepted);
  });

  it.each(fixtures.assistantResponses)('$name', ({ value, accepted }) => {
    expect(assistantResponseSchema.safeParse(value).success).toBe(accepted);
  });
});

describe('JSONL envelope boundaries', () => {
  it('rejects malformed UTF-8', () => {
    expect(() => parseJsonLine(Uint8Array.from([0xc3, 0x28]))).toThrow(/UTF-8/u);
  });

  it('rejects oversized messages', () => {
    expect(() => parseJsonLine(' '.repeat(MAX_MESSAGE_BYTES + 1))).toThrow(/maximum/u);
  });

  it('rejects protocol version mismatches', () => {
    const mismatched = { ...hello, protocolVersion: '3.0' };
    expect(envelopeSchema.safeParse(mismatched).success).toBe(false);
  });

  it('rejects out-of-order response messages', () => {
    const tracker = new ProtocolSequenceTracker();
    tracker.accept(hello);
    tracker.accept(ready);
    tracker.accept(requestEnvelope);
    const response = envelope('response', 2, {
      schemaVersion: '2.0',
      requestId: validRequest.requestId,
      status: 'completed',
      mode: validRequest.mode,
      answer: { summary: '', nextSteps: [], constraints: [], uncertainties: [], followUp: null },
      sources: [],
      provenance: [],
      usage: {
        imageUsed: false,
        knowledgeUsed: false,
        screenObservationUsed: false,
        addonBridgeUsed: false,
        runtime: 'codex',
        provider: 'mock',
      },
      error: null,
    });
    expect(() => {
      tracker.accept(response);
    }).toThrow(/out-of-order/u);
  });
});

describe('deterministic mock host', () => {
  it('returns byte-for-byte stable logical responses without a model', async () => {
    const first = new DeterministicMockHost();
    const second = new DeterministicMockHost();
    await first.handleLine(JSON.stringify(hello));
    await second.handleLine(JSON.stringify(hello));
    const firstResponse = await first.handleLine(JSON.stringify(requestEnvelope));
    const secondResponse = await second.handleLine(JSON.stringify(requestEnvelope));
    expect(firstResponse).toEqual(secondResponse);
    expect((firstResponse.payload as AssistantResponse).requestId).toBe(validRequest.requestId);
  });
});
