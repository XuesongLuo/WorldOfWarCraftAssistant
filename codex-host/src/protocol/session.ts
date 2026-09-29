import type { ProtocolEnvelope } from './types.js';
import { ProtocolValidationError } from './validation.js';

export class ProtocolSequenceTracker {
  private negotiationState: 'new' | 'hello' | 'ready' = 'new';
  private readonly nextByRequest = new Map<string, number>();

  public accept(envelope: ProtocolEnvelope): void {
    if (envelope.kind === 'hello') {
      if (this.negotiationState !== 'new')
        throw new ProtocolValidationError('duplicate or late hello');
      this.negotiationState = 'hello';
      return;
    }
    if (envelope.kind === 'ready') {
      if (this.negotiationState !== 'hello')
        throw new ProtocolValidationError('ready before hello');
      this.negotiationState = 'ready';
      return;
    }
    if (this.negotiationState !== 'ready')
      throw new ProtocolValidationError('request before negotiation');
    if (envelope.requestId === null) throw new ProtocolValidationError('missing requestId');

    if (envelope.kind === 'request') {
      if (this.nextByRequest.has(envelope.requestId)) {
        throw new ProtocolValidationError('duplicate requestId');
      }
      this.nextByRequest.set(envelope.requestId, 1);
      return;
    }

    const expected = this.nextByRequest.get(envelope.requestId);
    if (expected === undefined || envelope.sequence !== expected) {
      throw new ProtocolValidationError('out-of-order message');
    }
    this.nextByRequest.delete(envelope.requestId);
  }
}
