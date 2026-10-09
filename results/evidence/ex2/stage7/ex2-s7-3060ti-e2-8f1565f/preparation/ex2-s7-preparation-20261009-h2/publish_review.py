"""Create-new preparation report. Never launches the campaign."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path.cwd()
PREP = ROOT / 'results/local/ex2-s7-preparation-20261009-h2'
freeze = json.loads((PREP / 'artifact-freeze.json').read_text())
stable = json.loads((PREP / 'stability-verification.json').read_text())
assert stable['verification_pass'] is True
head = subprocess.run(['git','rev-parse','HEAD'], check=True, capture_output=True, text=True).stdout.strip()
status = subprocess.run(['git','status','--porcelain=v1','--untracked-files=all'], check=True, capture_output=True, text=True).stdout
assert head == freeze['source_revision'] and not status
manifest = ROOT / f"results/local/{stable['manifest_id']}-stage6-manifest.json"
assert hashlib.sha256(manifest.read_bytes()).hexdigest() == stable['physical_sha256']
assert all(not (ROOT / path).exists() for path in stable['absent_paths'])
for artifact in freeze['artifacts'].values():
    path = ROOT / artifact['path']
    assert path.stat().st_size == artifact['size_bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == artifact['sha256']
files = sorted([*PREP.iterdir(), ROOT / f"results/tmp/{stable['manifest_id']}-prep/candidate-manifest.json", manifest])
index = {str(path): {'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in files if path.is_file()}
summary = dict(verdict='INCOMPLETE', reason='Full native Debug build console output was not durably retained by the PowerShell transcript. No build or smoke retry was performed.', reported_utc=datetime.now(timezone.utc).isoformat(), source_revision=head, git_clean=True, campaign_executed=False, launch_authorized=False, debug_targeted_pass=3, direct_release_pass=4, supervised_release_pass=4, canonical_debug_suite='INCOMPLETE; NOT RERUN', candidate_verification='PASS', final_verification='PASS', stability_verification='PASS', semantic_sha256=stable['semantic_sha256'], physical_sha256=stable['physical_sha256'], retained_files=index)
with (PREP / 'preparation-index.json').open('x', encoding='utf-8', newline='\n') as out:
    json.dump(summary,out,sort_keys=True,indent=2)
    out.write('\n')
lines = [
    '# EX-2 Stage-7 RTX 3060 Ti pre-execution review',
    '',
    '**PRE-EXECUTION VERDICT: INCOMPLETE. This is not a campaign PASS.**',
    '',
    summary['reason'] + ' The Debug build did complete successfully with wrapper/native success; its invocation, developer-shell provenance, configure log, Ninja completion records and real exit status remain retained. All three targeted Debug smoke logs are complete. The explicitly required full Debug build console log cannot be reconstructed faithfully. This retention limitation requires independent human review; no extra build is authorized by this preparation.',
    '',
    '## Exact review tuple',
    '',
    f'- Actual execution worktree: `{ROOT}` (NTFS, plain directory and ancestors checked; no reparse alias).',
    f'- Source: `{head}`; detached HEAD; tracked/index/nonignored-untracked status empty immediately before report.',
    '- Git common directory: `C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git`; linked worktree relationship preserved.',
    '- Protocol/schema/kind/instrument: `1.4 / 2 / diagnostic / H`; manifest version `1`, parser type `ex2-stage6-diagnostic`.',
    f'- Machine ID: `{stable["machine_id"]}`.',
    f'- CUDA ordinal `0`, Vulkan physical-device index `0`; both independently captured API UUIDs: `{stable["gpu"]["cuda_uuid"]}`.',
    '- GPU name in both APIs: NVIDIA GeForce RTX 3060 Ti; CUDA compute capability 8.6.',
    f'- Manifest ID: `{stable["manifest_id"]}`.',
    f'- Semantic manifest SHA-256: `{stable["semantic_sha256"]}` (canonical payload excludes manifest_sha256, no final LF in semantic payload).',
    f'- Physical manifest-file SHA-256: `{stable["physical_sha256"]}` (exact canonical UTF-8 bytes, no BOM/CR, exactly one final LF).',
    f'- Final manifest: `{manifest}`.',
    '- Namespace proof: 665 unique absent production destinations = 660 child final/staging/sidecar paths, three control-ledger variants, one analysis destination, one execution-wrapper destination. Final manifest was also absent before exclusive publication. Each independent receipt retains the entire 665-path list.',
    '',
    '| Artifact | Actual absolute path | Bytes | SHA-256 |',
    '|---|---|---:|---|',
]
for key in ['child','supervisor','a1','a2','b1','b2','c','d1']:
    artifact = freeze['artifacts'][key]
    lines.append(f'| {key} | `{ROOT / artifact["path"]}` | {artifact["size_bytes"]} | `{artifact["sha256"]}` |')
lines += [
    '',
    '## Contract and scientific boundaries',
    '',
    'All 22 cells in source-approved order: A1 (256, 262144, 16777216 elements); A2 (262144, 16777216); B1 and B2 each (262144 structured-v1, 262144 shuffled-v1, 16777216 shuffled-v1); C (1048576 elements with 1048576, 32768, 64 active counters); D1 (262144 elements/16 iterations, 1048576/64); E1 host-to-device and E2 device-to-host each (1024, 1048576, 67108864 bytes). E3 adds no cell; D2 is excluded.',
    '',
    'Five paired fresh-process blocks CV/VC/CV/VC/CV per cell, ten children per cell, 220 total, 110 per backend, slot indices 0..219 and unique -slot-000..-slot-219 session names. Each successful child has 100 ordered H observations 0..99, zero warm-up: 22000 planned records. Native backend implementations and full CPU-oracle correctness remain unchanged. Retain raw failures and extreme timings. No extra warm-up, outlier deletion, adaptive count, timing stop, retry/resume. resolved-only-no-retry continuation; operation/child/campaign deadlines 60000/1200000/86400000 ms.',
    '',
    'Gate 0 remains FAIL for proposed D1/H comparative scope. Stage-2 candidate-performance execution remains NOT GRANTED; production backend remains UNSELECTED. No CUDA/Vulkan winner, cross-generation speedup/ranking, qualification or production-application inference is admitted.',
    '',
    '## Host and toolchain provenance',
    '',
    'Windows 10 Enterprise 10.0.19045; AMD Ryzen 7 5700X, 8 cores/16 threads. System memory at inventory: 33478696 KiB total visible, 24823684 KiB free. C: NTFS, 287323746304 bytes available of 499427872768. GPU API total memory 8589410304 bytes; NVML/nvidia-smi inventory 8192 MiB total, 7614 MiB free, 411 MiB used, 168 MiB reserved.',
    '',
    'Driver 616.92; WDDM; display attached No / active Disabled. Inventory observed P8, 30% fan, about 28 C and 14 W / 200 W in initial summary; 14–15% GPU utilization during inventory. Detailed current/default/max clocks, power, PCIe and GPU processes are retained in inventory.log. CUDA Driver API version 13040; Vulkan device API 1.4.351, driver version raw 2585198592, device ID 9353 / vendor 4318. Both APIs were enumerated and selected by matching the target UUID, then confirmed at ordinal/index 0.',
    '',
    'Visual Studio Community 2026 18.10.3 (18.10.12224.181); MSVC 19.51.36260, tool directory 14.51.36231; Windows SDK 10.0.26100.0 with rc/mt resolved in the x64 developer shell. CUDA nvcc 13.4.92; Vulkan SDK 1.4.363.0, glslc shaderc v2026.4 and spirv-val v2026.4; CMake 4.4.3; Ninja 1.13.2. NVML import lib at the CUDA 13.4 x64 lib directory, 118114 bytes.',
    '',
    'Normal environment: VCPKG_ROOT D:/vcpkg, version 2026-09-26-51bf87ca6e9bf3e622d84ff323bd202ab1ca0c0b. Actual script-established x64 developer shell: C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg, version 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8. This shell selection was retained. No root override, persistent PATH/registry/toolchain change was performed. GTest 1.18.0 and two vcpkg helper packages were restored from the host binary cache; GTest prints its cached original __FILE__ path in the archival checkout, which is dependency provenance, not evidence of building current project targets there. Actual EX-2 target compilation and artifact paths belong to this execution worktree.',
    '',
    'AnyDesk (three processes), ChatGPT (multiple processes), Codex, dwm and explorer were active. GPU clients included ChatGPT, explorer, Windows desktop applications and the Codex computer-use helper. No processes were terminated. Vulkan registry inventory includes SDK explicit layers plus OBS/Steam implicit-layer registrations. Identity capture explicitly enabled zero layers, but registration alone does not prove absence of runtime injection. External profiler/tracing/injection state, sleep/update/power policies and activity at a future launch require human review; they were not modified or declared fully controlled.',
    '',
    '## Exact builds and bounded smoke evidence',
    '',
    '`./scripts/build.ps1 -Preset x64-debug` — exactly once, exit 0. Inventory `ctest --preset x64-debug -N -R \'^ComputeLabSmoke\\.(CUDA|Vulkan|GpuIdentity)$\'` selected exactly 3. Execution `ctest --preset x64-debug -R \'^ComputeLabSmoke\\.(CUDA|Vulkan|GpuIdentity)$\' --output-on-failure` — exactly once, exit 0; CUDA, Vulkan and GpuIdentity all actually passed. LastTest records the physical RTX 3060 Ti and matching API UUID.',
    '',
    '**The 1242-test canonical Debug suite was NOT rerun and remains INCOMPLETE.** Prior operator-interrupted session: 924 PASS, one interrupted, 317 not started. The accepted waiver remains explicit; three targeted functional smokes do not retroactively complete canonical validation. No scripts/test.ps1, unfiltered CTest or Stage-6 supervisor simulation was run.',
    '',
    '`./scripts/build.ps1 -Preset x64-release` — exactly once, exit 0; real Release cache, actual bundled vcpkg toolchain, target -arch=native compilation and executable cuobjdump sm_86 cubins verified. Cache architecture default 75 was not mistaken for target architecture. All six EX-2 SPIR-V files and separate EX-1 shader generated; no seventh EX-2 cell. Build has existing C4834 warnings in test code; no source patches or rebuilds.',
    '',
    'Direct executable `out/build/x64-release/tests/ComputeLabEx2Stage6ChildSmokeTests.exe`, filter `Ex2Stage6ReleaseSmoke.A1Cuda:Ex2Stage6ReleaseSmoke.A1Vulkan:Ex2Stage6ReleaseSmoke.E1Cuda:Ex2Stage6ReleaseSmoke.E1Vulkan`, GoogleTest JSON retained. Exactly four listed cases ran once, all PASS, group exit 0; every child exit 0, four-file final package, 100 rows/100 successful, full oracle checksum/sample/summary verification, native A1/E1 route and before/after provenance checks. Tests use observed API index 0 and captured UUID; formal external matcher confirmed these selectors.',
    '',
    'Supervised fixture executable `out/build/x64-release/tests/ComputeLabEx2Stage6SupervisorSmokeTests.exe`, filter `Ex2Stage6SupervisorReleaseSmoke.A1Cuda:Ex2Stage6SupervisorReleaseSmoke.A1Vulkan:Ex2Stage6SupervisorReleaseSmoke.E1Cuda:Ex2Stage6SupervisorReleaseSmoke.E1Vulkan`, GoogleTest JSON retained. Exactly four listed cases ran once, all PASS, group exit 0; each primary exit 0, Job total 17 and final active 0, verified containment/resumption/primary termination/Job emptiness; no descendant survival or supervisor termination request, no operation/child/campaign timeout. Each fixture validated 200 progress observations, full terminal form, clean EOF/no trailing bytes, canonical unchanged FinalOnly four-file package, 100 ordered samples and successful reconciliation. Fixture deadlines are their existing smoke-specific values (operation 60000, child 120000, enclosing 240000 ms), not changes to production manifest deadlines.',
    '',
    'All eight owned disposable sessions were cleaned. Post-smoke and every independent verification found no s6-i*-smoke-* remnants. Smoke timings were engineering checks only and are not comparative or official campaign evidence.',
    '',
    '## Freeze, independent verification and execution firewall',
    '',
    'freeze-and-generate.ps1 created artifact-freeze.json and the candidate exactly once after smoke success and clean-source recapture. Eight artifacts were hashed from live absolute Release paths. No rebuilding/relinking, shader compilation, tracked edit, selector change or upgrade occurred after freeze. The new generator used the accepted archival preparation code only as a read-only reference; historical generation/launch scripts were never invoked.',
    '',
    'Independent Python standard-library verify.py uses its own strict duplicate-key/type/schema checks, schedule reconstruction and canonical serializer, rather than generator/project/supervisor serialization. Candidate PASS, final PASS and distinct read-only stability PASS; all exit 0. Each receipt contains the exact argv, all eight live artifact hashes, full live API capture, reconstructed schedule and 665 absent paths. Reports were create-new. Promotion used exclusive CreateNew, candidate byte copy, Flush(true), physical-hash readback; the candidate bytes were not regenerated.',
    '',
    'Interpreter: C:/Users/rolan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe; invocation prefix `python.exe -B results/local/ex2-s7-preparation-20261009-h2/verify.py`; remaining exact arguments are retained in each receipt. Candidate/final/stability stdout logs are distinct. Final source, manifest bytes, artifact sizes/hashes and namespace absence were rechecked immediately before this report; stability independently recaptured both API UUIDs.',
    '',
    'Production supervisor invocation count in this preparation: zero. Production child slots launched: zero. Only the eight expressly authorized disposable smoke children ran. No production manifest entrypoint, historical launch_once.ps1, campaign automation/background execution, commit/push, Notion write or next task was invoked. All production outputs remain absent; no official Stage-7 scientific timing package was generated. This statement is supported by this chat execution trace and retained namespaces, not an invented system-wide process audit.',
    '',
    'The original archival checkout at C:/Users/rolan/Desktop/CmptLab/ComputeLab remains at a4a5138bb694c6ad163aac58a90bee227673e9f9 as observed by Git worktree inventory. It was not built, edited, cleaned or used for new evidence. Its separately unresolved 446-file historical-evidence EOL question remains unresolved and outside this preparation; no byte-level archive certification is claimed. Git common-directory reads were necessary for linked worktree provenance.',
    '',
    'Remaining review caveats: incomplete canonical Debug coverage (accepted waiver); incomplete native Debug build console retention (new limitation); historical EOL question in the different checkout; live desktop/remote-control/GPU activity; bundled-vcpkg and toolchain differences; unverified runtime injection/external instrumentation and future sleep/update/power controls. The frozen manifest is retained for review and is not launch authorization. No launch wrapper or suggested authorization string was created.',
    '',
    '## Retained files (absolute paths)',
    '',
]
for path in index:
    lines.append(f'- `{path}`')
lines += [f'- `{PREP / "preparation-index.json"}`', f'- `{PREP / "review.md"}`', '', '**CAMPAIGN NOT EXECUTED. LAUNCH NOT AUTHORIZED.**', '', 'Stop here. A distinct future prompt and explicit human authorization are required for one production launch, after independent scientific-planning review.']
with (PREP / 'review.md').open('x', encoding='utf-8', newline='\n') as out:
    out.write('\n'.join(lines) + '\n')
print(f'PRE-EXECUTION VERDICT: INCOMPLETE\nREPORT={PREP / "review.md"}')
print('\n'.join(lines[6:29]))
