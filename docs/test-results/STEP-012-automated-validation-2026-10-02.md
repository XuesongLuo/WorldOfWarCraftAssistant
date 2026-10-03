# STEP-012 automated validation — 2026-10-02

## Scope

This record covers implementation and deterministic automated regression for screenshots, scene
awareness, coaching observation, and the optional visible addon bridge. It also records the 2026-10-03
real DeepSeek API and full Host/App Server vision smoke tests. It does not claim the pending real WoW,
display-matrix, secret-value, privacy, failure-matrix, or network-destination acceptance. No game input,
injection, memory access, packet inspection, or local model installation was used.

## Implemented boundaries

- Windows Graphics Capture targets only the explicitly selected non-minimized WoW window, crops to
  its client pixels, disables cursor capture, and excludes the companion overlay from capture.
- Active screenshots support calibrated-region crop, irreversible chat-area masking, image validity
  checks, at-most-2048-pixel adaptive resize to the 700 KiB bound, WIC in-memory PNG, SHA-256,
  preview, explicit confirmation, and buffer clearing on discard/use.
- Host accepts only confirmed, size-bounded, integrity-checked PNG data with explicit matching
  provider/purpose/time/notice metadata, and only when the exact cloud model, API credential, and
  cloud-upload opt-in are configured. App Server receives an application-owned random `localImage`; normal
  turn cleanup and next-start crash-remnant cleanup are covered.
- Scene/coaching sessions are off by default, capture at 1/2 Hz, require a visible status surface and
  selected foreground WoW/owned overlay, and stop without auto-resume on focus loss, minimize, exit,
  hidden indicator, or pause. Raw frames have one-scope memory ownership and are never request images.
- Addon bridge v1 is visible and user-controlled. It audits every field, rejects secret values before
  type conversion, publishes build/action/binding and tracked achievement/criteria context, uses a
  512-byte allowlisted payload with sequence/time/bitmap/CRC, and previews the actually encoded fields.
- Decoder tests cover palette sampling at multiple cell scales, strict field/bitmap/source/version/CRC
  validation, percent decoding, stale/future/duplicate rejection, and a maximum of ten frames/second.

## Commands and results

| Command | Result |
|---|---|
| `scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug` | Passed |
| `scripts/Invoke-CMake.ps1 --build --preset windows-msvc-debug --target verify` | Passed: 58/58 CTest, including the WebView2 component test, addon static check, and repository gates |
| direct `wowai_tests.exe` | Passed: 57 Catch2 cases, 273 assertions |
| `scripts/Invoke-Npm.ps1 run format:check` | Passed |
| `scripts/Invoke-Npm.ps1 run lint` | Passed |
| `scripts/Invoke-Npm.ps1 run typecheck` | Passed |
| `scripts/Invoke-Npm.ps1 test` | Passed initially: 9 files, 58 tests |
| `scripts/Invoke-Npm.ps1 run build` | Passed: bundled `codex-host/dist/index.js` (872.8 KiB reported by esbuild) |
| `scripts/Test-WowAddon.ps1` | Passed: six files, API boundary markers, and unsafe fixture rejection |
| `scripts/Test-DeepSeekApi.ps1` | Passed with the user-provided environment credential: structured text response completed (95 tokens); inline PNG vision response completed (361 tokens) |
| `scripts/Test-DeepSeekHost.ps1` | Passed with locked Codex 0.159.2: real Host/App Server/DeepSeek image turn completed with provider `deepseek` and `imageUsed=true` |
| Host regression after live findings | Passed: 9 files, 60 tests; TypeScript strict; Host bundle rebuilt |

The first sandboxed `verify` attempt could not write the WebView2 component's normal temporary app-data
asset. Re-running the same target with the required local app-data permission passed all 58 tests; this
was an execution-sandbox limitation, not a product test failure.

The live Host test exposed two protocol-integration defects that deterministic mocks had not covered:
Codex 0.159.2 emits the informational `warning` and `account/rateLimits/updated` notifications, and a
turn can emit commentary before its `phase=final_answer` message. The client now ignores only those
two schema-defined informational notifications, still fails closed for unknown notifications, and
selects the final-answer item instead of concatenating commentary into structured JSON. Regression
tests cover both behaviors. The API key and image payload were not printed.

## Synthetic evidence

- `companion/tests/fixtures/vision-samples.json` covers 1080p/100%, 1440p/125%, 1440p/150%, and
  4K/200%, with deterministic resize assertions.
- Scene fixtures cover map, bags, quest log, character/equipment, and unknown, plus empty, black,
  too-dark, and corrupt frames.
- Shared C++/TypeScript contract fixtures include a confirmed inline PNG, `screen-observed` provenance,
  and optional versioned `plugin-public` bridge metadata; omitted bridge metadata remains compatible.
- Unit tests verify privacy-mask pixels, crop bounds, image encoding/digest, strict UI messages,
  explicit confirmation, gate rate/timing, and bridge decode failures.
- Cloud configuration tests require an exact model, explicit upload opt-in, and the provider-specific
  API credential. DeepSeek tests verify fixed official base URL, `DEEPSEEK_API_KEY`, Responses wire
  protocol, registered vision capability, provider/destination matching, and configured App Server ID.

## Remaining gate and conclusion

Implementation, automated regression, direct DeepSeek text/vision, and the full synthetic-image Host
path are complete. The STEP-012 implementation checkbox remains checked, while validation remains
unchecked. Execute
`docs/test-plans/STEP-012-vision-manual-test.md` V-001 through V-018 on a real retail WoW window,
the recorded DPI/UI-scale matrix, and the configured `deepseek-flash` model. The remaining work
includes real-window capture/preview, privacy and cleanup inspection, auth/model/limit/disconnect
failures, network-destination audit, observation isolation, and addon bridge verification. No local
model is part of this gate. Until all cases pass, the current execution pointer stays at STEP-012 and
STEP-013 must not start.
