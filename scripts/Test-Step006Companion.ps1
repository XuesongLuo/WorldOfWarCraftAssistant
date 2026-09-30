param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter()]
    [ValidateRange(1, 100)]
    [int]$Cycles = 25
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class WowAIWindowTest
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
}
'@

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$wmClose = 0x0010
$wmCommand = 0x0111
$commandExit = 1001

for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
    $primary = Start-Process -FilePath $resolvedExecutable -PassThru
    try {
        $window = [IntPtr]::Zero
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 50
            $primary.Refresh()
            if ($primary.HasExited) {
                throw "Cycle $cycle exited during startup with code $($primary.ExitCode)."
            }
            $window = $primary.MainWindowHandle
        }
        if ($window -eq [IntPtr]::Zero) {
            throw "Cycle $cycle did not create the companion window."
        }

        $duplicate = Start-Process -FilePath $resolvedExecutable -PassThru -Wait
        if ($duplicate.ExitCode -ne 0) {
            throw "Cycle $cycle duplicate returned exit code $($duplicate.ExitCode)."
        }
        if ($primary.HasExited) {
            throw "Cycle $cycle duplicate replaced the primary process."
        }

        $focusDeadline = [DateTime]::UtcNow.AddSeconds(2)
        while ([WowAIWindowTest]::GetForegroundWindow() -ne $window -and
               [DateTime]::UtcNow -lt $focusDeadline) {
            Start-Sleep -Milliseconds 50
        }
        if ([WowAIWindowTest]::GetForegroundWindow() -ne $window) {
            throw "Cycle $cycle duplicate did not focus the existing window."
        }

        if (($cycle % 2) -eq 0) {
            $exitMessage = $wmCommand
            $exitWparam = [IntPtr]$commandExit
        }
        else {
            $exitMessage = $wmClose
            $exitWparam = [IntPtr]::Zero
        }
        if (-not [WowAIWindowTest]::PostMessage(
                $window,
                $exitMessage,
                $exitWparam,
                [IntPtr]::Zero
            )) {
            throw "Cycle $cycle could not request a normal exit."
        }
        if (-not $primary.WaitForExit(5000)) {
            throw "Cycle $cycle left the companion process running after exit."
        }
        if ($primary.ExitCode -ne 0) {
            throw "Cycle $cycle primary returned exit code $($primary.ExitCode)."
        }
    }
    finally {
        if (-not $primary.HasExited) {
            $primary.Kill()
            $primary.WaitForExit()
        }
        $primary.Dispose()
    }
}

Write-Output "STEP-006 companion validation passed: $Cycles start/duplicate/focus/exit cycles."
