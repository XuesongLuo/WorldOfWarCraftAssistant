import { z } from 'zod';

import type { AssistantRequest, AssistantResponse } from '../protocol/types.js';
import { PROTOCOL_VERSION } from '../protocol/validation.js';
import { AppServerClient, AppServerPolicyError } from '../app-server/client.js';
import { AppServerProtocolError } from '../app-server/protocol.js';
import type { LocalModelCapabilities, LocalModelConfiguration } from '../local-model/provider.js';
import { CodexRuntimeFailure } from './errors.js';
import type { ICodexRuntime } from './mock-runtime.js';

const MAX_SAFE_ANSWER_CHARACTERS = 8_000;
const structuredAnswerSchema = z
  .object({
    summary: z.string().max(MAX_SAFE_ANSWER_CHARACTERS),
    nextSteps: z.array(z.string().min(1).max(1_000)).max(5),
    constraints: z.array(z.string().min(1).max(1_000)).max(10),
    uncertainties: z.array(z.string().min(1).max(1_000)).max(10),
    followUp: z.string().min(1).max(1_000).nullable(),
  })
  .strict();
const structuredAnswerJsonSchema = {
  type: 'object',
  additionalProperties: false,
  required: ['summary', 'nextSteps', 'constraints', 'uncertainties', 'followUp'],
  properties: {
    summary: { type: 'string', maxLength: MAX_SAFE_ANSWER_CHARACTERS },
    nextSteps: { type: 'array', maxItems: 5, items: { type: 'string' } },
    constraints: { type: 'array', maxItems: 10, items: { type: 'string' } },
    uncertainties: { type: 'array', maxItems: 10, items: { type: 'string' } },
    followUp: { type: ['string', 'null'] },
  },
} as const;

type CapabilityProbe = () => Promise<LocalModelCapabilities>;

export class AppServerCodexRuntime implements ICodexRuntime {
  private readonly conversations = new Map<string, string>();
  private initializeTask: Promise<void> | undefined;

  public constructor(
    private readonly client: AppServerClient,
    private readonly workspace: string,
    private readonly localModel?: LocalModelConfiguration,
    private readonly capabilityProbe?: CapabilityProbe,
  ) {}

  public async answer(request: AssistantRequest, signal: AbortSignal): Promise<AssistantResponse> {
    if (request.images.length > 0) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_CAPABILITY_MISSING',
        message: 'STEP-010 supports text turns only.',
        retryable: false,
      });
    }
    const localCapabilities = await this.validateProvider(request);
    await this.initialize();
    const threadId = await this.threadFor(request);
    let text: string;
    try {
      text = await this.client.runTurn(
        threadId,
        request.question,
        signal,
        localCapabilities === undefined ? undefined : structuredAnswerJsonSchema,
      );
    } catch (error) {
      if (error instanceof AppServerPolicyError) {
        throw new CodexRuntimeFailure({
          code: 'CODEX_TOOL_BLOCKED',
          message: error.message,
          retryable: false,
        });
      }
      if (error instanceof AppServerProtocolError) {
        throw new CodexRuntimeFailure({
          code: 'CODEX_PROTOCOL_ERROR',
          message: error.message,
          retryable: false,
        });
      }
      throw error;
    }

    let answer: z.infer<typeof structuredAnswerSchema>;
    if (localCapabilities === undefined) {
      answer = {
        summary: truncate(text, MAX_SAFE_ANSWER_CHARACTERS),
        nextSteps: [],
        constraints: [],
        uncertainties: [],
        followUp: null,
      };
    } else {
      try {
        answer = structuredAnswerSchema.parse(JSON.parse(text));
      } catch {
        throw new CodexRuntimeFailure({
          code: 'AI_INVALID_RESPONSE',
          message: 'The local model returned a response that did not match the required structure.',
          retryable: true,
        });
      }
    }

    return {
      schemaVersion: PROTOCOL_VERSION,
      requestId: request.requestId,
      status: 'completed',
      mode: request.mode,
      answer,
      sources: [],
      provenance: request.observations.map((observation) => ({
        observationId: observation.id,
        source: observation.source,
        confidence: observation.confidence,
        reason: 'observation was supplied with the player request',
      })),
      usage: {
        imageUsed: false,
        knowledgeUsed: false,
        screenObservationUsed: request.observations.some(
          (observation) => observation.source === 'screen-observed',
        ),
        addonBridgeUsed: request.observations.some(
          (observation) => observation.source === 'plugin-public',
        ),
        runtime: 'codex',
        provider: request.runtime.provider,
      },
      error: null,
    };
  }

  public async start(): Promise<void> {
    await this.initialize();
  }

  public async restoreConversation(conversationId: string, threadId: string): Promise<void> {
    await this.initialize();
    const resumed = await this.client.resumeThread(threadId, this.workspace);
    this.conversations.set(conversationId, resumed);
  }

  public async createConversation(
    conversationId: string,
    model?: string,
    provider?: string,
  ): Promise<string> {
    await this.initialize();
    const threadId = await this.client.startThread(this.workspace, model, provider);
    this.conversations.set(conversationId, threadId);
    return threadId;
  }

  public async archiveConversation(conversationId: string): Promise<void> {
    const threadId = this.conversations.get(conversationId);
    if (threadId === undefined) return;
    await this.client.archiveThread(threadId);
    this.conversations.delete(conversationId);
  }

  public async close(): Promise<void> {
    await this.client.close();
  }

  private initialize(): Promise<void> {
    this.initializeTask ??= this.client.initialize();
    return this.initializeTask;
  }

  private async threadFor(request: AssistantRequest): Promise<string> {
    const existing = this.conversations.get(request.conversationId);
    if (existing !== undefined) return existing;
    const provider =
      request.runtime.provider === 'local-ollama'
        ? this.localModel?.appServerProviderId
        : request.runtime.provider;
    const threadId = await this.client.startThread(this.workspace, request.runtime.model, provider);
    this.conversations.set(request.conversationId, threadId);
    return threadId;
  }

  private async validateProvider(
    request: AssistantRequest,
  ): Promise<LocalModelCapabilities | undefined> {
    if (request.runtime.provider !== 'local-ollama') return undefined;
    if (
      this.localModel === undefined ||
      this.capabilityProbe === undefined ||
      request.runtime.model !== this.localModel.model ||
      request.runtime.allowCloudUpload
    ) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_PROVIDER_UNAVAILABLE',
        message: 'The request does not match the explicitly configured local Ollama model.',
        retryable: false,
      });
    }
    return await this.capabilityProbe();
  }
}

function truncate(value: string, maximum: number): string {
  return Array.from(value).slice(0, maximum).join('');
}
