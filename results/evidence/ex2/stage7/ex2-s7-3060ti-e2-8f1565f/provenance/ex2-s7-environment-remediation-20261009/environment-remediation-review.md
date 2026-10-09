# Stage-7 environment remediation and readiness review

**DISPOSITION: INCOMPLETE. CAMPAIGN NOT EXECUTED. LAUNCH NOT AUTHORIZED.**

OBS-specific remediation passed in a fresh, narrowly controlled child environment. The approved active-scheme AC sleep change passed. Frozen source, all eight artifacts, both manifest hashes, API identity and all 665 unused production destinations remain intact. Readiness remains incomplete because external NVIDIA capture state and enforceable host continuity were not established. Operator statements are retained exactly and are distinguished from measurements.

## Observed checks and exact frozen tuple

- Actual worktree: `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti`, NTFS, no relevant reparse ancestors. Source `8f1565f7fb92e2184f4d2382aabf8fa7d00e1274`, detached, empty initial/final tracked/index/nonignored-untracked status; linked common directory `C:/Users/rolan/Desktop/CmptLab/ComputeLab/.git` retained.
- Machine `ex2-s7-3060ti-machine`; RTX 3060 Ti, compute capability 8.6; CUDA ordinal 0/Vulkan physical index 0; both fresh API UUIDs `99a2369e-ca50-aac6-5c2c-fcaa44625084`.
- Protocol/schema/kind/instrument `1.4 / 2 / diagnostic / H`, parser type `ex2-stage6-diagnostic`, manifest version 1, dirty=false.
- Manifest ID `ex2-s7-3060ti-e1-8f1565f`; final path `results/local/ex2-s7-3060ti-e1-8f1565f-stage6-manifest.json`, 18474 bytes.
- Semantic SHA-256 `5082991f007606e8644601f0c6d076d9850730b54ad236f504aee39afa23fcb0`.
- Physical SHA-256 `175141852d8c04509bda9342d2ff9e534950a5e95c49e8c04654029e406dad91`.
- Strict UTF-8/no BOM/no CR/one final LF, duplicate-key rejection, exact canonical sorted/minimal JSON/key schema/types/constants and semantic payload excluding only manifest_sha256 verified. No manifest declared hash was replaced.
- Full 22 groups × 10 fresh-process slots, unique sequence 0..219/session -slot-000..-219; source-owned CV/VC/CV/VC/CV, 110 children/backend, warmup=0, 100 ordered H observations/child, 22000 planned records; no E3 extra cell/D2/extensions/adaptation/retry. Deadlines 60000/1200000/86400000 ms; resolved-only-no-retry.
- Reconstructed 665 unique protected destinations absent: 660 child final/incomplete/failure paths, three control ledgers, analysis and separate execution-wrapper destinations. No live production child/supervisor in observed snapshots. No protected namespace writes.

| Artifact | Absolute path | Bytes | SHA-256 (expected = observed before/final) |
|---|---|---:|---|
| child | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\app\ComputeLabEx2Stage6.exe` | 766464 | `4104e5f7237b03d5ac3265f450358e76533b099e4fdc049e62cd9ff20e9ae93c` |
| supervisor | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\app\ComputeLabEx2Stage6Supervisor.exe` | 659456 | `33a0bebcba66e02e2f39ee4b311844c748e1a9b0c828a9c49b5e57ea989de5c8` |
| a1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2A1.comp.spv` | 1552 | `c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a` |
| a2 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2A2.comp.spv` | 2188 | `bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f` |
| b1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2B1.comp.spv` | 1880 | `7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c` |
| b2 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2B2.comp.spv` | 1880 | `2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26` |
| c | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2C.comp.spv` | 1436 | `7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa` |
| d1 | `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\out\build\x64-release\src\vulkan\Ex2D1.comp.spv` | 1912 | `3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61` |

Mismatch count 0. Earlier retained input inventories/receipts were read and hash-reconciled unchanged; no prior verifier/generator was executed. Before/final integrity receipts include all exact paths/digests and the 665-path list. Archive checkout was not changed or repaired; shared Git queries used GIT_OPTIONAL_LOCKS=0.

## Installed OBS mechanism and actual fresh probe

Installed manifest `C:\ProgramData\obs-studio-hook\obs-vulkan64.json`, 514 bytes, SHA-256 `7b6b0813fb58b276847a8583eb5c3f94aee7d7ad0ae3a1ef6133d5d8771f20f4`. Actual installed disable_environment is DISABLE_VULKAN_OBS_CAPTURE=1; VK_LAYER_OBS_HOOK, library ./graphics-hook64.dll, no required enable condition. Its bytes/hash and relevant full JSON are retained in before/final-integrity.json. Registry registration remains intact.

Fresh subprocess PID 22096, pointer size 64 bits, exit 0. Exact argv: `["C:\\Users\\rolan\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\python.exe", "-B", "C:\\Users\\rolan\\src\\ComputeLab-Stage7-3060Ti\\results\\local\\ex2-s7-environment-remediation-20261009\\metadata-only-probe.py"]`. Audited helper SHA-256 `87ffcbb6709e9ed03c54eba3d5ac39eba3130958ddf0c72109f45de8cb1e0f3c`. The prior metadata function was reused by source extraction only, not importing/rerunning its checker; all module paths were collected while VkInstance was live. Fresh process inherited normal driver/SDK PATH, requested zero explicit layers, set only OBS disable=1 and removed probe instrumentation overrides. CUDA Driver UUID v2/attributes and Vulkan 1.3 physical properties/device-ID APIs matched the target; no CUDA context, VkDevice, queue, commands, submissions, kernel, timing or A–E operation.

OBS remains registered/advertised by discovery but graphics-hook64.dll was absent in the fresh process, as were Steam/SDK VkLayer/renderdoc/gfxreconstruct/Avast DLL matches. Module capture while instance alive, exact stdout/stderr, PID, argv, environment controls, helper digest and exit are retained. Parent OBS variable remained unchanged; user/machine environment and registry were never altered. No fallback to VK_LOADER_LAYERS_DISABLE or loader debug was used.

**Bounded certainty:** the automated OBS/external-layer-name screen passed, but full inventory includes C:/Windows/system32/nvspcap64.dll (NVIDIA capture support), version 11.0.9.251, SHA-256 ec8837a6f35507beef70382071842ef4b3a0d466aa0b8be02f0d5fa911d79767. NVIDIA container processes were observed; no separately named capture/overlay/profiler process was observed. Limited ShadowPlay/NvCamera registry reads did not provide an active-capture state. DLL loading alone is not proof of active capture or timing perturbation, and no universal injection-free claim is made. External capture OFF remains UNKNOWN, hence INCOMPLETE rather than a fabricated PASS.

## Exact intended future environment — prose only, not a launcher

A separately authorized native launcher must explicitly construct the supervisor environment BEFORE spawning it: set DISABLE_VULKAN_OBS_CAPTURE=1; preserve required PATH/CUDA_PATH/VULKAN_SDK and non-instrumentation state; leave VK_INSTANCE_LAYERS, VK_LAYER_PATH, VK_IMPLICIT_LAYER_PATH, VK_LOADER_LAYERS_ENABLE/ALLOW/DISABLE, VK_LOADER_DEBUG, VK_ICD_FILENAMES/VK_DRIVER_FILES, D1/E validation flags, CUDA_VISIBLE_DEVICES/CUDA_LAUNCH_BLOCKING and capture/profiler force-enable controls absent/inactive exactly as audited. Record redacted controls and reject new unreviewed injection settings. No probe-only debug may leak into production.

The future task must run a fresh fail-closed metadata check under that SAME intended environment immediately before its separately authorized ONE launch. It must require matching UUIDs and OBS absent, and resolve external capture/overlay state rather than infer it from registration or DLL names alone. The pinned supervisor CreateProcessW call has null lpEnvironment, so child processes inherit the supervisor environment; setting the variable only in this review subprocess does not configure an unrelated future launcher. The native supervisor must itself receive the constructed environment. See [Microsoft CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) and the installed manifest evidence, corroborated by [OBS official manifest](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/obs-vulkan64.json).

No production launch command/string was submitted, no launch wrapper/service/scheduled task was created, and no production binary was invoked.

## Power change, exact approval and restoration

Operator approval: “Approve this exact AC sleep change.” Executed exactly powercfg /change standby-timeout-ac 0, exit 0, active Balanced GUID 381b4222-f694-41f0-9685-ff5bb260df2e. Before AC 0x708=1800 seconds; after AC 0=Never. DC remains 0x384=900 seconds, scheme unchanged. Final host readback confirms Never and AC online. Only this persistent setting changed; no DC/screen/hibernate/all-scheme tuning.

Leave the approved AC setting as recorded for the planned campaign. AFTER campaign completion or failure, while the SAME scheme is active, restore `powercfg /change standby-timeout-ac 30`, then query/read back the scheme/standby timeout. Do not restore via another active scheme inadvertently. This task did not execute restoration because the campaign has not run. See [Microsoft powercfg](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/powercfg-command-line-options). Never does not prevent manual sleep, sign-out, power loss, external policy or a restart.

## Operator attestations, security and continuity

Exact operator replies retained in operator-decisions.json: “dont do any windows updates or reboots”; “there wont be any windows updates and anydesk is not an issue”; “its ok” (documented background acceptance); “all are disablerd” (Avast shields/firewall question); “Restore all protection immediately after the campaign ends, including failure”. These statements are attestations/choices, not automatically observed technical guarantees.

Current Windows Update pause start/expiry fields unset; active hours 8–17. CBS/WU reboot-required keys absent; pending EdgeUpdate file-rename entry persists. No pause/policy/service/task was changed. The operator intends no updates/reboots, but no enforceable 24-hour prevention method was documented. A separate approved/manual temporary control or a clearly accepted enforceable alternative and final recheck remain necessary to settle this condition.

AnyDesk is running and operator says it is not an issue. Future native supervision must survive Codex/AnyDesk disconnect without Windows sign-out, parent termination or replay: an operator-owned Windows process/session arrangement, independent of Codex tool lifetime, should be defined and confirmed in the separate launch workflow. No such arrangement was started or dynamically exercised here. Job kill-on-close protects containment if the supervisor dies; it makes the campaign incomplete rather than guarantees continuity. No retry/resume/automatic relaunch is permitted.

Avast raw SecurityCenter productState remains 270336 (0x42000); Antivirus/Firewall-related services remain present/running as recorded, which does not establish shield state. All protection disabled is the operator attestation; restore all protection immediately after campaign ends, including failure. Exact prior alert/detection/action remains UNKNOWN. No current active required-artifact block was independently established; all frozen bytes/read-access match. No exclusions, security changes or log reconstruction were performed. No indefinite global security disablement is recommended.

Background AnyDesk/ChatGPT/Codex/desktop/Avast/NVIDIA processes remain observed. Final nvidia-smi snapshot: driver 616.92, WDDM/display inactive, P8, 501 MiB used/7524 MiB free, transient 96% GPU utilization, average power about 15.89 W. This is a metadata snapshot, not accepted timing, idle qualification or a stable-load guarantee. Operator accepts documented desktop background as diagnostic. Nothing was closed, tuned or benchmarked.

## Known coverage/archive limitations and scientific firewall

Canonical Debug suite remains INCOMPLETE: 924 PASS / one interrupted / 317 unstarted of 1242; operator explicitly will not rerun it. Later three filtered CUDA/Vulkan/GpuIdentity Debug smokes and four direct plus four supervised Release smokes PASS at frozen bytes; no new execution. Entire native Debug console transcript missing; retained exit/configure/Ninja/smoke logs are not a fabricated full transcript. Separate archive has unresolved 446-evidence-file EOL/index question; untouched and not treated as dirty execution source. No waiver covers source/hash/UUID drift, hooks, containment or retry.

Entire 22-cell H-only protocol-1.4 diagnostic replication remains the scope. Gate 0 FAIL for proposed D1/H comparative scope; Stage 2 candidate-performance NOT GRANTED; production backend UNSELECTED. No CUDA/Vulkan or cross-generation ranking, scientific qualification or production-application conclusion.

## Remaining disposition conditions

- NVIDIA nvspcap64.dll capture-support module loaded in the fresh Vulkan metadata process. Capture/overlay activity is UNKNOWN; available settings/process inspection did not establish that external capture is OFF. This is not proof of active capture or measured interference, but the future no-capture condition remains unverified.
- Operator assures no updates/reboots; observed Windows Update pause/expiry fields are unset, active hours are 08:00–17:00, and no enforceable 24-hour unattended-restart control was documented. That assurance is not a technical guarantee.
- AnyDesk continuity is operator-assured. A native supervisor arrangement independent of Codex/AnyDesk/session termination has not been exercised or created; the separate execution task must establish it without a test campaign or automatic replay.
- All Avast shields/firewall disabled and restoration timing are operator-attested, not independently certified. Old alert details remain UNKNOWN; no current required-artifact block was independently established. Required-file read/access and exact bytes passed.

OBS and approved AC sleep are resolved observations. The remaining conditions prevent READY_FOR_EXECUTION_PROMPT in this receipt; they are not evidence of frozen corruption or a proven NVIDIA capture session. No broad layer/security/update changes were attempted to remove uncertainty.

## Reports and integrity inventory

Human review `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\environment-remediation-review.md`; receipt `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\environment-remediation-receipt.json`; file inventory `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\output-inventory.json`. Exact commands `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\exact-execution-log.txt`; decisions `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\operator-decisions.json`; initial/final integrity `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\before-integrity.json` and `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\final-integrity.json`; initial/final host `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\before-host.json` and `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\after-host.json`; power `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\approved-power-change.json`; probe raw output `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\probe-stdout.txt`, stderr `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\probe-stderr.txt`, results `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\probe-result.json`, environment/argv `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\probe-execution.json`; NVIDIA supplement `C:\Users\rolan\src\ComputeLab-Stage7-3060Ti\results\local\ex2-s7-environment-remediation-20261009\nvidia-capture-observation.json`.

All new records are create-new, durable writes within one ignored unused directory. Output inventory omits its own digest to avoid circular hashing. Old receipts, source/build files and frozen manifest were not modified. No automatic follow-up or execution task created.

PRODUCTION SUPERVISOR LAUNCHES BY THIS TASK: 0
PRODUCTION CHILDREN LAUNCHED BY THIS TASK: 0
CAMPAIGN NOT EXECUTED
LAUNCH NOT AUTHORIZED
