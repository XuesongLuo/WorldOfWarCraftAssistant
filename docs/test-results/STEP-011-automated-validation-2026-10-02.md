# STEP-011 automated validation — 2026-10-02

## Scope

This record covers the Ollama-only M0 implementation without claiming the still-pending real-model
offline acceptance. No model, provider, package, or network capability was installed during this
validation.

## Implemented path

`WebView2 overlay → C++ AssistantSession → packaged TypeScript Host → locked Codex App Server →
explicit loopback Ollama provider → strict AssistantResponse → overlay`

- Configuration is all-or-nothing and accepts only `local-ollama`, an exact model name, and a
  credential-free HTTP loopback origin.
- Capability probing covers `/api/version`, `/api/tags`, and `/api/show`; no pull or install endpoint
  exists in the implementation.
- Local requests force `allowCloudUpload: false`, contain no image or observation, and use the
  active v2 request validator before Host I/O.
- Local turns include a strict JSON output schema. Invalid JSON, missing/unknown fields, oversized
  fields, a missing model, bad status, and malformed capability responses fail closed.
- The UI permits one request at a time and returns completion/error messages on the UI thread.

## Commands and results

| Command | Result |
|---|---|
| `scripts/Invoke-Npm.ps1 run build` | Passed; bundled `codex-host/dist/index.js` |
| `scripts/Invoke-Npm.ps1 run typecheck` | Passed |
| `scripts/Invoke-Npm.ps1 test` | Passed: 8 files, 51 tests |
| `scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug` | Passed |
| `scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify` | Passed: 42/42 CTest plus repository checks |
| `git diff --check` | Passed |

The C++ suite includes valid and invalid local-only request construction. The TypeScript suite
includes configuration, loopback restrictions, provider arguments, capability success/failure,
exact-model matching, structured response parsing, and malformed-response rejection.

## Environment finding and remaining gate

`ollama` was not present on `PATH`, and `http://127.0.0.1:11434/api/version` was unavailable on this
development machine. Therefore this record does not mark STEP-011 validation complete. On
2026-10-02 the project owner explicitly chose to skip this phase's real-model acceptance and advance
to STEP-012; the unchanged manual gate is deferred to STEP-021. Follow
`docs/test-plans/STEP-011-local-model-offline-manual-test.md` after the user explicitly installs an
Ollama build and model.

## Conclusion

STEP-011 implementation and automated failure-path coverage are complete. ADR-010 is accepted for
the Ollama route. Real disconnected-network conversation through the locked Codex App Server remains
unverified but is no longer a STEP-012 prerequisite by explicit owner waiver; it must be restored at
STEP-021. Cloud fallback, image input, online knowledge, and LM Studio were not implemented.
