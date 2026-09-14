[CmdletBinding()]
param(
    [ValidateSet('x64-debug', 'x64-release')]
    [string] $Preset = 'x64-debug',

    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path "$PSScriptRoot\.."

if (-not $env:VCPKG_ROOT)
{
    throw 'VCPKG_ROOT is not set. Point it to the standalone vcpkg installation.'
}

$vcpkg = Join-Path $env:VCPKG_ROOT 'vcpkg.exe'

if (-not (Test-Path $vcpkg))
{
    throw "vcpkg was not found at '$vcpkg'."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path $vswhere))
{
    throw "vswhere.exe was not found at '$vswhere'."
}

$vsInstall = & $vswhere `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if ($LASTEXITCODE -ne 0)
{
    throw "vswhere failed with exit code $LASTEXITCODE."
}

if (-not $vsInstall)
{
    throw 'No compatible Visual Studio installation was found.'
}

$vsShell = Join-Path $vsInstall `
    'Common7\Tools\Launch-VsDevShell.ps1'

& $vsShell `
    -Arch amd64 `
    -HostArch amd64 `
    -SkipAutomaticLocation

$env:PATH = "$env:VCPKG_ROOT;$env:PATH"

Push-Location $repoRoot

try
{
    cmake --preset $Preset

    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    $buildArguments = @('--build', '--preset', $Preset)
    if ($Clean)
    {
        $buildArguments += '--clean-first'
    }

    cmake @buildArguments

    if ($LASTEXITCODE -ne 0)
    {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }
}
finally
{
    Pop-Location
}
