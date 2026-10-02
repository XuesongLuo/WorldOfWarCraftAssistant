import { ProtocolValidationError } from '../protocol/validation.js';

export class JsonLineReader {
  private buffered = Buffer.alloc(0);

  public constructor(private readonly maximumLineBytes = 1_048_576) {}

  public push(chunk: Uint8Array): string[] {
    if (chunk.byteLength === 0) return [];
    this.buffered = Buffer.concat([this.buffered, Buffer.from(chunk)]);
    const lines: string[] = [];

    for (
      let newline = this.buffered.indexOf(0x0a);
      newline >= 0;
      newline = this.buffered.indexOf(0x0a)
    ) {
      if (newline > this.maximumLineBytes) {
        throw new ProtocolValidationError('message exceeds maximum byte length');
      }
      const bytes = this.buffered.subarray(0, newline);
      this.buffered = this.buffered.subarray(newline + 1);
      lines.push(this.decode(bytes));
    }

    if (this.buffered.byteLength > this.maximumLineBytes) {
      throw new ProtocolValidationError('message exceeds maximum byte length');
    }
    return lines;
  }

  public finish(): void {
    if (this.buffered.byteLength !== 0) {
      throw new ProtocolValidationError('unterminated JSON line at end of stream');
    }
  }

  private decode(bytes: Uint8Array): string {
    try {
      return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    } catch {
      throw new ProtocolValidationError('message is not valid UTF-8');
    }
  }
}
