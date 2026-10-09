$ErrorActionPreference = 'Stop'
$env:GIT_OPTIONAL_LOCKS = '0'
function WriteNew([string]$name, $value) {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 15) + "`n")
    $stream = [IO.File]::Open((Join-Path $PSScriptRoot $name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
}
$taskCommands = [Collections.Generic.List[object]]::new()
function Observe([string]$label,[scriptblock]$action) {
    try { $result = & $action; $taskCommands.Add(@{query=$label;success=$true;utc=[DateTime]::UtcNow.ToString('o')}); return $result }
    catch { $taskCommands.Add(@{query=$label;success=$false;error=$_.Exception.Message;utc=[DateTime]::UtcNow.ToString('o')}); return @{unknown=$true;error=$_.Exception.Message} }
}
function Native([string]$tool,[string[]]$arguments) {
    $text = & $tool @arguments 2>&1 | Out-String
    return @{tool=$tool;arguments=$arguments;exit_code=$LASTEXITCODE;output=$text}
}
$taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$taskHost = [ordered]@{
    utc=[DateTime]::UtcNow.ToString('o');local_time=(Get-Date).ToString('o');cwd=(Get-Location).Path
    user=$taskIdentity.Name;elevated=([Security.Principal.WindowsPrincipal]::new($taskIdentity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    timezone=(Get-TimeZone | Select-Object Id,DisplayName,BaseUtcOffset)
    git_head=((& git rev-parse HEAD) -join "`n");git_status=@(& git status --porcelain=v1 --untracked-files=all);git_branch=((& git branch --show-current) -join "`n")
    drive=([IO.DriveInfo]::new('C:\') | Select-Object Name,DriveFormat,AvailableFreeSpace,TotalSize)
}
$taskHost.os = Observe 'Win32_OperatingSystem' { Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,TotalVisibleMemorySize,FreePhysicalMemory,LastBootUpTime }
$taskHost.cpu = Observe 'Win32_Processor' { Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors }
$taskAll = @(Observe 'Win32_Process relevant processes and production detection' { Get-CimInstance Win32_Process | Where-Object { $_.Name -match 'ComputeLab|AnyDesk|ChatGPT|codex|dwm|explorer|avast|asw|obs|steam|RTSS|Afterburner|ncu|nsight|renderdoc|gfxreconstruct|vulkaninfo|perfmon|wpr|xperf|gpu' } | Select-Object Name,ProcessId,ExecutablePath,CommandLine,CreationDate })
$taskHost.processes = $taskAll
$taskHost.production_processes = @($taskAll | Where-Object { $_.Name -match '^ComputeLabEx2Stage6( Supervisor)?' -or $_.Name -in @('ComputeLabEx2Stage6.exe','ComputeLabEx2Stage6Supervisor.exe') })
$taskHost.nvidia_smi = Observe 'nvidia-smi -q metadata only' { Native 'nvidia-smi' @('-q') }
$taskHost.environment = [ordered]@{}
foreach ($taskName in @('CUDA_PATH','VULKAN_SDK','VCPKG_ROOT','CUDA_VISIBLE_DEVICES','CUDA_LAUNCH_BLOCKING','VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_IMPLICIT_LAYER_PATH','VK_LOADER_LAYERS_ENABLE','VK_LOADER_LAYERS_DISABLE','VK_LOADER_LAYERS_ALLOW','VK_ICD_FILENAMES','VK_DRIVER_FILES','COMPUTELAB_EX2_D1_VALIDATION','COMPUTELAB_EX2_E_VALIDATION','DISABLE_VK_LAYER_VALVE_steam_overlay_1','DISABLE_VK_LAYER_VALVE_steam_fossilize_1','OBS_VKCAPTURE','ENABLE_VK_LAYER_OBS_HOOK')) {
    $taskHost.environment[$taskName] = [Environment]::GetEnvironmentVariable($taskName,'Process')
}
$taskHost.additional_instrumentation_environment = @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(VK_|CUDA_|COMPUTELAB_|NSIGHT|NVTX|NVIDIA_|RENDERDOC|GFXRECON|OBS_|STEAM_|DISABLE_VK_|ENABLE_VK_)|PROFIL|INJECT|TRACE' } | Select-Object Name,Value)
$taskHost.layers = [Collections.Generic.List[object]]::new()
foreach ($taskKey in @('HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers','HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers','HKCU:\SOFTWARE\Khronos\Vulkan\ExplicitLayers','HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers')) {
    $taskHost.layers.Add((Observe "Registry $taskKey" {
        if (-not (Test-Path $taskKey)) { return @{key=$taskKey;present=$false} }
        $key = Get-Item $taskKey
        $values = foreach ($name in $key.GetValueNames()) {
            $manifest = $null
            if (Test-Path -LiteralPath $name) { $manifest = Get-Content -LiteralPath $name -Raw | ConvertFrom-Json }
            @{path=$name;registry_value=$key.GetValue($name);manifest=$manifest}
        }
        @{key=$taskKey;present=$true;values=@($values)}
    }))
}
$taskHost.antivirus = [ordered]@{
    operator_statement='Avast reportedly blocked something during preparation without deleting/moving files; operator subsequently turned it off. Not independently established by that statement.'
    security_center=(Observe 'SecurityCenter2 AntiVirusProduct raw state' { Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct | Select-Object displayName,productState,pathToSignedProductExe,pathToSignedReportingExe,timestamp })
    services=(Observe 'Avast/asw service state' { Get-CimInstance Win32_Service | Where-Object { $_.Name -match 'avast|asw' -or $_.DisplayName -match 'avast' } | Select-Object Name,DisplayName,State,StartMode,PathName })
    events=(Observe 'Recent Avast/asw application events, maximum 30' { @(Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=[DateTime]::Now.AddDays(-1)} -MaxEvents 2000 -ErrorAction Stop | Where-Object { $_.ProviderName -match 'avast|asw' } | Select-Object -First 30 TimeCreated,ProviderName,Id,LevelDisplayName,Message) })
    log_matches=(Observe 'Bounded Avast report/log tail matches for ComputeLab or block/quarantine events' {
        $matches = @()
        foreach ($dir in @('C:\ProgramData\Avast Software\Avast\log','C:\ProgramData\Avast Software\Avast\report')) {
            if (Test-Path -LiteralPath $dir) {
                $files = Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in @('.log','.txt') -and $_.LastWriteTime -ge [DateTime]::Now.AddDays(-1) } | Sort-Object LastWriteTime -Descending | Select-Object -First 8
                foreach ($file in $files) {
                    $hits = @(Get-Content -LiteralPath $file.FullName -Tail 250 -ErrorAction Stop | Select-String -Pattern 'ComputeLab|8f1565f|blocked|quarant|99a2369e')
                    $matches += @{path=$file.FullName;last_write=$file.LastWriteTimeUtc.ToString('o');tail_lines_scanned=250;matches=@($hits | ForEach-Object Line)}
                }
            }
        }
        return $matches
    })
}
$taskHost.power = [ordered]@{
    active_scheme=(Observe 'powercfg /getactivescheme' { Native 'powercfg' @('/getactivescheme') })
    sleep_settings=(Observe 'powercfg /query SCHEME_CURRENT SUB_SLEEP' { Native 'powercfg' @('/query','SCHEME_CURRENT','SUB_SLEEP') })
    available_sleep=(Observe 'powercfg /a' { Native 'powercfg' @('/a') })
    requests=(Observe 'powercfg /requests' { Native 'powercfg' @('/requests') })
    battery=(Observe 'Win32_Battery' { @(Get-CimInstance Win32_Battery | Select-Object BatteryStatus,EstimatedChargeRemaining,EstimatedRunTime) })
    pending_reboot=(Observe 'Read pending reboot flags' { @{cbs=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending');windows_update=(Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired');pending_rename=(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue).PendingFileRenameOperations} })
    update_tasks=(Observe 'UpdateOrchestrator reboot task state' { @(Get-ScheduledTask | Where-Object { $_.TaskPath -match 'UpdateOrchestrator' -and $_.TaskName -match 'Reboot' } | Select-Object TaskName,TaskPath,State,Settings) })
    update_policy=(Observe 'Windows Update configured policy' { Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU' -ErrorAction Stop })
    sessions=(Observe 'query user' { Native 'quser' @() })
    disconnect_policy=(Observe 'Terminal Services disconnect/logoff policy' { if (Test-Path 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services') { Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services' } else { @{configured_policy_present=$false;AnyDesk_behavior='UNKNOWN'} } })
}
$taskToolPaths = @('C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin\nvcc.exe','C:\VulkanSDK\1.4.363.0\Bin\glslc.exe','C:\VulkanSDK\1.4.363.0\Bin\spirv-val.exe','C:\Program Files\CMake\bin\cmake.exe','C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe','C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\HostX64\x64\cl.exe','C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe','C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe','C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\vcpkg.exe','D:\vcpkg\vcpkg.exe','C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\lib\x64\nvml.lib')
$taskHost.tool_files = @(foreach ($path in $taskToolPaths) { Observe "Read tool file identity $path (never execute)" { $file = Get-Item -LiteralPath $path; @{path=$file.FullName;bytes=$file.Length;last_write_utc=$file.LastWriteTimeUtc.ToString('o');file_version=$file.VersionInfo.FileVersion;product_version=$file.VersionInfo.ProductVersion;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()} } })
$taskArtifactPaths = @('out/build/x64-release/src/app/ComputeLabEx2Stage6.exe','out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe','out/build/x64-release/src/vulkan/Ex2A1.comp.spv','out/build/x64-release/src/vulkan/Ex2A2.comp.spv','out/build/x64-release/src/vulkan/Ex2B1.comp.spv','out/build/x64-release/src/vulkan/Ex2B2.comp.spv','out/build/x64-release/src/vulkan/Ex2C.comp.spv','out/build/x64-release/src/vulkan/Ex2D1.comp.spv','results/local/ex2-s7-3060ti-e1-8f1565f-stage6-manifest.json')
$taskHashes = @(foreach ($path in $taskArtifactPaths) { $file=Get-Item -LiteralPath $path; @{path=$file.FullName;bytes=$file.Length;attributes=$file.Attributes.ToString();sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
WriteNew 'powershell-hash-crosscheck.json' $taskHashes
WriteNew 'final-host-state.json' $taskHost
WriteNew 'host-query-receipt.json' $taskCommands
"HOST_OBSERVATION_RECORDED production_processes=$($taskHost.production_processes.Count)"
if ($taskHost.production_processes.Count -gt 0) { exit 2 }
