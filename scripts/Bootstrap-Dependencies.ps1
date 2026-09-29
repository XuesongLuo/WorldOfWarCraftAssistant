param(
    [Parameter()]
    [switch]$SkipNode,

    [Parameter()]
    [switch]$SkipVcpkg
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $repositoryRoot '.tools'
$downloadsRoot = Join-Path $toolsRoot 'downloads'
$lock = Get-Content -Raw (Join-Path $repositoryRoot 'eng\bootstrap-lock.json') | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $downloadsRoot | Out-Null

function Get-VerifiedArchive {
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$Archive,
        [Parameter(Mandatory)] [string]$Sha256
    )

    $destination = Join-Path $downloadsRoot $Archive
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $destination
    }

    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash.ToLowerInvariant()
    if ($actual -ne $Sha256) {
        throw "依赖归档 SHA-256 不匹配：$Archive，期望 $Sha256，实际 $actual"
    }
    return $destination
}

function Get-VerifiedSha512File {
    param(
        [Parameter(Mandatory)] [string]$Url,
        [Parameter(Mandatory)] [string]$Archive,
        [Parameter(Mandatory)] [string]$Sha512
    )

    $destination = Join-Path $downloadsRoot $Archive
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $destination
    }
    $actual = (Get-FileHash -Algorithm SHA512 -LiteralPath $destination).Hash.ToLowerInvariant()
    if ($actual -ne $Sha512) {
        throw "依赖文件 SHA-512 不匹配：$Archive，期望 $Sha512，实际 $actual"
    }
    return $destination
}

if (-not $SkipNode) {
    $nodeRoot = Join-Path $toolsRoot 'node'
    $nodeExecutable = Join-Path $nodeRoot 'node.exe'
    if (-not (Test-Path -LiteralPath $nodeExecutable -PathType Leaf)) {
        $archive = Get-VerifiedArchive $lock.node.url $lock.node.archive $lock.node.sha256
        $extractRoot = Join-Path $toolsRoot "node-$($lock.node.version)-extract"
        Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force
        $expanded = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
        if (-not $expanded) { throw 'Node.js 归档没有顶层目录。' }
        Move-Item -LiteralPath $expanded.FullName -Destination $nodeRoot
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }

    $nodeVersion = (& $nodeExecutable --version).TrimStart('v')
    $npmVersion = (& (Join-Path $nodeRoot 'npm.cmd') --version).Trim()
    if ($nodeVersion -ne $lock.node.version -or $npmVersion -ne $lock.npm) {
        throw "Node/npm 版本不匹配：Node $nodeVersion，npm $npmVersion"
    }
}

if (-not $SkipVcpkg) {
    $binRoot = Join-Path $toolsRoot 'bin'
    $sevenZipRoot = Join-Path $binRoot '7zip'
    New-Item -ItemType Directory -Force -Path $sevenZipRoot | Out-Null
    $sevenZip = Join-Path $sevenZipRoot '7z.exe'
    if (-not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
        $extractor = Get-VerifiedSha512File $lock.sevenZip.extractorUrl $lock.sevenZip.extractorArchive $lock.sevenZip.extractorSha512
        $sevenZipDownload = Get-VerifiedSha512File $lock.sevenZip.url $lock.sevenZip.archive $lock.sevenZip.sha512
        & $extractor x $sevenZipDownload "-o$sevenZipRoot" -y | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
            throw '无法解压项目级 7-Zip。'
        }
    }

    $vcpkgRoot = Join-Path $toolsRoot 'vcpkg'
    $vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -PathType Leaf)) {
        $archive = Get-VerifiedArchive $lock.vcpkg.url $lock.vcpkg.archive $lock.vcpkg.sha256
        $extractRoot = Join-Path $toolsRoot "vcpkg-$($lock.vcpkg.commit)-extract"
        Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force
        $expanded = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
        if (-not $expanded) { throw 'vcpkg 归档没有顶层目录。' }
        Move-Item -LiteralPath $expanded.FullName -Destination $vcpkgRoot
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath $vcpkgExecutable -PathType Leaf)) {
        & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $hasBaseline = $false
    if (Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git') -PathType Container) {
        & git -C $vcpkgRoot cat-file -e "$($lock.vcpkg.commit)^{commit}" 2>$null
        $hasBaseline = $LASTEXITCODE -eq 0
    }
    if (-not $hasBaseline) {
        if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git') -PathType Container)) {
            & git -C $vcpkgRoot init
            & git -C $vcpkgRoot remote add origin 'https://github.com/microsoft/vcpkg.git'
        }
        & git -C $vcpkgRoot fetch --depth=1 origin $lock.vcpkg.commit
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $versionOutput = (& $vcpkgExecutable version | Select-Object -First 1)
    if ($versionOutput -notmatch [regex]::Escape($lock.vcpkg.toolVersion)) {
        throw "vcpkg 工具版本不匹配：$versionOutput"
    }
}

Write-Output "项目依赖工具验证通过：Node $($lock.node.version)、npm $($lock.npm)、vcpkg $($lock.vcpkg.toolVersion)。"
