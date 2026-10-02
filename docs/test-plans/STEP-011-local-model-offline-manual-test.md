# STEP-011 Ollama offline manual test

## Purpose

Verify the complete user-visible text path with an explicitly installed Ollama model while external
network access is disabled. This plan never authorizes automatic installation, model pulling, cloud
fallback, image input, online search, or tools with side effects.

## Record before testing

- Windows build and test time
- Ollama version and installation source
- Exact model name/tag and locally reported digest
- Locked Codex version and binary hash result
- Companion/Host commit
- Node version
- How external network access was disabled

Do not record credentials, chat history unrelated to this test, user-global Codex configuration, or
private screen content.

## Preparation

1. The user installs Ollama and the chosen model through their normal trusted process.
2. Confirm the exact model appears in the local Ollama model list before disconnecting the network.
3. Build the repository and set every STEP-011 variable shown in `.env.example`. Use
   `WOWAI_MODEL_PROVIDER=local-ollama` and a credential-free loopback endpoint.
4. Disconnect or block external networking while preserving loopback traffic.
5. Start Ollama, WoW, and the companion. Do not enable screenshots or the optional addon bridge.

## Cases

| ID | Action | Expected result |
|---|---|---|
| O-001 | Ask a short Chinese text question in the overlay | One local reply returns through the overlay; no shell, file, browser, MCP, computer-control, or game-input action occurs |
| O-002 | Ask a second question in the same session | Conversation remains usable and the reply satisfies the structured response contract |
| O-003 | Submit empty text and then more than 4,000 characters | UI/request validation rejects both before model execution |
| O-004 | Stop Ollama and submit a question | UI reports a retryable local-provider error; no cloud fallback occurs |
| O-005 | Configure an exact model name that is not installed | Capability probe reports the model missing; nothing is downloaded |
| O-006 | Point the endpoint at a non-loopback host, HTTPS URL, URL with credentials, or URL with a path | Startup/configuration fails closed with an actionable message |
| O-007 | Return malformed or schema-incompatible model output using a controlled local fixture | Host returns `AI_INVALID_RESPONSE`; partial text is not displayed as a valid answer |
| O-008 | Submit twice while the first request is running | The second request is rejected as already in progress; the first remains correlated |
| O-009 | Exit the companion during or immediately after a request | Companion, Host, and App Server exit without an orphan process or late UI update |
| O-010 | Inspect process/network activity during O-001/O-002 | Traffic is limited to local stdio and the configured loopback Ollama endpoint; user-global Codex configuration is not read |

## Pass condition

O-001 through O-010 pass with evidence sufficient to reproduce the environment. Only then may
STEP-011 “验证通过” be checked and the execution pointer move to STEP-012. Any external request,
automatic download, side-effect tool request that does not fail closed, malformed output displayed as
valid, or orphan process is a failure.
