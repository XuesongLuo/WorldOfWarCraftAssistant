# STEP-004 local validation — 2026-09-29

## Contract baseline

- Protocol version: `1.0`.
- Transport: UTF-8 JSON Lines over child-process standard input/output.
- Maximum encoded message: 1,048,576 bytes, excluding the line terminator.
- Request timeout: 30,000 ms by default; accepted range 1,000–120,000 ms.
- Schema objects reject unknown fields and require RFC 4122 request/message IDs.
- Session negotiation uses `hello`/`ready`; request-scoped messages use strictly ordered sequence numbers.

## Cross-language validation

- C++ and TypeScript consume `contracts/tests/validation-cases.json` as the same source of truth.
- Both implementations accepted the valid request and response samples.
- Both implementations rejected missing fields, unknown fields, a schema-version mismatch, an unconfirmed image, and an insecure source URL.
- Dedicated boundary tests rejected malformed UTF-8, messages above 1 MiB, out-of-order responses, and incompatible protocol versions.
- The deterministic mock Host/runtime returned repeatable responses without starting Codex or contacting a model provider.

## Commands and results

- Prettier: passed for TypeScript and contract documents.
- ESLint strict typed rules: passed.
- TypeScript strict typecheck: passed.
- Vitest: 13/13 passed.
- esbuild: passed; sandboxed execution required read-only parent-directory traversal, while the same project-local binary passed outside that restriction.
- C++ Debug: configure, build, repository verification, and 8/8 Catch2 tests passed.
- C++ Release: configure, build, repository verification, and 8/8 Catch2 tests passed.
- Dependency validation: npm audit reported 0 vulnerabilities; SBOM/NOTICE generation and unused-DLL checks passed.

## Remote clean-run result

- Commit: `79dd1ba6003fa756a63d67a8b26fafae59c306e6`.
- Workflow: <https://github.com/XuesongLuo/WorldOfWarCraftAssistant/actions/runs/36565531127>.
- Runner: `windows-2022`.
- Result: success.
- Passed stages: locked dependency bootstrap, clean npm restore, TypeScript formatting/lint/typecheck/tests/bundle, CMake configure/build/tests, dependency and vulnerability checks, and repository checks.
