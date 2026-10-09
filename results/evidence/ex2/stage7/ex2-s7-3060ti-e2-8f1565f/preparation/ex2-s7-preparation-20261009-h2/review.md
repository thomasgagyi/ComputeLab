# EX-2 Stage-7 RTX 3060 Ti pre-execution review

**PRE-EXECUTION VERDICT: INCOMPLETE. This is not a campaign PASS.**

Full native Debug build console output was not durably retained by the PowerShell transcript. No build or smoke retry was performed. The Debug build did complete successfully with wrapper/native success; its invocation, developer-shell provenance, configure log, Ninja completion records and real exit status remain retained. All three targeted Debug smoke logs are complete. The explicitly required full Debug build console log cannot be reconstructed faithfully. This retention limitation requires independent human review; no extra build is authorized by this preparation.

## Exact review tuple

- Actual execution worktree: `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti` (NTFS, plain directory and ancestors checked; no reparse alias).
- Source: `8f1565f7fb92e2184f4d2382aabf8fa7d00e1274`; detached HEAD; tracked/index/nonignored-untracked status empty immediately before report.
- Git common directory: `C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git`; linked worktree relationship preserved.
- Protocol/schema/kind/instrument: `1.4 / 2 / diagnostic / H`; manifest version `1`, parser type `ex2-stage6-diagnostic`.
- Machine ID: `ex2-s7-3060ti-machine`.
- CUDA ordinal `0`, Vulkan physical-device index `0`; both independently captured API UUIDs: `99a2369e-ca50-aac6-5c2c-fcaa44625084`.
- GPU name in both APIs: NVIDIA GeForce RTX 3060 Ti; CUDA compute capability 8.6.
- Manifest ID: `ex2-s7-3060ti-e1-8f1565f`.
- Semantic manifest SHA-256: `5082991f007606e8644601f0c6d076d9850730b54ad236f504aee39afa23fcb0` (canonical payload excludes manifest_sha256, no final LF in semantic payload).
- Physical manifest-file SHA-256: `175141852d8c04509bda9342d2ff9e534950a5e95c49e8c04654029e406dad91` (exact canonical UTF-8 bytes, no BOM/CR, exactly one final LF).
- Final manifest: `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-3060ti-e1-8f1565f-stage6-manifest.json`.
- Namespace proof: 665 unique absent production destinations = 660 child final/staging/sidecar paths, three control-ledger variants, one analysis destination, one execution-wrapper destination. Final manifest was also absent before exclusive publication. Each independent receipt retains the entire 665-path list.

| Artifact | Actual absolute path | Bytes | SHA-256 |
|---|---|---:|---|
| child | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\app\ComputeLabEx2Stage6.exe` | 766464 | `4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c` |
| supervisor | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\app\ComputeLabEx2Stage6Supervisor.exe` | 659456 | `33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8` |
| a1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2A1.comp.spv` | 1552 | `c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a` |
| a2 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2A2.comp.spv` | 2188 | `bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f` |
| b1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2B1.comp.spv` | 1880 | `7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c` |
| b2 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2B2.comp.spv` | 1880 | `2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26` |
| c | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2C.comp.spv` | 1436 | `7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa` |
| d1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2D1.comp.spv` | 1912 | `3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61` |

## Contract and scientific boundaries

All 22 cells in source-approved order: A1 (256, 262144, 16777216 elements); A2 (262144, 16777216); B1 and B2 each (262144 structured-v1, 262144 shuffled-v1, 16777216 shuffled-v1); C (1048576 elements with 1048576, 32768, 64 active counters); D1 (262144 elements/16 iterations, 1048576/64); E1 host-to-device and E2 device-to-host each (1024, 1048576, 67108864 bytes). E3 adds no cell; D2 is excluded.

Five paired fresh-process blocks CV/VC/CV/VC/CV per cell, ten children per cell, 220 total, 110 per backend, slot indices 0..219 and unique -slot-000..-slot-219 session names. Each successful child has 100 ordered H observations 0..99, zero warm-up: 22000 planned records. Native backend implementations and full CPU-oracle correctness remain unchanged. Retain raw failures and extreme timings. No extra warm-up, outlier deletion, adaptive count, timing stop, retry/resume. resolved-only-no-retry continuation; operation/child/campaign deadlines 60000/1200000/86400000 ms.

Gate 0 remains FAIL for proposed D1/H comparative scope. Stage-2 candidate-performance execution remains NOT GRANTED; production backend remains UNSELECTED. No CUDA/Vulkan winner, cross-generation speedup/ranking, qualification or production-application inference is admitted.

## Host and toolchain provenance

Windows 10 Enterprise 10.0.19045; AMD Ryzen 7 5700X, 8 cores/16 threads. System memory at inventory: 33478696 KiB total visible, 24823684 KiB free. C: NTFS, 287323746304 bytes available of 499427872768. GPU API total memory 8589410304 bytes; NVML/nvidia-smi inventory 8192 MiB total, 7614 MiB free, 411 MiB used, 168 MiB reserved.

Driver 616.92; WDDM; display attached No / active Disabled. Inventory observed P8, 30% fan, about 28 C and 14 W / 200 W in initial summary; 14–15% GPU utilization during inventory. Detailed current/default/max clocks, power, PCIe and GPU processes are retained in inventory.log. CUDA Driver API version 13040; Vulkan device API 1.4.351, driver version raw 2585198592, device ID 9353 / vendor 4318. Both APIs were enumerated and selected by matching the target UUID, then confirmed at ordinal/index 0.

Visual Studio Community 2026 18.10.3 (18.10.12224.181); MSVC 19.51.36260, tool directory 14.51.36231; Windows SDK 10.0.26100.0 with rc/mt resolved in the x64 developer shell. CUDA nvcc 13.4.92; Vulkan SDK 1.4.363.0, glslc shaderc v2026.4 and spirv-val v2026.4; CMake 4.4.3; Ninja 1.13.2. NVML import lib at the CUDA 13.4 x64 lib directory, 118114 bytes.

Normal environment: VCPKG_ROOT D:/vcpkg, version 2026-09-26-51bf87ca6e9bf3e622d84ff323bd202ab1ca0c0b. Actual script-established x64 developer shell: C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg, version 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8. This shell selection was retained. No root override, persistent PATH/registry/toolchain change was performed. GTest 1.18.0 and two vcpkg helper packages were restored from the host binary cache; GTest prints its cached original __FILE__ path in the archival checkout, which is dependency provenance, not evidence of building current project targets there. Actual EX-2 target compilation and artifact paths belong to this execution worktree.

AnyDesk (three processes), ChatGPT (multiple processes), Codex, dwm and explorer were active. GPU clients included ChatGPT, explorer, Windows desktop applications and the Codex computer-use helper. No processes were terminated. Vulkan registry inventory includes SDK explicit layers plus OBS/Steam implicit-layer registrations. Identity capture explicitly enabled zero layers, but registration alone does not prove absence of runtime injection. External profiler/tracing/injection state, sleep/update/power policies and activity at a future launch require human review; they were not modified or declared fully controlled.

## Exact builds and bounded smoke evidence

`./scripts/build.ps1 -Preset x64-debug` — exactly once, exit 0. Inventory `ctest --preset x64-debug -N -R '^ComputeLabSmoke\.(CUDA|Vulkan|GpuIdentity)$'` selected exactly 3. Execution `ctest --preset x64-debug -R '^ComputeLabSmoke\.(CUDA|Vulkan|GpuIdentity)$' --output-on-failure` — exactly once, exit 0; CUDA, Vulkan and GpuIdentity all actually passed. LastTest records the physical RTX 3060 Ti and matching API UUID.

**The 1242-test canonical Debug suite was NOT rerun and remains INCOMPLETE.** Prior operator-interrupted session: 924 PASS, one interrupted, 317 not started. The accepted waiver remains explicit; three targeted functional smokes do not retroactively complete canonical validation. No scripts/test.ps1, unfiltered CTest or Stage-6 supervisor simulation was run.

`./scripts/build.ps1 -Preset x64-release` — exactly once, exit 0; real Release cache, actual bundled vcpkg toolchain, target -arch=native compilation and executable cuobjdump sm_86 cubins verified. Cache architecture default 75 was not mistaken for target architecture. All six EX-2 SPIR-V files and separate EX-1 shader generated; no seventh EX-2 cell. Build has existing C4834 warnings in test code; no source patches or rebuilds.

Direct executable `out/build/x64-release/tests/ComputeLabEx2Stage6ChildSmokeTests.exe`, filter `Ex2Stage6ReleaseSmoke.A1Cuda:Ex2Stage6ReleaseSmoke.A1Vulkan:Ex2Stage6ReleaseSmoke.E1Cuda:Ex2Stage6ReleaseSmoke.E1Vulkan`, GoogleTest JSON retained. Exactly four listed cases ran once, all PASS, group exit 0; every child exit 0, four-file final package, 100 rows/100 successful, full oracle checksum/sample/summary verification, native A1/E1 route and before/after provenance checks. Tests use observed API index 0 and captured UUID; formal external matcher confirmed these selectors.

Supervised fixture executable `out/build/x64-release/tests/ComputeLabEx2Stage6SupervisorSmokeTests.exe`, filter `Ex2Stage6SupervisorReleaseSmoke.A1Cuda:Ex2Stage6SupervisorReleaseSmoke.A1Vulkan:Ex2Stage6SupervisorReleaseSmoke.E1Cuda:Ex2Stage6SupervisorReleaseSmoke.E1Vulkan`, GoogleTest JSON retained. Exactly four listed cases ran once, all PASS, group exit 0; each primary exit 0, Job total 17 and final active 0, verified containment/resumption/primary termination/Job emptiness; no descendant survival or supervisor termination request, no operation/child/campaign timeout. Each fixture validated 200 progress observations, full terminal form, clean EOF/no trailing bytes, canonical unchanged FinalOnly four-file package, 100 ordered samples and successful reconciliation. Fixture deadlines are their existing smoke-specific values (operation 60000, child 120000, enclosing 240000 ms), not changes to production manifest deadlines.

All eight owned disposable sessions were cleaned. Post-smoke and every independent verification found no s6-i*-smoke-* remnants. Smoke timings were engineering checks only and are not comparative or official campaign evidence.

## Freeze, independent verification and execution firewall

freeze-and-generate.ps1 created artifact-freeze.json and the candidate exactly once after smoke success and clean-source recapture. Eight artifacts were hashed from live absolute Release paths. No rebuilding/relinking, shader compilation, tracked edit, selector change or upgrade occurred after freeze. The new generator used the accepted archival preparation code only as a read-only reference; historical generation/launch scripts were never invoked.

Independent Python standard-library verify.py uses its own strict duplicate-key/type/schema checks, schedule reconstruction and canonical serializer, rather than generator/project/supervisor serialization. Candidate PASS, final PASS and distinct read-only stability PASS; all exit 0. Each receipt contains the exact argv, all eight live artifact hashes, full live API capture, reconstructed schedule and 665 absent paths. Reports were create-new. Promotion used exclusive CreateNew, candidate byte copy, Flush(true), physical-hash readback; the candidate bytes were not regenerated.

Interpreter: C:/Users/rolan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe; invocation prefix `python.exe -B results/local/ex2-s7-preparation-20261009-h2/verify.py`; remaining exact arguments are retained in each receipt. Candidate/final/stability stdout logs are distinct. Final source, manifest bytes, artifact sizes/hashes and namespace absence were rechecked immediately before this report; stability independently recaptured both API UUIDs.

Production supervisor invocation count in this preparation: zero. Production child slots launched: zero. Only the eight expressly authorized disposable smoke children ran. No production manifest entrypoint, historical launch_once.ps1, campaign automation/background execution, commit/push, Notion write or next task was invoked. All production outputs remain absent; no official Stage-7 scientific timing package was generated. This statement is supported by this chat execution trace and retained namespaces, not an invented system-wide process audit.

The original archival checkout at C:/Users/rolan/Desktop/CmptLab/ComputeLab remains at a4a5138bb694c6ad163aac58a90bee227673e9f9 as observed by Git worktree inventory. It was not built, edited, cleaned or used for new evidence. Its separately unresolved 446-file historical-evidence EOL question remains unresolved and outside this preparation; no byte-level archive certification is claimed. Git common-directory reads were necessary for linked worktree provenance.

Remaining review caveats: incomplete canonical Debug coverage (accepted waiver); incomplete native Debug build console retention (new limitation); historical EOL question in the different checkout; live desktop/remote-control/GPU activity; bundled-vcpkg and toolchain differences; unverified runtime injection/external instrumentation and future sleep/update/power controls. The frozen manifest is retained for review and is not launch authorization. No launch wrapper or suggested authorization string was created.

## Retained files (absolute paths)

- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-3060ti-e1-8f1565f-stage6-manifest.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\artifact-freeze.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\build-debug-once.ps1`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\build-release-once.ps1`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\candidate-verification.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\candidate-verification.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-build-exit.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-build.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-configure.yaml`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-inventory.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-last-test.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-ninja.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-smoke-exit.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\debug-smoke.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\deployment-contract.md`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\direct-inventory.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\direct-smoke-exit.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\direct-smoke.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\direct-smoke.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\final-verification.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\final-verification.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\freeze-and-generate.ps1`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\freeze-generate.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\gpu-freeze.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\gpu-initial.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\gpu_identity.py`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\inventory.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\inventory.ps1`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\namespace-initial.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\publication.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\publish_review.py`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-build-exit.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-build.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-build.ninja`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-cache.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-configure.yaml`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-cuda-elf.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\release-ninja.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\stability-verification.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\stability-verification.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\supervisor-inventory.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\supervisor-smoke-exit.txt`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\supervisor-smoke.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\supervisor-smoke.log`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\verify.py`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\tmp\ex2-s7-3060ti-e1-8f1565f-prep\candidate-manifest.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\preparation-index.json`
- `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-preparation-20261009-h2\review.md`

**CAMPAIGN NOT EXECUTED. LAUNCH NOT AUTHORIZED.**

Stop here. A distinct future prompt and explicit human authorization are required for one production launch, after independent scientific-planning review.
