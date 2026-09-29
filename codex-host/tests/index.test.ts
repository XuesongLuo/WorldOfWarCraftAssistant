import { describe, expect, it } from 'vitest';

import { hostBuildInfo, hostBuildInfoSchema } from '../src/index.js';

describe('host build metadata', () => {
  it('uses a validated development version', () => {
    expect(hostBuildInfoSchema.parse(hostBuildInfo)).toEqual({
      component: 'codex-host',
      version: '0.1.0-dev',
    });
  });
});
