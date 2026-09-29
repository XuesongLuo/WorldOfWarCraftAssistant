import { z } from 'zod';

export const hostBuildInfoSchema = z.object({
  component: z.literal('codex-host'),
  version: z.string().min(1),
});

export const hostBuildInfo = hostBuildInfoSchema.parse({
  component: 'codex-host',
  version: '0.1.0-dev',
});
