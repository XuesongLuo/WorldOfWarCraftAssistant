import { describe, expect, it } from 'vitest';
import { z } from 'zod';

import {
  ReadOnlyToolPolicy,
  ToolPolicyViolation,
  denyAllToolsPolicy,
} from '../src/app-server/tool-policy.js';

describe('read-only App Server tool policy', () => {
  it('blocks unknown tools at the name layer', async () => {
    await expect(
      denyAllToolsPolicy.execute('shell', {}, new AbortController().signal),
    ).rejects.toMatchObject({ stage: 'name' } satisfies Partial<ToolPolicyViolation>);
  });

  it('rejects argument overreach before invoking a read-only handler', async () => {
    let invoked = false;
    const policy = new ReadOnlyToolPolicy(
      new Map([
        [
          'lookup',
          {
            input: z.object({ query: z.string() }).strict(),
            output: z.object({ text: z.string() }).strict(),
            execute: () => {
              invoked = true;
              return Promise.resolve({ text: 'safe' });
            },
          },
        ],
      ]),
    );

    await expect(
      policy.execute('lookup', { query: 'x', write: true }, new AbortController().signal),
    ).rejects.toMatchObject({ stage: 'arguments' } satisfies Partial<ToolPolicyViolation>);
    expect(invoked).toBe(false);
  });

  it('validates handler output before returning it to App Server', async () => {
    const policy = new ReadOnlyToolPolicy(
      new Map([
        [
          'lookup',
          {
            input: z.object({ query: z.string() }).strict(),
            output: z.object({ text: z.string() }).strict(),
            execute: () => Promise.resolve({ raw: 'unsafe' }),
          },
        ],
      ]),
    );

    await expect(
      policy.execute('lookup', { query: 'x' }, new AbortController().signal),
    ).rejects.toMatchObject({ stage: 'result' } satisfies Partial<ToolPolicyViolation>);
  });
});
