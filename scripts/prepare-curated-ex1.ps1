[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('x64-debug', 'x64-release')]
    [string] $Preset
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
. "$PSScriptRoot\ex1-build-provenance.ps1"

Push-Location $repoRoot
try
{
    & "$PSScriptRoot\assert-curated-evidence-ready.ps1"

    $paths = Get-Ex1BuildProvenancePaths -RepoRoot $repoRoot -Preset $Preset
    if (Test-Path -LiteralPath $paths.Manifest)
    {
        Remove-Item -LiteralPath $paths.Manifest -Force
    }

    & "$PSScriptRoot\build.ps1" -Preset $Preset -Clean

    # Fail closed if source changed while the clean build was running.
    & "$PSScriptRoot\assert-curated-evidence-ready.ps1"
    $gitCommit = git rev-parse --verify 'HEAD^{commit}'
    if ($LASTEXITCODE -ne 0 -or -not $gitCommit)
    {
        throw 'Unable to resolve the curated EX-1 build commit.'
    }

    $manifestPath = Write-Ex1BuildProvenance `
        -RepoRoot $repoRoot `
        -Preset $Preset `
        -GitCommit $gitCommit
    Write-Output "Curated EX-1 build provenance: $manifestPath"
}
finally
{
    Pop-Location
}
