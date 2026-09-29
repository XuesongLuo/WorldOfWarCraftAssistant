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
    'vcpkg.json',
    'package.json',
    'package-lock.json',
    '.env.example',
    'companion/CMakeLists.txt',
    'companion/include/wowai/app/build_info.hpp',
    'companion/include/wowai/codex/protocol.hpp',
    'companion/include/wowai/codex/mock_host.hpp',
    'companion/src/app/main.cpp',
    'companion/src/codex/protocol.cpp',
    'companion/src/codex/mock_host.cpp',
    'companion/src/codex/mock_host_main.cpp',
    'companion/tests/codex_protocol/contract_tests.cpp',
    'companion/tests/unit/build_info_tests.cpp',
    'codex-host/src/protocol/types.ts',
    'codex-host/src/protocol/validation.ts',
    'codex-host/src/protocol/session.ts',
    'codex-host/src/runtime/mock-runtime.ts',
    'codex-host/tests/protocol.test.ts',
    'contracts/PROTOCOL.md',
    'contracts/v1/assistant-request.schema.json',
    'contracts/v1/assistant-response.schema.json',
    'contracts/v1/envelope.schema.json',
    'contracts/v1/error.schema.json',
    'contracts/tests/validation-cases.json',
    'addon/WowAIAssistant/WowAIAssistant.toc',
    'addon/WowAIAssistant/Settings.lua',
    'addon/WowAIAssistant/Context.lua',
    'addon/WowAIAssistant/UI.lua',
    'addon/WowAIAssistant/Core.lua',
    'docs/test-plans/STEP-005-wow-addon-manual-test.md',
    'scripts/Test-WowAddon.ps1',
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
