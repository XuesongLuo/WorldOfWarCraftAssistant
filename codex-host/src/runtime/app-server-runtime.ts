import { z } from 'zod';
import { createHash, randomUUID } from 'node:crypto';
import { mkdir, unlink, writeFile } from 'node:fs/promises';
import { join } from 'node:path';

import type { AssistantRequest, AssistantResponse } from '../protocol/types.js';
import { PROTOCOL_VERSION } from '../protocol/validation.js';
import {
  AppServerClient,
  AppServerPolicyError,
  AppServerTimeoutError,
  AppServerUnavailableError,
} from '../app-server/client.js';
import { AppServerProtocolError } from '../app-server/protocol.js';
import type { LocalModelCapabilities, LocalModelConfiguration } from '../local-model/provider.js';
import type { CloudModelConfiguration } from '../cloud-model/provider.js';
import { classifyCloudModelError, CodexRuntimeFailure } from './errors.js';
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
    private readonly visionTemp?: string,
    private readonly cloudModel?: CloudModelConfiguration,
  ) {}

  public async answer(request: AssistantRequest, signal: AbortSignal): Promise<AssistantResponse> {
    const localCapabilities = await this.validateProvider(request);
    const cloudRequest =
      request.runtime.provider === 'openai' || request.runtime.provider === 'deepseek';
    if (cloudRequest) this.validateCloudProvider(request);
    if (request.images.length > 0 && cloudRequest && this.cloudModel?.vision !== true) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_CAPABILITY_MISSING',
        message: 'The selected cloud model is not registered for image input.',
        retryable: false,
      });
    }
    if (request.images.length > 0 && !cloudRequest && localCapabilities?.vision !== true) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_CAPABILITY_MISSING',
        message: 'The selected provider does not support the confirmed image input.',
        retryable: false,
      });
    }
    let text: string;
    let imagePaths: string[] = [];
    try {
      await this.initialize();
      const threadId = await this.threadFor(request);
      imagePaths = await this.materializeImages(request);
      text = await this.client.runTurn(
        threadId,
        request.question,
        signal,
        localCapabilities === undefined && !cloudRequest ? undefined : structuredAnswerJsonSchema,
        imagePaths,
      );
    } catch (error) {
      if (error instanceof AppServerPolicyError) {
        throw new CodexRuntimeFailure({
          code: 'CODEX_TOOL_BLOCKED',
          message: error.message,
          retryable: false,
        });
      }
      if (signal.aborted) throw error;
      if (cloudRequest) {
        const classified = classifyCloudModelError(error);
        if (classified !== undefined) throw new CodexRuntimeFailure(classified);
      }
      if (error instanceof AppServerTimeoutError) {
        throw new CodexRuntimeFailure({
          code: 'AI_TIMEOUT',
          message: 'The Codex App Server request timed out.',
          retryable: true,
        });
      }
      if (error instanceof AppServerUnavailableError) {
        this.initializeTask = undefined;
        this.conversations.clear();
        throw new CodexRuntimeFailure({
          code: 'CODEX_START_FAILED',
          message: 'The Codex App Server exited unexpectedly.',
          retryable: true,
        });
      }
      if (error instanceof AppServerProtocolError) {
        throw new CodexRuntimeFailure({
          code: 'CODEX_PROTOCOL_ERROR',
          message: error.message,
          retryable: false,
        });
      }
      if (error instanceof z.ZodError) {
        throw new CodexRuntimeFailure({
          code: 'CODEX_PROTOCOL_ERROR',
          message: 'The Codex App Server returned an invalid protocol structure.',
          retryable: false,
        });
      }
      throw error;
    } finally {
      await Promise.all(imagePaths.map(async (path) => await unlink(path).catch(() => undefined)));
    }

    let answer: z.infer<typeof structuredAnswerSchema>;
    if (localCapabilities === undefined && !cloudRequest) {
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
          message:
            'The selected model returned a response that did not match the required structure.',
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
        imageUsed: request.images.length === 1,
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

  private async materializeImages(request: AssistantRequest): Promise<string[]> {
    if (request.images.length === 0) return [];
    if (this.visionTemp === undefined) {
      throw new CodexRuntimeFailure({
        code: 'CAPTURE_DENIED',
        message: 'The application-owned vision temporary directory is unavailable.',
        retryable: false,
      });
    }
    await mkdir(this.visionTemp, { recursive: true });
    const image = request.images[0];
    if (image === undefined) return [];
    const bytes = Buffer.from(image.dataBase64, 'base64');
    const digest = createHash('sha256').update(bytes).digest('hex');
    const png =
      bytes.length >= 8 &&
      bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    if (
      bytes.length === 0 ||
      bytes.length > 700 * 1024 ||
      digest !== image.sha256.toLowerCase() ||
      !png
    ) {
      throw new CodexRuntimeFailure({
        code: 'CAPTURE_EMPTY',
        message: 'The confirmed screenshot failed integrity or format validation.',
        retryable: false,
      });
    }
    const path = join(this.visionTemp, `${randomUUID()}.png`);
    await writeFile(path, bytes, { flag: 'wx', mode: 0o600 });
    return [path];
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
    if (this.initializeTask === undefined) {
      const task = this.client.initialize();
      const wrapped = task.catch((error: unknown) => {
        if (this.initializeTask === wrapped) this.initializeTask = undefined;
        throw error;
      });
      this.initializeTask = wrapped;
    }
    return this.initializeTask;
  }

  private async threadFor(request: AssistantRequest): Promise<string> {
    const existing = this.conversations.get(request.conversationId);
    if (existing !== undefined) return existing;
    const provider =
      request.runtime.provider === 'local-ollama'
        ? this.localModel?.appServerProviderId
        : request.runtime.provider === 'openai' || request.runtime.provider === 'deepseek'
          ? this.cloudModel?.appServerProviderId
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

  private validateCloudProvider(request: AssistantRequest): void {
    if (
      this.cloudModel === undefined ||
      request.runtime.provider !== this.cloudModel.provider ||
      request.runtime.model !== this.cloudModel.model ||
      !request.runtime.allowCloudUpload
    ) {
      throw new CodexRuntimeFailure({
        code: 'MODEL_PROVIDER_UNAVAILABLE',
        message: 'The request does not match the explicitly enabled OpenAI cloud model.',
        retryable: false,
      });
    }
  }
}

function truncate(value: string, maximum: number): string {
  return Array.from(value).slice(0, maximum).join('');
}
