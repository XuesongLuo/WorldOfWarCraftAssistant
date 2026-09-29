import { z } from 'zod';

export * from './protocol/session.js';
export * from './protocol/types.js';
export * from './protocol/validation.js';
export * from './runtime/mock-runtime.js';

export const hostBuildInfoSchema = z.object({
  component: z.literal('codex-host'),
  version: z.string().min(1),
});

export const hostBuildInfo = hostBuildInfoSchema.parse({
  component: 'codex-host',
  version: '0.1.0-dev',
});
