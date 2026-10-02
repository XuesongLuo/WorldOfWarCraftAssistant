import { PassThrough } from 'node:stream';

import { describe, expect, it } from 'vitest';

import { AppServerClient } from '../src/app-server/client.js';
import { AppServerCodexRuntime } from '../src/runtime/app-server-runtime.js';
import type { AssistantRequest } from '../src/protocol/types.js';
import { assistantRequestSchema } from '../src/protocol/validation.js';

type Scenario = 'complete' | 'approval' | 'unknown' | 'wait';

const request = assistantRequestSchema.parse({
  schemaVersion: '2.0',
  requestId: '11111111-1111-4111-8111-111111111111',
  conversationId: '22222222-2222-4222-8222-222222222222',
  createdAt: '2026-10-02T12:00:00Z',
  mode: 'general',
  locale: 'zh-CN',
  gameFlavor: 'retail',
  question: '解释这个机制',
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

class ScriptedAppServer {
  public readonly input = new PassThrough();
  public readonly output = new PassThrough();
  public readonly received: Array<Record<string, unknown>> = [];
  public threadStarts = 0;
  private buffer = '';
  private activeThread = 'thread-1';

  public constructor(
    private readonly scenario: Scenario,
    private readonly serverRequestMethod = 'item/commandExecution/requestApproval',
  ) {
    this.output.setEncoding('utf8');
    this.output.on('data', (chunk: string) => {
      this.buffer += chunk;
      let newline = this.buffer.indexOf('\n');
      while (newline >= 0) {
        const line = this.buffer.slice(0, newline);
        this.buffer = this.buffer.slice(newline + 1);
        this.handle(JSON.parse(line) as Record<string, unknown>);
        newline = this.buffer.indexOf('\n');
      }
    });
  }

  public close(): void {
    this.input.end();
    this.output.end();
  }

  private handle(message: Record<string, unknown>): void {
    this.received.push(message);
    const id = message.id;
    switch (message.method) {
      case 'initialize':
        this.send({
          id,
          result: {
            userAgent: 'codex-cli/0.159.2',
            codexHome: 'C:\\wowai\\state',
            platformFamily: 'windows',
            platformOs: 'windows',
          },
        });
        return;
      case 'thread/start':
        this.threadStarts += 1;
        this.send({ id, result: { thread: { id: 'thread-1' } } });
        return;
      case 'thread/resume':
        this.send({ id, result: { thread: { id: 'thread-restored' } } });
        return;
      case 'turn/start':
        this.activeThread = (message.params as { threadId: string }).threadId;
        this.send({ id, result: { turn: { id: 'turn-1', status: 'inProgress' } } });
        queueMicrotask(() => {
          this.afterTurnStart();
        });
        return;
      case 'turn/interrupt':
      case 'thread/archive':
        this.send({ id, result: {} });
        return;
      default:
        return;
    }
  }

  private afterTurnStart(): void {
    if (this.scenario === 'complete') {
      this.send({
        method: 'item/agentMessage/delta',
        params: {
          threadId: this.activeThread,
          turnId: 'turn-1',
          itemId: 'item-1',
          delta: '安全回答',
        },
      });
      this.send({
        method: 'turn/completed',
        params: {
          threadId: this.activeThread,
          turn: { id: 'turn-1', status: 'completed', error: null },
        },
      });
    } else if (this.scenario === 'approval') {
      this.send({
        id: 'approval-1',
        method: this.serverRequestMethod,
        params: { threadId: this.activeThread, turnId: 'turn-1', itemId: 'item-2' },
      });
    } else if (this.scenario === 'unknown') {
      this.send({ method: 'future/unsafeEvent', params: { turnId: 'turn-1' } });
    }
  }

  private send(message: unknown): void {
    this.input.write(`${JSON.stringify(message)}\n`);
  }
}

function createRuntime(
  scenario: Scenario,
  serverRequestMethod?: string,
): { server: ScriptedAppServer; runtime: AppServerCodexRuntime } {
  const server = new ScriptedAppServer(scenario, serverRequestMethod);
  const client = new AppServerClient({
    input: server.input,
    output: server.output,
    requestTimeoutMs: 1_000,
  });
  return { server, runtime: new AppServerCodexRuntime(client, 'C:\\wowai\\workspace') };
}

describe('locked Codex App Server adapter', () => {
  it('initializes, creates one mapped thread, and normalizes completed turn text', async () => {
    const { server, runtime } = createRuntime('complete');
    const first = await runtime.answer(request, new AbortController().signal);
    const second = await runtime.answer(
      { ...request, requestId: '33333333-3333-4333-8333-333333333333' },
      new AbortController().signal,
    );

    expect(first.answer.summary).toBe('安全回答');
    expect(second.answer.summary).toBe('安全回答');
    expect(server.threadStarts).toBe(1);
    expect(server.received.map((message) => message.method)).toContain('initialized');
    server.close();
    await runtime.close();
  });

  it('declines command approval and reports a stable blocked-tool failure', async () => {
    const { server, runtime } = createRuntime('approval');
    await expect(runtime.answer(request, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'CODEX_TOOL_BLOCKED', retryable: false },
    });
    expect(server.received).toContainEqual({ id: 'approval-1', result: { decision: 'decline' } });
    server.close();
    await runtime.close();
  });

  it.each([
    ['item/fileChange/requestApproval', 'decline'],
    ['applyPatchApproval', 'legacy-deny'],
    ['execCommandApproval', 'legacy-deny'],
    ['item/permissions/requestApproval', 'error'],
    ['item/tool/requestUserInput', 'error'],
    ['mcpServer/elicitation/request', 'error'],
    ['item/tool/call', 'error'],
    ['account/chatgptAuthTokens/refresh', 'error'],
    ['attestation/generate', 'error'],
  ] as const)('fails closed for server request %s', async (method, expectedReply) => {
    const { server, runtime } = createRuntime('approval', method);
    await expect(runtime.answer(request, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'CODEX_TOOL_BLOCKED', retryable: false },
    });
    const reply = server.received.find((message) => message.id === 'approval-1');
    if (expectedReply === 'decline') {
      expect(reply).toEqual({ id: 'approval-1', result: { decision: 'decline' } });
    } else if (expectedReply === 'legacy-deny') {
      expect(reply).toEqual({
        id: 'approval-1',
        result: {
          decision: { denied: { rejection: 'WorldOfWarcraftAssistant is read-only' } },
        },
      });
    } else {
      expect(reply).toMatchObject({ id: 'approval-1', error: { code: -32_603 } });
    }
    server.close();
    await runtime.close();
  });

  it('resumes and archives an explicitly restored conversation mapping', async () => {
    const { server, runtime } = createRuntime('complete');
    await runtime.restoreConversation(request.conversationId, 'thread-persisted');
    const response = await runtime.answer(request, new AbortController().signal);
    await runtime.archiveConversation(request.conversationId);

    expect(response.answer.summary).toBe('安全回答');
    expect(server.threadStarts).toBe(0);
    expect(server.received).toEqual(
      expect.arrayContaining([
        expect.objectContaining({ method: 'thread/resume' }),
        expect.objectContaining({ method: 'thread/archive' }),
      ]),
    );
    server.close();
    await runtime.close();
  });

  it('fails closed on an unknown notification', async () => {
    const { server, runtime } = createRuntime('unknown');
    await expect(runtime.answer(request, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'CODEX_PROTOCOL_ERROR', retryable: false },
    });
    server.close();
    await runtime.close();
  });

  it('interrupts an active turn after cancellation and emits no answer', async () => {
    const { server, runtime } = createRuntime('wait');
    const controller = new AbortController();
    const answer = runtime.answer(request, controller.signal);
    await waitUntil(() => server.received.some((message) => message.method === 'turn/start'));
    controller.abort(new Error('cancelled'));

    await expect(answer).rejects.toThrow('cancelled');
    await waitUntil(() => server.received.some((message) => message.method === 'turn/interrupt'));
    server.close();
    await runtime.close();
  });

  it('rejects image input before creating an App Server thread', async () => {
    const { server, runtime } = createRuntime('complete');
    const withImage = {
      ...request,
      images: [
        {
          id: '44444444-4444-4444-8444-444444444444',
          mimeType: 'image/png',
          captureScope: 'selected-region',
          sha256: 'a'.repeat(64),
          privacyMaskApplied: true,
          userConfirmed: true,
        },
      ],
    } as AssistantRequest;
    await expect(runtime.answer(withImage, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'MODEL_CAPABILITY_MISSING' },
    });
    expect(server.threadStarts).toBe(0);
    server.close();
    await runtime.close();
  });
});

async function waitUntil(predicate: () => boolean): Promise<void> {
  const deadline = Date.now() + 1_000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error('condition timed out');
    await new Promise((resolve) => setTimeout(resolve, 1));
  }
}
