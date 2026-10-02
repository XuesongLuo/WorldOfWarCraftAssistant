import type { z } from 'zod';

export type ToolPolicyStage = 'name' | 'arguments' | 'result';

export class ToolPolicyViolation extends Error {
  public constructor(
    public readonly stage: ToolPolicyStage,
    message: string,
  ) {
    super(message);
    this.name = 'ToolPolicyViolation';
  }
}

export interface ReadOnlyToolDefinition {
  input: z.ZodType;
  output: z.ZodType;
  execute: (input: unknown, signal: AbortSignal) => Promise<unknown>;
}

export class ReadOnlyToolPolicy {
  public constructor(private readonly tools: ReadonlyMap<string, ReadOnlyToolDefinition>) {}

  public async execute(name: string, input: unknown, signal: AbortSignal): Promise<unknown> {
    const definition = this.tools.get(name);
    if (definition === undefined) {
      throw new ToolPolicyViolation('name', `tool is not allowlisted: ${name}`);
    }
    const parsedInput = definition.input.safeParse(input);
    if (!parsedInput.success) {
      throw new ToolPolicyViolation('arguments', `tool arguments rejected: ${name}`);
    }
    const output = await definition.execute(parsedInput.data, signal);
    const parsedOutput = definition.output.safeParse(output);
    if (!parsedOutput.success) {
      throw new ToolPolicyViolation('result', `tool result rejected: ${name}`);
    }
    return parsedOutput.data;
  }
}

export const denyAllToolsPolicy = new ReadOnlyToolPolicy(new Map());
