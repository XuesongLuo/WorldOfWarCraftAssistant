# Dependency and supply-chain policy

## Locked inputs

- Project-local bootstrap downloads are declared with exact versions, URLs, and hashes in `eng/bootstrap-lock.json`.
- C++ packages use manifest mode and the exact `builtin-baseline` in `vcpkg.json`.
- Node packages use exact direct versions plus npm lockfile v3 in `package-lock.json`.
- Installation scripts are denied by default by npm 11; only the exact `esbuild@0.28.2` postinstall is approved in `package.json`.

## Verification gates

CI and local verification run formatting, ESLint, TypeScript, Vitest, esbuild, C++ tests, `npm audit --audit-level=high`, lockfile stability checks, and dependency artifact generation. A high or critical npm advisory fails the build. Pull-request changes to manifests, hashes, approved install scripts, or license declarations require review.

vcpkg does not provide a vulnerability database. Its immutable baseline, generated SPDX files, and the generated CycloneDX inventory are the inputs for release-time advisory review. A known unresolved high/critical issue blocks release even if compilation succeeds.

## Generated evidence

Run `scripts/Generate-DependencyArtifacts.ps1` after `npm ci` and CMake/vcpkg configure. It writes a CycloneDX inventory and a third-party license index to `out/dependency-artifacts/`. vcpkg's package-level SPDX documents and copyright texts remain under `out/vcpkg/<triplet>/share/<package>/`.

Codex CLI/App Server is deliberately not installed in STEP-003. Its executable, schema, hashes, and license set are locked together in STEP-009 so that the two alternative integration routes do not enter the production dependency tree simultaneously.
