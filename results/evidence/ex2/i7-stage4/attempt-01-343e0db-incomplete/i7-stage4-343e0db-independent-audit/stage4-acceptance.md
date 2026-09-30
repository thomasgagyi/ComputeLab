# ComputeLab EX-2 I7 Prompt-4 human-review report

I7 / STAGE-4 CORRECTNESS: INCOMPLETE. The sole campaign stopped at E1 cell 16 because the I7 adapter rejects E's undashed UUID text. No rerun or source change was performed.

## 1. Baseline verification

Branch main. HEAD and origin/main both `343e0dbcb28c46b6bbfa5309d72d6a5b0c5ffac7`; commit `343e0db Add EX-2 I7 correctness supervisor and control ledger`. Full porcelain clean; git diff --check passed before builds and at handoff. No staged, unstaged, or untracked source files.

## 2. Authority/source review

Governance, EX-2 scoped v1.1 contract, Prompt-2 foundation/execution/package boundaries, Prompt-3 supervisor/progress/inspection/ledger, native/integration semantics, shader definitions and CTest ownership were reviewed. Current ApprovedCoreCells matches the exact requested inventory. No documents/source/tests/scripts/CMake/shaders were edited.

The exposed implementation defect is in the E-to-I7 observation seam: D:/ComputeLab/ComputeLab/src/app/Ex2EIntegration.cpp:708 formats the same 16-byte UUID as 32 undashed hex characters; D:/ComputeLab/ComputeLab/src/app/Ex2CorrectnessExecution.cpp:590 requires the 36-character dashed FormatUuid value at its line 602 comparison. The E caller at line 1725 consequently throws before serialization.

Source-derived E text: `0340eaacdc67f450d558d47c55cc4417`.
Required I7 text: `0340eaac-dc67-f450-d558-d47c55cc4417`.
This is a deterministic text-format contract mismatch, not evidence that different physical GPUs were selected. No fix or reproducer GPU execution was attempted.

## 3. Pre-campaign regression results

Canonical host build passed, with `ninja: no work to do`. Initial sandbox attempt exited 1 because installed vcpkg/compiler resources were inaccessible; unchanged canonical command then passed with approved host access.

Canonical `.\\scripts\\test.ps1`: exit 0, 681 passed, 0 failed, 0 skipped; CTest real time 217.13 seconds. Retained log: pre-canonical-ctest.log.

Live CTest inventory contains 681 tests. Included subsets: Ex1-prefixed historical support/native 39; Prompt-1 I7 foundation 7; Prompt-2 correctness 16; Prompt-3 supervisor 85; G0 support 57. Composite/golden/configuration/evidence tests and historical EX-1 serializer tests also passed within canonical validation. These subset counts are not additional invocations. Process isolation was preserved; historical Prompt-3 dirty one-shot smoke was not launched.

Native E tests pass independently, but the existing I7 child smoke covers A1, not the E identity adaptation exposed by this campaign.

## 4. Vulkan validation results

| Pre-campaign mode | Passed | Failed | Skipped | Reported errors/hazards | Real seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| standard | 43 | 0 | 0 | 0 | 44.33 |
| sync | 43 | 0 | 0 | 0 | 46.60 |

Both variables were set to the named mode and removed in finally after each run. Native D1/E paths and actual integration tests were selected from live CTest inventory; six pure seam tests are included in each 43-test selection. Successful native tests assert validation-layer state and zero diagnostic error counts. Logs contain no VUID, SYNC-HAZARD, Validation Error or VALIDATION_ERROR. No validation override remained in the campaign environment.

## 5. Curated-readiness check

`scripts/assert-curated-evidence-ready.ps1` passed after pretests and again immediately before launch, identifying the exact clean committed source. All 71 exact campaign paths were absent before ownership/creation. Immediately before invocation, all 69 output paths were still absent; the newly owned manifest/audit directory were intentionally present. Results remain under results/local. Clean source provenance does not cure an incomplete campaign or authorize promotion.

## 6. Frozen binary/shader/device provenance

Built paths verified against CMake/build.ninja: child and supervisor under `out/build/x64-debug/src/app/`; six shaders under `out/build/x64-debug/src/vulkan/Ex2<name>.comp.spv`.

| Artifact | Actual frozen SHA-256 |
| --- | --- |
| Child executable | `cdb3b2b504bae0016117fc7f37cbeb777e42d1fc3cba917627679c8a5f513f29` |
| Supervisor executable | `1ad25840df454d0618d9ca6198755cf1939d187c73e989543fbcf855b931a66b` |
| A1 SPIR-V | `c40c5ef608254d7763aa244d1ca6403605f750212a038ccf1913cff2bd61c63a` |
| A2 SPIR-V | `bfda73b6abe562facaba4fa265b281144e2c8761517ffad71c8176184cf17f3f` |
| B1 SPIR-V | `7eb4a7d40c2ea5aa133821290318b18b45c49f5284cb6ffc8ebda5f9b35e933c` |
| B2 SPIR-V | `2761fe5fab23630ce7ed47a93afe74cd769d1b154fa13a33a49601ed02120a26` |
| C SPIR-V | `7e4643308689eb49ee19a205ad126cf616e57f2c613b2e11007ad5a95446a4aa` |
| D1 SPIR-V | `3d0787c7ce83802caca54cd9bc107d506949a509ba0ffe5f0604bf72ed9e7d61` |

All eight hashes were rechecked unchanged immediately before launch and after stop. No rebuild after freeze.
CUDA ordinal 0 and Vulkan physical index 0 independently verified by the supervisor as UUID `0340eaac-dc67-f450-d558-d47c55cc4417`; NVIDIA GeForce RTX 2060 SUPER. Evidence scope is x64-debug/Debug only.

## 7. Final 22-cell manifest

Retained exact manifest: D:/ComputeLab/ComputeLab/results/local/i7-stage4-343e0db-manifest.json.
v1 i7-correctness, protocol 1.1, machine i7-stage4-machine, expected_git_dirty false; exact approved 22 children, sequence_index == core_cell_index, c00..c21 unique. CUDA first/Vulkan second. A1/A2/B1/B2/C/D1 exact shader mapping; E JSON null. Operation/child/campaign deadlines 60000/1200000/86400000 ms; continuation completed-validation-only.

## 8. Canonical manifest hash

Committed hash-print CLI invoked twice without execution; printed hash equals embedded hash and independent Python canonical-payload hash.
Canonical payload: `5bbecef2058d4998de917839030f4402d1800a01dfc6bcc15332a0f0275def6d`.
Actual manifest file: `b61991b97509d365040f91360b7825785271c1ae5277e484eec6dea8a2554cbe`.

## 9. Supervisor execution result

Exactly one invocation:
`out/build/x64-debug/src/app/ComputeLabEx2CorrectnessSupervisor.exe --manifest results/local/i7-stage4-343e0db-manifest.json`.

Supervisor exit 4; final ledger status stopped_incomplete_or_unsafe, reason stop_incomplete_or_unsafe.
Start UTC 2026-09-30T14:04:39.275Z; end UTC 2026-09-30T14:10:30.969Z.
22 scheduled; 17 actual launches; 16 complete-pass; c16 child exit 3; c17..c21 unlaunched.
All 17 launched children have complete four-event progress; no termination reason, timeout, retry or replacement. Both retained supervisor console logs are empty.

## 10. 22-cell execution inventory

| Index | Exact session ID | Variant | Frozen condition | Result |
| ---: | --- | --- | --- | --- |
| 0 | i7-stage4-c00 | A1 | N=256 | 0 / complete-pass |
| 1 | i7-stage4-c01 | A1 | N=262144 | 0 / complete-pass |
| 2 | i7-stage4-c02 | A1 | N=16777216 | 0 / complete-pass |
| 3 | i7-stage4-c03 | A2 | N=262144 | 0 / complete-pass |
| 4 | i7-stage4-c04 | A2 | N=16777216 | 0 / complete-pass |
| 5 | i7-stage4-c05 | B1 | N=262144; structured-v1 | 0 / complete-pass |
| 6 | i7-stage4-c06 | B1 | N=262144; shuffled-v1 | 0 / complete-pass |
| 7 | i7-stage4-c07 | B1 | N=16777216; shuffled-v1 | 0 / complete-pass |
| 8 | i7-stage4-c08 | B2 | N=262144; structured-v1 | 0 / complete-pass |
| 9 | i7-stage4-c09 | B2 | N=262144; shuffled-v1 | 0 / complete-pass |
| 10 | i7-stage4-c10 | B2 | N=16777216; shuffled-v1 | 0 / complete-pass |
| 11 | i7-stage4-c11 | C | N=1048576; active=1048576; allocated=1048576 | 0 / complete-pass |
| 12 | i7-stage4-c12 | C | N=1048576; active=32768; allocated=1048576 | 0 / complete-pass |
| 13 | i7-stage4-c13 | C | N=1048576; active=64; allocated=1048576 | 0 / complete-pass |
| 14 | i7-stage4-c14 | D1 | N=262144; K=16 | 0 / complete-pass |
| 15 | i7-stage4-c15 | D1 | N=1048576; K=64 | 0 / complete-pass |
| 16 | i7-stage4-c16 | E1 | S=1024; H2D | 3 / incomplete |
| 17 | i7-stage4-c17 | E1 | S=1048576; H2D | Not launched |
| 18 | i7-stage4-c18 | E1 | S=67108864; H2D | Not launched |
| 19 | i7-stage4-c19 | E2 | S=1024; D2H | Not launched |
| 20 | i7-stage4-c20 | E2 | S=1048576; D2H | Not launched |
| 21 | i7-stage4-c21 | E2 | S=67108864; D2H | Not launched |

## 11. Package topology

16 finalized sessions, 32 complete standard backend series, 128 standard files. One c16.incomplete with two empty backend directories; one c16.failure.json. Thus 34 retained backend directories, but only 32 schema series. No c17..c21 paths. Final ledger exists; ledger .incomplete and .incomplete.tmp absent.

c16 sidecar: record_version 1, ex2-i7-correctness-child-failure, failure_phase validation, error_code completed_validation_failed, foundation_established true, staging_retained true, exact clean source/run IDs, detail `native observation contradicts the preconstructed I7 identity`. Retained filesystem agrees. No initialization/sample/summary/environment was fabricated for c16.

## 12. Independent manifest/ledger audit

Read-only stdlib audit parsed strict JSON with duplicate-key/non-finite rejection; exact keys/types, canonical hash, frozen values, scheduled order, actual 17-child prefix, source/dirty/binary/shader/GPU provenance, sequential UTC order, progress/exit/classification and filesystem topology passed for the retained stopped state. The committed ledger keys are scope, scheduled_children and executions.

Ledger SHA-256: `e56d6f33cd0f88de4bbb23b49786fee3b35016ef378be27bc9817b2d85001b6e`.
c16 sidecar SHA-256: `d2b6104b489d228ce62541587a46c8d6125bd953b6252e9f8572cef3eecf9fef`; 433 bytes.
Audit result is PASS_FOR_RETAINED_ARTIFACTS_ONLY, not Stage-4 PASS.

## 13. Artifact hash reconciliation

129/129 ledger artifacts independently rehashed and size-reconciled: 128 standard files plus one failure sidecar. No duplicate/path-escape/reparse/unexpected standard artifact. Raw files rehashed unchanged at audit end. artifact-hashes.csv has exactly 129 data rows and required ledger/actual size/SHA columns; it does not invent 47 missing files to reach 176.

## 14. 44-series semantic audit

Only 32/44 required series exist. All 32 available series independently pass exact schema-v2/1.1/P shape, clean provenance, approved workload/seed/generator/condition/series identities, source/binary/GPU/shader rules, run uniqueness, exact initialization and raw-row/summary checks. 32 run IDs, 32 series IDs, 16 condition IDs. Pair identities match for all 16 finalized pairs.
series-audit.csv has exactly 32 data rows; no fabricated rows for missing E evidence.
The full 44-series acceptance criterion remains unmet.

## 15. B/C composite-input verification

Both written byte-packing fixtures independently hash to:
B `c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de`.
C `57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7`.

All six B and three C campaign cells independently regenerated from written formulas using Python stdlib, including primary/permutation composite, targets/full allocated zero state, and full expected-output digests; all 18 corresponding backend environments match. The committed Prompt-3 independent CPU inspector also regenerated every complete-pass cell's expected identities. No v1.0 evidence was relabeled or modified.

## 16. Raw-sample and summary regeneration

32 raw sample rows: sample_index 0, validation_passed true, status ok, null/empty failure fields; four timing fields empty. 32 initialization rows: backend_setup/setup_complete, sequence/process 0, empty duration and qualitative observation. Exact 13-/31-column headers and CRLF parsed.

32/32 summaries independently reconstructed and compared as complete typed objects: one row, zero validation failures, zero failed samples, exact group identities/parameters, all metrics null and correctness-only admission. No QPC counters, durations, statistics or numerical performance comparison persisted. c16 contains no raw sample.

## 17. Post-campaign regression results

NOT RUN. The campaign exposed a source implementation defect requiring modification, triggering the explicit STOP for human review. No post build, canonical suite, EX-1 rerun or standard/sync GPU validation was launched. These acceptance items are NOT-EVALUABLE, not PASS. Pre-campaign passes do not replace missing post-campaign results.

## 18. Retained limitations

The acceptance run does not safely assess the full 22-cell inventory: five E children unlaunched and c16 has no standard package. The UUID text adapter defect remains unmodified. No wrong-output GPU byte mismatch has been established by the retained disk evidence; INCOMPLETE is a full-verdict limitation, not a GPU arithmetic failure claim.

Schema v2 retains validation status and expected-output SHA but no observed-output bytes/digest/length. Neither this audit nor the Prompt-3 disk inspector reconstructs unretained wrong GPU output. Native live readbacks/comparisons remain child execution observations. Debug/one GPU/one correctness attempt is not performance qualification or cross-platform evidence. Prior shared-process GoogleTest sensitivity is retained as a limitation; only canonical process-isolated CTest was used.

EX-2 performance remains Proposed. Gate 0 NOT PASSED; Stage-2 measurement-execution NOT GRANTED; candidate-performance PROHIBITED; D2 conditional/unimplemented; Stage 5 not automatically authorized; Step 10b NOT passed; no production backend decision.

## 19. Acceptance matrix A–M

Each item is evaluated below. Full-44 completeness requirements are FAIL when only 32 records exist; missing E-only evidence and stopped post-firewall checks are NOT-EVALUABLE.

| Item | Required criterion | Result | Evidence / limitation |
| --- | --- | --- | --- |
| A1 | main branch | PASS | Verified within stated scope. |
| A2 | exact HEAD | PASS | Verified within stated scope. |
| A3 | origin/main exact | PASS | Verified within stated scope. |
| A4 | clean source | PASS | Verified within stated scope. |
| A5 | no tracked change | PASS | Verified within stated scope. |
| B1 | build PASS | PASS | Verified within stated scope. |
| B2 | canonical suite PASS | PASS | Verified within stated scope. |
| B3 | historical EX-1 PASS | PASS | Verified within stated scope. |
| B4 | Prompt-1 contract regressions PASS | PASS | Verified within stated scope. |
| B5 | Prompt-2 execution regressions PASS | PASS | Verified within stated scope. |
| B6 | Prompt-3 supervisor regressions PASS | PASS | Verified within stated scope. |
| B7 | standard Vulkan validation PASS | PASS | Verified within stated scope. |
| B8 | sync Vulkan validation PASS | PASS | Verified within stated scope. |
| C1 | readiness immediately before run | PASS | Verified within stated scope. |
| C2 | expected_git_dirty false | PASS | Verified within stated scope. |
| C3 | source SHA exact | PASS | Verified within stated scope. |
| C4 | binary hashes exact | PASS | Verified within stated scope. |
| C5 | shader hashes exact | PASS | Verified within stated scope. |
| C6 | target UUID exact | PASS | Verified within stated scope. |
| D1 | v1/type/protocol exact | PASS | Verified within stated scope. |
| D2 | canonical hash exact | PASS | Verified within stated scope. |
| D3 | 22 children | PASS | Verified within stated scope. |
| D4 | indices 0..21 exact | PASS | Verified within stated scope. |
| D5 | unique session IDs | PASS | Verified within stated scope. |
| D6 | shader mapping exact | PASS | Verified within stated scope. |
| D7 | deadline bounds | PASS | Verified within stated scope. |
| D8 | continuation policy exact | PASS | Verified within stated scope. |
| E1 | single invocation only | PASS | Verified within stated scope. |
| E2 | supervisor exit 0 | FAIL | exit 4 |
| E3 | final ledger | PASS | Verified within stated scope. |
| E4 | execution_complete | FAIL | stopped_incomplete_or_unsafe |
| E5 | 22 scheduled/22 actual | FAIL | 22 scheduled / 17 actual |
| E6 | no retry | PASS | Verified within stated scope. |
| E7 | no timeout/termination | PASS | Verified within stated scope. |
| E8 | 22 complete progress sequences | FAIL | 17 complete progress sequences / 22 required |
| F1 | CUDA UUID | PASS | Verified within stated scope. |
| F2 | Vulkan UUID | PASS | Verified within stated scope. |
| F3 | exact equality | PASS | Verified within stated scope. |
| F4 | target UUID match | PASS | Verified within stated scope. |
| G1 | 22 final sessions | FAIL | 16 final sessions |
| G2 | 0 incomplete sessions | FAIL | 1 retained incomplete |
| G3 | 0 failure sidecars | FAIL | 1 retained sidecar |
| G4 | 44 series | FAIL | 32 standard series; 2 empty staging directories |
| G5 | 176 standard files | FAIL | 128 standard files |
| G6 | no unexpected standard files | PASS | Verified within stated scope. |
| H1 | 44/44 schema 2 | FAIL | 32/32 available; 12 absent |
| H2 | 44/44 protocol 1.1 | FAIL | 32/32 available; 12 absent |
| H3 | 44/44 mode P | FAIL | 32/32 available; 12 absent |
| H4 | 44/44 clean provenance | FAIL | 32/32 available; 12 absent |
| H5 | exact input identities for full inventory | NOT-EVALUABLE | 16 completed cells verified by committed inspector; E standard evidence absent |
| H6 | exact expected-output identities for full inventory | NOT-EVALUABLE | 16 completed cells verified by committed inspector; E standard evidence absent |
| H7 | B/C composite semantics | PASS | all 9 B/C cells independently reconstructed |
| H8 | 44/44 validation true/status ok | FAIL | 32 passing rows / 44 required |
| H9 | exact shader rules across full inventory | NOT-EVALUABLE | 32 available series obey exact mapping; no E schema rows to evaluate |
| H10 | pair invariants across full inventory | FAIL | c16 I7/E UUID text identity invariant rejected |
| I1 | all retained sample timings null | PASS | Verified within stated scope. |
| I2 | no QPC evidence | PASS | Verified within stated scope. |
| I3 | no numerical CUDA/Vulkan performance comparison | PASS | Verified within stated scope. |
| J1 | manifest hash rechecked | PASS | Verified within stated scope. |
| J2 | ledger SHA recorded | PASS | Verified within stated scope. |
| J3 | every ledger artifact rehashed | PASS | 129/129, including failure sidecar |
| J4 | all sizes match | PASS | 129/129 |
| J5 | 44/44 summary regeneration | FAIL | 32/32 available / 44 required |
| J6 | 44/44 series audit | FAIL | 32/32 available / 44 required |
| J7 | wrong-output limitation accurate | PASS | Verified within stated scope. |
| K1 | post canonical suite | NOT-EVALUABLE | Not run under defect-triggered STOP. |
| K2 | post historical EX-1 | NOT-EVALUABLE | Not run under defect-triggered STOP. |
| K3 | post standard Vulkan validation | NOT-EVALUABLE | Not run under defect-triggered STOP. |
| K4 | post sync Vulkan validation | NOT-EVALUABLE | Not run under defect-triggered STOP. |
| K5 | post zero errors/hazards | NOT-EVALUABLE | Not run under defect-triggered STOP. |
| L1 | no source edits | PASS | Verified within stated scope. |
| L2 | no G0 campaign or decision | PASS | canonical existing G0 regressions only; no qualification campaign/verdict |
| L3 | no candidate-performance | PASS | Verified within stated scope. |
| L4 | no Stage-2 authorization | PASS | Verified within stated scope. |
| L5 | no D2 | PASS | Verified within stated scope. |
| L6 | no Notion | PASS | Verified within stated scope. |
| L7 | no results/evidence promotion | PASS | Verified within stated scope. |
| L8 | no commit/push | PASS | Verified within stated scope. |
| M1 | one PASS/FAIL/INCOMPLETE outcome | PASS | Verified within stated scope. |
| M2 | I7 completion claimed only on PASS | PASS | completion not claimed |
| M3 | Stage 3 completion claimed only with prerequisites | PASS | Stage 3 completion not claimed |
| M4 | Gate 0 pending | PASS | Verified within stated scope. |
| M5 | Stage 2 unauthorized | PASS | Verified within stated scope. |
| M6 | Step 10b unpassed | PASS | Verified within stated scope. |

## 20. Final Stage-4 verdict

INCOMPLETE. First blocking cell: c16 / core 16 / E1 H2D 1024 bytes. Its completed integration observation fails I7 textual UUID identity reconciliation before schema serialization. Supervisor safely stops with exit 4, preserves the incomplete session/sidecar and launches no later cell. Source correction and any future campaign require separate human direction. No I7, Stage-3 implementation, full integrated A–E or Stage-4 completion is claimed.

## 21. Exact retained local artifact inventory

143 files and 52 directories in this task's retained campaign namespace:
manifest (1), final control ledger (1), c00..c15 standard packages (128 files), c16 failure sidecar (1), c16.incomplete (two empty backend directories), independent audit (12 files).

Audit files: artifact-hashes.csv; audit.py; command-log.txt; manifest-check.json; pre-canonical-ctest.log; pre-vulkan-standard-ctest.log; pre-vulkan-sync-ctest.log; retained-inventory.txt; series-audit.csv; stage4-acceptance.md; supervisor-stderr.log; supervisor-stdout.log.
retained-inventory.txt lists every exact absolute file and directory path, including itself. No paths for c17..c21, no ledger incomplete/tmp, no hidden cleanup or deletion. Canonical tooling's ignored out/build logs are not counted as campaign evidence.

## 22. Final repository state

main; HEAD/origin/main `343e0dbcb28c46b6bbfa5309d72d6a5b0c5ffac7`; source clean, git diff --check PASS, no tracked diff. Only ignored results/local artifacts created. No source patch, protocol/schema change, retry, manual child, new dependency, Gate-0 campaign/decision, candidate-performance campaign, D2, Notion update, results/evidence promotion, commit or push.

STOPPED for human review.

I7 / STAGE-4 CORRECTNESS: INCOMPLETE
Gate 0 remains NOT PASSED.
Stage-2 measurement-execution authorization remains NOT GRANTED.
EX-2 candidate-performance collection remains PROHIBITED.
