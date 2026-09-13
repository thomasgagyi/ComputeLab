$ErrorActionPreference = 'Stop'

& "$PSScriptRoot\build.ps1"

Push-Location "$PSScriptRoot\.."

try
{
    $testInventoryJson = ctest `
        --preset x64-debug `
        --show-only=json-v1

    if ($LASTEXITCODE -ne 0)
    {
        throw "CTest inventory failed with exit code $LASTEXITCODE."
    }

    $testInventory = $testInventoryJson | ConvertFrom-Json
    $actualTestNames = @($testInventory.tests | ForEach-Object { $_.name })
    $requiredTestNames = @(
        'ComputeLabBootstrap.GoogleTestIsAvailable'
        'ComputeLabSmoke.CUDA'
        'ComputeLabSmoke.Vulkan'
        'ComputeLabSmoke.GpuIdentity'
    )
    $missingTestNames = @(
        $requiredTestNames | Where-Object { $_ -notin $actualTestNames }
    )

    if ($missingTestNames.Count -ne 0)
    {
        throw (
            'Required tests are missing: ' +
            ($missingTestNames -join ', ')
        )
    }

    ctest --preset x64-debug --output-on-failure

    if ($LASTEXITCODE -ne 0)
    {
        throw "CTest failed with exit code $LASTEXITCODE."
    }
}
finally
{
    Pop-Location
}
