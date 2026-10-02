# STEP-009 automated validation — 2026-10-01

## Scope

TypeScript Codex Host foundation only: project JSONL/stdio transport, protocol negotiation, timeout
and cancellation, deterministic `ICodexRuntime`, application-owned directories, C++ process ownership,
C++ Host client, and locked Codex runtime metadata. No real App Server session, model provider, network
connection, screen recognition, or addon field expansion was implemented.

## Locked runtime compatibility unit

- Codex CLI: `0.159.2`
- Official Windows x64 npm variant: `@openai/codex@0.159.2-win32-x64`
- npm integrity: `sha512-1ZJVTO40/ZaHPUUWc3uCX73jwzJRaDxAjRQEf37dC5Q3h1fp/Z7+1L8oX3bPlXgjhEY6kHW3rm0wz2sdQ5rxNw==`
- Extracted `codex.exe` SHA-256: `52f75c649bebb8001102a1dd129c1ea6d02b0940321e6d7e82ee0526753bd58a`
- Checked-in App Server v2 Schema SHA-256: `78c28e952990f3de413a4e5890eb62e298193b4387c53bd2512ea3c1d453bc07`
- License declared by npm: Apache-2.0

The official package was downloaded to a temporary directory with `npm pack`, its integrity matched the
registry metadata, and the extracted binary reported `codex-cli 0.159.2`. The Host verifier accepted the
locked binary and rejected both version drift and binary tampering fixtures.

## Commands and results

```powershell
.\scripts\Invoke-Npm.ps1 run format:check
.\scripts\Invoke-Npm.ps1 run lint
.\scripts\Invoke-Npm.ps1 run typecheck
.\scripts\Invoke-Npm.ps1 test
.\scripts\Invoke-Npm.ps1 run build
```

Result: Prettier, ESLint, TypeScript strict, 23/23 Vitest, and esbuild passed.

```powershell
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-debug
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify
.\scripts\Invoke-CMake.ps1 --preset windows-msvc-release
.\scripts\Invoke-CMake.ps1 --build --preset windows-msvc-release --target verify
```

Result: Debug and Release built successfully; 40/40 CTest cases passed in each configuration; repository
and addon safety gates passed.

## Covered failure and boundary paths

- Arbitrarily split JSONL input and multiple lines in one chunk.
- Invalid UTF-8, oversized and unterminated lines, unknown fields, protocol version mismatch, and invalid
  sequence ordering.
- Runtime timeout even when an implementation ignores `AbortSignal`; no late response is emitted.
- User cancellation aborts the runtime and produces no later UI update.
- Host stdout contains protocol envelopes only; diagnostics are isolated to stderr.
- C++ detects timeout and Host crash independently.
- Host is created suspended, assigned to a kill-on-close Job Object, then resumed; an owned descendant
  process is terminated during shutdown, proving no orphan remains.
- A real C++ client negotiated with the bundled TypeScript Host and completed a deterministic offline
  request.
- Paths containing spaces work in both Node and Win32 process launch.

## Conclusion

STEP-009 implementation and automated validation pass. STEP-010 may connect the locked App Server and
must add initialize/thread/turn, event filtering, approval failure-close, crash recovery, and protocol
compatibility tests without weakening the STEP-009 process or stdout boundaries.
