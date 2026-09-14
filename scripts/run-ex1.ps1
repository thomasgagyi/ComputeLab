[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('initialization', 'series')]
    [string] $Mode,

    [Parameter(Mandatory)]
    [ValidateSet('cpu', 'cuda', 'vulkan')]
    [string] $Backend,

    [Parameter(Mandatory)]
    [UInt64] $ElementCount,

    [Parameter(Mandatory)]
    [UInt64] $Seed,

    [Parameter(Mandatory)]
    [string] $Variant,

    [Parameter(Mandatory)]
    [string] $MachineId,

    [Parameter(Mandatory)]
    [bool] $ValidationEnabled,

    [Parameter(Mandatory)]
    [bool] $DiagnosticInstrumentation,

    [Nullable[UInt64]] $WarmupCount,

    [Nullable[UInt64]] $PlannedSampleCount,

    [string] $RunId,

    [ValidateSet('x64-debug', 'x64-release')]
    [string] $Preset = 'x64-release',

    [switch] $CuratedSourceEligible
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path "$PSScriptRoot\.."
. "$PSScriptRoot\ex1-build-provenance.ps1"
$executable = Join-Path $repoRoot "out\build\$Preset\src\app\ComputeLabEx1.exe"

if (-not (Test-Path -LiteralPath $executable -PathType Leaf))
{
    throw (
        "The A8 executable was not found at '$executable'. " +
        'Build the selected preset separately before measurement.'
    )
}

if ($Mode -eq 'initialization' -and $Backend -eq 'cpu')
{
    throw 'EX-1 initialization mode applies only to CUDA or Vulkan.'
}

if ($Mode -eq 'series')
{
    if ($null -eq $WarmupCount -or $null -eq $PlannedSampleCount)
    {
        throw 'Series mode requires -WarmupCount and -PlannedSampleCount.'
    }
}
elseif ($null -ne $WarmupCount -or $null -ne $PlannedSampleCount)
{
    throw 'Initialization mode does not accept warm-up or sample-count parameters.'
}

if (-not $RunId)
{
    $utcForId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')
    $RunId = "ex1-$utcForId-$([Guid]::NewGuid().ToString('N'))"
}

$timestampUtc = [DateTime]::UtcNow.ToString('o')

Push-Location $repoRoot
try
{
    $gitCommit = git rev-parse --verify 'HEAD^{commit}'
    if ($LASTEXITCODE -ne 0 -or -not $gitCommit)
    {
        throw 'EX-1 launch requires a committed HEAD revision.'
    }

    $workingTreeState = @(git status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0)
    {
        throw "Unable to inspect the Git working tree (exit code $LASTEXITCODE)."
    }
    $gitDirty = $workingTreeState.Count -ne 0

    $runnerArguments = @(
        '--mode', $Mode,
        '--backend', $Backend,
        '--element-count', $ElementCount.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--seed', $Seed.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--variant', $Variant,
        '--run-id', $RunId,
        '--timestamp-utc', $timestampUtc,
        '--git-commit', $gitCommit,
        '--git-dirty', $gitDirty.ToString().ToLowerInvariant(),
        '--machine-id', $MachineId,
        '--validation-enabled', $ValidationEnabled.ToString().ToLowerInvariant(),
        '--diagnostic-instrumentation', $DiagnosticInstrumentation.ToString().ToLowerInvariant()
    )

    if ($Mode -eq 'series')
    {
        $runnerArguments += @(
            '--warmup-count', $WarmupCount.ToString([Globalization.CultureInfo]::InvariantCulture),
            '--planned-sample-count', $PlannedSampleCount.ToString([Globalization.CultureInfo]::InvariantCulture)
        )
    }

    if ($CuratedSourceEligible)
    {
        Assert-Ex1BuildProvenance `
            -RepoRoot $repoRoot `
            -Preset $Preset `
            -Backend $Backend `
            -GitCommit $gitCommit

        # This remains the final source-state operation before process launch.
        & "$PSScriptRoot\assert-curated-evidence-ready.ps1"
    }

    & $executable @runnerArguments
    $runnerExitCode = $LASTEXITCODE
    if ($runnerExitCode -ne 0)
    {
        throw "ComputeLabEx1 exited with code $runnerExitCode."
    }
}
finally
{
    Pop-Location
}
