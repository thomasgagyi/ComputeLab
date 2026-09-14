function Get-Ex1BuildProvenancePaths
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $RepoRoot,

        [Parameter(Mandatory)]
        [ValidateSet('x64-debug', 'x64-release')]
        [string] $Preset
    )

    $buildRelative = "out/build/$Preset"
    [pscustomobject]@{
        Manifest = Join-Path $RepoRoot "$buildRelative/curated-ex1-provenance.json"
        Executable = Join-Path $RepoRoot "$buildRelative/src/app/ComputeLabEx1.exe"
        ExecutableIdentity = "$buildRelative/src/app/ComputeLabEx1.exe"
        Spirv = Join-Path $RepoRoot "$buildRelative/src/vulkan/Ex1Transform.comp.spv"
        SpirvIdentity = "$buildRelative/src/vulkan/Ex1Transform.comp.spv"
    }
}

function Get-Ex1Sha256
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $LiteralPath
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf))
    {
        throw "Required curated EX-1 build artifact is missing: '$LiteralPath'."
    }
    (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Ex1BuildProvenance
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $RepoRoot,

        [Parameter(Mandatory)]
        [ValidateSet('x64-debug', 'x64-release')]
        [string] $Preset,

        [Parameter(Mandatory)]
        [string] $GitCommit
    )

    $paths = Get-Ex1BuildProvenancePaths -RepoRoot $RepoRoot -Preset $Preset
    $executableHash = Get-Ex1Sha256 -LiteralPath $paths.Executable
    $spirvRecord = $null
    if (Test-Path -LiteralPath $paths.Spirv -PathType Leaf)
    {
        $spirvRecord = [ordered]@{
            path = $paths.SpirvIdentity
            sha256 = Get-Ex1Sha256 -LiteralPath $paths.Spirv
        }
    }

    $manifest = [ordered]@{
        schema_version = 1
        git_commit = $GitCommit
        preset = $Preset
        executable = [ordered]@{
            path = $paths.ExecutableIdentity
            sha256 = $executableHash
        }
        spirv = $spirvRecord
    }

    $temporaryManifest = "$($paths.Manifest).tmp-$PID-$([Guid]::NewGuid().ToString('N'))"
    try
    {
        $manifest | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath $temporaryManifest -Encoding utf8NoBOM
        Move-Item -LiteralPath $temporaryManifest -Destination $paths.Manifest
    }
    finally
    {
        if (Test-Path -LiteralPath $temporaryManifest)
        {
            Remove-Item -LiteralPath $temporaryManifest -Force
        }
    }

    $paths.Manifest
}

function Assert-Ex1BuildProvenance
{
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string] $RepoRoot,

        [Parameter(Mandatory)]
        [ValidateSet('x64-debug', 'x64-release')]
        [string] $Preset,

        [Parameter(Mandatory)]
        [ValidateSet('cpu', 'cuda', 'vulkan')]
        [string] $Backend,

        [Parameter(Mandatory)]
        [string] $GitCommit
    )

    $paths = Get-Ex1BuildProvenancePaths -RepoRoot $RepoRoot -Preset $Preset
    if (-not (Test-Path -LiteralPath $paths.Manifest -PathType Leaf))
    {
        throw (
            "Curated EX-1 launch requires build provenance for preset '$Preset'. " +
            "Run scripts/prepare-curated-ex1.ps1 -Preset $Preset from a clean committed tree."
        )
    }

    try
    {
        $manifest = Get-Content -Raw -LiteralPath $paths.Manifest | ConvertFrom-Json
    }
    catch
    {
        throw "Curated EX-1 build provenance manifest is malformed: $($_.Exception.Message)"
    }

    if ($manifest.schema_version -ne 1)
    {
        throw 'Curated EX-1 build provenance schema_version is unsupported.'
    }
    if ($manifest.git_commit -cne $GitCommit)
    {
        throw 'Curated EX-1 build provenance Git commit does not match current HEAD.'
    }
    if ($manifest.preset -cne $Preset)
    {
        throw 'Curated EX-1 build provenance preset does not match the requested preset.'
    }
    if ($manifest.executable.path -cne $paths.ExecutableIdentity)
    {
        throw 'Curated EX-1 executable identity does not match the requested preset.'
    }

    $actualExecutableHash = Get-Ex1Sha256 -LiteralPath $paths.Executable
    if ($manifest.executable.sha256 -cne $actualExecutableHash)
    {
        throw 'Curated EX-1 executable SHA-256 does not match build provenance.'
    }

    if ($Backend -eq 'vulkan')
    {
        if ($null -eq $manifest.spirv)
        {
            throw 'Curated Vulkan EX-1 launch requires SPIR-V build provenance.'
        }
        if ($manifest.spirv.path -cne $paths.SpirvIdentity)
        {
            throw 'Curated EX-1 SPIR-V identity does not match the requested preset.'
        }
        $actualSpirvHash = Get-Ex1Sha256 -LiteralPath $paths.Spirv
        if ($manifest.spirv.sha256 -cne $actualSpirvHash)
        {
            throw 'Curated EX-1 SPIR-V SHA-256 does not match build provenance.'
        }
    }
}
