$ErrorActionPreference = 'Stop'
Start-Transcript -Path "$PSScriptRoot/inventory.log" -NoClobber
try {
    Get-Location
    git rev-parse HEAD
    git status --porcelain=v1
    git worktree list --porcelain
    [System.IO.DriveInfo]::new('C:\') | Format-List Name,DriveFormat,AvailableFreeSpace,TotalSize
    Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,BuildNumber,TotalVisibleMemorySize,FreePhysicalMemory
    Get-CimInstance Win32_Processor | Format-List Name,NumberOfCores,NumberOfLogicalProcessors
    Get-Process | Where-Object { $_.ProcessName -match 'AnyDesk|ChatGPT|Codex|dwm|explorer' } | Select-Object ProcessName,Id | Format-Table
    nvidia-smi -q
    'NORMAL ENVIRONMENT'
    Get-Item Env:CUDA_PATH,Env:VULKAN_SDK,Env:VCPKG_ROOT
    Get-Command cl,nvcc,glslc,spirv-val,rc,mt,ninja,cmake,vcpkg -ErrorAction SilentlyContinue | Format-List Name,Source
    nvcc --version
    glslc --version
    spirv-val --version
    cmake --version
    vcpkg version
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $taskVsInstall = & $taskVswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    & (Join-Path $taskVsInstall 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $env:PATH = "$env:VCPKG_ROOT;$env:PATH"
    'ACTUAL BUILD DEVELOPER SHELL'
    Get-Item Env:CUDA_PATH,Env:VULKAN_SDK,Env:VCPKG_ROOT,Env:WindowsSDKVersion
    Get-Command cl,nvcc,glslc,spirv-val,rc,mt,ninja,cmake,vcpkg | Format-List Name,Source
    cl 2>&1 | Out-Host
    nvcc --version
    glslc --version
    spirv-val --version
    cmake --version
    ninja --version
    vcpkg version
    Get-Item "$env:CUDA_PATH/lib/x64/nvml.lib" | Format-List FullName,Length
    Get-ChildItem HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers,HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -ErrorAction SilentlyContinue
    Get-ItemProperty HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers,HKLM:\SOFTWARE\Khronos\Vulkan\ImplicitLayers -ErrorAction SilentlyContinue
} finally { Stop-Transcript }
