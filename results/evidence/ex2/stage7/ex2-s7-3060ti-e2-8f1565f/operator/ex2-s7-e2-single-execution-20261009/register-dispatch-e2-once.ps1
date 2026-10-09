$ErrorActionPreference='Stop'
$env:GIT_OPTIONAL_LOCKS='0'
$root='C:\Users\rolan\src\ComputeLab-Stage7-3060Ti'
$record=Join-Path $root 'results/local/ex2-s7-e2-single-execution-20261009'
$prep=Join-Path $root 'results/local/ex2-s7-3060ti-e2-8f1565f-preparation-20261009'
$owned=Join-Path $root 'results/tmp/ex2-s7-3060ti-e2-8f1565f-execution'
$name='ComputeLab-EX2-S7-3060Ti-ex2-s7-3060ti-e2-8f1565f'
$python='C:\Users\rolan\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
function NewRecord([string]$path,$value){$b=[Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 8)+"`n");$f=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$f.Write($b,0,$b.Length);$f.Flush($true)}finally{$f.Dispose()}}
if((Get-Location).Path -ne $root){throw 'Wrong root'}
if(Test-Path $owned){throw 'E2 output ownership namespace already exists'}
if(Test-Path "$record/dispatch-intent.json"){throw 'Prior dispatch intent; no retry'}
if(@(Get-ScheduledTask | Where-Object TaskName -eq $name).Count){throw 'E2 scheduled task name collision'}
$auth=Get-Content "$record/operator-launch-confirmation.json" -Raw | ConvertFrom-Json
$verified=Get-Content "$prep/independent-prelaunch-verification.json" -Raw | ConvertFrom-Json
if($auth.authorization -ne $verified.human_authorization_string -or -not $verified.verification_pass){throw 'Exact authorization/verification mismatch'}
$baseline=Get-Content "$prep/e1-preservation-baseline.json" -Raw | ConvertFrom-Json
foreach($item in $baseline.files){$file=Get-Item -LiteralPath $item.path;if($file.Length -ne $item.bytes -or (Get-FileHash -LiteralPath $item.path).Hash.ToLowerInvariant() -ne $item.sha256){throw 'E1 preservation drift; no adoption/repair'}}
$e1Xml=[Text.UTF8Encoding]::new($false).GetBytes((Export-ScheduledTask -TaskName 'ComputeLab-EX2-S7-3060Ti-ex2-s7-3060ti-e1-8f1565f'))
if([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($e1Xml)).ToLowerInvariant() -ne (Get-FileHash "$prep/e1-task-preserved.xml").Hash.ToLowerInvariant()){throw 'E1 task configuration changed'}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$script=Join-Path $record 'single-use-execution.py'
$args='-B "'+$script+'" execute-once'
$action=New-ScheduledTaskAction -Execute $python -Argument $args -WorkingDirectory $root
$principal=New-ScheduledTaskPrincipal -UserId $identity.User.Value -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -Compatibility Win8
$settings.RestartCount=0;$settings.StartWhenAvailable=$false;$settings.AllowHardTerminate=$false
$definition=New-ScheduledTask -Action $action -Principal $principal -Settings $settings -Description 'Exact human-authorized EX2 Stage7 E2 one-shot. No triggers/restart/retry/resume; exclusive intent guards; no live-ledger monitoring.'
NewRecord "$record/task-registration-intent.json" @{utc=[DateTime]::UtcNow.ToString('o');task_name=$name;principal_sid=$identity.User.Value;trigger_count=0;restart_count=0;python_sha256=(Get-FileHash $python).Hash.ToLowerInvariant();script_sha256=(Get-FileHash $script).Hash.ToLowerInvariant();execute=$python;arguments=$args;e1_unchanged=$true}
Register-ScheduledTask -TaskName $name -InputObject $definition -ErrorAction Stop | Out-Null
$task=Get-ScheduledTask -TaskName $name
[xml]$xml=Export-ScheduledTask -TaskName $name
$ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('t','http://schemas.microsoft.com/windows/2004/02/mit/task')
$sid=$xml.SelectSingleNode('/t:Task/t:Principals/t:Principal/t:UserId',$ns).InnerText
$actual=$xml.SelectSingleNode('/t:Task/t:Actions/t:Exec',$ns)
$safe=$sid -eq $identity.User.Value -and $xml.SelectNodes('/t:Task/t:Triggers/*',$ns).Count -eq 0 -and $xml.SelectNodes('/t:Task/t:Settings/t:RestartOnFailure',$ns).Count -eq 0 -and $task.Settings.RestartCount -eq 0 -and $task.Principal.LogonType.ToString() -eq 'Interactive' -and $task.Principal.RunLevel.ToString() -eq 'Limited' -and $task.Settings.MultipleInstances.ToString() -eq 'IgnoreNew' -and $task.Settings.ExecutionTimeLimit -eq 'PT0S' -and -not $task.Settings.StartWhenAvailable -and $task.Settings.AllowDemandStart -and $actual.Command -eq $python -and $actual.Arguments -eq $args -and $actual.WorkingDirectory -eq $root -and $task.State.ToString() -eq 'Ready'
NewRecord "$record/task-registration-check.json" @{utc=[DateTime]::UtcNow.ToString('o');passed=$safe;principal_sid=$sid;task_name=$name;triggers=0;restarts=0;state=$task.State.ToString();xml=$xml.OuterXml;no_start_yet=$true}
if(-not $safe){throw 'Scheduled task safety check failed; stop before start'}
# Only this E2 wrapper directory is claimed; native point-of-use preflight checks all other 664 paths.
New-Item -ItemType Directory -Path $owned -ErrorAction Stop | Out-Null
NewRecord "$owned/ownership.json" @{utc=[DateTime]::UtcNow.ToString('o');owner_task=$name;manifest_id=$auth.manifest_id;no_retry_resume=$true}
NewRecord "$record/dispatch-intent.json" @{utc=[DateTime]::UtcNow.ToString('o');task_name=$name;one_start_request_only=$true;never_retry_after_lost_contact=$true}
try{
 Start-ScheduledTask -TaskName $name -ErrorAction Stop
 NewRecord "$record/dispatch-returned.json" @{utc=[DateTime]::UtcNow.ToString('o');request_returned=$true;supervisor_creation_not_inferred=$true;task_name=$name}
 'E2_TASK_DISPATCHED_ONCE; monitor only process/task/wrapper receipts'
}catch{
 NewRecord "$record/dispatch-error.json" @{utc=[DateTime]::UtcNow.ToString('o');error=$_.Exception.Message;no_retry=$true;creation_unknown=$true}
 throw
}
