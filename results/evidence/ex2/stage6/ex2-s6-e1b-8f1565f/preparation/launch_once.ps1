$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Get-Location).Path
$id = 'ex2-s6-e1b-8f1565f'
$semantic = '5a87f15ef384d3c8c1c0f43adf736e17a5e6cdc46fcd2e849f333ae3e07c0963'
$physical = 'ad83b68e8ff430854325ec944c1317a348b087dd9daaf825f51100b7cdaa68b8'
$manifest = "results/local/$id-stage6-manifest.json"
$executable = Join-Path $repo 'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'
$execution = Join-Path $repo "results/tmp/$id-execution"
function Publish([string]$name, $value) {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 12) + "`n")
    $stream = [IO.File]::Open((Join-Path $execution $name), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
}
if ((git rev-parse --verify HEAD) -ne '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274' -or
    (git branch --show-current) -ne 'main' -or @(git status --porcelain=v1 --untracked-files=all).Count -ne 0) { throw 'Source guard rejected' }
git diff --check
if ($LASTEXITCODE -ne 0 -or @(git diff --cached --name-status).Count -ne 0) { throw 'Git diff/index guard rejected' }
if ((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $physical) { throw 'Authorized physical hash differs' }
$m = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
if ($m.manifest_id -ne $id -or $m.manifest_sha256 -ne $semantic) { throw 'Authorized semantic tuple differs' }
$freeze = Get-Content -Raw -LiteralPath "results/local/$id-stage6-artifact-freeze.json" | ConvertFrom-Json
$verification = Get-Content -Raw -LiteralPath "results/local/$id-stage6-verification.json" | ConvertFrom-Json
if (-not $verification.verification_pass -or $verification.status -ne 'PASS' -or
    $verification.manifest_sha256 -ne $semantic -or $verification.manifest_file_sha256 -ne $physical) { throw 'Verification guard rejected' }
foreach ($a in $freeze.artifacts.PSObject.Properties) {
    $file = Get-Item -LiteralPath $a.Value.path
    if ($file.PSIsContainer -or $file.Length -ne $a.Value.size_bytes -or
        (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $a.Value.sha256) { throw 'Artifact drift' }
}
$live = @(Get-CimInstance Win32_Process -Filter "Name='ComputeLabEx2Stage6Supervisor.exe' OR Name='ComputeLabEx2Stage6.exe'")
if ($live.Count -ne 0) { throw 'Live production process exists; STOP without termination' }
foreach ($n in 0..219) {
    $sid = '{0}-slot-{1:D3}' -f $id,$n
    foreach ($suffix in @('','.incomplete','.failure.json')) {
        if (Test-Path -LiteralPath "results/local/$sid$suffix") { throw 'Production child namespace collision' }
    }
}
foreach ($p in @("results/local/$id-stage6-control.json", "results/local/$id-stage6-control.json.incomplete",
    "results/local/$id-stage6-control.json.incomplete.tmp", "results/local/$id-stage6-analysis", $execution)) {
    if (Test-Path -LiteralPath $p) { throw "Production namespace collision: $p" }
}
New-Item -ItemType Directory -Path $execution -ErrorAction Stop | Out-Null
Publish 'launch-intent.json' ([ordered]@{
    manifest_id=$id; manifest_sha256=$semantic; manifest_file_sha256=$physical
    authorization="AUTHORIZE S6-E1B $id $semantic $physical"
    executable='out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'
    arguments=@('--manifest', $manifest); working_directory=$repo
    read_only_guards='PASS'; matching_live_processes=0; namespace_paths_absent=665
    standalone_verifiers_rerun=0; external_timeout=$false
    intent_utc=[DateTime]::UtcNow.ToString('o')
})
# The single production process-creation call. Never repeat after any exception or disconnect.
$process = Start-Process -FilePath $executable -ArgumentList @('--manifest', $manifest) -WorkingDirectory $repo `
    -WindowStyle Hidden -RedirectStandardOutput (Join-Path $execution 'stdout.log') `
    -RedirectStandardError (Join-Path $execution 'stderr.log') -PassThru
$retainedHandle = $process.Handle
$startUtc = $process.StartTime.ToUniversalTime()
Publish 'launch.json' ([ordered]@{launch_count=1;process_id=$process.Id;start_utc=$startUtc.ToString('o')
    manifest_id=$id;manifest_sha256=$semantic;manifest_file_sha256=$physical
    executable='out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'
    arguments=@('--manifest', $manifest); wait_mechanism='WaitForExit on retained exact process handle; no timeout'})
Write-Output "PRODUCTION LAUNCHED ONCE: PID=$($process.Id) START_UTC=$($startUtc.ToString('o'))"
$process.WaitForExit()
$process.Refresh()
$exitCode = $process.ExitCode
$endUtc = $process.ExitTime.ToUniversalTime()
Publish 'terminal.json' ([ordered]@{launch_count=1;process_id=$process.Id;start_utc=$startUtc.ToString('o')
    end_utc=$endUtc.ToString('o');raw_exit_code=$exitCode;operator_wall_seconds=($endUtc-$startUtc).TotalSeconds
    manifest_id=$id;manifest_sha256=$semantic;manifest_file_sha256=$physical})
Write-Output "PRODUCTION TERMINAL: PID=$($process.Id) RAW_EXIT=$exitCode END_UTC=$($endUtc.ToString('o'))"
$process.Dispose()
