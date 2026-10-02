# Companion ↔ Codex Host protocol 2.0

The companion and TypeScript Host communicate with UTF-8 JSON Lines over child-process stdio. Each line contains exactly one envelope and ends with LF. No local TCP port is opened.

## Limits and negotiation

- Protocol version: `2.0`.
- Maximum encoded line size: 1,048,576 bytes, excluding the terminating LF.
- UTF-8 must be canonical and valid; a UTF-8 BOM, replacement decoding, NUL, CR outside JSON escaping, and trailing bytes are rejected.
- The companion sends `hello`; the Host replies with `ready` only when `2.0` is in `supportedVersions` and both peers agree on a maximum line size.
- Request timeout defaults to 30,000 ms and must be between 1,000 and 120,000 ms.
- UUID fields use lowercase or uppercase RFC 4122 text form. Timestamps use ISO-8601 UTC with a trailing `Z`.
- All schema objects use `additionalProperties: false`. Unknown fields fail closed.

## Envelope flow

`hello` and `ready` use `requestId: null` and sequence numbers 0 and 1. An assistant `request` uses sequence 0 for its request ID. Its terminal `response` or `error` uses sequence 1. A `cancel` uses the next expected sequence. Duplicate, skipped, or decreasing sequence numbers are protocol errors.

The receiver must correlate terminal messages by `requestId`, ignore no malformed input, and produce no UI update after cancellation. A timeout produces `AI_TIMEOUT`; malformed JSON, invalid UTF-8, an unsupported version, an unknown field, an oversized line, or invalid ordering produces `CODEX_PROTOCOL_ERROR`.

## Files

- `v2/assistant-request.schema.json`: active request contract with coach mode, observations and privacy boundary.
- `v2/assistant-response.schema.json`: active response contract with observation provenance.
- `v2/error.schema.json`: stable application error object.
- `v2/envelope.schema.json`: active JSONL transport envelope and negotiation payloads.
- `v1/`: retained as the historical pre-overlay contract; it is not negotiated by the current pre-release build.
- `tests/validation-cases.json`: shared positive and negative fixtures consumed by C++ and TypeScript tests.

The `mock` provider is deterministic and offline. It never launches Codex, accesses the network, reads user files, or executes tools.

## STEP-009 Host transport behavior

- The packaged TypeScript Host accepts arbitrarily split stdin chunks and multiple JSONL messages in
  one chunk. It bounds the unfinished line before allocating beyond the negotiated maximum.
- Protocol replies are the only stdout content. Diagnostics use stderr and are capped by the C++
  owner; malformed UTF-8 or framing ends the Host with a non-zero exit.
- A request timeout produces exactly one `AI_TIMEOUT` error. A received `cancel` aborts the runtime
  operation and produces no later UI update for that request.
- The C++ owner launches the Host suspended, attaches it to a kill-on-close Windows Job Object, then
  resumes it. Shutdown first closes stdin and waits briefly; timeout terminates only that owned job
  tree, never an independently started Codex process.
- STEP-009 runs only `DeterministicMockRuntime`. The locked real App Server Schema is compatibility
  input for STEP-010; no real Codex session, model, network endpoint, tool, or approval flow is active.

## STEP-010 App Server boundary

- Real App Server mode is opt-in through `WOWAI_CODEX_BINARY`, `WOWAI_CODEX_LOCK`, and
  `WOWAI_CODEX_ROOT`; all three are required together. The binary is hash/version verified before
  launch and receives an application-owned `CODEX_HOME` plus an empty workspace.
- The Host performs `initialize`/`initialized`, maps application conversations to Codex threads, and
  supports thread start, resume, archive, turn start, streaming text, interrupt, timeout, and exit.
- Only explicitly allowlisted lifecycle/text notifications cross the adapter. Item types capable of
  commands, file changes, MCP, browser/computer control, collaboration, or other side effects fail
  the turn. Unknown requests, notifications, fields, response IDs, and correlation mismatches fail
  closed.
- Command and file approvals receive explicit decline responses. Permission, elicitation, dynamic
  tool, auth-refresh, attestation, and all other server requests receive protocol errors. The
  production tool registry is empty until later read-only knowledge tools are implemented.
- Tool policy validates the exact tool name, strict arguments, and strict result before any result
  can return to App Server. STEP-010 registers no executable tools.
