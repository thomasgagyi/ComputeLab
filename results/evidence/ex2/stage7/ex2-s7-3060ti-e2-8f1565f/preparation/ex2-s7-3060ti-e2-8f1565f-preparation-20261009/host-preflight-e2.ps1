$ErrorActionPreference='Stop'
function Capture([scriptblock]$query){try{& $query}catch{@{unknown=$true;error=$_.Exception.Message}}}
function Native([string]$name,[string[]]$argv){$text=& $name @argv 2>&1 | Out-String;@{argv=@($name)+$argv;exit_code=$LASTEXITCODE;stdout=$text}}
function NewRecord([string]$name,$value){$b=[Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 8)+"`n");$f=[IO.File]::Open((Join-Path $PSScriptRoot $name),[IO.FileMode]::CreateNew);try{$f.Write($b,0,$b.Length);$f.Flush($true)}finally{$f.Dispose()}}
$beforeScheme=Native 'powercfg' @('/getactivescheme');$beforeSleep=Native 'powercfg' @('/query','SCHEME_CURRENT','SUB_SLEEP')
if($beforeScheme.stdout -notmatch '381b4222-f694-41f0-9685-ff5bb260df2e'){throw 'Unexpected scheme'}
if($beforeSleep.stdout -notmatch 'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000708'){throw 'Expected restored 30-minute baseline differs'}
# Reapply the exact active-scheme AC setting already explicitly approved by this operator,
# now requested as the E2 AC-Never preflight condition. No other power/security setting changes.
$change=Native 'powercfg' @('/change','standby-timeout-ac','0');$afterSleep=Native 'powercfg' @('/query','SCHEME_CURRENT','SUB_SLEEP')
$powerPass=$change.exit_code -eq 0 -and $afterSleep.stdout -match 'STANDBYIDLE[\s\S]*?Current AC Power Setting Index: 0x00000000[\s\S]*?Current DC Power Setting Index: 0x00000384'
NewRecord 'e2-power-setting.json' @{authority='Prior direct approval of exact active Balanced AC Never change, plus current E2 request to confirm AC Never and restore prior 30 minutes on terminal';before_scheme=$beforeScheme;before_sleep=$beforeSleep;change=$change;after_sleep=$afterSleep;passed=$powerPass;restoration_command='powercfg /change standby-timeout-ac 30';restore_only_same_balanced_scheme=$true}
if(-not $powerPass){throw 'E2 AC Never readback failed'}
$e1Name='ComputeLab-EX2-S7-3060Ti-ex2-s7-3060ti-e1-8f1565f';$e1Xml=Export-ScheduledTask -TaskName $e1Name
$e1Bytes=[Text.UTF8Encoding]::new($false).GetBytes($e1Xml);$f=[IO.File]::Open((Join-Path $PSScriptRoot 'e1-task-preserved.xml'),[IO.FileMode]::CreateNew);try{$f.Write($e1Bytes,0,$e1Bytes.Length);$f.Flush($true)}finally{$f.Dispose()}
$futureName='ComputeLab-EX2-S7-3060Ti-ex2-s7-3060ti-e2-8f1565f'
if(@(Get-ScheduledTask | Where-Object TaskName -eq $futureName).Count){throw 'E2 task-name collision'}
$data=@{utc=[DateTime]::UtcNow.ToString('o');os=(Capture {Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,TotalVisibleMemorySize,FreePhysicalMemory});security_center=(Capture {@(Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct | Select-Object displayName,productState,timestamp)});processes=(Capture {@(Get-CimInstance Win32_Process | Where-Object Name -Match 'ComputeLab|AnyDesk|ChatGPT|codex|obs|steam|overlay|RTSS|Afterburner|renderdoc|nsight|gfxrecon|Avast|asw|nvcontainer' | Select-Object Name,ProcessId,ExecutablePath)});pending_cbs=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending');pending_wu=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired');nvidia=(Native 'nvidia-smi' @('-q'));e2_task_registered=$false;future_task_name_absent=$true;power_never=$true;residual_os_interruption_accepted=$true;e1_task_unchanged=$true}
NewRecord 'e2-host-preflight.json' $data
if($data.pending_cbs -or $data.pending_wu){throw 'Current restart-required flag'}
if(@($data.processes | Where-Object Name -In @('ComputeLabEx2Stage6.exe','ComputeLabEx2Stage6Supervisor.exe')).Count){throw 'Conflicting production process'}
'E2_HOST_AND_POWER_PREFLIGHT_PASS; no E2 task registered'
