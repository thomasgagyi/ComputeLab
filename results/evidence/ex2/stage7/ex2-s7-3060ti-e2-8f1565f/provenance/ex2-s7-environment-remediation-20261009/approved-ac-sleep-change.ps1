$ErrorActionPreference='Stop'
$receiptPath=Join-Path $PSScriptRoot 'approved-power-change.json'
if(Test-Path $receiptPath){throw 'Power change already attempted'}
$beforeScheme=(& powercfg /getactivescheme 2>&1 | Out-String);if($LASTEXITCODE -ne 0){throw 'Power scheme query failed'}
$before=(& powercfg /query SCHEME_CURRENT SUB_SLEEP 2>&1 | Out-String);if($LASTEXITCODE -ne 0){throw 'Sleep query failed'}
if($beforeScheme -notmatch '381b4222-f694-41f0-9685-ff5bb260df2e' -or $before -notmatch 'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000708'){throw 'Approved baseline differs; no mutation'}
$changeOutput=(& powercfg /change standby-timeout-ac 0 2>&1 | Out-String);$changeExit=$LASTEXITCODE
$after=(& powercfg /query SCHEME_CURRENT SUB_SLEEP 2>&1 | Out-String);$queryExit=$LASTEXITCODE
$afterScheme=(& powercfg /getactivescheme 2>&1 | Out-String)
$pass=$changeExit -eq 0 -and $queryExit -eq 0 -and $afterScheme -match '381b4222-f694-41f0-9685-ff5bb260df2e' -and $after -match 'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000000[\s\S]*?Current DC Power Setting Index: 0x00000384'
$receipt=@{operator_approval='Approve this exact AC sleep change';approval_source='Direct user reply in this conversation';utc=[DateTime]::UtcNow.ToString('o');command='powercfg /change standby-timeout-ac 0';exit_code=$changeExit;stdout=$changeOutput;before_scheme=$beforeScheme;before_sleep=$before;after_scheme=$afterScheme;after_sleep=$after;readback_pass=$pass;restoration_after_campaign='While this same scheme is active: powercfg /change standby-timeout-ac 30';dc_changed=$false;other_power_settings_changed=$false}
$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($receipt | ConvertTo-Json -Depth 5)+"`n")
$stream=[IO.File]::Open($receiptPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
"AC_SLEEP_CHANGE_EXIT=$changeExit READBACK_PASS=$pass"
if(-not $pass){exit 2}
