import type { AssistantRequest, AssistantResponse, ProtocolEnvelope } from '../protocol/types.js';
import { ProtocolSequenceTracker } from '../protocol/session.js';
import {
  MAX_MESSAGE_BYTES,
  PROTOCOL_VERSION,
  ProtocolValidationError,
  assistantRequestSchema,
  parseJsonLine,
} from '../protocol/validation.js';

const READY_MESSAGE_ID = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa';
const RESPONSE_MESSAGE_ID = 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb';
const FIXED_TIMESTAMP = '2000-01-01T00:00:00Z';

export interface ICodexRuntime {
  answer(request: AssistantRequest, signal: AbortSignal): Promise<AssistantResponse>;
  close?(): Promise<void>;
}

export class DeterministicMockRuntime implements ICodexRuntime {
  public answer(request: AssistantRequest, signal: AbortSignal): Promise<AssistantResponse> {
    if (signal.aborted) {
      return Promise.reject(
        signal.reason instanceof Error ? signal.reason : new Error('request aborted'),
      );
    }
    const validRequest = assistantRequestSchema.parse(request);
    return Promise.resolve({
      schemaVersion: PROTOCOL_VERSION,
      requestId: validRequest.requestId,
      status: 'completed',
      mode: validRequest.mode,
      answer: {
        summary: `[mock:${validRequest.mode}] ${validRequest.question}`,
        nextSteps: ['确认问题与角色上下文', '使用正式运行时时重新请求'],
        constraints: ['确定性离线模拟器未连接模型或知识源'],
        uncertainties: [],
        followUp: null,
      },
      sources: [],
      provenance: validRequest.observations.map((observation) => ({
        observationId: observation.id,
        source: observation.source,
        confidence: observation.confidence,
        reason: 'deterministic mock acknowledged the supplied observation',
      })),
      usage: {
        imageUsed: validRequest.images.length === 1,
        knowledgeUsed: false,
        screenObservationUsed: validRequest.observations.some(
          (observation) => observation.source === 'screen-observed',
        ),
        addonBridgeUsed: validRequest.observations.some(
          (observation) => observation.source === 'plugin-public',
        ),
        runtime: 'codex',
        provider: 'mock',
      },
      error: null,
    });
  }
}

export class DeterministicMockHost {
  private readonly sequence = new ProtocolSequenceTracker();

  public constructor(private readonly runtime: ICodexRuntime = new DeterministicMockRuntime()) {}

  public async handleLine(line: Uint8Array | string): Promise<ProtocolEnvelope> {
    const envelope = parseJsonLine(line);
    this.sequence.accept(envelope);
    if (envelope.kind === 'hello') {
      const ready = {
        protocolVersion: PROTOCOL_VERSION,
        messageId: READY_MESSAGE_ID,
        kind: 'ready',
        requestId: null,
        sequence: 1,
        sentAt: FIXED_TIMESTAMP,
        timeoutMs: null,
        payload: { selectedVersion: PROTOCOL_VERSION, maxMessageBytes: MAX_MESSAGE_BYTES },
      } satisfies ProtocolEnvelope;
      this.sequence.accept(ready);
      return ready;
    }
    if (envelope.kind !== 'request') {
      throw new ProtocolValidationError(`mock host does not accept ${envelope.kind} as input`);
    }
    const request = assistantRequestSchema.parse(envelope.payload);
    const response = await this.runtime.answer(request, new AbortController().signal);
    const result = {
      protocolVersion: PROTOCOL_VERSION,
      messageId: RESPONSE_MESSAGE_ID,
      kind: 'response',
      requestId: request.requestId,
      sequence: 1,
      sentAt: FIXED_TIMESTAMP,
      timeoutMs: null,
      payload: response,
    } satisfies ProtocolEnvelope;
    this.sequence.accept(result);
    return result;
  }
}
