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
    # The source tree must begin clean.
    & "$PSScriptRoot\assert-curated-evidence-ready.ps1"

    # Capture the exact revision that is about to be built.
    $gitCommitBeforeBuild = git rev-parse --verify 'HEAD^{commit}'
    if ($LASTEXITCODE -ne 0 -or -not $gitCommitBeforeBuild)
    {
        throw 'Unable to resolve the curated EX-1 source commit before build.'
    }

    $gitCommitBeforeBuild = $gitCommitBeforeBuild.Trim()

    $paths = Get-Ex1BuildProvenancePaths `
        -RepoRoot $repoRoot `
        -Preset $Preset

    if (Test-Path -LiteralPath $paths.Manifest)
    {
        Remove-Item -LiteralPath $paths.Manifest -Force
    }

    & "$PSScriptRoot\build.ps1" -Preset $Preset -Clean

    # The source tree must still be clean after the build.
    & "$PSScriptRoot\assert-curated-evidence-ready.ps1"

    # HEAD must still be the exact revision we started building.
    $gitCommitAfterBuild = git rev-parse --verify 'HEAD^{commit}'
    if ($LASTEXITCODE -ne 0 -or -not $gitCommitAfterBuild)
    {
        throw 'Unable to resolve the curated EX-1 source commit after build.'
    }

    $gitCommitAfterBuild = $gitCommitAfterBuild.Trim()

    if ($gitCommitAfterBuild -cne $gitCommitBeforeBuild)
    {
        throw (
            'Git HEAD changed while the curated EX-1 build was running. ' +
            'The build provenance is invalid; rerun preparation from a stable clean tree.'
        )
    }

    # Stamp the artifacts with the revision captured BEFORE building.
    $manifestPath = Write-Ex1BuildProvenance `
        -RepoRoot $repoRoot `
        -Preset $Preset `
        -GitCommit $gitCommitBeforeBuild

    Write-Output "Curated EX-1 build provenance: $manifestPath"
}
finally
{
    Pop-Location
}