# STEP-019/020 automated validation — 2026-10-03

## Scope

This record covers the quota-free completion of STEP-019 C-05/C-06 and STEP-020 H-01 through H-07.
All model paths use existing mocks; no real provider request or game input is performed.

## Security and lifecycle assertions

- The default `Ctrl+Shift+Space` hotkey is registered with `MOD_NOREPEAT`, can be disabled or
  rebound, and only toggles the assistant overlay. A second real Windows registration of the same
  chord is rejected; the application restores the previous binding on conflict.
- The tray settings window edits hotkey, opacity, font size, and the conversation-saving switch.
  Appearance and hotkey changes take effect immediately.
- SQLite schema v2 is created transactionally. Tests cover first install, v1 upgrade, corrupt-file
  recovery, corrupt-value default restoration, refusal to downgrade a future schema, and a safe
  in-memory fallback when LocalAppData is unavailable.
- Provider secrets are stored in per-user DPAPI ciphertext outside SQLite. Round-trip, revocation,
  invalid provider IDs, and raw disk scans are covered. Runtime migration from `.env.local` remains
  explicitly assigned to STEP-021.
- Full conversation persistence is off by default. Disabled storage writes no exchange and clears
  previous exchanges; enabled storage retains at most 100 exchanges. `secure_delete`, WAL
  truncation, and `VACUUM` remove tested plaintext after reset.
- C++ screenshots remain memory-only. Startup clears the application-owned `vision-temp` directory;
  cleanup path-containment tests prove an adjacent file is untouched.
- Diagnostics are redacted before a 1 MiB × 3 rotating sink. Authorization values, explicit
  credentials, token assignments, common key forms, newlines, and image data URLs are removed.
- “Delete my local data” cancels and joins work, clears conversations/settings, revokes DPAPI files,
  removes screenshot/log/UI/legacy-selection data and corrupt database backups, then recreates safe
  defaults without requiring an application restart.

## Automated result

- Full Debug MSVC build: passed.
- Catch2: 77 test cases, 340 assertions: passed.
- WebView2 component: settings window create/show/hide, overlay appearance and user visibility are
  exercised together with existing destruction-queue coverage.
- Existing TypeScript/Vitest, repository, addon, formatting, and secret/diff gates are included in
  the repository `verify` target.

No credential value, screenshot, conversation body, or player identity is included in this record.
