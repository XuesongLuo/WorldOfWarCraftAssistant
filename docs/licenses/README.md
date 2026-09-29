# Third-party license baseline

The authoritative dependency sets are `package-lock.json` and `vcpkg.json`. Run `scripts/Generate-DependencyArtifacts.ps1` after restoring both ecosystems to generate the complete transitive inventory at `out/dependency-artifacts/THIRD-PARTY-NOTICES.md` and the CycloneDX SBOM at `out/dependency-artifacts/sbom.cdx.json`.

Current direct dependencies:

| Ecosystem | Package | Locked version | Declared license | Purpose |
|---|---|---:|---|---|
| npm | `@eslint/js` | 10.0.1 | MIT | JavaScript lint rules |
| npm | `@types/node` | 24.19.0 | MIT | Node.js API types |
| npm | `esbuild` | 0.28.2 | MIT | Host bundling |
| npm | `eslint` | 10.11.0 | MIT | Static analysis |
| npm | `prettier` | 3.9.9 | MIT | Formatting |
| npm | `typescript` | 6.0.2 | Apache-2.0 | Type checking |
| npm | `typescript-eslint` | 8.71.0 | MIT | Typed lint rules |
| npm | `vitest` | 5.0.2 | MIT | TypeScript tests |
| npm | `zod` | 4.6.5 | MIT | Runtime validation |
| vcpkg | `catch2` | 3.16.0 | BSL-1.0 | C++ tests |
| vcpkg | `nlohmann-json` | 3.12.0#2 | MIT | JSON parsing |
| vcpkg | `spdlog` | 1.17.0#1 | MIT | Local logging |
| vcpkg | `sqlite3` | 3.53.4#1 | blessing | Local storage |
| vcpkg | `wil` | 1.0.260126.7 | MIT | Windows resource management |

The generated inventory also includes transitive packages such as `fmt`, `vcpkg-cmake`, and npm toolchain dependencies. Codex CLI/App Server is intentionally deferred to STEP-009, where its binary, protocol schema, hashes, and licenses must be locked as one unit.
