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

Cloud failures are normalized before they cross the Host boundary: missing credentials use
`AI_CREDENTIALS_MISSING`, rejected credentials use `AI_AUTH_FAILED`, an unavailable exact model uses
`AI_MODEL_UNAVAILABLE`, connectivity failures use `AI_NETWORK_UNAVAILABLE`, provider throttling uses
`AI_RATE_LIMITED`, and an invalid structured answer uses `AI_INVALID_RESPONSE`. Provider response
text, API keys, Authorization headers, and request bodies are not copied into UI errors. A local
usage stop returns `AI_USAGE_LIMIT_REACHED`; a reused durable request ID returns
`AI_DUPLICATE_REQUEST_BLOCKED` before network dispatch.

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

## STEP-011 local model boundary

This is retained as historical/experimental evidence after ADR-017. It is not a normal-user runtime
requirement or a release gate.

- The companion emits only `runtime.provider: "local-ollama"`, an explicit model name, empty image
  and observation arrays, and `allowCloudUpload: false`. The existing request schema bounds and
  validates the question before Host I/O.
- Local mode requires the locked real App Server configuration and an explicit credential-free HTTP
  loopback Ollama origin. Missing, partial, non-loopback, HTTPS, path-bearing, or credential-bearing
  configuration fails closed; no model pull, install, discovery substitution, or cloud fallback is
  implemented.
- Before each local turn, the Host probes Ollama version, installed models, and model capabilities.
  It accepts only the exact configured model and records whether vision and tools are supported.
- Local turns request the exact `AssistantResponse.answer` shape as JSON Schema. Host parses the
  returned text as strict JSON and maps malformed, missing, unknown, oversized, or mistyped fields
  to `AI_INVALID_RESPONSE` rather than displaying untrusted partial output.
- The overlay/C++ bridge permits one in-flight request. Completion and actionable errors return to
  the UI thread through owned messages; shutdown joins the request thread before releasing the Host,
  overlay, and process Job.

## STEP-012 vision and observation boundary

- An image is inline PNG data only after an explicit capture, preview, and confirmation. Its digest,
  MIME type, capture scope, mask flag, and `userConfirmed: true` travel in the request; unconfirmed,
  malformed, oversized, non-PNG, or digest-mismatched data fails closed.
- Every image declares a registered `uploadDestination` matching `runtime.provider`,
  `uploadPurpose: "visual-question"`, a UTC confirmation time, and the consent-notice version. Host
  also requires the exact explicitly configured cloud model, matching credential, registered vision
  capability, and cloud-upload opt-in before materializing an image. It creates a
  randomly named file only in the application-owned vision temporary directory, supplies that path
  as an App Server `localImage`, and removes it in `finally`; startup removes remnants from an
  interrupted prior process. Images never enter logs or the persistent conversation mapping.
- `observations` distinguish `screen-observed` from `plugin-public` provenance. Continuous screen
  observations contain summaries, time, and confidence, never raw frames, and are stripped before
  a cloud request. `visualBridge` is optional
  for old clients and records protocol version, source, sequence, capture time, confidence, allowed
  fields, and unavailable fields.
- The plugin bridge is a visible, player-controlled, one-way channel. Protocol v1 is bounded to 512
  payload bytes and carries magic/version/source, sequence, Unix capture time, a field bitmap,
  payload length, percent-encoded allowlisted fields, and CRC32. Unknown, corrupt, stale, duplicate,
  future, or over-10-Hz frames are discarded.

## STEP-019 request recovery boundary

- The overlay exposes `submitting`, `cancelling`, `completed`, `cancelled`, and `error` request states.
  Only an active request exposes Cancel; Retry is an explicit player action and appears only for a
  retryable error.
- Cancellation sends the protocol `cancel`, interrupts an active App Server turn, stops the C++
  waiter, and suppresses all late output. Shutdown requests cancellation before joining the worker
  and destroying the Host, overlay, or queued owned UI messages.
- A Host/App Server exit or protocol failure discards the broken conversation mapping and creates a
  fresh owned Host/App Server session at most once for recovery. The failed cloud request is never
  replayed automatically; the player must choose Retry.
- Diagnostics are bounded and redacted before forwarding. Credential values, bearer tokens, raw
  provider errors, image Base64, and request bodies must not enter stdout, UI errors, or committed
  evidence.

## STEP-021 cloud connection and usage boundary

- Cloud providers are `openai`, `deepseek`, `xai`, `openrouter`, `dashscope`, and `azure-openai`.
  Each uses an allowlisted HTTPS Responses endpoint and exact model/deployment. A cloud request has
  `allowCloudUpload: true`; all other providers require `false`; image `uploadDestination` must
  exactly match the runtime provider.
- The locked App Server accepts only the Responses wire API. Anthropic Messages, Gemini native
  `generateContent`, and Mistral Chat are not protocol members and cannot be selected through this
  contract until a separately tested Host adapter exists.
- Provider/profile DPAPI values never enter an assistant envelope. The C++ parent injects one
  `WOWAI_ACTIVE_API_KEY` only into its owned Host; the Host strips every unrelated provider key and
  forwards the active key only to its owned App Server.
- SQLite audit stores request ID, UTC time, provider, profile, model, destination domain and an
  image-present boolean. It stores no question, response, Base64, path, header or credential.
  A unique request ID plus session/day/month stop thresholds authorize dispatch before any image is
  consumed or any network-capable work begins.
- Provider request and stream retry counts are zero. `Retry-After` creates a bounded local cooldown;
  neither Host recovery nor a rate-limit error resubmits text or images automatically.
