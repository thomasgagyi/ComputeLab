$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path "$PSScriptRoot\.."

Push-Location $repoRoot

try
{
    $commit = git rev-parse --verify 'HEAD^{commit}'

    if ($LASTEXITCODE -ne 0 -or -not $commit)
    {
        throw 'Curated evidence requires a committed HEAD revision.'
    }

    $workingTreeState = @(git status --porcelain=v1 --untracked-files=all)

    if ($LASTEXITCODE -ne 0)
    {
        throw "Unable to inspect the Git working tree (exit code $LASTEXITCODE)."
    }

    if ($workingTreeState.Count -ne 0)
    {
        throw (
            'Curated evidence requires a clean committed working tree. ' +
            'Keep evidence from dirty or untracked source state under results/local/.'
        )
    }

    Write-Output "Curated evidence source state: $commit"
}
finally
{
    Pop-Location
}
