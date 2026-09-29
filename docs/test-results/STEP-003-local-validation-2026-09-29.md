# STEP-003 local validation — 2026-09-29

## Locked tool bootstrap

| Tool | Version / commit | Verification |
|---|---|---|
| Node.js | 24.19.0 | Official ZIP SHA-256 matched `eng/bootstrap-lock.json` |
| npm | 11.17.0 | Bundled project-local npm; exact engine and package manager lock |
| vcpkg | `b8b8df2201ad8509b81a830fe0957bcb98e06c27` | Official codeload SHA-256 plus shallow Git baseline object |
| vcpkg tool | `2026-09-26-51bf87ca6e9bf3e622d84ff323bd202ab1ca0c0b` | Bootstrap version check |
| 7-Zip | 26.03 x64 | Official executable and extractor SHA-512 checks |

All tools are stored below ignored project `.tools/`; no global npm or vcpkg installation is required.

## Dependency restoration

- `npm ci` restored 134 packages, did not change `package-lock.json`, and reported 0 vulnerabilities.
- vcpkg manifest mode restored Catch2 3.16.0, nlohmann-json 3.12.0#2, spdlog 1.17.0#1, sqlite3 3.53.4#1, WIL 1.0.260126.7, and required build helpers from the fixed baseline.
- CMake now consumes Catch2 through vcpkg rather than a separate FetchContent download.

## Validation results

- Prettier: passed.
- Prettier negative path: a deliberately malformed TypeScript line failed with exit code 1, then was removed.
- ESLint strict typed rules: passed.
- TypeScript `strict` typecheck: passed.
- Vitest: 1/1 passed.
- esbuild: passed with source map output.
- `npm audit --audit-level=high`: 0 vulnerabilities.
- C++ Debug: configure, build, and 2/2 tests passed.
- C++ Release: fresh configure, build, and 2/2 tests passed.
- Repository verifier: 26 required files and sensitive artifact checks passed.
- CycloneDX and transitive license inventories generated successfully under `out/dependency-artifacts/`.
- No unused sqlite3, spdlog, or fmt DLL entered the companion build output.

## Remote clean-run result

- Commit: `34195350739b42f5c44afc165ff83e7d7fb67b99`
- Workflow: <https://github.com/XuesongLuo/WorldOfWarCraftAssistant/actions/runs/36561787192>
- Runner: `windows-2022`
- Result: success
- Passed steps: project tool bootstrap, `npm ci`, TypeScript verification, vcpkg configure, C++ build, C++ tests, dependency/vulnerability checks, and repository checks.

The first STEP-003 CI attempt passed bootstrap, `npm ci`, formatting, lint, typecheck, tests, and bundling, then exposed that `Invoke-CMake.ps1` only knew the workstation's private Build Tools path. The wrapper now falls back to the runner-provided `cmake.exe`; the configured generator and vcpkg baseline remain fixed. The next run passed every gate.
