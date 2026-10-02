param(
    [Parameter()]
    [string]$OutputDirectory = 'out\dependency-artifacts'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repositoryRoot $OutputDirectory
$packageLockPath = Join-Path $repositoryRoot 'package-lock.json'
$vcpkgRoot = Join-Path $repositoryRoot '.tools\vcpkg'
$vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
$vcpkgInstalledRoot = Join-Path $repositoryRoot 'out\vcpkg'
$bootstrapLockPath = Join-Path $repositoryRoot 'eng\bootstrap-lock.json'

if (-not (Test-Path -LiteralPath $packageLockPath -PathType Leaf)) {
    throw '缺少 package-lock.json。'
}
if (-not (Test-Path -LiteralPath $vcpkgExecutable -PathType Leaf)) {
    throw '缺少项目级 vcpkg。'
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$components = [System.Collections.Generic.List[object]]::new()
$notices = [System.Collections.Generic.List[string]]::new()
$bootstrapLock = Get-Content -Raw $bootstrapLockPath | ConvertFrom-Json

$components.Add([ordered]@{
    type = 'library'
    name = 'Microsoft.Web.WebView2'
    version = [string]$bootstrapLock.webView2Sdk.version
    licenses = @(@{ license = @{ id = 'BSD-3-Clause' } })
    purl = "pkg:nuget/Microsoft.Web.WebView2@$($bootstrapLock.webView2Sdk.version)"
})
$notices.Add("| NuGet archive | Microsoft.Web.WebView2 | $($bootstrapLock.webView2Sdk.version) | BSD-3-Clause | `.tools/webview2-sdk/LICENSE.txt` |")

$packageLock = Get-Content -Raw $packageLockPath | ConvertFrom-Json -AsHashtable
foreach ($entry in $packageLock.packages.GetEnumerator() | Sort-Object Key) {
    if (-not $entry.Key -or -not $entry.Key.Contains('node_modules/') -or -not $entry.Value.version) {
        continue
    }
    $name = ($entry.Key -split 'node_modules/')[-1]
    $license = if ($entry.Value.license) { [string]$entry.Value.license } else { 'NOASSERTION' }
    $components.Add([ordered]@{
        type = 'library'
        name = $name
        version = [string]$entry.Value.version
        licenses = @(@{ license = @{ id = $license } })
        purl = "pkg:npm/$([uri]::EscapeDataString($name))@$($entry.Value.version)"
    })
    $notices.Add("| npm | $($name.Replace('|', '\|')) | $($entry.Value.version) | $($license.Replace('|', '\|')) | `package-lock.json` |")
}

$env:VCPKG_ROOT = $vcpkgRoot
$vcpkgListText = (& $vcpkgExecutable list --x-json --x-install-root=$vcpkgInstalledRoot | Out-String)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$vcpkgPackages = $vcpkgListText | ConvertFrom-Json -AsHashtable
foreach ($entry in $vcpkgPackages.GetEnumerator() | Sort-Object Key) {
    $package = $entry.Value
    $portManifestPath = Join-Path $vcpkgRoot "ports\$($package.package_name)\vcpkg.json"
    $license = 'NOASSERTION'
    if (Test-Path -LiteralPath $portManifestPath -PathType Leaf) {
        $portManifest = Get-Content -Raw $portManifestPath | ConvertFrom-Json
        if ($portManifest.license) { $license = [string]$portManifest.license }
    }
    $version = [string]$package.version
    if ([int]$package.port_version -gt 0) { $version = "$version#$($package.port_version)" }
    $name = [string]$package.package_name
    $components.Add([ordered]@{
        type = 'library'
        name = $name
        version = $version
        licenses = @(@{ license = @{ id = $license } })
        purl = "pkg:generic/$name@$([uri]::EscapeDataString($version))?triplet=$($package.triplet)"
    })
    $notices.Add("| vcpkg | $($name.Replace('|', '\|')) | $version | $($license.Replace('|', '\|')) | `out/vcpkg/$($package.triplet)/share/$name/copyright` |")
}

$seedMaterial = (Get-FileHash -Algorithm SHA256 $packageLockPath).Hash +
    (Get-FileHash -Algorithm SHA256 $bootstrapLockPath).Hash
$seedBytes = [System.Text.Encoding]::UTF8.GetBytes($seedMaterial)
$seed = [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($seedBytes)).ToLowerInvariant()
$uuidText = '{0}-{1}-{2}-{3}-{4}' -f $seed.Substring(0, 8), $seed.Substring(8, 4), $seed.Substring(12, 4), $seed.Substring(16, 4), $seed.Substring(20, 12)
$sbom = [ordered]@{
    bomFormat = 'CycloneDX'
    specVersion = '1.6'
    serialNumber = "urn:uuid:$uuidText"
    version = 1
    metadata = @{
        component = @{
            type = 'application'
            name = 'world-of-warcraft-assistant'
            version = '0.1.0-dev'
        }
        tools = @(@{ name = 'Generate-DependencyArtifacts.ps1'; vendor = 'WorldOfWarcraftAssistant' })
    }
    components = @($components | Sort-Object name, version)
}

$sbomPath = Join-Path $outputRoot 'sbom.cdx.json'
$sbom | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $sbomPath -Encoding utf8

$noticeLines = @(
    '# Third-party dependency notice inventory',
    '',
    'This generated inventory covers the currently restored npm and vcpkg dependency trees.',
    'It is an engineering review aid; the referenced package copyright files remain authoritative.',
    '',
    '| Ecosystem | Package | Version | Declared license | License source |',
    '|---|---|---|---|---|'
) + @($notices | Sort-Object)
$noticePath = Join-Path $outputRoot 'THIRD-PARTY-NOTICES.md'
$noticeLines | Set-Content -LiteralPath $noticePath -Encoding utf8

Write-Output "已生成 SBOM：$sbomPath"
Write-Output "已生成许可证清单：$noticePath"
