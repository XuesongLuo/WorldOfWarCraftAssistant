import { z } from 'zod';

import {
  assistantErrorSchema,
  assistantRequestSchema,
  assistantResponseSchema,
  envelopeSchema,
} from './validation.js';

export type AssistantError = z.infer<typeof assistantErrorSchema>;
export type AssistantRequest = z.infer<typeof assistantRequestSchema>;
export type AssistantResponse = z.infer<typeof assistantResponseSchema>;
export type ProtocolEnvelope = z.infer<typeof envelopeSchema>;

export type EnvelopeKind = ProtocolEnvelope['kind'];
