# Replacement clean I7 Stage-4 correctness acceptance

All 22 NEW approved cells complete-pass; all 96 acceptance checks PASS. Source was read-only. One supervisor execution; no campaign retry. Evidence remains local pending human review.

## 1. New baseline verification

Branch main. HEAD and origin/main both exactly 8eb7f3654e83073ece4d7ef2a8a5c2301cafe793; commit 8eb7f36 Canonicalize EX-2 E GPU UUID identity. Initial and final porcelain empty; no staged, unstaged, or untracked source files. git diff --check exit 0.

## 2. Historical first-campaign preservation verification

The 343e0db campaign was present before execution: 143 files and 52 directories. Full path/type/size/hash snapshot matched before launch, after campaign, and after post-firewall. Anchors:

| Historical anchor | SHA-256 |
| --- | --- |
| Canonical manifest payload | 5bbecef2058d4998de917839030f4402d1800a01dfc6bcc15332a0f0275def6d |
| Manifest file | b61991b97509d365040f91360b7825785271c1ae5277e484eec6dea8a2554cbe |
| Control ledger | e56d6f33cd0f88de4bbb23b49786fee3b35016ef378be27bc9817b2d85001b6e |
| c16 external failure sidecar | d2b6104b489d228ce62541587a46c8d6125bd953b6252e9f8572cef3eecf9fef |

Old c00..c15 remain complete-pass; old c16 remains incomplete at the E textual UUID identity seam; old c17..c21 remain absent. Old verdict INCOMPLETE, not FAIL and not retroactive PASS. No physical-device or transfer-arithmetic mismatch is inferred.

## 3. Authority/source review

Reviewed current AGENTS.md; charter, methodology, results-format, EX-2, I7 correctness-supervisor and G0-05 documentation; relevant Prompt-1 configuration/input/oracle/composite/evidence/foundation contracts; Prompt-2 dispatch/identity/setup/failure/package boundaries and child tests; Prompt-3 manifest/progress/Job/watchdog/inspector/ledger contracts and test ownership; corrected E producer/tests; backend-native A–E execution/integration and shader build mapping; EX-1/G0 regression ownership; canonical build/test/readiness scripts and live CTest inventory.

No source, test, documentation, script, CMake or shader changed. The obsolete development-only Prompt-3 one-shot supervisor smoke was inspected but NOT executed; CMake explicitly excludes it from CTest.

## 4. Pre-campaign canonical regression

Canonical build exit 0. Canonical scripts/test.ps1 exit 0: 683 passed, 0 failed, 0 skipped; test time 215.23 seconds. Included EX-1 39/39, I7 Foundation 7/7, I7 Correctness 17/17 and I7 Supervisor 85/85, plus the full existing configuration/oracle/logical-input/native/integration and G0 support regressions. Corrected UUID pure tests 2/2; six real E cells 6/6; A1 and corrected core-cell-16 E1 direct child smokes 2/2. Counts are subsets of 683, not additive new campaigns.

## 5. Pre-campaign Vulkan validation

Live inventory selected 43 D1/E tests with:
`^(Ex2Vulkan(D1|E).*\.|Ex2D1Integration(Literal|Core)\.|Ex2EIntegrationReal\.|SixApprovedCells/Ex2EIntegrationCore\.)`

Standard: 43 passed / 0 failed / 0 skipped, 43.89 seconds. Sync: 43 passed / 0 failed / 0 skipped, 44.28 seconds. Zero validation errors and synchronization hazards in both. Both COMPUTELAB_EX2_D1_VALIDATION and COMPUTELAB_EX2_E_VALIDATION enabled per named mode and removed in finally after each. Native/integration diagnostics assert real layer/mode enablement and zero error counters.

## 6. Curated-readiness result

scripts/assert-curated-evidence-ready.ps1 passed before manifest construction and immediately before launch. Exact clean committed source; initial 71-path namespace collision guard passed. Before launch all 69 ledger/session output paths absent except the newly owned manifest/audit setup. Prelaunch console text incorrectly labeled the 69 checked paths as 66; the loop and retained command log establish 3 + 22*3 = 69. No output path was reused. All artifacts remain under results/local for review; no promotion.

## 7. New frozen binary/shader/device provenance

Target CUDA ordinal 0 and Vulkan physical-device index 0: NVIDIA GeForce RTX 2060 SUPER. Canonical UUID 0340eaac-dc67-f450-d558-d47c55cc4417. Verified raw UUID byte sequence is 03 40 ea ac dc 67 f4 50 d5 58 d4 7c 55 cc 44 17; committed native metadata collection/supervisor checks compare the complete UUID, and every native pair verifies raw-byte equality. External metadata read-only checks also matched NVIDIA/Vulkan GPU0; driver 616.92, Vulkan API 1.4.351.

| Artifact | Actual frozen SHA-256 |
| --- | --- |
| out/build/x64-debug/src/app/ComputeLabEx2Correctness.exe | a7d289ae77ae40fba0e2ed3783f87fd665de32cc5a67522c03724fbb76ab4a23 |
| out/build/x64-debug/src/app/ComputeLabEx2CorrectnessSupervisor.exe | 1ad25840df454d0618d9ca6198755cf1939d187c73e989543fbcf855b931a66b |
| out/build/x64-debug/src/vulkan/Ex2A1.comp.spv | c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a |
| out/build/x64-debug/src/vulkan/Ex2A2.comp.spv | bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f |
| out/build/x64-debug/src/vulkan/Ex2B1.comp.spv | 7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c |
| out/build/x64-debug/src/vulkan/Ex2B2.comp.spv | 2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26 |
| out/build/x64-debug/src/vulkan/Ex2C.comp.spv | 7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa |
| out/build/x64-debug/src/vulkan/Ex2D1.comp.spv | 3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61 |

Hashes computed from actual files after final pre-build/tests, not copied from historical evidence. No rebuild occurred between freeze and campaign execution. All eight hashes and sizes remain identical after post-canonical rebuild.

## 8. New 22-cell manifest

Manifest id i7-stage4-8eb7f36; type i7-correctness; manifest version 1; protocol 1.1; machine i7-stage4-machine; exact 21 keys and 22 exact four-key children. Sequence equals core index 0..21; all new unique sessions, no D2/extras. CUDA-first, Vulkan-second. Shader map: 0..2 A1; 3..4 A2; 5..7 B1; 8..10 B2; 11..13 C; 14..15 D1; 16..21 null. Deadlines operation 60000 ms, child 1200000 ms, campaign 86400000 ms; continuation completed-validation-only.

## 9. New canonical manifest hash

Two committed --print-manifest-hash calls returned the same hash; only embedded placeholder hash was changed before execution. Independent duplicate-key/non-finite/type/member/schedule checks match.

Canonical payload SHA: 15e75febf36ef31585a2466f184040ddc54ab0cf2a1644256c7a766157de3c6d
Physical manifest-file SHA: 90f0a9b66a76ffde5ab96073ca0c30d46846b6e66bcef56caabe4e0b6bd16f46

## 10. New supervisor execution result

Exactly one final --manifest invocation. Exit 0. Final ledger exists; overall_execution_status execution_complete; failure_reason null. Committed ledger field scope = correctness_control_only_no_stage4_verdict (the attachment's descriptive execution_scope wording was not used to change the schema). Scheduled 22, actual 22. All 22: child exit 0, termination null, complete progress, CUDA Started/Returned then Vulkan Started/Returned, complete-pass, structurally valid, empty integrity errors, continue_complete_pass. No retry, replacement child, timeout or termination. Stdout/stderr retained, both 0 bytes.

Control timestamps: 2026-09-30T15:44:04.734Z through 2026-09-30T15:52:03.164Z. These are control records, not performance measurements.

## 11. Full 22-cell execution inventory

| Sequence/core cell | New session ID | Workload | Result |
| --- | --- | --- | --- |
| 0 | i7-stage4-8eb7f36-c00 | A1 N=256 | complete-pass; exit 0 |
| 1 | i7-stage4-8eb7f36-c01 | A1 N=262144 | complete-pass; exit 0 |
| 2 | i7-stage4-8eb7f36-c02 | A1 N=16777216 | complete-pass; exit 0 |
| 3 | i7-stage4-8eb7f36-c03 | A2 N=262144 | complete-pass; exit 0 |
| 4 | i7-stage4-8eb7f36-c04 | A2 N=16777216 | complete-pass; exit 0 |
| 5 | i7-stage4-8eb7f36-c05 | B1 N=262144 structured-v1 | complete-pass; exit 0 |
| 6 | i7-stage4-8eb7f36-c06 | B1 N=262144 shuffled-v1 | complete-pass; exit 0 |
| 7 | i7-stage4-8eb7f36-c07 | B1 N=16777216 shuffled-v1 | complete-pass; exit 0 |
| 8 | i7-stage4-8eb7f36-c08 | B2 N=262144 structured-v1 | complete-pass; exit 0 |
| 9 | i7-stage4-8eb7f36-c09 | B2 N=262144 shuffled-v1 | complete-pass; exit 0 |
| 10 | i7-stage4-8eb7f36-c10 | B2 N=16777216 shuffled-v1 | complete-pass; exit 0 |
| 11 | i7-stage4-8eb7f36-c11 | C N=1048576 active=1048576 allocated=1048576 | complete-pass; exit 0 |
| 12 | i7-stage4-8eb7f36-c12 | C N=1048576 active=32768 allocated=1048576 | complete-pass; exit 0 |
| 13 | i7-stage4-8eb7f36-c13 | C N=1048576 active=64 allocated=1048576 | complete-pass; exit 0 |
| 14 | i7-stage4-8eb7f36-c14 | D1 N=262144 K=16 | complete-pass; exit 0 |
| 15 | i7-stage4-8eb7f36-c15 | D1 N=1048576 K=64 | complete-pass; exit 0 |
| 16 | i7-stage4-8eb7f36-c16 | E1 S=1024 H2D | complete-pass; exit 0 |
| 17 | i7-stage4-8eb7f36-c17 | E1 S=1048576 H2D | complete-pass; exit 0 |
| 18 | i7-stage4-8eb7f36-c18 | E1 S=67108864 H2D | complete-pass; exit 0 |
| 19 | i7-stage4-8eb7f36-c19 | E2 S=1024 D2H | complete-pass; exit 0 |
| 20 | i7-stage4-8eb7f36-c20 | E2 S=1048576 D2H | complete-pass; exit 0 |
| 21 | i7-stage4-8eb7f36-c21 | E2 S=67108864 D2H | complete-pass; exit 0 |

## 12. New package topology

Exactly 22 final sessions, 44 backend directories/series, 176 standard schema files. Each session exactly its cuda and vulkan directories; each backend exactly environment.json, initialization.csv, samples.csv and summary.json. Zero incomplete sessions, failure sidecars, ledger incomplete/tmp siblings or unexpected standard members.

## 13. New independent manifest/ledger audit

Strict parse, exact members/types, canonical hash, full schedule, source/dirty/binary/GPU/deadlines, serial UTC order and complete lifecycle reconciled. Ledger SHA: 7e5ce4f40d40f8c2f08794b7a02ad860ae4cef0de2e04d509b385c4920636d1c. No manifest/ledger drift during audit or final recheck.

The first local audit-helper attempt passed every series check but its final -c* glob also matched -control.json. Only the new local helper's filter was corrected to -c[0-9]*. The original failed audit log remains retained. The second read-only audit passed; no campaign, Prompt-3 code, package or ledger was changed or rerun. manifest-check.json records the integrity audit before post-firewall completion; this final report supplies the now-complete Stage-4 verdict.

## 14. New artifact hash reconciliation

176/176 ledger-listed artifact SHA-256 hashes and sizes exactly match recalculated ordinary files confined under results/local, with reparse/escape rejection. Exact membership, no duplicate or unlisted standard files. Rehashed after the audit and again after regression; zero drift. See artifact-hashes.csv.

## 15. New 44-series semantic audit

44/44 schema 2, protocol 1.1, correctness, mode P, zero warmups, one planned sample, clean exact new source, actual child SHA, approved machine and GPU. Parameters/condition/series identities and backend order independently reconstructed; 44 unique run IDs, 44 series IDs, 22 condition IDs. All 22 pairs share exact input/expected/source/GPU/executable identities while backend/run/series are distinct. CUDA shader always null; Vulkan A–D exact frozen map and E null. All 44 audit_result PASS; all input/expected digests additionally validated by the committed Prompt-3 CPU inspector.

## 16. Corrected E UUID identity verification

E producer remains canonical lower-case 36-character 8-4-4-4-12 text, zero rejected, raw-byte equality unchanged. Exact pure fixture 00112233-4455-6677-8899-aabbccddeeff and same-device formatting pass, as do zero/mismatch rejection tests. I7 RequireObservationIdentity remains strict for both raw bytes and canonical text. New campaign c16 and all six E cells pass actual child publication; all 12 E series carry the exact canonical target UUID, both E shaders null, validation true/status ok, timing fields null. The corrected E direct smoke passes pre/post and cleans only its own session; no test-owned smoke artifacts remain.

## 17. B/C composite-input verification

All nine B/C cells (18 backend series) independently reconstructed in the local audit using approved little-endian two-component framing, complete B primary/permutation and complete C target/zero-initial-counter input. Inputs and expected-output hashes match both backend series and Prompt-3 checks; no component-digest-text substitute or historical v1.0 relabeling.

B golden: c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de
C golden: 57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7

## 18. Raw samples + summary regeneration

44 initialization headers/rows exact: backend_setup/setup_complete, sequence 0, duration empty. 44 sample headers/rows exact: sample_index 0, validation true, status ok, empty failure fields and all four timing fields empty. 44/44 complete typed summaries regenerate exactly from raw rows: each 1 recorded, 0 validation failures, 0 failed samples, all metrics null and correctness-only admission. No QPC/performance metric evidence or backend comparison; QPC is control-watchdog-only.

## 19. Post-campaign canonical regression

Canonical scripts/test.ps1 exit 0: 683 passed / 0 failed / 0 skipped; 211.91 seconds. EX-1 39/39, I7 Foundation 7/7, I7 Correctness 17/17, I7 Supervisor 85/85, both real child smokes, six approved E cells and all other canonical regressions pass. Rebuild reports ninja no work to do; frozen binary/shader bytes unchanged.

## 20. Post-campaign Vulkan validation

Identical actual 43-test grouping. Standard: 43 passed / 0 failed / 0 skipped, 44.65 seconds. Sync: 43 passed / 0 failed / 0 skipped, 44.80 seconds. Zero validation errors or synchronization hazards in both; mode variables removed after each and absent at final guard. All four pre/post validation logs retained.

## 21. Historical first-campaign preservation recheck

Full original 195-entry inventory unchanged after all campaign/regression activity: all 143 file path/size/hash tuples and 52 directory paths/types. All four historical anchors above unchanged; old c17..c21 absent; old c16 incomplete/sidecar retained. No renaming, deletion, replacement, audit rewrite, evidence combination or retrospective acceptance.

## 22. Retained limitations

Scope is x64-debug/Debug, this source SHA, these exact binaries/six shaders, this verified GPU and these 22 approved cells, not Release, other hardware/workloads or a production backend decision. Schema v2 archives no observed GPU output bytes/digest/length; no independent reconstruction of an unretained wrong GPU output or third GPU-output oracle is claimed. Native integration performs full comparisons; Prompt-3 CPU inspector is the independent semantic truth path. Final audit is an integrity/identity/raw-row/summary check with additional B/C host reconstruction.

No Gate-0 qualification or candidate-performance campaign, measurement admission, Stage-2 authorization, D2, Notion update, evidence promotion or commit/push. DR-41 remains Proposed; Stage 5 not automatically authorized; Step 10b not passed. Canonical native timing regressions are tests, not performance acceptance.

## 23. Acceptance matrix A–N

96/96 PASS; 0 FAIL; 0 NOT-EVALUABLE.

| Item | Criterion | Result |
| --- | --- | --- |
| A1 | branch main. | PASS |
| A2 | exact new HEAD. | PASS |
| A3 | origin/main exact. | PASS |
| A4 | clean source. | PASS |
| A5 | no source modification. | PASS |
| B1 | old campaign present. | PASS |
| B2 | old manifest hash unchanged. | PASS |
| B3 | old ledger hash unchanged. | PASS |
| B4 | old c16 sidecar unchanged. | PASS |
| B5 | old c17-c21 remain absent. | PASS |
| B6 | old verdict remains INCOMPLETE. | PASS |
| C1 | build PASS. | PASS |
| C2 | canonical 683/683 expected or explained exact current count. | PASS |
| C3 | EX-1 PASS. | PASS |
| C4 | Prompt-1 regressions PASS. | PASS |
| C5 | Prompt-2 regressions PASS. | PASS |
| C6 | Prompt-3 regressions PASS. | PASS |
| C7 | corrected E UUID tests PASS. | PASS |
| C8 | corrected E→I7 c16 smoke PASS. | PASS |
| C9 | standard Vulkan validation PASS. | PASS |
| C10 | sync Vulkan validation PASS. | PASS |
| D1 | readiness PASS. | PASS |
| D2 | source exact. | PASS |
| D3 | dirty=false. | PASS |
| D4 | target GPU exact. | PASS |
| D5 | child SHA frozen. | PASS |
| D6 | supervisor SHA frozen. | PASS |
| D7 | six shader SHAs frozen. | PASS |
| E1 | new id/type/protocol exact. | PASS |
| E2 | canonical hash exact. | PASS |
| E3 | 22 children. | PASS |
| E4 | indices 0..21. | PASS |
| E5 | new unique sessions. | PASS |
| E6 | shader mapping exact. | PASS |
| E7 | deadlines exact. | PASS |
| E8 | continuation exact. | PASS |
| F1 | exactly one NEW final invocation. | PASS |
| F2 | exit 0 for PASS. | PASS |
| F3 | final ledger. | PASS |
| F4 | execution_complete. | PASS |
| F5 | 22 scheduled. | PASS |
| F6 | 22 actual. | PASS |
| F7 | no retry. | PASS |
| F8 | no timeout/termination. | PASS |
| F9 | all 22 complete progress. | PASS |
| G1 | CUDA raw UUID. | PASS |
| G2 | Vulkan raw UUID. | PASS |
| G3 | exact raw equality. | PASS |
| G4 | exact canonical target UUID. | PASS |
| G5 | corrected E canonical UUID seam accepted. | PASS |
| H1 | 22 NEW final sessions. | PASS |
| H2 | 0 NEW incomplete. | PASS |
| H3 | 0 NEW sidecars. | PASS |
| H4 | 44 NEW backend series. | PASS |
| H5 | 176 NEW standard files. | PASS |
| H6 | no unexpected standard files. | PASS |
| I1 | 44/44 schema 2. | PASS |
| I2 | 44/44 protocol 1.1. | PASS |
| I3 | 44/44 mode P. | PASS |
| I4 | 44/44 clean 8eb7f36 provenance. | PASS |
| I5 | exact input identities. | PASS |
| I6 | exact expected outputs. | PASS |
| I7 | B/C composite semantics. | PASS |
| I8 | 44/44 validation true/status ok. | PASS |
| I9 | exact shader rules. | PASS |
| I10 | pair invariants. | PASS |
| J1 | all sample timing null. | PASS |
| J2 | no QPC evidence. | PASS |
| J3 | no performance comparison. | PASS |
| K1 | new manifest hash rechecked. | PASS |
| K2 | new ledger SHA recorded. | PASS |
| K3 | all NEW ledger artifacts rehashed. | PASS |
| K4 | all sizes match. | PASS |
| K5 | 44/44 summary regeneration. | PASS |
| K6 | 44/44 series audit PASS. | PASS |
| K7 | wrong-output limitation accurate. | PASS |
| L1 | canonical suite PASS. | PASS |
| L2 | EX-1 PASS. | PASS |
| L3 | standard Vulkan validation PASS. | PASS |
| L4 | sync Vulkan validation PASS. | PASS |
| L5 | zero validation errors/hazards. | PASS |
| M1 | no source edits. | PASS |
| M2 | no Gate-0 qualification. | PASS |
| M3 | no candidate-performance. | PASS |
| M4 | no Stage-2 authorization. | PASS |
| M5 | no D2. | PASS |
| M6 | no Notion. | PASS |
| M7 | no results/evidence promotion. | PASS |
| M8 | no commit/push. | PASS |
| N1 | exactly PASS/FAIL/INCOMPLETE. | PASS |
| N2 | I7 completion only on PASS. | PASS |
| N3 | Stage 3 complete only on PASS. | PASS |
| N4 | Gate 0 pending. | PASS |
| N5 | Stage 2 unauthorized. | PASS |
| N6 | Step 10b unpassed. | PASS |
| N7 | historical first attempt explicitly retained as INCOMPLETE. | PASS |

## 24. Final Stage-4 verdict

PASS. I7 implementation/control/acceptance is technically complete; Stage 3 EX-2 implementation complete; A–E individual and supervised correctness accepted only within the frozen source/binary/shader/GPU/22-cell scope above. The historical incomplete campaign is superseded by this new independent campaign, never rewritten or retroactively passed.

## 25. Exact NEW local artifact inventory

Under D:/ComputeLab/ComputeLab/results/local only:
- i7-stage4-8eb7f36-manifest.json
- i7-stage4-8eb7f36-control.json
- the exact 22 new session IDs in section 11, each with its two named backend directories and exactly the four standard files in section 12;
- i7-stage4-8eb7f36-independent-audit/ with exactly 19 files:

- artifact-hashes.csv
- audit.py
- command-log.txt
- final-reconciliation.json
- frozen-provenance.json
- independent-audit-pass.log
- independent-audit.log
- manifest-check.json
- post-canonical-ctest.log
- post-vulkan-standard.log
- post-vulkan-sync.log
- pre-canonical-ctest.log
- pre-vulkan-standard.log
- pre-vulkan-sync.log
- retained-inventory.txt
- series-audit.csv
- stage4-acceptance.md
- supervisor-stderr.txt
- supervisor-stdout.txt

Total NEW retained namespace: 197 files (176 standard + 2 manifest/ledger + 19 audit aids) and 67 directories (22 session + 44 backend + 1 audit). retained-inventory.txt enumerates every path including itself. Zero partial/failure siblings. All ignored/local; no promotion. Canonical ignored build outputs/logs outside this campaign namespace are normal regression infrastructure and are not included as campaign evidence.

## 26. Final repository state

main; HEAD and origin/main 8eb7f3654e83073ece4d7ef2a8a5c2301cafe793. Empty staged/unstaged/untracked source status; git diff --check exit 0. No source changes. All new evidence local and retained. No commit/push/Notion/promotion. STOP for human review.

I7 / STAGE-4 CORRECTNESS: PASS
The earlier 343e0db Stage-4 campaign remains historical INCOMPLETE evidence.
Gate 0 remains NOT PASSED.
Stage-2 measurement-execution authorization remains NOT GRANTED.
EX-2 candidate-performance collection remains PROHIBITED.
