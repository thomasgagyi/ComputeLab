param([ValidateSet('before','after')][string]$Phase='before')
$ErrorActionPreference='Stop'
function ReadFact([scriptblock]$query){try{& $query}catch{@{unknown=$true;error=$_.Exception.Message}}}
function Native([string]$name,[string[]]$argv){$output=& $name @argv 2>&1 | Out-String;@{command=$name;argv=$argv;exit_code=$LASTEXITCODE;stdout=$output}}
$data=[ordered]@{phase=$Phase;utc=[DateTime]::UtcNow.ToString('o');local_time=(Get-Date).ToString('o');timezone=(Get-TimeZone).Id;user=[Security.Principal.WindowsIdentity]::GetCurrent().Name;cwd=(Get-Location).Path}
$data.power_scheme=Native 'powercfg' @('/getactivescheme')
$data.sleep=Native 'powercfg' @('/query','SCHEME_CURRENT','SUB_SLEEP')
$data.nvidia=Native 'nvidia-smi' @('-q')
$data.os=ReadFact {Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,TotalVisibleMemorySize,FreePhysicalMemory,LastBootUpTime}
$data.processes=ReadFact {@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match 'ComputeLab|AnyDesk|ChatGPT|codex|dwm|explorer|avast|asw|obs|steam|RTSS|Afterburner|nsight|ncu|renderdoc|gfxreconstruct|perfmon|wpr|xperf|chrome|msedge|overlay'} | Select-Object Name,ProcessId,ExecutablePath)}
$data.security_center=ReadFact {@(Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct | Select-Object displayName,productState,timestamp,pathToSignedReportingExe)}
$data.avast_services=ReadFact {@(Get-CimInstance Win32_Service | Where-Object {$_.Name -match 'avast|asw'} | Select-Object Name,State,StartMode)}
$data.pending_restart=ReadFact {@{cbs=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending');windows_update=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired');pending_rename=(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue).PendingFileRenameOperations}}
$data.update_settings=ReadFact {Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\WindowsUpdate\UX\Settings' | Select-Object PauseUpdatesStartTime,PauseUpdatesExpiryTime,PauseFeatureUpdatesStartTime,PauseFeatureUpdatesEndTime,PauseQualityUpdatesStartTime,PauseQualityUpdatesEndTime,ActiveHoursStart,ActiveHoursEnd,SmartActiveHoursState}
$data.sessions=ReadFact {Native 'quser' @()}
$data.layers=@(foreach($keyPath in @('HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers','HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers','HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers','HKCU:\SOFTWARE\Khronos\Vulkan\ExplicitLayers')){ReadFact {if(Test-Path $keyPath){$key=Get-Item $keyPath;@{key=$keyPath;registrations=@(foreach($name in $key.GetValueNames()){@{path=$name;value=$key.GetValue($name)}})}}else{@{key=$keyPath;present=$false}}}})
$data.environment=@(Get-ChildItem Env: | Where-Object {$_.Name -match '^(VK_|CUDA_|COMPUTELAB_|NSIGHT|NVTX|NVIDIA_|RENDERDOC|GFXRECON|OBS_|STEAM_|DISABLE_VULKAN|DISABLE_VK_|ENABLE_VK_)|PROFIL|INJECT|TRACE'} | Select-Object Name,Value)
$data.sdk=@{CUDA_PATH=$env:CUDA_PATH;VULKAN_SDK=$env:VULKAN_SDK;VCPKG_ROOT=$env:VCPKG_ROOT}
$data.drive=[IO.DriveInfo]::new('C:\') | Select-Object DriveFormat,AvailableFreeSpace,TotalSize
$json=($data | ConvertTo-Json -Depth 8)+"`n";$bytes=[Text.UTF8Encoding]::new($false).GetBytes($json)
$file=[IO.File]::Open((Join-Path $PSScriptRoot "$Phase-host.json"),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$file.Write($bytes,0,$bytes.Length);$file.Flush($true)}finally{$file.Dispose()}
"HOST_PHASE=$Phase"
if(@($data.processes | Where-Object Name -In @('ComputeLabEx2Stage6.exe','ComputeLabEx2Stage6Supervisor.exe')).Count){exit 2}
