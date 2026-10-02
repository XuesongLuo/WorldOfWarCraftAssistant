# STEP-010 automated validation — 2026-10-02

## Scope

Codex App Server integration boundary only: locked-process startup, initialize/initialized,
thread/turn lifecycle, request correlation, safe streaming normalization, cancellation, timeout,
process exit, approval rejection, empty production tool registry, and ADR-003 maturity review.
No model provider, network knowledge source, image recognition, or addon field expansion was added.

## Real locked-runtime smoke

The SHA-256-verified official `codex-cli 0.159.2` Windows x64 binary was launched with application-owned
state and an empty workspace. The real stdio sequence completed:

1. `initialize`
2. `initialized`
3. `thread/start`
4. `thread/archive`
5. graceful process shutdown

No model turn or network provider was started. App Server diagnostics remained on stderr.

## Automated protocol coverage

- 42/42 Vitest cases passed across seven files.
- A scripted App Server completed initialize, a streamed text turn, thread reuse, resume, archive,
  cancellation/interrupt, and image-capability rejection.
- Command and file approvals were explicitly declined.
- Permission, user-input, MCP elicitation, dynamic tool, authentication refresh, and attestation
  requests were rejected.
- Unknown notifications failed closed and produced `CODEX_PROTOCOL_ERROR`.
- Tool name, strict argument, and strict result validation each have a negative test.
- Prettier, ESLint, TypeScript strict checking, and esbuild passed.

## Native regression

Debug and Release `verify` targets pass all 40 CTest cases, including the C++ to packaged TypeScript
Host integration and descendant-process Job cleanup. Repository and addon safety gates pass.

## Conclusion

STEP-010 is complete. ADR-003 is accepted with explicit replacement triggers. STEP-011 may connect one
local text model provider without weakening the empty-workspace, approval, event-filtering, or tool
policy boundaries.
