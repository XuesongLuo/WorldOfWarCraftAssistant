param(
    [Parameter()]
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = $RepositoryRoot
$requiredPaths = @(
    'README.md',
    '.editorconfig',
    '.clang-format',
    '.clang-tidy',
    '.prettierrc.json',
    '.luacheckrc',
    'CMakeLists.txt',
    'CMakePresets.json',
    'eng/toolchain.json',
    'eng/toolchain.schema.json',
    'eng/bootstrap-lock.json',
    'eng/bootstrap-lock.schema.json',
    'eng/codex-runtime-lock.json',
    'eng/codex-runtime-lock.schema.json',
    'vcpkg.json',
    'package.json',
    'package-lock.json',
    '.env.example',
    'companion/CMakeLists.txt',
    'companion/include/wowai/app/build_info.hpp',
    'companion/include/wowai/app/application_shell.hpp',
    'companion/include/wowai/app/lifecycle.hpp',
    'companion/include/wowai/platform/resources.hpp',
    'companion/include/wowai/platform/single_instance.hpp',
    'companion/include/wowai/capture/anchor_detector.hpp',
    'companion/include/wowai/capture/calibration.hpp',
    'companion/include/wowai/capture/geometry.hpp',
    'companion/include/wowai/capture/selection_store.hpp',
    'companion/include/wowai/capture/window_capture.hpp',
    'companion/include/wowai/capture/window_discovery.hpp',
    'companion/include/wowai/codex/protocol.hpp',
    'companion/include/wowai/codex/mock_host.hpp',
    'companion/include/wowai/codex/host_client.hpp',
    'companion/include/wowai/codex/host_process.hpp',
    'companion/src/app/main.cpp',
    'companion/src/app/application_shell.cpp',
    'companion/src/app/lifecycle.cpp',
    'companion/src/platform/single_instance.cpp',
    'companion/src/capture/anchor_detector.cpp',
    'companion/src/capture/calibration.cpp',
    'companion/src/capture/selection_store.cpp',
    'companion/src/capture/window_capture.cpp',
    'companion/src/capture/window_discovery.cpp',
    'companion/src/codex/protocol.cpp',
    'companion/src/codex/mock_host.cpp',
    'companion/src/codex/mock_host_main.cpp',
    'companion/src/codex/host_client.cpp',
    'companion/src/codex/host_process.cpp',
    'companion/tests/codex_protocol/contract_tests.cpp',
    'companion/tests/unit/build_info_tests.cpp',
    'companion/tests/unit/lifecycle_tests.cpp',
    'companion/tests/unit/host_process_tests.cpp',
    'companion/tests/fixtures/host_process_child.cpp',
    'companion/tests/unit/resource_tests.cpp',
    'companion/tests/unit/single_instance_tests.cpp',
    'companion/tests/fixtures/anchor-samples.json',
    'companion/tests/unit/anchor_detector_tests.cpp',
    'companion/tests/unit/calibration_tests.cpp',
    'companion/tests/unit/selection_store_tests.cpp',
    'companion/tests/unit/window_discovery_tests.cpp',
    'codex-host/src/protocol/types.ts',
    'codex-host/src/protocol/validation.ts',
    'codex-host/src/protocol/session.ts',
    'codex-host/src/runtime/mock-runtime.ts',
    'codex-host/src/runtime/runtime-lock.ts',
    'codex-host/src/runtime/app-server-runtime.ts',
    'codex-host/src/runtime/errors.ts',
    'codex-host/src/app-server/client.ts',
    'codex-host/src/app-server/process.ts',
    'codex-host/src/app-server/protocol.ts',
    'codex-host/src/app-server/tool-policy.ts',
    'codex-host/src/host/application-directories.ts',
    'codex-host/src/host/json-line-reader.ts',
    'codex-host/src/host/server.ts',
    'codex-host/tests/host-server.test.ts',
    'codex-host/tests/line-reader.test.ts',
    'codex-host/tests/runtime-environment.test.ts',
    'codex-host/tests/app-server-client.test.ts',
    'codex-host/tests/tool-policy.test.ts',
    'codex-host/tests/protocol.test.ts',
    'contracts/PROTOCOL.md',
    'contracts/v1/assistant-request.schema.json',
    'contracts/v1/assistant-response.schema.json',
    'contracts/v1/envelope.schema.json',
    'contracts/v1/error.schema.json',
    'contracts/v2/assistant-request.schema.json',
    'contracts/v2/assistant-response.schema.json',
    'contracts/v2/envelope.schema.json',
    'contracts/v2/error.schema.json',
    'contracts/tests/validation-cases.json',
    'contracts/codex-app-server/0.159.2/codex_app_server_protocol.v2.schemas.json',
    'addon/WowAIAssistant/WowAIAssistant.toc',
    'addon/WowAIAssistant/Settings.lua',
    'addon/WowAIAssistant/Context.lua',
    'addon/WowAIAssistant/Bridge.lua',
    'addon/WowAIAssistant/UI.lua',
    'addon/WowAIAssistant/Core.lua',
    'docs/test-plans/STEP-005-wow-addon-manual-test.md',
    'docs/architecture/companion-resource-ownership.md',
    'docs/architecture/2026-10-01-implementation-migration.md',
    'docs/licenses/codex-cli-0.159.2.md',
    'docs/test-results/STEP-006-local-validation-2026-09-30.md',
    'docs/test-plans/STEP-007-wow-window-anchor-manual-test.md',
    'docs/test-results/STEP-007-automated-validation-2026-09-30.md',
    'docs/test-results/STEP-009-automated-validation-2026-10-01.md',
    'docs/test-results/STEP-010-automated-validation-2026-10-02.md',
    'docs/environment/2026-10-01-overlay-vision-environment.md',
    'scripts/Test-WowAddon.ps1',
    'scripts/Test-Step006Companion.ps1',
    'scripts/Invoke-CMake.ps1',
    'scripts/Invoke-Npm.ps1',
    'scripts/Bootstrap-Dependencies.ps1',
    'scripts/Generate-DependencyArtifacts.ps1',
    'scripts/Test-Dependencies.ps1',
    '.github/workflows/ci.yml'
)

$missing = $requiredPaths | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $repositoryRoot $_))
}

if ($missing) {
    $missing | ForEach-Object { Write-Error "缺少仓库骨架文件：$_" }
    exit 1
}

$codexLock = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'eng\codex-runtime-lock.json') | ConvertFrom-Json
$codexSchemaPath = Join-Path $repositoryRoot $codexLock.appServerSchema.path
$codexSchemaHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $codexSchemaPath).Hash.ToLowerInvariant()
if ($codexSchemaHash -ne $codexLock.appServerSchema.sha256) {
    throw 'Codex App Server Schema hash does not match eng/codex-runtime-lock.json.'
}

& (Join-Path $PSScriptRoot 'verify-step001.ps1') -DocumentsRoot $repositoryRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& (Join-Path $PSScriptRoot 'Test-WowAddon.ps1') -RepositoryRoot $repositoryRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$trackedSensitivePatterns = @('*.png', '*.jpg', '*.jpeg', '*.webp', '*.dmp', '.env')
foreach ($pattern in $trackedSensitivePatterns) {
    $matches = git -C $repositoryRoot ls-files $pattern
    if ($matches) {
        Write-Error "仓库包含禁止提交的敏感产物模式 $pattern：$matches"
        exit 1
    }
}

git -C $repositoryRoot diff --check
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Output "仓库骨架验证通过：$($requiredPaths.Count) 个关键文件存在，STEP-001 产物与基础安全检查通过。"
exit 0
