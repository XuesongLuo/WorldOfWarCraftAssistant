import { randomUUID } from 'node:crypto';
import type { Readable, Writable } from 'node:stream';

import { JsonLineReader } from './json-line-reader.js';
import { ProtocolSequenceTracker } from '../protocol/session.js';
import type { AssistantError, AssistantRequest, ProtocolEnvelope } from '../protocol/types.js';
import {
  MAX_MESSAGE_BYTES,
  PROTOCOL_VERSION,
  ProtocolValidationError,
  assistantRequestSchema,
  envelopeSchema,
  parseJsonLine,
} from '../protocol/validation.js';
import type { ICodexRuntime } from '../runtime/mock-runtime.js';

interface PendingRequest {
  controller: AbortController;
  timer: NodeJS.Timeout;
}

export interface CodexHostServerOptions {
  input: Readable;
  output: Writable;
  errors: Writable;
  runtime: ICodexRuntime;
}

export class CodexHostServer {
  private readonly reader = new JsonLineReader(MAX_MESSAGE_BYTES);
  private readonly sequence = new ProtocolSequenceTracker();
  private readonly pending = new Map<string, PendingRequest>();
  private readonly active = new Set<Promise<void>>();
  private negotiatedMaxBytes = MAX_MESSAGE_BYTES;

  public constructor(private readonly options: CodexHostServerOptions) {}

  public async run(): Promise<void> {
    try {
      for await (const chunk of this.options.input) {
        for (const line of this.reader.push(toBytes(chunk))) this.handleLine(line);
      }
      this.reader.finish();
      await Promise.allSettled(this.active);
    } catch (error) {
      this.abortAll(error);
      this.log(error);
      throw error;
    }
  }

  private handleLine(line: string): void {
    const envelope = parseJsonLine(line);
    this.sequence.accept(envelope);
    switch (envelope.kind) {
      case 'hello':
        this.handleHello(envelope);
        return;
      case 'request':
        this.handleRequest(envelope);
        return;
      case 'cancel':
        this.handleCancel(envelope);
        return;
      default:
        throw new ProtocolValidationError(`Host cannot receive ${envelope.kind}`);
    }
  }

  private handleHello(envelope: ProtocolEnvelope): void {
    const payload = envelope.payload as { maxMessageBytes: number };
    this.negotiatedMaxBytes = Math.min(payload.maxMessageBytes, MAX_MESSAGE_BYTES);
    this.send({
      protocolVersion: PROTOCOL_VERSION,
      messageId: randomUUID(),
      kind: 'ready',
      requestId: null,
      sequence: 1,
      sentAt: new Date().toISOString(),
      timeoutMs: null,
      payload: { selectedVersion: PROTOCOL_VERSION, maxMessageBytes: this.negotiatedMaxBytes },
    });
  }

  private handleRequest(envelope: ProtocolEnvelope): void {
    if (envelope.requestId === null || envelope.timeoutMs === null) {
      throw new ProtocolValidationError('request metadata is incomplete');
    }
    const requestId = envelope.requestId;
    const request = assistantRequestSchema.parse(envelope.payload);
    const controller = new AbortController();
    const timer = setTimeout(() => {
      const current = this.pending.get(requestId);
      if (current === undefined) return;
      this.pending.delete(requestId);
      current.controller.abort(new Error('request timed out'));
      this.sendError(requestId, {
        code: 'AI_TIMEOUT',
        message: 'The local Codex Host request timed out.',
        retryable: true,
      });
    }, envelope.timeoutMs);
    this.pending.set(requestId, { controller, timer });

    const work = this.answer(request, controller.signal).finally(() => this.active.delete(work));
    this.active.add(work);
  }

  private async answer(request: AssistantRequest, signal: AbortSignal): Promise<void> {
    try {
      const response = await Promise.race([
        this.options.runtime.answer(request, signal),
        new Promise<never>((_resolve, reject) => {
          signal.addEventListener(
            'abort',
            () => {
              reject(signal.reason instanceof Error ? signal.reason : new Error('request aborted'));
            },
            { once: true },
          );
        }),
      ]);
      const current = this.pending.get(request.requestId);
      if (current === undefined || signal.aborted) return;
      clearTimeout(current.timer);
      this.pending.delete(request.requestId);
      this.send({
        protocolVersion: PROTOCOL_VERSION,
        messageId: randomUUID(),
        kind: 'response',
        requestId: request.requestId,
        sequence: 1,
        sentAt: new Date().toISOString(),
        timeoutMs: null,
        payload: response,
      });
    } catch (error) {
      const current = this.pending.get(request.requestId);
      if (current === undefined || signal.aborted) return;
      clearTimeout(current.timer);
      this.pending.delete(request.requestId);
      this.log(error);
      this.sendError(request.requestId, {
        code: 'CODEX_START_FAILED',
        message: 'The local Codex runtime could not complete the request.',
        retryable: true,
      });
    }
  }

  private handleCancel(envelope: ProtocolEnvelope): void {
    if (envelope.requestId === null) return;
    const current = this.pending.get(envelope.requestId);
    if (current === undefined) return;
    clearTimeout(current.timer);
    this.pending.delete(envelope.requestId);
    current.controller.abort(new Error('request cancelled'));
  }

  private sendError(requestId: string, error: AssistantError): void {
    this.send({
      protocolVersion: PROTOCOL_VERSION,
      messageId: randomUUID(),
      kind: 'error',
      requestId,
      sequence: 1,
      sentAt: new Date().toISOString(),
      timeoutMs: null,
      payload: error,
    });
  }

  private send(value: unknown): void {
    const envelope = envelopeSchema.parse(value);
    this.sequence.accept(envelope);
    const line = JSON.stringify(envelope);
    if (Buffer.byteLength(line, 'utf8') > this.negotiatedMaxBytes) {
      throw new ProtocolValidationError('outbound message exceeds negotiated byte length');
    }
    this.options.output.write(`${line}\n`);
  }

  private abortAll(reason: unknown): void {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.controller.abort(reason);
    }
    this.pending.clear();
  }

  private log(error: unknown): void {
    const message = error instanceof Error ? error.message : String(error);
    this.options.errors.write(`[codex-host] ${message}\n`);
  }
}

function toBytes(chunk: unknown): Uint8Array {
  if (typeof chunk === 'string') return Buffer.from(chunk, 'utf8');
  if (chunk instanceof Uint8Array) return chunk;
  throw new ProtocolValidationError('stdio produced an unsupported chunk type');
}
