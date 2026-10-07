$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Get-Location).Path
$id = 'ex2-s6-e1b-8f1565f'
$source = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
$prep = Join-Path $repo "results/tmp/$id-prep"
function NewBytes([string]$path, [byte[]]$bytes) {
    $f = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $f.Write($bytes, 0, $bytes.Length); $f.Flush($true) } finally { $f.Dispose() }
}
function Canon($v) {
    if ($null -eq $v) { return 'null' }
    if ($v -is [bool]) { return $v.ToString().ToLowerInvariant() }
    if ($v -is [string]) {
        # All protocol keys/values are printable ASCII without JSON metacharacters.
        if ($v -notmatch '^[\x20-\x21\x23-\x5b\x5d-\x7e]*$') { throw 'Unexpected JSON string character' }
        return '"' + $v + '"'
    }
    if ($v -is [Collections.IDictionary]) {
        [string[]]$keys = @($v.Keys); [Array]::Sort($keys, [StringComparer]::Ordinal)
        $parts = foreach ($key in $keys) { (Canon $key) + ':' + (Canon $v[$key]) }
        return '{' + ($parts -join ',') + '}'
    }
    if ($v -is [Collections.IEnumerable]) {
        $parts = foreach ($item in $v) { Canon $item }
        return '[' + ($parts -join ',') + ']'
    }
    if ($v -is [int] -or $v -is [long]) { return $v.ToString([Globalization.CultureInfo]::InvariantCulture) }
    throw "Unsupported canonical value $($v.GetType())"
}
function HashBytes([byte[]]$bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
$utf8 = [Text.UTF8Encoding]::new($false)
if ((git rev-parse --verify HEAD) -ne $source -or (git branch --show-current) -ne 'main' -or @(git status --porcelain=v1 --untracked-files=all).Count -ne 0) { throw 'Source authority mismatch' }
if (-not (Test-Path -LiteralPath "$prep/logs/release-build-success.json")) { throw 'Release build success absent' }
$gpu = Get-Content -Raw -LiteralPath "$prep/logs/gpu-initial.json" | ConvertFrom-Json
if ($gpu.cuda_uuid -ne '0340eaac-dc67-f450-d558-d47c55cc4417' -or $gpu.vulkan_uuid -ne $gpu.cuda_uuid) { throw 'GPU mismatch' }
$definitions = [ordered]@{
    child = 'out/build/x64-release/src/app/ComputeLabEx2Stage6.exe'
    supervisor = 'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'
    a1 = 'out/build/x64-release/src/vulkan/Ex2A1.comp.spv'
    a2 = 'out/build/x64-release/src/vulkan/Ex2A2.comp.spv'
    b1 = 'out/build/x64-release/src/vulkan/Ex2B1.comp.spv'
    b2 = 'out/build/x64-release/src/vulkan/Ex2B2.comp.spv'
    c = 'out/build/x64-release/src/vulkan/Ex2C.comp.spv'
    d1 = 'out/build/x64-release/src/vulkan/Ex2D1.comp.spv'
}
$artifacts = [ordered]@{}
foreach ($key in $definitions.Keys) {
    $p = Join-Path $repo $definitions[$key]
    $file = Get-Item -LiteralPath $p
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Nonregular artifact' }
    $artifacts[$key] = [ordered]@{path=$definitions[$key];size_bytes=[long]$file.Length;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$freeze = [ordered]@{
    record_version=1;record_type='ex2-stage6-artifact-freeze';source_revision=$source;git_clean=$true
    build_preset='x64-release';build_type='Release';machine_id='ex2-s6-e1-machine'
    expected_gpu_uuid=$gpu.expected_gpu_uuid;cuda_device_ordinal=0;cuda_uuid=$gpu.cuda_uuid
    vulkan_physical_device_index=0;vulkan_uuid=$gpu.vulkan_uuid;artifacts=$artifacts
    protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H'
    release_build_pass=$true;tests_rerun=$false
}
NewBytes (Join-Path $repo "results/local/$id-stage6-artifact-freeze.json") ($utf8.GetBytes((Canon $freeze) + "`n"))
$shaderHashes = [ordered]@{}
foreach ($key in @('a1','a2','b1','b2','c','d1')) { $shaderHashes[$key] = $artifacts[$key].sha256 }
$groups = @(foreach ($cell in 0..21) {
    $children = @(foreach ($position in 0..9) {
        $sequence = $cell * 10 + $position
        [ordered]@{sequence_index=$sequence;session_id=('{0}-slot-{1:D3}' -f $id,$sequence)}
    })
    [ordered]@{cell_index=$cell;declared_child_count=10;children=$children}
})
$manifest = [ordered]@{
    manifest_version=1;manifest_type='ex2-stage6-diagnostic';manifest_id=$id
    protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H'
    machine_id='ex2-s6-e1-machine';expected_source_revision=$source;expected_git_dirty=$false
    child_executable_path=$definitions.child;expected_child_executable_sha256=$artifacts.child.sha256
    expected_supervisor_executable_sha256=$artifacts.supervisor.sha256;expected_vulkan_shader_sha256=$shaderHashes
    expected_gpu_uuid=$gpu.expected_gpu_uuid;cuda_device_ordinal=0;vulkan_physical_device_index=0
    operation_timeout_ms=60000;child_timeout_ms=1200000;campaign_timeout_ms=86400000
    continuation_policy='resolved-only-no-retry';declared_cell_group_count=22;declared_child_count=220;groups=$groups
}
$manifest['manifest_sha256'] = HashBytes ($utf8.GetBytes((Canon $manifest)))
NewBytes "$prep/candidate-manifest.json" ($utf8.GetBytes((Canon $manifest) + "`n"))
Write-Output "Artifact freeze published; candidate semantic SHA-256: $($manifest.manifest_sha256)"
