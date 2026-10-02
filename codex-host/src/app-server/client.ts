import type { Readable, Writable } from 'node:stream';

import { z } from 'zod';

import { JsonLineReader } from '../host/json-line-reader.js';
import {
  AppServerProtocolError,
  type AppServerMessage,
  type AppServerNotification,
  type AppServerRequest,
  type AppServerRequestId,
  isRecord,
  parseAppServerMessage,
} from './protocol.js';

const APP_SERVER_MAX_LINE_BYTES = 1_048_576;
const DEFAULT_REQUEST_TIMEOUT_MS = 10_000;
const SAFE_ITEM_TYPES = new Set([
  'userMessage',
  'agentMessage',
  'plan',
  'reasoning',
  'contextCompaction',
]);
const SAFE_IGNORED_NOTIFICATIONS = new Set([
  'thread/started',
  'thread/archived',
  'thread/unarchived',
  'thread/closed',
  'thread/status/changed',
  'thread/tokenUsage/updated',
  'turn/started',
  'turn/plan/updated',
  'item/plan/delta',
  'item/reasoning/summaryTextDelta',
  'item/reasoning/summaryPartAdded',
  'item/reasoning/textDelta',
  'serverRequest/resolved',
  'remoteControl/status/changed',
]);

const initializeResponseSchema = z
  .object({
    userAgent: z.string().min(1),
    codexHome: z.string().min(1),
    platformFamily: z.string().min(1),
    platformOs: z.string().min(1),
  })
  .strict();
const threadResponseSchema = z.looseObject({
  thread: z.looseObject({ id: z.string().min(1) }),
});
const turnResponseSchema = z
  .object({
    turn: z.looseObject({ id: z.string().min(1), status: z.string() }),
  })
  .strict();

interface PendingRequest {
  method: string;
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timer: NodeJS.Timeout;
}

interface TurnState {
  text: string;
  threadId?: string;
  failure?: Error;
  terminal?: { status: string; error: unknown };
  waiters: Array<{ resolve: (text: string) => void; reject: (error: Error) => void }>;
}

export interface AppServerClientOptions {
  input: Readable;
  output: Writable;
  diagnostics?: Writable;
  requestTimeoutMs?: number;
}

export class AppServerClient {
  private readonly reader = new JsonLineReader(APP_SERVER_MAX_LINE_BYTES);
  private readonly pending = new Map<AppServerRequestId, PendingRequest>();
  private readonly turns = new Map<string, TurnState>();
  private readonly cancelledTurns = new Set<string>();
  private readonly requestTimeoutMs: number;
  private nextRequestId = 1;
  private readTask: Promise<void>;
  private closed = false;
  private fatalError: Error | undefined;

  public constructor(private readonly options: AppServerClientOptions) {
    this.requestTimeoutMs = options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS;
    this.readTask = this.readLoop().catch(() => undefined);
  }

  public async initialize(): Promise<void> {
    const response = await this.request('initialize', {
      clientInfo: { name: 'WorldOfWarcraftAssistant', version: '0.1.0-dev', title: null },
      capabilities: { experimentalApi: false, requestAttestation: false },
    });
    initializeResponseSchema.parse(response);
    this.write({ method: 'initialized' });
  }

  public async startThread(workspace: string, model?: string, provider?: string): Promise<string> {
    const response = threadResponseSchema.parse(
      await this.request('thread/start', {
        ...(model === undefined ? {} : { model }),
        ...(provider === undefined ? {} : { modelProvider: provider }),
        cwd: workspace,
        approvalPolicy: 'never',
        approvalsReviewer: 'user',
        sandbox: 'read-only',
        ephemeral: false,
      }),
    );
    return response.thread.id;
  }

  public async resumeThread(threadId: string, workspace: string): Promise<string> {
    const response = threadResponseSchema.parse(
      await this.request('thread/resume', {
        threadId,
        cwd: workspace,
        approvalPolicy: 'never',
        approvalsReviewer: 'user',
        sandbox: 'read-only',
        excludeTurns: true,
      }),
    );
    return response.thread.id;
  }

  public async archiveThread(threadId: string): Promise<void> {
    await this.request('thread/archive', { threadId });
  }

  public async runTurn(
    threadId: string,
    text: string,
    signal: AbortSignal,
    outputSchema?: unknown,
  ): Promise<string> {
    if (signal.aborted) throw abortReason(signal);
    const response = turnResponseSchema.parse(
      await this.request(
        'turn/start',
        {
          threadId,
          input: [{ type: 'text', text, text_elements: [] }],
          approvalPolicy: 'never',
          approvalsReviewer: 'user',
          disabledPluginIds: [],
          ...(outputSchema === undefined ? {} : { outputSchema }),
        },
        signal,
      ),
    );
    const turnId = response.turn.id;
    const state = this.turnState(turnId, threadId);
    if (state.failure !== undefined) {
      this.turns.delete(turnId);
      throw state.failure;
    }
    if (state.terminal !== undefined) return this.finishTurn(turnId, state);

    return await new Promise<string>((resolve, reject) => {
      const waiter = { resolve, reject };
      state.waiters.push(waiter);
      const onAbort = (): void => {
        state.waiters = state.waiters.filter((candidate) => candidate !== waiter);
        this.cancelledTurns.add(turnId);
        this.turns.delete(turnId);
        void this.request('turn/interrupt', { threadId, turnId }).catch(() => undefined);
        reject(abortReason(signal));
      };
      signal.addEventListener('abort', onAbort, { once: true });
      void Promise.resolve().then(() => {
        if (state.terminal !== undefined) {
          signal.removeEventListener('abort', onAbort);
          try {
            resolve(this.finishTurn(turnId, state));
          } catch (error) {
            reject(asError(error));
          }
        }
      });
    });
  }

  public async close(): Promise<void> {
    if (!this.closed) {
      this.closed = true;
      this.options.output.end();
      this.options.input.destroy();
    }
    await this.readTask;
  }

  private request(method: string, params: unknown, signal?: AbortSignal): Promise<unknown> {
    if (this.closed) return Promise.reject(this.fatalError ?? new Error('App Server is closed'));
    if (signal?.aborted === true) return Promise.reject(abortReason(signal));
    const id = this.nextRequestId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new AppServerProtocolError(`App Server ${method} timed out`));
      }, this.requestTimeoutMs);
      const onAbort = (): void => {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(signal === undefined ? new Error('request aborted') : abortReason(signal));
      };
      signal?.addEventListener('abort', onAbort, { once: true });
      this.pending.set(id, {
        method,
        timer,
        resolve: (value) => {
          signal?.removeEventListener('abort', onAbort);
          resolve(value);
        },
        reject: (error) => {
          signal?.removeEventListener('abort', onAbort);
          reject(error);
        },
      });
      this.write({ id, method, params });
    });
  }

  private async readLoop(): Promise<void> {
    try {
      for await (const chunk of this.options.input) {
        for (const line of this.reader.push(toBytes(chunk)))
          this.handle(parseAppServerMessage(line));
      }
      this.reader.finish();
      if (!this.closed) throw new AppServerProtocolError('App Server exited unexpectedly');
    } catch (error) {
      if (!this.closed) {
        this.fail(asError(error));
        throw error;
      }
    } finally {
      this.closed = true;
    }
  }

  private handle(message: AppServerMessage): void {
    if ('method' in message && 'id' in message) {
      this.handleServerRequest(message);
      return;
    }
    if ('method' in message) {
      this.handleNotification(message);
      return;
    }
    const pending = this.pending.get(message.id);
    if (pending === undefined) throw new AppServerProtocolError('unmatched App Server response id');
    clearTimeout(pending.timer);
    this.pending.delete(message.id);
    if ('error' in message) {
      pending.reject(
        new AppServerProtocolError(
          `App Server ${pending.method} failed (${String(message.error.code)}): ${message.error.message}`,
        ),
      );
    } else {
      pending.resolve(message.result);
    }
  }

  private handleServerRequest(request: AppServerRequest): void {
    switch (request.method) {
      case 'item/commandExecution/requestApproval':
      case 'item/fileChange/requestApproval':
        this.write({ id: request.id, result: { decision: 'decline' } });
        this.failRelatedTurn(request.params, 'Codex requested a prohibited side effect');
        return;
      case 'applyPatchApproval':
      case 'execCommandApproval':
        this.write({
          id: request.id,
          result: { decision: { denied: { rejection: 'WorldOfWarcraftAssistant is read-only' } } },
        });
        this.failRelatedTurn(request.params, 'Codex requested a prohibited legacy side effect');
        return;
      case 'item/permissions/requestApproval':
      case 'item/tool/requestUserInput':
      case 'mcpServer/elicitation/request':
      case 'item/tool/call':
      case 'account/chatgptAuthTokens/refresh':
      case 'attestation/generate':
        this.write({
          id: request.id,
          error: { code: -32_603, message: 'request blocked by read-only client policy' },
        });
        this.failRelatedTurn(request.params, `Codex server request blocked: ${request.method}`);
        return;
      default:
        this.write({ id: request.id, error: { code: -32_601, message: 'unknown server request' } });
        throw new AppServerProtocolError(`unknown App Server request: ${request.method}`);
    }
  }

  private handleNotification(notification: AppServerNotification): void {
    if (notification.method === 'item/agentMessage/delta') {
      const params = notificationParams(notification.params);
      const threadId = requiredString(params, 'threadId');
      const turnId = requiredString(params, 'turnId');
      if (this.cancelledTurns.has(turnId)) return;
      const delta = requiredString(params, 'delta');
      this.turnState(turnId, threadId).text += delta;
      return;
    }
    if (notification.method === 'item/started' || notification.method === 'item/completed') {
      const params = notificationParams(notification.params);
      const threadId = requiredString(params, 'threadId');
      const turnId = requiredString(params, 'turnId');
      if (this.cancelledTurns.has(turnId)) return;
      const item = params.item;
      if (!isRecord(item) || typeof item.type !== 'string' || !SAFE_ITEM_TYPES.has(item.type)) {
        this.failTurn(turnId, new AppServerPolicyError('prohibited App Server item type'));
        return;
      }
      if (notification.method === 'item/completed' && item.type === 'agentMessage') {
        const text = typeof item.text === 'string' ? item.text : '';
        const state = this.turnState(turnId, threadId);
        if (state.text.length === 0) state.text = text;
      }
      return;
    }
    if (notification.method === 'turn/completed') {
      const params = notificationParams(notification.params);
      const threadId = requiredString(params, 'threadId');
      const turn = params.turn;
      if (!isRecord(turn)) throw new AppServerProtocolError('turn/completed has no turn');
      const turnId = requiredString(turn, 'id');
      if (this.cancelledTurns.delete(turnId)) return;
      const status = requiredString(turn, 'status');
      const state = this.turnState(turnId, threadId);
      state.terminal = { status, error: turn.error };
      this.settleTurn(turnId, state);
      return;
    }
    if (notification.method === 'error') {
      throw new AppServerProtocolError('App Server reported an error notification');
    }
    if (SAFE_IGNORED_NOTIFICATIONS.has(notification.method)) return;
    throw new AppServerProtocolError(
      `unknown or unsafe App Server notification: ${notification.method}`,
    );
  }

  private failRelatedTurn(params: unknown, message: string): void {
    if (isRecord(params) && typeof params.turnId === 'string') {
      this.failTurn(params.turnId, new AppServerPolicyError(message));
      return;
    }
    this.fail(new AppServerPolicyError(message));
  }

  private turnState(turnId: string, threadId?: string): TurnState {
    const existing = this.turns.get(turnId);
    if (existing !== undefined) {
      if (
        threadId !== undefined &&
        existing.threadId !== undefined &&
        existing.threadId !== threadId
      ) {
        throw new AppServerProtocolError('turn notification thread correlation mismatch');
      }
      if (existing.threadId === undefined && threadId !== undefined) existing.threadId = threadId;
      return existing;
    }
    const state = {
      text: '',
      waiters: [],
      ...(threadId === undefined ? {} : { threadId }),
    } satisfies TurnState;
    this.turns.set(turnId, state);
    return state;
  }

  private failTurn(turnId: string, error: Error): void {
    const state = this.turnState(turnId);
    state.failure = error;
    if (state.waiters.length === 0) return;
    for (const waiter of state.waiters.splice(0)) waiter.reject(error);
    this.turns.delete(turnId);
  }

  private settleTurn(turnId: string, state: TurnState): void {
    if (state.waiters.length === 0) return;
    try {
      const text = this.finishTurn(turnId, state);
      for (const waiter of state.waiters.splice(0)) waiter.resolve(text);
    } catch (error) {
      const failure = asError(error);
      for (const waiter of state.waiters.splice(0)) waiter.reject(failure);
    }
  }

  private finishTurn(turnId: string, state: TurnState): string {
    this.turns.delete(turnId);
    if (state.terminal?.status !== 'completed') {
      throw new AppServerProtocolError(
        `Codex turn ended with status ${String(state.terminal?.status)}`,
      );
    }
    if (state.text.length === 0)
      throw new AppServerProtocolError('Codex turn produced no safe text');
    return state.text;
  }

  private fail(error: Error): void {
    if (this.fatalError !== undefined) return;
    this.fatalError = error;
    this.closed = true;
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
    for (const turnId of this.turns.keys()) this.failTurn(turnId, error);
    this.options.diagnostics?.write(`[codex-app-server] ${error.message}\n`);
  }

  private write(message: unknown): void {
    if (this.closed) throw this.fatalError ?? new Error('App Server is closed');
    const line = JSON.stringify(message);
    if (Buffer.byteLength(line, 'utf8') > APP_SERVER_MAX_LINE_BYTES) {
      throw new AppServerProtocolError('outbound App Server message exceeds limit');
    }
    this.options.output.write(`${line}\n`);
  }
}

export class AppServerPolicyError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = 'AppServerPolicyError';
  }
}

function notificationParams(value: unknown): Record<string, unknown> {
  if (!isRecord(value)) throw new AppServerProtocolError('invalid notification params');
  return value;
}

function requiredString(value: Record<string, unknown>, key: string): string {
  const result = value[key];
  if (typeof result !== 'string' || result.length === 0) {
    throw new AppServerProtocolError(`missing notification field: ${key}`);
  }
  return result;
}

function abortReason(signal: AbortSignal): Error {
  return signal.reason instanceof Error ? signal.reason : new Error('request aborted');
}

function asError(error: unknown): Error {
  return error instanceof Error ? error : new Error(String(error));
}

function toBytes(chunk: unknown): Uint8Array {
  if (typeof chunk === 'string') return Buffer.from(chunk, 'utf8');
  if (chunk instanceof Uint8Array) return chunk;
  throw new AppServerProtocolError('App Server stdout produced an unsupported chunk type');
}
