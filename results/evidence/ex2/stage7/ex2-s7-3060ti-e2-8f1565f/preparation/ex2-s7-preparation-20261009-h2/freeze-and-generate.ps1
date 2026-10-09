$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskRoot = (Get-Location).Path
$taskId = 'ex2-s7-3060ti-e1-8f1565f'
$taskMachine = 'ex2-s7-3060ti-machine'
$taskSource = '8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
$taskCandidateDir = Join-Path $taskRoot "results/tmp/$taskId-prep"
function NewBytes([string]$path, [byte[]]$bytes) {
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
}
function Canon($value) {
    if ($null -eq $value) { return 'null' }
    if ($value -is [bool]) { return $value.ToString().ToLowerInvariant() }
    if ($value -is [string]) {
        if ($value -notmatch '^[\x20-\x21\x23-\x5b\x5d-\x7e]*$') { throw 'Unexpected canonical string' }
        return '"' + $value + '"'
    }
    if ($value -is [Collections.IDictionary]) {
        [string[]]$keys = @($value.Keys); [Array]::Sort($keys, [StringComparer]::Ordinal)
        $parts = foreach ($key in $keys) { (Canon $key) + ':' + (Canon $value[$key]) }
        return '{' + ($parts -join ',') + '}'
    }
    if ($value -is [Collections.IEnumerable]) {
        $parts = foreach ($item in $value) { Canon $item }
        return '[' + ($parts -join ',') + ']'
    }
    if ($value -is [int] -or $value -is [long]) { return $value.ToString([Globalization.CultureInfo]::InvariantCulture) }
    throw "Unsupported canonical type: $($value.GetType())"
}
function Digest([byte[]]$bytes) { return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() }
$taskUtf8 = [Text.UTF8Encoding]::new($false)
if ($taskRoot -ne 'C:\Users\rolan\src\ComputeLab-Stage7-3060Ti' -or (git rev-parse HEAD) -ne $taskSource -or (git branch --show-current) -or @(git status --porcelain=v1 --untracked-files=all).Count) { throw 'Source gate failed' }
foreach ($name in @('debug-build','debug-smoke','release-build','direct-smoke','supervisor-smoke')) {
    if ((Get-Content "$PSScriptRoot/$name-exit.txt" -Raw).Trim() -ne '0') { throw "Failed prior gate $name" }
}
foreach ($name in @('direct','supervisor')) {
    $suite = Get-Content "$PSScriptRoot/$name-smoke.json" -Raw | ConvertFrom-Json
    if ($suite.tests -ne 4 -or $suite.failures -ne 0 -or $suite.disabled -ne 0 -or $suite.errors -ne 0) { throw 'Smoke count/status failed' }
}
if (@(Get-ChildItem results/local -Force | Where-Object Name -Like 's6-i*-smoke-*').Count) { throw 'Disposable fixture cleanup incomplete' }
foreach ($pair in @(@('out/build/x64-debug/Testing/Temporary/LastTest.log','debug-last-test.log'),@('out/build/x64-debug/.ninja_log','debug-ninja.log'),@('out/build/x64-debug/CMakeFiles/CMakeConfigureLog.yaml','debug-configure.yaml'),@('out/build/x64-release/build.ninja','release-build.ninja'),@('out/build/x64-release/CMakeCache.txt','release-cache.txt'),@('out/build/x64-release/.ninja_log','release-ninja.log'),@('out/build/x64-release/CMakeFiles/CMakeConfigureLog.yaml','release-configure.yaml'))) {
    NewBytes (Join-Path $PSScriptRoot $pair[1]) ([IO.File]::ReadAllBytes((Join-Path $taskRoot $pair[0])))
}
$taskGpu = Get-Content "$PSScriptRoot/gpu-freeze.json" -Raw | ConvertFrom-Json
if ($taskGpu.cuda_uuid -ne '99a2369e-ca50-aac6-5c2c-fcaa44625084' -or $taskGpu.vulkan_uuid -ne $taskGpu.cuda_uuid -or $taskGpu.cuda_device_ordinal -ne 0 -or $taskGpu.vulkan_physical_device_index -ne 0) { throw 'GPU gate failed' }
$taskPaths = [ordered]@{
    child = 'out/build/x64-release/src/app/ComputeLabEx2Stage6.exe'
    supervisor = 'out/build/x64-release/src/app/ComputeLabEx2Stage6Supervisor.exe'
    a1 = 'out/build/x64-release/src/vulkan/Ex2A1.comp.spv'
    a2 = 'out/build/x64-release/src/vulkan/Ex2A2.comp.spv'
    b1 = 'out/build/x64-release/src/vulkan/Ex2B1.comp.spv'
    b2 = 'out/build/x64-release/src/vulkan/Ex2B2.comp.spv'
    c = 'out/build/x64-release/src/vulkan/Ex2C.comp.spv'
    d1 = 'out/build/x64-release/src/vulkan/Ex2D1.comp.spv'
}
$taskArtifacts = [ordered]@{}
foreach ($key in $taskPaths.Keys) {
    $path = Join-Path $taskRoot $taskPaths[$key]; $file = Get-Item -LiteralPath $path
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Artifact topology failed' }
    $taskArtifacts[$key] = [ordered]@{path=$taskPaths[$key];size_bytes=[long]$file.Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
}
$taskAbsent = @(foreach ($slot in 0..219) { foreach ($suffix in @('','.incomplete','.failure.json')) { 'results/local/{0}-slot-{1:D3}{2}' -f $taskId,$slot,$suffix } })
$taskAbsent += @(foreach ($suffix in @('','.incomplete','.incomplete.tmp')) { "results/local/$taskId-stage6-control.json$suffix" })
$taskAbsent += @("results/local/$taskId-stage6-analysis", "results/tmp/$taskId-execution")
if ($taskAbsent.Count -ne 665 -or @($taskAbsent | Sort-Object -Unique).Count -ne 665) { throw 'Namespace count failed' }
foreach ($path in $taskAbsent + @("results/local/$taskId-stage6-manifest.json",$taskCandidateDir)) { if (Test-Path -LiteralPath $path) { throw "Namespace collision: $path" } }
$taskFreeze = [ordered]@{
    record_version=1;record_type='ex2-stage7-deployment-artifact-freeze';source_revision=$taskSource;git_clean=$true;detached_head=$true
    build_preset='x64-release';build_type='Release';machine_id=$taskMachine;manifest_id=$taskId
    protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H'
    cuda_device_ordinal=0;vulkan_physical_device_index=0;cuda_uuid=$taskGpu.cuda_uuid;vulkan_uuid=$taskGpu.vulkan_uuid
    artifacts=$taskArtifacts
    provenance=[ordered]@{os='Windows 10 Enterprise 10.0.19045';cpu='AMD Ryzen 7 5700X';driver='616.92';visual_studio='2026 18.10.3';msvc='19.51.36260';windows_sdk='10.0.26100.0';cuda='13.4.92';vulkan_sdk='1.4.363.0';cmake='4.4.3';ninja='1.13.2';vcpkg='2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8';vcpkg_root='C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg';cuda_target='native sm_86';inventory_sha256=(Get-FileHash "$PSScriptRoot/inventory.log").Hash.ToLowerInvariant()}
    debug_targeted_pass_count=3;release_direct_pass_count=4;release_supervisor_pass_count=4;canonical_debug_suite='INCOMPLETE; NOT RERUN; prior 924 PASS, one interrupted, 317 not started of 1242';debug_build_log_limitation='Transcript omitted native output; successful exit and retained configure/Ninja records, no rebuild'
}
NewBytes "$PSScriptRoot/artifact-freeze.json" ($taskUtf8.GetBytes((Canon $taskFreeze) + "`n"))
NewBytes "$PSScriptRoot/namespace-initial.json" ($taskUtf8.GetBytes((Canon ([ordered]@{manifest_id=$taskId;namespace_clear=$true;count=665;absent_paths=$taskAbsent;final_manifest_absent=$true})) + "`n"))
$taskShaders = [ordered]@{}; foreach ($key in @('a1','a2','b1','b2','c','d1')) { $taskShaders[$key] = $taskArtifacts[$key].sha256 }
$taskGroups = @(foreach ($cell in 0..21) {
    $children = @(foreach ($position in 0..9) { $sequence = $cell * 10 + $position; [ordered]@{sequence_index=$sequence;session_id=('{0}-slot-{1:D3}' -f $taskId,$sequence)} })
    [ordered]@{cell_index=$cell;declared_child_count=10;children=$children}
})
$taskManifest = [ordered]@{
    manifest_version=1;manifest_type='ex2-stage6-diagnostic';manifest_id=$taskId;protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H'
    machine_id=$taskMachine;expected_source_revision=$taskSource;expected_git_dirty=$false
    child_executable_path=$taskPaths.child;expected_child_executable_sha256=$taskArtifacts.child.sha256;expected_supervisor_executable_sha256=$taskArtifacts.supervisor.sha256;expected_vulkan_shader_sha256=$taskShaders
    expected_gpu_uuid=$taskGpu.cuda_uuid;cuda_device_ordinal=0;vulkan_physical_device_index=0
    operation_timeout_ms=60000;child_timeout_ms=1200000;campaign_timeout_ms=86400000;continuation_policy='resolved-only-no-retry';declared_cell_group_count=22;declared_child_count=220;groups=$taskGroups
}
$taskManifest['manifest_sha256'] = Digest ($taskUtf8.GetBytes((Canon $taskManifest)))
New-Item -ItemType Directory -Path $taskCandidateDir | Out-Null
NewBytes "$taskCandidateDir/candidate-manifest.json" ($taskUtf8.GetBytes((Canon $taskManifest) + "`n"))
"FROZEN: 8 artifacts; 665 destinations absent; candidate semantic SHA256=$($taskManifest.manifest_sha256)"
