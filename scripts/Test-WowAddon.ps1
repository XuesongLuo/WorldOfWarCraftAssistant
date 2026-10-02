param(
    [Parameter()]
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$addonRoot = Join-Path $RepositoryRoot 'addon/WowAIAssistant'
$tocPath = Join-Path $addonRoot 'WowAIAssistant.toc'
$requiredFiles = @(
    'WowAIAssistant.toc',
    'Settings.lua',
    'Context.lua',
    'Bridge.lua',
    'UI.lua',
    'Core.lua'
)

$missingFiles = $requiredFiles | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $addonRoot $_) -PathType Leaf)
}
if ($missingFiles) {
    throw "WoW addon files are missing: $($missingFiles -join ', ')"
}

$toc = Get-Content -LiteralPath $tocPath -Raw
$requiredMetadata = @(
    '## Interface: 120100',
    '## Version: 0.1.0-dev',
    '## SavedVariables: WowAIAssistantDB'
)
foreach ($metadata in $requiredMetadata) {
    if (-not $toc.Contains($metadata)) {
        throw "WoW addon TOC metadata is missing: $metadata"
    }
}

$tocLuaFiles = @(
    Get-Content -LiteralPath $tocPath |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') -and $_.EndsWith('.lua') }
)
$expectedLoadOrder = @('Settings.lua', 'Context.lua', 'Bridge.lua', 'UI.lua', 'Core.lua')
if (($tocLuaFiles -join '|') -ne ($expectedLoadOrder -join '|')) {
    throw "Unexpected Lua load order. Expected: $($expectedLoadOrder -join ', ')"
}

$luaFiles = Get-ChildItem -LiteralPath $addonRoot -Filter '*.lua' -File
$source = ($luaFiles | Get-Content -Raw) -join "`n"

$requiredPatterns = [ordered]@{
    'slash command' = 'SLASH_WOWAI1'
    'panel toggle' = 'TogglePanel'
    'drag persistence' = 'SaveWindowPlacement'
    'scale persistence' = 'SetScale'
    'center physical position' = 'local screenX = centerX and centerX * oldFrameScale'
    'scale coordinate conversion' = 'screenX / newFrameScale'
    'visible anchor' = 'WowAIAssistantAnchor'
    'visible data bridge' = 'WowAIAssistantVisibleDataBridge'
    'bridge protocol version' = 'PROTOCOL_VERSION'
    'bridge payload limit' = 'MAX_PAYLOAD_BYTES = 512'
    'event coalescing' = 'COALESCE_SECONDS = 0.1'
    'CRC32 frame integrity' = 'crc32'
    'secret value rejection' = 'issecretvalue'
    'human-readable preview' = '公开上下文预览'
    'title-only dragging' = 'WowAIAssistantTitleBar'
    'protected dispatch' = 'xpcall'
    'debug setting' = 'debugEnabled'
}

$removedChatPatterns = [ordered]@{
    'plugin chat input' = 'CreateFrame\s*\(\s*["'']EditBox["'']'
    'plugin send handler' = '\bHandleSend\b'
    'fake companion status' = '\bOFFLINE\b'
}
foreach ($entry in $removedChatPatterns.GetEnumerator()) {
    if ($source -match $entry.Value) {
        throw "WoW addon still contains removed behavior '$($entry.Key)': $($Matches[0])"
    }
}
foreach ($entry in $requiredPatterns.GetEnumerator()) {
    if ($source -notmatch [regex]::Escape($entry.Value)) {
        throw "WoW addon is missing required behavior marker '$($entry.Key)': $($entry.Value)"
    }
}

function Assert-SafeAddonSource {
    param(
        [Parameter(Mandatory)]
        [string]$LuaSource
    )

    $forbiddenPatterns = [ordered]@{
    'network API' = '\b(?:C_HTTP|HTTP|Socket|SendAddonMessage)\b'
    'external execution' = '\b(?:RunScript|LoadAddOn|C_AddOns\.LoadAddOn)\b'
    'protected game action' = '\b(?:CastSpell|UseAction|TargetUnit|MoveForwardStart)\b'
    'per-frame polling' = 'SetScript\s*\(\s*["'']OnUpdate["'']'
    }
    foreach ($entry in $forbiddenPatterns.GetEnumerator()) {
        if ($LuaSource -match $entry.Value) {
            throw "WoW addon contains forbidden $($entry.Key): $($Matches[0])"
        }
    }
}

Assert-SafeAddonSource -LuaSource $source

if ($source -match 'frame:RegisterForDrag') {
    throw 'The whole addon panel must not be registered as a drag surface.'
}

$unsafeFixtureRejected = $false
try {
    Assert-SafeAddonSource -LuaSource 'C_ChatInfo.SendAddonMessage("unsafe")'
}
catch {
    $unsafeFixtureRejected = $true
}
if (-not $unsafeFixtureRejected) {
    throw 'WoW addon safety validator failed to reject the unsafe API fixture.'
}

$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
foreach ($file in @($tocPath) + @($luaFiles.FullName)) {
    try {
        $null = $utf8.GetString([System.IO.File]::ReadAllBytes($file))
    }
    catch {
        throw "WoW addon file is not valid UTF-8: $file"
    }
}

Write-Output "WoW addon static verification passed: $($requiredFiles.Count) files, safe API boundary, required behavior markers, unsafe fixture rejected."
