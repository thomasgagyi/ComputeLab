param(
    [Parameter(Mandatory = $true)]
    [string]$CudaExecutable,

    [Parameter(Mandatory = $true)]
    [string]$VulkanExecutable
)

$ErrorActionPreference = 'Stop'

function Invoke-SmokeExecutable
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Backend
    )

    $output = & $Path 2>&1

    if ($LASTEXITCODE -ne 0)
    {
        $output | ForEach-Object { Write-Error $_ }
        throw "$Backend smoke executable failed with exit code $LASTEXITCODE."
    }

    return @($output)
}

$cudaOutput = Invoke-SmokeExecutable `
    -Path $CudaExecutable `
    -Backend 'CUDA'
$vulkanOutput = Invoke-SmokeExecutable `
    -Path $VulkanExecutable `
    -Backend 'Vulkan'

$cudaUuids = @(
    $cudaOutput |
        Select-String -Pattern '^COMPUTELAB_DEVICE_UUID=([0-9a-f]{32})$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
)
$vulkanUuids = @(
    $vulkanOutput |
        Select-String -Pattern '^COMPUTELAB_DEVICE_UUID=([0-9a-f]{32})$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value }
)

if ($cudaUuids.Count -ne 1)
{
    throw 'CUDA smoke output did not identify exactly one device UUID.'
}

if ($vulkanUuids.Count -eq 0)
{
    throw 'Vulkan smoke output did not identify a qualified device UUID.'
}

if ($cudaUuids[0] -notin $vulkanUuids)
{
    throw 'CUDA and Vulkan smoke checks did not identify the same physical GPU.'
}

Write-Output 'CUDA/Vulkan physical GPU identity: OK'
