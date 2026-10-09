$ErrorActionPreference='Stop'
$env:GIT_OPTIONAL_LOCKS='0'
$root=(Get-Location).Path;$id='ex2-s7-3060ti-e2-8f1565f';$source='8f1565f7fb92e2184f4d2382aabf8fa7d00e1274'
function NewBytes([string]$path,[byte[]]$bytes){$f=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$f.Write($bytes,0,$bytes.Length);$f.Flush($true)}finally{$f.Dispose()}}
function Canon($v){
 if($null -eq $v){return 'null'}
 if($v -is [bool]){return $v.ToString().ToLowerInvariant()}
 if($v -is [string]){if($v -notmatch '^[\x20-\x21\x23-\x5b\x5d-\x7e]*$'){throw 'Unexpected canonical string'};return '"'+$v+'"'}
 if($v -is [Collections.IDictionary]){[string[]]$keys=@($v.Keys);[Array]::Sort($keys,[StringComparer]::Ordinal);$parts=foreach($key in $keys){(Canon $key)+':'+(Canon $v[$key])};return '{'+($parts -join ',')+'}'}
 if($v -is [Collections.IEnumerable]){$parts=foreach($item in $v){Canon $item};return '['+($parts -join ',')+']'}
 if($v -is [int] -or $v -is [long]){return $v.ToString([Globalization.CultureInfo]::InvariantCulture)}
 throw 'Unsupported canonical type'
}
function Digest([byte[]]$b){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b)).ToLowerInvariant()}
$utf8=[Text.UTF8Encoding]::new($false)
if($root -ne 'C:\Users\rolan\src\ComputeLab-Stage7-3060Ti' -or (git rev-parse HEAD) -ne $source -or (git branch --show-current) -or @(git status --porcelain=v1 --untracked-files=all).Count){throw 'Source gate'}
$probe=Get-Content "$PSScriptRoot/probe-result.json" -Raw | ConvertFrom-Json
if(-not $probe.pass -or -not $probe.obs_absent -or -not $probe.identity_pass){throw 'Probe gate'}
$observed=Get-Content "$PSScriptRoot/observed-frozen-artifacts.json" -Raw | ConvertFrom-Json -AsHashtable
$artifacts=[ordered]@{}
foreach($key in @('child','supervisor','a1','a2','b1','b2','c','d1')){$p=Join-Path $root $observed[$key].path;$file=Get-Item -LiteralPath $p;if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Artifact reparse'};$row=[ordered]@{path=$observed[$key].path;size_bytes=[long]$file.Length;sha256=(Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant()};if($row.size_bytes -ne $observed[$key].size_bytes -or $row.sha256 -ne $observed[$key].sha256){throw 'Artifact drift'};$artifacts[$key]=$row}
$absent=@(foreach($slot in 0..219){foreach($suffix in @('','.incomplete','.failure.json')){'results/local/{0}-slot-{1:D3}{2}' -f $id,$slot,$suffix}})
$absent+=@(foreach($suffix in @('','.incomplete','.incomplete.tmp')){"results/local/$id-stage6-control.json$suffix"})
$absent+=@("results/local/$id-stage6-analysis","results/tmp/$id-execution")
if($absent.Count -ne 665 -or @($absent | Sort-Object -Unique).Count -ne 665){throw 'Namespace count'}
foreach($p in $absent+@("results/local/$id-stage6-manifest.json","results/tmp/$id-prep")){if(Test-Path -LiteralPath $p){throw "E2 collision $p"}}
$freeze=[ordered]@{record_type='ex2-stage7-e2-artifact-freeze';record_version=1;manifest_id=$id;machine_id='ex2-s7-3060ti-machine';source_revision=$source;git_clean=$true;detached_head=$true;protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H';build_type='Release';build_preset='x64-release';artifacts=$artifacts;cuda_device_ordinal=0;vulkan_physical_device_index=0;expected_gpu_uuid='99a2369e-ca50-aac6-5c2c-fcaa44625084';release_instrument_rebuilt=$false;tests_rerun=$false;e1_permanently_incomplete=$true;e1_control_publication_failure_exit=4;e1_authoritative_revision=20;e1_distinct_temporary_revision=21;created_utc=[DateTime]::UtcNow.ToString('o')}
NewBytes "$PSScriptRoot/e2-artifact-freeze.json" ($utf8.GetBytes((Canon $freeze)+"`n"))
NewBytes "$PSScriptRoot/e2-initial-namespace-proof.json" ($utf8.GetBytes((Canon ([ordered]@{id=$id;count=665;paths=$absent;all_absent=$true;final_manifest_absent=$true}))+"`n"))
$shader=[ordered]@{};foreach($key in @('a1','a2','b1','b2','c','d1')){$shader[$key]=$artifacts[$key].sha256}
$groups=@(foreach($cell in 0..21){$children=@(foreach($p in 0..9){$n=$cell*10+$p;[ordered]@{sequence_index=$n;session_id=('{0}-slot-{1:D3}' -f $id,$n)}});[ordered]@{cell_index=$cell;declared_child_count=10;children=$children}})
$manifest=[ordered]@{manifest_version=1;manifest_type='ex2-stage6-diagnostic';manifest_id=$id;protocol_version='1.4';evidence_schema_version=2;evidence_kind='diagnostic';instrument_mode='H';machine_id='ex2-s7-3060ti-machine';expected_source_revision=$source;expected_git_dirty=$false;child_executable_path=$artifacts.child.path;expected_child_executable_sha256=$artifacts.child.sha256;expected_supervisor_executable_sha256=$artifacts.supervisor.sha256;expected_vulkan_shader_sha256=$shader;expected_gpu_uuid='99a2369e-ca50-aac6-5c2c-fcaa44625084';cuda_device_ordinal=0;vulkan_physical_device_index=0;operation_timeout_ms=60000;child_timeout_ms=1200000;campaign_timeout_ms=86400000;continuation_policy='resolved-only-no-retry';declared_cell_group_count=22;declared_child_count=220;groups=$groups}
$manifest['manifest_sha256']=Digest ($utf8.GetBytes((Canon $manifest)))
$candidateDir=Join-Path $root "results/tmp/$id-prep";New-Item -ItemType Directory -Path $candidateDir -ErrorAction Stop | Out-Null
$bytes=$utf8.GetBytes((Canon $manifest)+"`n");NewBytes "$candidateDir/candidate-manifest.json" $bytes
"E2_CANDIDATE semantic=$($manifest.manifest_sha256) physical=$(Digest $bytes)"
