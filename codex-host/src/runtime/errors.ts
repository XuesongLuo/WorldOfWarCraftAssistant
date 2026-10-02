import type { AssistantError } from '../protocol/types.js';

export class CodexRuntimeFailure extends Error {
  public constructor(public readonly assistantError: AssistantError) {
    super(assistantError.message);
    this.name = 'CodexRuntimeFailure';
  }
}
