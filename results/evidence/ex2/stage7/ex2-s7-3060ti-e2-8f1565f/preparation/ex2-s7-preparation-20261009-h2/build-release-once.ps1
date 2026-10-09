$ErrorActionPreference = 'Stop'
$taskLog = Join-Path $PSScriptRoot 'release-build.log'
if (Test-Path $taskLog) { throw 'One-shot Release build already attempted' }
if ((git rev-parse HEAD) -ne '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274' -or @(git status --porcelain=v1 --untracked-files=all).Count) { throw 'Source gate failed' }
'INVOCATION: .\scripts\build.ps1 -Preset x64-release' | Set-Content $taskLog
$taskExit = 1
try {
    & .\scripts\build.ps1 -Preset x64-release 2>&1 | Tee-Object -FilePath $taskLog -Append
    $taskExit = 0
} catch {
    $_ | Out-String | Tee-Object -FilePath $taskLog -Append
    "LAST_NATIVE_EXIT=$LASTEXITCODE" | Tee-Object -FilePath $taskLog -Append
} finally {
    "WRAPPER_EXIT=$taskExit" | Tee-Object -FilePath $taskLog -Append
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'release-build-exit.txt'), "$taskExit`n", [Text.UTF8Encoding]::new($false))
}
exit $taskExit
