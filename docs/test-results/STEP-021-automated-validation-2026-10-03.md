# STEP-021 automated validation — 2026-10-03

## Scope and safety

This validation used only deterministic mocks, local child-process fixtures, SQLite, DPAPI, and the
checked-in/locked Codex App Server 0.159.2 binary. It made no real provider request, consumed no
quota, uploaded no screenshot, and did not perform a WoW acceptance test.

## Provider evidence and locked-runtime probe

Official documentation was checked for OpenAI, DeepSeek, Anthropic, Gemini, Azure OpenAI, xAI,
Mistral, OpenRouter and Alibaba Cloud Model Studio. The locked binary was then invoked with
`app-server --strict-config --listen off`:

- Responses provider configuration was accepted, including `env_key`, `env_http_headers`,
  `http_headers` and `query_params`.
- `wire_api="chat"` returned “no longer supported”; `chat_completions` returned “unknown variant”.
- Therefore OpenAI, DeepSeek, xAI, OpenRouter, DashScope and Azure OpenAI have controlled Responses
  configurations. Anthropic native Messages, Gemini native `generateContent`, and Mistral Chat are
  documented but disabled, not reported as compatible.
- All ordinary endpoints are fixed. DashScope and Azure endpoints are generated from DNS-safe IDs
  plus allowlisted vendor suffixes; invalid regions and URL-shaped identifiers fail closed.

No real-account claim is made for the newly added providers. DeepSeek's earlier real smoke record
remains separate and is not generalized to another vendor.

## Automated assertions

- Provider/profile credentials use distinct DPAPI paths and scope-bound entropy; save, replace,
  read and delete never place plaintext in SQLite.
- The Host child environment removes all unrelated provider keys and receives only
  `WOWAI_ACTIVE_API_KEY`; the Node Host forwards it only to its owned App Server and deletes its own
  environment copy after spawn.
- Cloud-off settings do not create a Host/App Server session. `.env.local` is reachable only through
  the explicit `WOWAI_DEVELOPMENT_ENV_FALLBACK=1` development switch.
- Request IDs are inserted into a metadata-only SQLite audit before dispatch. The unique primary key
  blocks duplicate dispatch; session/day/month caps and stop percentage block before network work.
- Questions, answers, image data and keys are absent from the audit schema and its tests.
- Confirmed images are consumed once before dispatch. Provider and stream retry counts are zero.
  `Retry-After` seconds/milliseconds are parsed, bounded to one hour, and used for a local cooldown;
  no automatic request or image retry is scheduled.
- Capability checks remain fail-closed: only pinned model families accept images; OpenRouter and
  Azure deployment names are text-only until their exact routed/deployed model is independently
  verified.
- The settings window contains the cloud master switch, provider, connection/destination hint,
  exact model/deployment, profile, conditional organization/region/resource/API-version fields,
  password-only key replacement, configured/suffix status, credential deletion, usage limits,
  upload/cost warning and an explicit Mock-only connection test.

## Commands and results

```text
scripts/Invoke-Npm.ps1 run typecheck
  passed

scripts/Invoke-Npm.ps1 test
  10 files / 76 tests passed

scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug
  passed (existing MSBuild shared-intermediate-directory warnings only)

ctest --preset windows-msvc-debug --output-on-failure
  83/83 tests passed

scripts/Invoke-Npm.ps1 run lint
scripts/Invoke-Npm.ps1 run format:check
git diff --check
  passed

scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify
  passed: 83/83 CTest, addon static verification, protocol/document and repository gates
```

No credential, request body, screenshot, player identity, or provider response is included in this
record.
