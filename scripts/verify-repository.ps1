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
    'companion/CMakeLists.txt',
    'companion/include/wowai/app/build_info.hpp',
    'companion/src/app/main.cpp',
    'companion/tests/unit/build_info_tests.cpp',
    'scripts/Invoke-CMake.ps1',
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
