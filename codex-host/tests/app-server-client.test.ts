import { PassThrough } from 'node:stream';
import { createHash } from 'node:crypto';
import { mkdtemp, readdir, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { describe, expect, it } from 'vitest';

import { AppServerClient } from '../src/app-server/client.js';
import { AppServerCodexRuntime } from '../src/runtime/app-server-runtime.js';
import { CodexRuntimeFailure } from '../src/runtime/errors.js';
import type { AssistantRequest } from '../src/protocol/types.js';
import { assistantRequestSchema } from '../src/protocol/validation.js';

type Scenario =
  | 'complete'
  | 'structured'
  | 'malformed'
  | 'warning'
  | 'commentary-before-final'
  | 'approval'
  | 'unknown'
  | 'auth-error'
  | 'exit'
  | 'delayed-turn-start'
  | 'wait';

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
        if (this.scenario === 'delayed-turn-start') {
          setTimeout(() => {
            this.send({ id, result: { turn: { id: 'turn-1', status: 'inProgress' } } });
          }, 20);
          return;
        }
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
    if (
      this.scenario === 'complete' ||
      this.scenario === 'structured' ||
      this.scenario === 'malformed' ||
      this.scenario === 'warning' ||
      this.scenario === 'commentary-before-final'
    ) {
      if (this.scenario === 'warning') {
        this.send({ method: 'warning', params: { message: 'configuration warning' } });
        this.send({ method: 'account/rateLimits/updated', params: { rateLimits: null } });
      }
      const text =
        this.scenario === 'structured'
          ? JSON.stringify({
              summary: '本地结构化回答',
              nextSteps: ['继续提问'],
              constraints: ['仅本地文本'],
              uncertainties: [],
              followUp: null,
            })
          : this.scenario === 'malformed'
            ? '{"summary":'
            : '安全回答';
      if (this.scenario === 'commentary-before-final') {
        this.send({
          method: 'item/agentMessage/delta',
          params: {
            threadId: this.activeThread,
            turnId: 'turn-1',
            itemId: 'commentary-1',
            delta: '先检查一下。',
          },
        });
        this.send({
          method: 'item/completed',
          params: {
            threadId: this.activeThread,
            turnId: 'turn-1',
            item: { type: 'agentMessage', text: '先检查一下。', phase: 'commentary' },
          },
        });
      }
      this.send({
        method: 'item/agentMessage/delta',
        params: {
          threadId: this.activeThread,
          turnId: 'turn-1',
          itemId: 'item-1',
          delta: text,
        },
      });
      this.send({
        method: 'item/completed',
        params: {
          threadId: this.activeThread,
          turnId: 'turn-1',
          item: { type: 'agentMessage', text, phase: 'final_answer' },
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
    } else if (this.scenario === 'auth-error') {
      this.send({
        method: 'turn/completed',
        params: {
          threadId: this.activeThread,
          turn: {
            id: 'turn-1',
            status: 'failed',
            error: { status: 401, message: 'Unauthorized: invalid API key sk-never-log-this' },
          },
        },
      });
    } else if (this.scenario === 'exit') {
      this.input.end();
    }
  }

  private send(message: unknown): void {
    this.input.write(`${JSON.stringify(message)}\n`);
  }
}

function createRuntime(
  scenario: Scenario,
  serverRequestMethod?: string,
  local = false,
  vision = false,
  visionTemp?: string,
  cloud: false | 'openai' | 'deepseek' = false,
): { server: ScriptedAppServer; runtime: AppServerCodexRuntime } {
  const server = new ScriptedAppServer(scenario, serverRequestMethod);
  const client = new AppServerClient({
    input: server.input,
    output: server.output,
    requestTimeoutMs: 1_000,
  });
  return {
    server,
    runtime: new AppServerCodexRuntime(
      client,
      'C:\\wowai\\workspace',
      local
        ? {
            provider: 'local-ollama',
            appServerProviderId: 'wowai_ollama',
            endpoint: new URL('http://127.0.0.1:11434'),
            model: 'qwen3:8b',
            probeTimeoutMs: 1_000,
          }
        : undefined,
      local
        ? () =>
            Promise.resolve({
              provider: 'local-ollama',
              model: 'qwen3:8b',
              version: '0.12.3',
              text: true,
              vision,
              tools: true,
            })
        : undefined,
      visionTemp,
      cloud
        ? cloud === 'deepseek'
          ? {
              provider: 'deepseek',
              appServerProviderId: 'wowai_deepseek',
              displayName: 'DeepSeek',
              model: 'deepseek-flash',
              baseUrl: new URL('https://api.deepseek.com'),
              credentialEnvironmentKey: 'DEEPSEEK_API_KEY',
              vision: true,
            }
          : {
              provider: 'openai',
              appServerProviderId: 'openai',
              displayName: 'OpenAI',
              model: 'vision-model-fixture',
              credentialEnvironmentKey: 'OPENAI_API_KEY',
              vision: true,
            }
        : undefined,
    ),
  };
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

  it('maps an unexpected App Server exit to a recoverable Host failure', async () => {
    const { server, runtime } = createRuntime('exit');
    await expect(runtime.answer(request, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'CODEX_START_FAILED', retryable: true },
    });
    server.close();
    await runtime.close();
  });

  it('classifies cloud authentication errors without exposing provider diagnostics', async () => {
    const { server, runtime } = createRuntime(
      'auth-error',
      undefined,
      false,
      false,
      undefined,
      'deepseek',
    );
    const cloudRequest = {
      ...request,
      runtime: {
        engine: 'codex' as const,
        provider: 'deepseek' as const,
        model: 'deepseek-flash',
        allowCloudUpload: true,
      },
    } satisfies AssistantRequest;
    try {
      await runtime.answer(cloudRequest, new AbortController().signal);
      expect.fail('expected authentication failure');
    } catch (error) {
      expect(error).toBeInstanceOf(CodexRuntimeFailure);
      if (!(error instanceof CodexRuntimeFailure)) throw error;
      expect(error.assistantError).toMatchObject({ code: 'AI_AUTH_FAILED', retryable: false });
      expect(error.assistantError.message).not.toContain('sk-never-log-this');
    }
    server.close();
    await runtime.close();
  });

  it('accepts locked informational notifications without failing the turn', async () => {
    const { server, runtime } = createRuntime('warning');
    const response = await runtime.answer(request, new AbortController().signal);
    expect(response.answer.summary).toBe('安全回答');
    server.close();
    await runtime.close();
  });

  it('uses only the final answer when a turn emits commentary first', async () => {
    const { server, runtime } = createRuntime('commentary-before-final');
    const response = await runtime.answer(request, new AbortController().signal);
    expect(response.answer.summary).toBe('安全回答');
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

  it('interrupts a turn whose start response arrives after cancellation', async () => {
    const { server, runtime } = createRuntime('delayed-turn-start');
    const controller = new AbortController();
    const answer = runtime.answer(request, controller.signal);
    await waitUntil(() => server.received.some((message) => message.method === 'turn/start'));
    controller.abort(new Error('cancelled before turn id'));

    await expect(answer).rejects.toThrow('cancelled before turn id');
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
          dataBase64: 'iVBORw0KGgo=',
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

  it('passes only a confirmed consented cloud image and cleans the temporary copy', async () => {
    const visionTemp = await mkdtemp(join(tmpdir(), 'wowai-vision-test-'));
    const { server, runtime } = createRuntime(
      'structured',
      undefined,
      false,
      false,
      visionTemp,
      'deepseek',
    );
    const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 0]);
    const withImage = {
      ...request,
      images: [
        {
          id: '44444444-4444-4444-8444-444444444444',
          mimeType: 'image/png' as const,
          captureScope: 'wow-window' as const,
          sha256: createHash('sha256').update(png).digest('hex'),
          dataBase64: png.toString('base64'),
          privacyMaskApplied: true,
          userConfirmed: true as const,
          uploadDestination: 'deepseek' as const,
          uploadPurpose: 'visual-question' as const,
          uploadConfirmedAt: '2026-10-02T12:00:01Z',
          consentNoticeVersion: 1 as const,
        },
      ],
      runtime: {
        engine: 'codex' as const,
        provider: 'deepseek' as const,
        model: 'deepseek-flash',
        allowCloudUpload: true,
      },
    } satisfies AssistantRequest;

    const response = await runtime.answer(withImage, new AbortController().signal);
    expect(response.usage.imageUsed).toBe(true);
    const turn = server.received.find((message) => message.method === 'turn/start');
    expect(turn?.params).toMatchObject({
      input: [{ type: 'text' }, { type: 'localImage', detail: 'auto' }],
    });
    const thread = server.received.find((message) => message.method === 'thread/start');
    expect(thread?.params).toMatchObject({
      model: 'deepseek-flash',
      modelProvider: 'wowai_deepseek',
    });
    expect(await readdir(visionTemp)).toEqual([]);
    await runtime.close();
    server.close();
    await rm(visionTemp, { recursive: true, force: true });
  });

  it('rejects a cloud request without matching explicit configuration', async () => {
    const { server, runtime } = createRuntime('structured');
    await expect(
      runtime.answer(
        {
          ...request,
          runtime: {
            engine: 'codex',
            provider: 'openai',
            model: 'vision-model-fixture',
            allowCloudUpload: true,
          },
        },
        new AbortController().signal,
      ),
    ).rejects.toMatchObject({
      assistantError: { code: 'MODEL_PROVIDER_UNAVAILABLE', retryable: false },
    });
    expect(server.threadStarts).toBe(0);
    server.close();
    await runtime.close();
  });

  it('maps the explicit local provider and validates a structured model answer', async () => {
    const { server, runtime } = createRuntime('structured', undefined, true);
    const localRequest = {
      ...request,
      runtime: {
        engine: 'codex' as const,
        provider: 'local-ollama' as const,
        model: 'qwen3:8b',
        allowCloudUpload: false,
      },
    };
    const response = await runtime.answer(localRequest, new AbortController().signal);

    expect(response.answer).toMatchObject({ summary: '本地结构化回答' });
    expect(response.usage.provider).toBe('local-ollama');
    const threadStart = server.received.find((message) => message.method === 'thread/start');
    expect(threadStart?.params).toMatchObject({
      model: 'qwen3:8b',
      modelProvider: 'wowai_ollama',
    });
    const turnStart = server.received.find((message) => message.method === 'turn/start');
    expect(turnStart?.params).toHaveProperty('outputSchema');
    server.close();
    await runtime.close();
  });

  it('rejects malformed local model output before it reaches the UI', async () => {
    const { server, runtime } = createRuntime('malformed', undefined, true);
    const localRequest = {
      ...request,
      runtime: {
        engine: 'codex' as const,
        provider: 'local-ollama' as const,
        model: 'qwen3:8b',
        allowCloudUpload: false,
      },
    };
    await expect(runtime.answer(localRequest, new AbortController().signal)).rejects.toMatchObject({
      assistantError: { code: 'AI_INVALID_RESPONSE', retryable: true },
    });
    server.close();
    await runtime.close();
  });

  it('does not silently route a local request without matching explicit configuration', async () => {
    const { server, runtime } = createRuntime('structured');
    await expect(
      runtime.answer(
        {
          ...request,
          runtime: {
            engine: 'codex',
            provider: 'local-ollama',
            model: 'qwen3:8b',
            allowCloudUpload: false,
          },
        },
        new AbortController().signal,
      ),
    ).rejects.toMatchObject({
      assistantError: { code: 'MODEL_PROVIDER_UNAVAILABLE', retryable: false },
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
