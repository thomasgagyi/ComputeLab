$ErrorActionPreference = 'Stop'

$vsShell = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1'

& $vsShell `
    -Arch amd64 `
    -HostArch amd64 `
    -SkipAutomaticLocation

$env:VCPKG_ROOT = 'D:\Tools\vcpkg'
$env:PATH = "$env:VCPKG_ROOT;$env:PATH"

Push-Location "$PSScriptRoot\.."

try
{
    cmake --preset x64-debug
    cmake --build --preset x64-debug
}
finally
{
    Pop-Location
}