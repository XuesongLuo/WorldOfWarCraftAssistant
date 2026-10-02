import { describe, expect, it } from 'vitest';

import { JsonLineReader } from '../src/host/json-line-reader.js';
import { MAX_MESSAGE_BYTES, ProtocolValidationError } from '../src/protocol/validation.js';

describe('JSONL stream framing', () => {
  it('reassembles split input and separates sticky packets', () => {
    const reader = new JsonLineReader();

    expect(reader.push(Buffer.from('{"one":'))).toEqual([]);
    expect(reader.push(Buffer.from('1}\n{"two":2}\n'))).toEqual(['{"one":1}', '{"two":2}']);
    reader.finish();
  });

  it('rejects a line before an unbounded buffer can form', () => {
    const reader = new JsonLineReader(16);

    expect(() => reader.push(Buffer.from('x'.repeat(17)))).toThrow(ProtocolValidationError);
  });

  it('rejects an incomplete final line', () => {
    const reader = new JsonLineReader(MAX_MESSAGE_BYTES);
    reader.push(Buffer.from('{"partial":true}'));

    expect(() => {
      reader.finish();
    }).toThrow(/unterminated/u);
  });
});
