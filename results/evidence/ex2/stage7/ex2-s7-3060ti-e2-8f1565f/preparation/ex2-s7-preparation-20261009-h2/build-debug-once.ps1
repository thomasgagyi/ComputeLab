$ErrorActionPreference = 'Stop'
$taskLog = Join-Path $PSScriptRoot 'debug-build.log'
if (Test-Path $taskLog) { throw 'One-shot build already attempted' }
Start-Transcript -Path $taskLog -NoClobber
$taskExit = 1
try {
    'INVOCATION: .\scripts\build.ps1 -Preset x64-debug'
    & .\scripts\build.ps1 -Preset x64-debug
    $taskExit = 0
} catch {
    $_ | Format-List * -Force | Out-Host
    "LAST_NATIVE_EXIT=$LASTEXITCODE"
} finally {
    "WRAPPER_EXIT=$taskExit"
    Stop-Transcript
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'debug-build-exit.txt'), "$taskExit`n", [Text.UTF8Encoding]::new($false))
}
exit $taskExit
