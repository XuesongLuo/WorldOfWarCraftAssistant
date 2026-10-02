import type { AssistantRequest, AssistantResponse } from '../protocol/types.js';
import { PROTOCOL_VERSION } from '../protocol/validation.js';
import { AppServerClient, AppServerPolicyError } from '../app-server/client.js';
import { AppServerProtocolError } from '../app-server/protocol.js';
import { CodexRuntimeFailure } from './errors.js';
import type { ICodexRuntime } from './mock-runtime.js';

const MAX_SAFE_ANSWER_CHARACTERS = 8_000;

export class AppServerCodexRuntime implements ICodexRuntime {
  private readonly conversations = new Map<string, string>();
  private initializeTask: Promise<void> | undefined;

  public constructor(
    private readonly client: AppServerClient,
    private readonly workspace: string,
  ) {}

  public async answer(request: AssistantRequest, signal: AbortSignal): Promise<AssistantResponse> {
    if (request.images.length > 0) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_CAPABILITY_MISSING',
        message: 'STEP-010 supports text turns only.',
        retryable: false,
      });
    }
    await this.initialize();
    const threadId = await this.threadFor(request);
    let text: string;
    try {
      text = await this.client.runTurn(threadId, request.question, signal);
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

    return {
      schemaVersion: PROTOCOL_VERSION,
      requestId: request.requestId,
      status: 'completed',
      mode: request.mode,
      answer: {
        summary: truncate(text, MAX_SAFE_ANSWER_CHARACTERS),
        nextSteps: [],
        constraints: [],
        uncertainties: [],
        followUp: null,
      },
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
    const threadId = await this.client.startThread(
      this.workspace,
      request.runtime.model,
      request.runtime.provider,
    );
    this.conversations.set(request.conversationId, threadId);
    return threadId;
  }
}

function truncate(value: string, maximum: number): string {
  return Array.from(value).slice(0, maximum).join('');
}
