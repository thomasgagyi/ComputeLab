# EX-2 — Generic GPU infrastructure qualification

## Status and authorization

**Specification v1.0 — approved base design; historical measurement clauses where amended.** Design freeze date: 2026-09-17. v1.0 fixes the generic workload definitions, implementation boundary, maximum initial matrix, schema-v2 record contract and original bounded qualification procedure. Its A–E semantic design remains authoritative except where a later amendment explicitly says otherwise; historical v1.0 evidence and meanings remain immutable. Later amendments must name exactly what they add or supersede before affected evidence is collected.

**Current EX-2 state:** I7 implementation/control/acceptance is complete; Stage 3 implementation is complete; Stage 4 integrated correctness is **PASS** for clean source `8eb7f3654e83073ece4d7ef2a8a5c2301cafe793`, RTX 2060 SUPER UUID `0340eaac-dc67-f450-d558-d47c55cc4417`, and the exact 22 approved A–E correctness cells. The first clean Stage-4 attempt at `343e0dbcb28c46b6bbfa5309d72d6a5b0c5ffac7` remains preserved as historical **INCOMPLETE** evidence after stopping at E1 cell 16 on the E textual UUID-format contract. After the bounded producer correction, the replacement campaign at `8eb7f3654e83073ece4d7ef2a8a5c2301cafe793` passed 22/22 cells, 44/44 backend series and 176/176 standard package-artifact reconciliation, with 683/683 canonical tests before and after and 43/43 Vulkan standard plus 43/43 synchronization validation. Both Stage-4 attempts are curated at evidence commit `24558d0d121a41e3d92f407f804ffbd1154c24b3` under `results/evidence/ex2/i7-stage4/`.

**Current Stage-5 / Gate-0 state:** S5-E1 is a valid complete qualification campaign, **COMPLETE — SCIENTIFICALLY NONQUALIFIED**. The Stage-5 v1.2 scientific protocol was frozen at `0f8a82838bd601f6662272b36d3acb15f4da10db`; S5-I5 / S5-E1 source is `70859055bf4280f5aeacd8039e9d3c41c9487e84`; curated S5-E1 evidence is committed at `a5694b6afa918499607949ec0c60d63d21be1270` under `results/evidence/ex2/stage5/ex2-s5-e1-7085905/`. All ten D1-W inputs are valid; five of ten have no qualifying `W<=16`; `selected_common_w=null`, `d1_warmup_qualified=false`, `d1_scope_qualified=false`, and D1-S is `skipped_by_protocol`. Gate 0 is **FAIL for the proposed D1/H `host_completion_ns` comparative scope**. The protocol-directed D1-S skip is a complete negative scientific outcome, not an execution defect.

**Authorization and next branch:** DR-41 is **Retired**; DR-43 is **Accepted**, and its bounded Stage-5 path completed as designed; DR-44 is **Accepted** and owns the post-Gate-0 full descriptive/diagnostic Stage-6 branch. EX-2 remains active for bounded diagnostic engineering evidence. Stage-2 comparative measurement-execution authorization remains **NOT GRANTED**; comparative CUDA-versus-Vulkan candidate-performance remains **PROHIBITED under the failed ruler**. D2 remains conditional and unimplemented; Step 10b is **NOT PASSED**. The S6-C1 v1.3 amendment below is **READY FOR HUMAN REVIEW**, not self-accepted. Only after human acceptance does Stage 6 become **PROTOCOL FROZEN / IMPLEMENTATION NOT STARTED**. No Stage-6 implementation or campaign has begun.

EX-1's final outcome remains **measurement methodology not qualified for downstream comparison**. Its correctness/provenance evidence is useful; fresh-process host-visible CUDA/256 states, universally justified warm-up, and instrumentation-perturbation control remain unresolved. We do **not** investigate the CUDA/256 root cause as a prerequisite. Tiny host-visible CUDA/Vulkan ranking remains excluded. A tidy later run does not erase the historical anomaly.

**Authority:** `AGENTS.md`, `docs/charter.md`, `docs/methodology.md`, and `docs/results-format.md` define common rules; this EX-2 specification narrows them without changing EX-1's historical interpretation. EX-1 source, frozen executable/SPIR-V, evidence and schema v1 remain intact. A contradiction with a common rule blocks execution until reviewed, not silently overridden.

**Implementation sequencing:** the 2026-09-18 correctness-first decision allowed implementation and correctness qualification independently of unresolved Gate-0 measurement qualification; that path completed through I7 and Stage 4. The v1.2 bounded Stage-5 path subsequently completed with Gate-0 FAIL. DR-44 now strategically authorizes the full Stage-6 diagnostic branch, subject to human acceptance of S6-C1 and separately reviewed implementation/execution increments. The future sequence is `S6-C1 -> S6-I1 -> S6-I2 -> S6-I3 -> S6-E1`. The existing A–E semantic design remains authoritative; implementation, correctness and diagnostic collection do not grant comparative candidate-performance authorization. Historical amendment prose below records its publication-time state and is preserved unchanged.

### Scoped v1.1 correctness logical-input identity amendment

**Specification v1.1 scope:** this additive revision defines logical-input identity for future I7 correctness packages only. The original approved v1.0 measurement-design baseline, its A–E operations, qualification/warm-up/sample/stop rules, and the Gate-0 diagnostic and qualification instrument remain historical v1.0 contracts. EX-2 still uses `schema_version: 2`; Gate 0 remains **NOT PASSED**; Stage-2 measurement-execution authorization remains **NOT GRANTED**; candidate-performance collection remains **PROHIBITED**; and DR-41 remains Proposed. This amendment authorizes no sample, comparison, campaign, runner, child process, package publication, or measurement-protocol change.

New I7 correctness packages declare `protocol_version="1.1"`. A, D1 and E retain their existing single logical-input semantics: canonical little-endian `uint32_t` bytes for A/D1 and exact generated bytes for E. B and C use `ex2-logical-input-v1`, a SHA-256 over the complete ordered logical input rather than over component digest text. From byte offset zero, the stream is the ASCII bytes `ComputeLab/EX-2/logical-input/v1`, one `0x00` byte, a two-component unsigned 32-bit little-endian count, then each component as an unsigned 32-bit little-endian ASCII name length, the exact name bytes without NUL, an unsigned 64-bit little-endian payload byte length, and the exact payload. There is no padding or trailing data. Every `uint32_t` payload word is encoded least-significant byte first in index order, and `payload_byte_length` is checked as `4*n` without overflow before hashing.

B components are, in order, `primary_words` and `permutation_words`. C components are, in order, `targets_words` and `initial_counters_words`; the latter is the complete allocated N-word pre-operation counter state and every word is zero. It is never the final histogram and is not replaced by a seed, component digest, length-only description, or known digest of a synthetic zero state. Empty components are representable by the byte encoding, although workload configuration validation separately controls executable conditions. Implementations hash the actual owned source bytes supplied to the operation and stream the encoding without constructing a second full concatenated payload.

The composite digest deliberately excludes workload variant, generator revision, seed, GPU UUID, execution mode, protocol version and backend. Those fields remain in the canonical comparison-condition and series identities. Thus B1 and B2 have the same `input_sha256` exactly when the two ordered component byte sequences match, and CUDA and Vulkan use the same input identity for the same declared logical input. Existing separate B primary/permutation and C target/reset diagnostics remain useful independent anchors but do not replace the composite `input_sha256`.

Independent byte-packing fixtures are: B `primary_words=[0x12345678,0,0xffffffff,0xabcdef01]`, `permutation_words=[2,0,3,1]`, 123 encoded bytes, SHA-256 `c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de`; and C `targets_words=[3,0,3,1]`, `initial_counters_words=[0,0,0,0]`, 128 encoded bytes, SHA-256 `57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7`. These identities anchor domain/NUL framing, little-endian widths, names, component order, payload lengths and word encoding.

Compatibility is one-way and explicit. Historical v1.0 schema-v2 packages, fixtures and their recorded B/C `input_sha256` meanings remain unchanged: do not relabel, rewrite, backfill, reinterpret or retroactively curate them. New I7 B/C correctness construction requires v1.1 and only the complete composite digest above; a new publisher cannot select v1.0 and supply an arbitrary single-buffer digest. At v1.1 publication, Gate-0 qualification still followed the v1.0 contract; the later scoped v1.2 amendment below supersedes only the named Stage-5/Gate-0 clauses for new qualification evidence. The four required files and exact existing `samples.csv` v2 header remain unchanged.

An I7 configuration or input-generation failure that occurs before a valid correctness foundation exists cannot produce a plan-bound schema-v2 sample row. The implemented I7 execution/package path therefore retains that pre-foundation failure externally without fabricating a plan, expected output, GPU identity or sample record. The typed failure adapter begins only after successful foundation construction.

Schema v2 records expected-output SHA-256 and validation status, but it does not retain the actual bytes, length or digest of a failed observed output. v1.1 added no observed-output field, no extra mandatory artifact and no diagnostic archival scheme. Consequently, retained packages can establish the recorded comparison result and expected digest but cannot independently reconstruct or verify unretained wrong GPU output bytes. This limitation remains active after I7 and Stage-4 completion and must be stated whenever relevant.

### Scoped v1.2 bounded Stage-5 qualification and Gate-0 scope-admission amendment

**Specification v1.2 scope — approved scientific design, not yet executed. Design freeze date: 2026-10-01.** This additive measurement amendment freezes the bounded Stage-5 qualification contract used to decide the still-open Gate-0 measurement scope after I7/Stage-4 correctness completion. It does **not** alter A–E workload semantics, the `ex2-mix64-v1` generator, schema version 2, the exact `samples.csv` v2 header, v1.1 logical-input identity, same-physical-GPU enforcement, the H `t0/t1/t2` boundary, correctness requirements, Gate 0A's native cross-API exclusion, EX-1 history, D2's conditional status, or any retained evidence meaning. New Stage-5 qualification packages use `protocol_version="1.2"` and `evidence_kind="qualification"`. Historical v1.0/v1.1 packages remain immutable and are never relabeled.

**Sequencing amendment:** Stage 5 is now the bounded qualification campaign that supplies the evidence needed to decide Gate 0. The historical numerical stage labels are not renumbered. The current flow is `Stage 4 PASS -> Stage 5 qualification -> Gate-0 verdict -> Stage-2 measurement-scope review/explicit authorization -> Stage 6`. A Stage-5 result never by itself grants Stage-2 authorization or converts qualification evidence into candidate-performance evidence.

#### Stage-5 frozen machine and conditions

The initial Stage-5 qualification slice remains same-GPU and Turing-specific:

- physical GPU: RTX 2060 SUPER UUID `0340eaac-dc67-f450-d558-d47c55cc4417`; compare CUDA/Vulkan only after exact UUID equality and do not substitute another adapter;
- A1 diagnostic sentinel: `N=256`, ordinary logical execution, instrument mode H;
- D1 sustained qualification target: `N=1048576`, `K=64`, ordinary logical execution, instrument mode H;
- every Stage-5 package is qualification evidence and remains qualification evidence permanently;
- no broad parameter sweep, CUDA/256 root-cause branch, profiler campaign, D2 implementation, A1 128-MiB extension or additional workload family is part of this amendment.

The bounded Stage-5 campaign uses exactly three independent fresh-process groups when all gates are reached:

1. A1 sentinel: five counterbalanced CUDA/Vulkan paired blocks = 10 fresh processes, 48 ordered diagnostic complete operations per process.
2. D1 warm-up qualification: five new counterbalanced paired blocks = 10 fresh processes, 48 ordered diagnostic complete operations per process.
3. D1 sample-count/process-repeatability qualification: five further new counterbalanced paired blocks = 10 fresh processes; each process applies the selected common D1 warm-up W, then records 200 ordered measured complete operations.

Thus a Stage-5 slice that reaches D1-S contains 30 fresh processes. A protocol-directed stop after a valid D1-W scientific qualification failure is a complete Stage-5 **FAIL** outcome even though D1-S is not launched; it is not an INCOMPLETE campaign. Process groups are distinct and cannot be pooled or reused as interchangeable replicates. No automatic retry or unplanned extension is allowed.

#### A1 diagnostic-sentinel contract

A1/256 asks only whether the historical tiny-operation host/process-state instability recurs in the final EX-2 implementation. It is a diagnostic process-state sentinel, not a performance-ranking condition.

Each of the five paired fresh-process blocks retains all 48 H observations, including `host_submission_ns`, `host_wait_ns` and `host_completion_ns`, plus process/block/order identity and normal correctness/provenance fields. The existing v1.0 candidate-W calculations may be regenerated descriptively from these observations, but A1 does **not** need a common qualifying W and cannot admit tiny host latency for CUDA/Vulkan ranking.

Interpretation is intentionally bounded:

- a sustained high/low fresh-process regime, abrupt within-process switch, drift or other state structure is retained and reported without root-cause escalation;
- if the historical split does not recur, report only that it was not reproduced in this campaign;
- a tidy A1 campaign does not erase EX-1 and does not qualify tiny CUDA/Vulkan host-visible ranking;
- A1 timing instability does not make D1 fail;
- an A1 correctness, provenance, GPU-identity, schema, timeout or execution-control failure makes Stage 5 incomplete until separately resolved because the frozen qualification campaign did not execute as specified.

#### D1 sustained qualification contract

D1 `(N=1048576,K=64)` is the primary Stage-5 Gate-0 qualification target. The scientific question is whether ordinary H `host_completion_ns` for the complete sustained device-resident D1 operation is interpretable across fresh processes on the named RTX 2060 SUPER.

The logical operation and backend-native implementations remain unchanged. Before each D1 complete operation, restore initial state A and complete preparation outside `t0`. CUDA's K native launches and Vulkan's ordinary per-operation command reset/record, K dispatches, required inter-pass barriers and submit remain inside the existing `t0->t1` submission boundary; completion wait ends at `t2`; readback and validation remain outside `t2`. The comparison is the same logical complete operation, not a claim that the two APIs perform identical host-side work.

D1 qualification has two mandatory, nonoverlapping process phases:

- **D1-W:** five paired fresh-process blocks for warm-up qualification;
- **D1-S:** only if D1-W succeeds, five new paired fresh-process blocks for sample-count and fresh-process stability qualification.

No D1-W process may be reused in D1-S.

#### D1 warm-up qualification

The v1.0 bounded warm-up algorithm is inherited unchanged for D1 H `host_completion_ns`.

Each D1-W process records 48 ordered complete-operation diagnostic timings. Candidate warm-ups are exactly `W={0,1,2,4,8,16}`. For each candidate W compare medians for `[W,W+8)` and `[W+8,W+16)` against the disjoint late reference `[40,48)`, and compare `[32,40)` against `[40,48)` for late drift. A candidate W qualifies within one process only when all three relative median differences are at most 5%, host-clock effective resolution permits a meaningful 5% distinction, and there is no clear persistent trend or abrupt state switch. The first qualifying candidate is that process's W.

All ten D1-W processes—five CUDA and five Vulkan—must find some qualifying `W<=16`. The Stage-5 common D1 W is the maximum of the ten per-process qualifying W values. If any D1-W process has no qualifying W by 16, D1 H host completion is unqualified under v1.2. Do not try W=32/64/128, change thresholds, discard the process or start a root-cause campaign. Preserve all 48 observations.

#### D1 sample-count and fresh-process qualification

Only after D1-W succeeds, each D1-S process applies exactly the selected common W and then records exactly 200 ordered measured complete D1 operations. For H `host_completion_ns`, calculate each process's prefix medians at 50, 100 and 200 observations and four consecutive nonoverlapping 50-sample windows.

Candidate ordinary count 100 qualifies within one process only when:

- `abs(median100/median200 - 1) <= 0.02`;
- `abs(median50/median200 - 1) <= 0.05`;
- first-to-last 50-sample window drift is no more than 5% and there is no systematic within-process drift/state switch;
- the 200-sample value is nonzero and resolved relative to the host clock's effective precision.

All ten D1-S processes must satisfy the rule. Stage 5 has **no automatic 200-sample ordinary fallback**: if any process fails to qualify 100, D1 H host completion is unqualified under v1.2. Do not collect 400 observations, keep doubling, or choose a count after viewing the direction of results. A future different ordinary count requires a separately reviewed protocol amendment.

Passing within-process convergence is not sufficient. For each backend independently, let the five process medians be the median of the first 100 accepted D1-S H `host_completion_ns` observations from each fresh process. Define `R_process = max(process_median)/min(process_median)` over positive, clock-resolved medians. Require `R_process <= 1.10` for CUDA and independently `R_process <= 1.10` for Vulkan, with no discarded process. This threshold is tied to the existing v1.0 10% minimum practically meaningful host-completion effect: a backend whose own qualified-process centers span more than that effect cannot support a 10% cross-backend claim under this scope.

D1 qualification does **not** require CUDA and Vulkan themselves to differ by 10%. Near-equal, stable backend distributions can qualify the ruler. Stage 5 does not issue a backend-performance winner or use paired process ratios as candidate-performance claims.

#### Stage-5 operational fact freeze — host clock and ordered state

No Stage-5 qualification evidence exists before this operational fact freeze.
Protocol version remains `"1.2"`: this completes operational definitions already
required by v1.2 and changes no workload, metric, hardware, H timing boundary,
W/sample count, `R_process` threshold or verdict policy. If preexisting real
v1.2 Stage-5 qualification evidence is discovered, STOP for human review rather
than silently retaining protocol 1.2. Historical v1.0/v1.1 sections are unchanged.

**Host-clock calibration:** every fresh Stage-5 process performs exactly 4,096
consecutive captures using `computelab::timing::HostClock` (`steady_clock`), after
normal process/setup initialization and before its A1/D1 observation sequence.
No intentional sleep or GPU operation occurs between captures. The resulting
4,095 consecutive integer-nanosecond deltas are outside every `t0/t1/t2` interval
and do not count as warm-up or measured operations. Equal captures may produce
zero deltas; a decreasing reading, invalid conversion or unrepresentable
arithmetic is invalid. A later execution increment retains every delta in the
additive qualification diagnostic artifact `host-clock.csv`, with exact header:

```csv
schema_version,run_id,series_id,process_index,sequence_index,delta_ns
```

S5-I2 implements pure analysis only, with no calibration execution or package
publication. Raw analysis input contains exactly 4,095 optional unsigned integer
nanosecond deltas in capture order; an absent delta explicitly represents a
missing/decreasing reading or invalid/unrepresentable conversion. Wrong count,
any absent delta, or no strictly positive delta makes calibration input invalid.
Zero deltas are allowed when at least one positive delta exists. Let `q` be the
minimum strictly positive observed delta. It is an operational effective-step
proxy, not physical oscillator resolution or nominal API precision.

For exact positive decision scale `S`, clock adequacy is `100*q <= S`, evaluated
with overflow-safe exact arithmetic. Exactly 1% is adequate; over 1% is
inadequate, with no floating epsilon. The 5% decision tolerance must span at
least five observed effective steps. Malformed calibration is an input/evidence
validity problem; valid but coarse calibration is scientific nonqualification.

**Ordered-state windows:** partition every observation in the eligible range
into exactly four contiguous, nonoverlapping windows covering that range in
original order. Lengths differ by at most one; remainders go to earlier windows.

| Eligible range | Window lengths |
| --- | --- |
| A1 `[0,48)`; D1-W W=0 | 12,12,12,12 |
| D1-W W=1 `[1,48)` | 12,12,12,11 |
| D1-W W=2 `[2,48)` | 12,12,11,11 |
| D1-W W=4 `[4,48)` | 11,11,11,11 |
| D1-W W=8 `[8,48)` | 10,10,10,10 |
| D1-W W=16 `[16,48)` | 8,8,8,8 |
| D1-S `[0,200)` | 50,50,50,50 |

Calculate exact integer/half-nanosecond window medians `M0..M3`. Persistent trend
is true only when these medians are monotonically nondecreasing or monotonically
nonincreasing **and** `abs(M0/M3-1)>0.05`, using the existing exact 5% tolerance.
Exactly 5% does not trigger; flat/equal sequences are not trends. This detects a
clear sustained direction, not a statistical trend test or p-value.

Abrupt state switch is true when any adjacent pair satisfies
`max(Mi,Mi+1)/min(Mi,Mi+1)>1.10`, using exact overflow-safe arithmetic. Exactly
1.10 is accepted. Window medians cover at least eight complete D1-W operations
or fifty D1-S operations: this is interval-level state evidence, not rejection
of an individual outlier. Never discard raw observations. The combined state
fact is `persistent_trend || abrupt_state_switch`; no supplied conclusion
boolean or override is accepted.

**Decision scales and integration:** A1 uses the minimum positive median of its
four windows. Each D1-W candidate independently analyzes `[W,48)` and uses the
minimum of its four state medians and the existing `[40,48)` late-reference
median. It qualifies only when the existing three inclusive 5% comparisons
pass, the candidate clock is adequate, and neither state fact is detected. The
first passing W remains selected; an early transient before W does not
automatically fail a later candidate. No candidate beyond 16 is allowed.
D1-S uses the minimum of `median200` and its four 50-sample window medians.
It requires all existing 2%/5% convergence criteria plus adequate clock and no
detected state structure; ordinary count 100 has no fallback. Backend-specific
`R_process` still uses the first-100 process medians with inclusive `<=1.10`.

Valid D1 clock inadequacy or state detection leaves input structurally valid
but scientifically unqualified. Missing/malformed calibration or observations
invalidate analysis input and are future execution/evidence-control concerns;
they are never relabeled a scientific coarse-clock result.

**A1 descriptive boundary:** each valid process reports its all-48 median,
calibration/effective step, descriptive adequacy, four 12-observation medians,
persistent trend, abrupt state switch and combined structure. For the exact
five processes per backend, separately report
`R_A1_process=max(all-48 process medians)/min(all-48 process medians)` and whether
it exceeds 1.10. A1 instability never blocks D1 qualification or admits tiny
host latency; malformed A1 evidence remains a later campaign evidence problem.
No ranking, winner, speedup, admission or human verdict is derived here.

Derived analysis uses `analysis_schema_version=2` for operational diagnostics,
stable process/reason ordering, locale-independent finite numbers and explicit
nulls. Evidence `schema_version=2`, protocol `"1.2"`, the exact `samples.csv`
header, v1.1 logical-input identity and same-GPU requirements remain unchanged.

#### Metric admission and instrumentation policy

For Stage 5, **Mode H `host_completion_ns` is the sole decision-bearing metric**. `host_submission_ns` and `host_wait_ns` remain recorded diagnostics and do not inherit host-completion admission. Their direct CUDA/Vulkan comparison requires a future explicit justification.

Mode N and Mode P are non-decision-bearing in v1.2:

- Mode N native CUDA event / Vulkan timestamp observations are optional backend-native explanatory or within-backend diagnostics only.
- Gate 0A remains unchanged: CUDA event and Vulkan timestamp intervals are not admitted for direct cross-API scalar ranking.
- Mode N never rescues failed H evidence, never substitutes for H and is never relabeled as ordinary candidate-performance evidence.
- Mode P / profiler / validation instrumentation is diagnostic only and never supplies ordinary timing evidence.
- Because N and P are excluded from the Stage-5 decision metric, H/N perturbation qualification is **not** a prerequisite for admitting H under v1.2. This does not claim EX-1 instrumentation perturbation was characterized; its downstream effect is resolved by scoping instrumented timing out of the admitted metric.

The v1.0 five-block H/N perturbation campaign and the v1.0 one-time five-pair borderline-effect extension do not execute as part of Stage 5 v1.2. No extra process group is added merely because cross-backend ratios appear interesting or borderline.

#### Stage-5 execution state and Gate-0 verdict

`INCOMPLETE` is a Stage-5 execution state, not a Gate-0 scientific verdict. Stage 5 is INCOMPLETE if the frozen campaign cannot validly answer its question because of a crash, timeout, device loss, wrong/mismatched GPU, source/provenance drift, malformed evidence, supervisor/control failure, required-capability failure, incorrect output/correctness regression or another execution/evidence defect. Preserve all partial evidence; do not silently rerun. Gate 0 remains NOT PASSED until the execution defect is separately reviewed.

The strongest Gate-0 verdict available from this bounded Stage-5 slice is **LIMITED PASS**. Full Gate-0 PASS is intentionally unavailable because tiny A1 ranking, direct native CUDA/Vulkan timing comparison, other workload families and other GPUs remain unqualified.

Issue **LIMITED PASS** only if the Stage-5 campaign is complete and D1 satisfies all of the following:

- every retained operation used for qualification is correct;
- CUDA and Vulkan match the exact intended physical RTX 2060 SUPER;
- H `t0/t1/t2` boundaries are valid and arithmetically consistent;
- all ten D1-W processes qualify some `W<=16`;
- the common W is the maximum of those ten values;
- all ten D1-S processes qualify ordinary count 100 under the frozen prefix/window rules;
- CUDA `R_process<=1.10`;
- Vulkan `R_process<=1.10`;
- host-clock resolution is adequate;
- raw packages, summaries, provenance, process identities and derived qualification analysis independently reconcile.

A LIMITED PASS admits only the named scope: the verified RTX 2060 SUPER, sustained D1-style device-resident sequences represented by D1 `(1048576,64)`, instrument mode H, metric `host_completion_ns`, the qualified common W, ordinary measured count 100, fresh OS process as the independent experimental unit, and the frozen counterbalanced same-GPU CUDA/Vulkan process-block procedure. It explicitly excludes A1/256 host-latency ranking, `host_submission_ns` ranking, `host_wait_ns` ranking, CUDA-event versus Vulkan-timestamp ranking, N/P timings as candidate-performance evidence, other GPUs and other A–E workload families until separately qualified.

Issue **FAIL** for the proposed D1 H comparison scope when the campaign executes validly but D1 fails a scientific measurement-admission criterion—for example no common `W<=16`, failure of 100-sample convergence, persistent within-process drift/state switching, `R_process>1.10` for either backend, or inadequate host-clock resolution. A FAIL triggers no automatic root-cause investigation, threshold relaxation, additional warm-up, sample-count expansion or broader matrix.

A Stage-5 LIMITED PASS does not itself grant Stage-2 measurement-execution authorization. Human review must still freeze the resulting admitted scope in a clean committed specification and explicitly authorize Stage-2 candidate-performance execution.

#### Stage-6 consequence

Gate 0 controls claim strength, not whether ComputeLab may learn anything further.

- After a Stage-5 LIMITED PASS and explicit Stage-2 authorization, Stage 6 may collect candidate-performance evidence only inside the admitted D1/H host-completion scope and any additional workload-family scopes that later pass the same bounded applicability method.
- D1 sustained is already the admitted reference class after LIMITED PASS. A/B/C/E representative classes require their own explicitly labeled bounded applicability qualification before their Stage-6 candidate cells may use H `host_completion_ns` comparatively.
- A1/256 remains diagnostic-only unless a future amendment explicitly qualifies tiny host latency.
- If Stage 5 yields Gate-0 FAIL for D1 H, comparative candidate-performance ranking remains blocked. Stage 6 may proceed only after a separate human-reviewed scope freeze as descriptive/diagnostic infrastructure characterization; such work may study process/driver behavior, resource/submission mechanics, within-backend scaling, contention, transfers, native explanatory timing and tooling, but cannot claim that CUDA is X% faster/slower than Vulkan.
- Nothing in v1.2 authorizes D2, Step 10b, production backend selection or production-runtime architectural conclusions.

#### v1.2 compatibility and evidence identity

v1.2 changes no schema-v2 column or mandatory standard file. The existing four-file package remains authoritative; `warmup.csv` remains the additive qualification diagnostic artifact where warm-up characterization is performed. New Stage-5 packages and derived analysis must identify `protocol_version="1.2"` and `evidence_kind="qualification"` and must retain exact source/build/binary/shader/GPU/process provenance. Historical v1.0 and v1.1 package identities, B/C digest meanings and evidence remain unchanged.

Where the historical v1.0 Gate-0 body below conflicts with this scoped amendment for new Stage-5 qualification evidence, **v1.2 governs only the named Stage-5/Gate-0 clauses above**. In particular v1.2 supersedes v1.0's Stage-5 sequencing, mandatory H/N perturbation prerequisite, optional 200-sample ordinary fallback, one-time borderline-effect extension for this Stage-5 slice, and the possibility of a full Gate-0 PASS from this bounded campaign. All other compatible v1.0 rules remain inherited.

### Scoped v1.3 Stage-6 full descriptive/diagnostic screen amendment

**Specification v1.3 scope — additive revision, READY FOR HUMAN REVIEW. Design freeze date: 2026-10-02.** On human acceptance, this amendment freezes the accepted DR-44 Stage-6 diagnostic protocol and applies only to **new Stage-6 diagnostic evidence**. It does not reverse Gate-0 FAIL, grant Stage 2, admit comparative candidate-performance evidence, change A–E or generator semantics, change schema 2, rewrite v1.0/v1.1/v1.2 evidence, or implement or execute Stage 6. After human acceptance the exact state is **PROTOCOL FROZEN / IMPLEMENTATION NOT STARTED**.

#### Historical evidence and decision anchors

Stage 4 remains correctness PASS at source `8eb7f3654e83073ece4d7ef2a8a5c2301cafe793`, with curated evidence at `24558d0d121a41e3d92f407f804ffbd1154c24b3`. Stage-5 v1.2 was frozen at `0f8a82838bd601f6662272b36d3acb15f4da10db`. S5-E1 source `70859055bf4280f5aeacd8039e9d3c41c9487e84` and curated evidence `a5694b6afa918499607949ec0c60d63d21be1270` establish a valid complete scientific nonqualification: ten valid D1-W inputs, five without qualifying `W<=16`, null common W, false warm-up/scope qualification and D1-S `skipped_by_protocol`. Gate 0 remains **FAIL for proposed D1/H `host_completion_ns` comparative scope**. DR-41 is Retired; DR-43 is Accepted/completed; DR-44 is Accepted. Stage 2 is NOT GRANTED, D2 is conditional/unimplemented, and Step 10b is not passed.

Stage 6 is **not a Gate-0 retry**. A1 and D1 appear because they belong to the core portfolio. Their diagnostic observations do not rerun S5-E1, requalify W, replace Stage-5 evidence, repair Gate 0, create a new Gate-0 verdict or retroactively qualify D1/H for comparative inference.

#### Complete portfolio, physical GPU and fresh-process topology

The complete existing `ApprovedCoreCells()` set is mandatory for v1.3 diagnostic evidence, not a menu: **22 core cells** with unchanged variants, N/K/sizes, patterns, transfer directions and operation semantics.

| Family | Cells | Existing conditions retained |
| --- | ---: | --- |
| A1 | 3 | N = 256, 262144, 16777216 |
| A2 | 2 | N = 262144, 16777216 |
| B1 | 3 | (262144, structured), (262144, shuffled), (16777216, shuffled) |
| B2 | 3 | (262144, structured), (262144, shuffled), (16777216, shuffled) |
| C | 3 | N = 1048576; active counters = N, N/32, 64 |
| D1 | 2 | (N=262144,K=16), (N=1048576,K=64) |
| E1 | 3 | H2D; S = 1024, 1048576, 67108864 bytes |
| E2 | 3 | D2H; S = 1024, 1048576, 67108864 bytes |

E3 is a view over real operations and adds no cell. D2 and the A1 128-MiB-per-buffer confirmation extension are excluded. The initial diagnostic GPU is exactly **RTX 2060 SUPER**, UUID `0340eaac-dc67-f450-d558-d47c55cc4417`. CUDA and Vulkan must resolve to that same physical UUID before later execution; no substitute GPU is allowed. Stage-7 cross-generation work remains separate.

Each cell has five chronological paired blocks, with a fresh OS process for each backend/cell condition and sequential execution:

| Block | Backend order |
| ---: | --- |
| 0 | CUDA -> Vulkan |
| 1 | Vulkan -> CUDA |
| 2 | CUDA -> Vulkan |
| 3 | Vulkan -> CUDA |
| 4 | CUDA -> Vulkan |

This is **10 fresh processes/cell, 220 fresh processes total**. Preserve existing process/block/order identities. No process pooling, hand-picked subset, simultaneous backend load, unplanned extension or automatic retry is allowed.

#### H-only instrument, production build and inherited timing

The mandatory core freezes `instrument_mode="H"`. Mode N is excluded from the core: there is no 440-process H+N campaign and no currently planned native-Mode-N prompt. Future N diagnostics require a separate question, review and authorization.

The sole curated S6-E1 production preset is **`x64-release`**. Debug may later support implementation/unit/smoke work but cannot produce the curated S6-E1 campaign. For ordinary curated H, external Vulkan/API validation layers, profiler collection and tracing are **OFF**, truthfully declared; external instrumentation cannot become timing evidence. Experiment-owned per-operation correctness validation remains **MANDATORY**, independently of those external flags.

H retains the existing boundary without redefining it: capture `t0`; submit/enqueue the declared complete operation; capture `t1`; immediately wait for completion; capture `t2`. When valid:

```text
host_submission_ns = t1 - t0
host_wait_ns       = t2 - t1
host_completion_ns = t2 - t0
host_completion_ns = host_submission_ns + host_wait_ns
```

Route-specific preparation remains before `t0`; readback/correctness remains after `t2`. A/B/C/D1/E operation boundaries and backend-native command/launch/copy semantics remain unchanged. The three host metrics are applicable when truthful. `native_device_interval_ns` is null/empty/inapplicable in H, **never zero**; no native interval is fabricated or inferred.

#### Fixed diagnostic sequence and per-operation correctness

Freeze **`warmup_count=0`** and **`planned_sample_count=100`**. Normal process/setup initialization occurs before the sequence; the first declared complete workload operation after normal setup is `sample_index=0`. A fully successful process records indices **0..99**. Zero warm-up deliberately preserves initial process-state behavior; it is not a claim that zero warm-up is adequate for comparative performance. Early behavior is data. No unrecorded workload iterations may be inserted to stabilize timing.

There is no 48-operation qualification, W search, selected/common W, D1-W/D1-S, 50/100/200 convergence, 200 fallback, 400 doubling, timing-triggered extension or adaptive sampling. The fixed count 100 is diagnostic, not a new qualification verdict or stability gate.

Every completed operation must be validated against the existing independent CPU oracle and frozen semantics, including complete output comparison and required input preservation/invariants. An incorrect operation is not successful timing evidence. If sample j fails validation, retain all prior rows and the failing row truthfully, stop that child, and do not execute j+1..99. Never delete or outlier-filter the failing row. Later predeclared slots may continue only under the safe-continuation criterion below.

#### Diagnostic evidence identity, packages and raw retention

New Stage-6 packages use exactly these protocol values:

```text
protocol_version = "1.3"
schema_version = 2
evidence_kind = "diagnostic"
instrument_mode = "H"
warmup_count = 0
planned_sample_count = 100
```

`diagnostic` extends the `evidence_kind` vocabulary only for new v1.3 Stage-6 evidence. Historical `correctness`, `qualification` and `candidate-performance` meanings and identities remain unchanged; no relabeling or schema bump occurs. Preserve canonical logical-input/condition/series/run identities and exact source/build/binary/shader/GPU/process provenance.

Exactly four standard files remain required: `environment.json`, `initialization.csv`, `samples.csv`, `summary.json`. Neither `warmup.csv` nor `host-clock.csv` is mandatory for Stage 6. Initialization remains separate from measured samples, with truthful observations and no invented zero setup durations. The exact existing `samples.csv` v2 header below is unchanged: no added, removed, reordered or renamed column; existing null semantics remain authoritative. H native-device fields remain empty/null.

Separate **retention** from **summary eligibility**:

| Class | Examples | Retain raw evidence | Successful-operation timing summaries |
| --- | --- | --- | --- |
| A: correctness/execution-invalid | `validation_failed`, `submit_failed`, `wait_failed`, `timeout`, `device_lost`, `incomplete` | YES | NO |
| B: correctness-valid completed, unusual timing | very slow/fast, early/late state, drift, abrupt shift, multimodal behavior, extreme valid process median | YES | YES |

All failed/missing/partial outcomes remain truthful evidence with applicable timing components or nulls. Only fully completed correctness-passing `ok` rows enter successful-operation summaries. Every valid unusual timing remains in its diagnostic distribution; Stage 6 has **no statistical-outlier deletion/filtering mechanism** and never drops an extreme valid process.

A package may be **FINAL** with fewer than 100 successful operations. For example, `planned_sample_count=100`, `recorded_sample_count=18`, indices 0..16 `ok`, and index 17 `validation_failed` can form a truthful final diagnostic-failure package. **FINAL does not mean success**. `.incomplete` denotes lifecycle/publication/control uncertainty, not merely fewer than 100 successes. Pre-foundation failures cannot fabricate plan-bound rows, input/oracle identities or setup completion. Exact publication mechanics belong to S6-I2/I3.

#### Slot resolution, safe continuation and campaign integrity

Conceptual slot states are **`resolved_success`**, **`resolved_diagnostic_failure`**, **`unresolved_campaign_fatal`**, and **`not_launched`**; exact enum/member serialization is deferred. A campaign is **COMPLETE** only when all 220 declared slots are truthfully resolved; COMPLETE does not mean 220 successful processes. It is **INCOMPLETE** when campaign-integrity/control is lost before the complete schedule can be safely resolved. Coverage distinguishes `declared_slots`, `attempted_slots`, `resolved_slots`, `successful_slots`, `diagnostic_failure_slots`, and `unlaunched_slots`. No ambiguous global PASS is issued.

A terminal slot-local diagnostic failure permits the next declared slot only if the future supervisor can establish **all** of: child terminated; no operation outstanding; completion state known; future GPU execution safe; progress/control prefix trustworthy; truthful terminal evidence durably captured; and supervisor control remains durable. Otherwise stop and mark the campaign INCOMPLETE. No automatic retry occurs.

Potentially slot-local outcomes, only when safely resolved, include clean initialization failure; clean resource/capability failure; deterministic input/configuration failure; correctness failure after known completion; readback failure after known completion; clean pre-submission failure; and child-local evidence publication failure **only if trustworthy external evidence fully resolves that slot**. A failure name alone never grants continuation.

Campaign-fatal classes include source/provenance drift; wrong/mismatched GPU; control identity corruption; malformed progress/control; operation/child timeout with uncertain completion; campaign deadline; device loss; completion uncertainty; abnormal death with work outstanding; containment failure; unknown descendant survival; inability to maintain durable supervisor state; and supervisor ledger publication failure. Preserve partial evidence and unlaunched slots truthfully.

#### Pure descriptive analysis and claim firewall

S6-I1 owns a **pure Stage-6 analysis layer** for descriptive/statistical logic. The future S6-I3 supervisor may determine when independently inspected inputs are ready, supply pure inputs, invoke analysis, and publish/hash-anchor outputs; it must **not own statistical formulas**.

For each process, successful H rows use existing shared algorithms and null semantics for sample count, minimum, median, mean, sample standard deviation, coefficient of variation and p95 for `host_submission_ns`, `host_wait_ns`, and `host_completion_ns`. Preserve process grouping and original sample order; do not pool processes into a primary sample group. Four positional windows are fixed by original `sample_index`: **0..24, 25..49, 50..74, 75..99**. Each may report `successful_row_count` and the successful-row `host_completion_ns` median or null. Failed/missing rows do not shift later rows into earlier windows. No 5%/10% stability threshold or qualification verdict is attached.

For each backend within one cell, canonical analysis may represent five process identities, five terminal states, recorded/successful/failure counts, process H summaries and diagnostic failure counts. The threshold-free diagnostic

```text
process_median_span_ratio = max(five process host_completion medians)
                            / min(five process host_completion medians)
```

is applicable **only if all five processes have complete 100-successful-row H series and valid positive medians**; otherwise it is null/inapplicable. Do not call it `R_process` or attach `<=1.10` or any Stage-5 qualification threshold.

Canonical Stage-6 derived analysis must not define cross-backend fields such as `cuda_over_vulkan`, `vulkan_over_cuda`, `speedup`, `percent_difference`, `winner`, `loser`, `paired_ratio`, `aggregate_backend_score`, or `backend_rank`. Raw evidence remains available. Canonical interpretation cannot claim a CUDA/Vulkan winner or speedup, production backend selection, Stage-2 comparative admission or production-runtime architecture/performance.

Within one backend, diagnostic interpretation may describe A1 scaling across N, C contention regimes, E transfer-size behavior, submission/wait/completion structure, process-state variation and other frozen-cell scaling behavior. These observations do not become cross-backend candidate-performance claims.

Future derived identities are **`cell-00-analysis.json` through `cell-21-analysis.json`**, plus **`campaign-analysis.json`**. Analysis must be independently regenerable from exact referenced raw-package hashes/resolved inputs and cannot replace raw evidence. C1 freezes identity/purpose only; full JSON member layout and exact serialization belong to S6-I1 and are not implemented here.

#### Immutable manifest, ledger ownership and external safety ceilings

The future campaign uses **one immutable full production manifest**, 22 cell groups, ten predeclared child identities per group and 220 declared children, sequential fresh-process execution, all-or-nothing preflight before child zero, no arbitrary subset, no dynamic addition and no retry. The manifest is control/provenance, not a scientific configuration surface: it cannot override cell definitions, sample/warm-up counts, H-only mode, backend order, thresholds, analysis formulas or continuation policy. Exact JSON schema belongs to S6-I3.

The future supervisor ledger owns manifest identity/hash; source/build/GPU provenance; deadlines; all 220 slots; launch/exit; progress; package inspection; slot resolution; continuation decisions; cell-analysis hashes; campaign-analysis hash; and terminal campaign state/fatal reason. It does not duplicate 22,000 raw timing values or own statistics. Exact ledger schema and durable mechanics belong to S6-I3.

Preserve external maxima: **operation <=60 seconds, child <=20 minutes, campaign <=24 hours**. These are maximum control envelopes, not exact S6-E1 production values. S6-E1 freezes those values after implementation review. No result automatically expands a deadline; no retry follows a deadline.

#### Human-controlled increment sequence and future ownership

| Increment | Purpose and future ownership | State after human acceptance of C1 |
| --- | --- | --- |
| S6-C1 — Protocol 1.3 diagnostic-contract freeze | This normative EX-2 amendment only | Protocol frozen |
| S6-I1 — Diagnostic plan/evidence/analysis foundation | `Ex2Stage6Plan`, `Ex2Stage6Evidence`, `Ex2Stage6Analysis`; pure protocol/data/math, no GPU/process control | NOT STARTED |
| S6-I2 — Full A–E Mode-H diagnostic child | `Ex2Stage6Execution`; one H child over existing routes, 100 attempted sample indices subject to terminal-failure stop, correctness, truthful package lifecycle, Started/Returned observer seam; no campaign | NOT STARTED |
| S6-I3 — Diagnostic supervisor/control + independent inspection | `Ex2Stage6Progress`, `Ex2Stage6Supervisor`; immutable manifest/preflight, sequential fresh-process control, independent disk inspection, continuation state machine, durable ledger, analysis publication/hash anchors; no supervisor-owned statistics or campaign evidence | NOT STARTED |
| S6-E1 — Full 22-cell / 220-process diagnostic campaign + acceptance | One clean reviewed implementation source, one immutable production manifest, one 220-process H attempt, independent audit, curation and bounded diagnostic interpretation | NOT EXECUTED |

There is **no planned S6-E2** and **no planned native-Mode-N prompt**. Correction/continuation prompts occur only if human review finds a real defect. S6-I1 starts only after human review and a separate accepted commit. This amendment does not perform any later increment.

#### v1.3 compatibility and explicit supersession

For **new v1.3 Stage-6 diagnostic evidence only**, this amendment supersedes historical clauses that conflict with the accepted diagnostic branch: Stage-6 execution gated on Gate-0 LIMITED PASS; family-specific comparative admission before the full diagnostic screen; warm-up qualification and 100-vs-200 convergence prerequisites; H/N perturbation qualification for this H-only screen; the historical 10% cross-backend effect requirement as an execution gate; the borderline paired-block extension; candidate-performance failure-stop behavior conflicting with safe diagnostic continuation; omission of `diagnostic` from the evidence-kind vocabulary; and treating the 22 diagnostic core cells as an optional menu. Those historical rules and thresholds retain their meanings for historical evidence and comparative admission; they are not relaxed into new performance claims.

This amendment does **not** supersede A–E semantics, `ex2-mix64-v1` constants/semantics, the exact 22 core cells, CPU oracles, logical-input identity, the same-GPU rule, H `t0/t1/t2`, route operation semantics, mandatory correctness, schema 2, the exact `samples.csv` v2 header, summary formulas/null rules, process-pair identities, historical v1.0/v1.1/v1.2 evidence, EX-1 history, Stage-4 correctness history, Stage-5 qualification history or curation principles. Compatible historical rules remain inherited. Gate-0 FAIL and the comparative-claim firewall remain authoritative.

## Question

Are native CUDA and Raw Vulkan Compute credible initial generic infrastructure candidates on the available NVIDIA systems, and what bounded trade-offs arise from contiguous and indexed memory access, contention, dependent dispatch, transfers, host submission/completion and engineering workflow?

## Purpose and interpretation boundary

Deliver correct, attributable, reproducible, multidimensional engineering evidence for named generic operations. EX-2 may inform a **provisional production starting path**; it cannot determine another application's architecture, performance or final runtime backend. Every executed condition must answer a named infrastructure question. No automatic full factorial expansion or universal CUDA/Vulkan performance score is allowed.

The five-family suite is synthetic engineering stress, not a model of any consuming application. Only measurements, compatibility observations, limitations and engineering conclusions may leave ComputeLab; never copy its source, headers, schemas, APIs or architecture into production.

## Hypothesis

Both APIs may execute the stated generic operations correctly on accessible NVIDIA generations, while differing in dispatch, access patterns, atomics, dependency handling, transfers and tooling. The hypothesis does not presuppose a winning backend or any neural-runtime effect.

## Non-goals

Do not implement neural/biological entities, renamed production models, application schedulers, learning/plasticity, execution images, production checkpoints, spatial neighborhoods/indexes, visualization, compute/graphics interop, application graph traversal, multi-GPU causal execution, complex reduction or matrix/tensor accelerators. EX-3 owns generic visualization interoperability, EX-4 conditional spatial indexing and EX-5 conditional bulk persistence. No inference of AMD or Intel portability from NVIDIA-only tests. Do not build a reusable GPU framework or `IGpuBackend`; shared **semantics** do not imply identical CUDA and Vulkan internals.

## Baseline, hardware and implementation separation

Audit starting revision: `e0b13968ffecce5913baa4619b8c6e3a4bed4eae`. The operator-supplied read-only Codex audit reported clean source and 112/112 canonical Windows tests passing; this is an audit report, not new EX-2 evidence. Before editing, inspect actual `HEAD`, local changes and committed `docs/ex2.md`: the uploaded document and a local addition need not be visible through the GitHub default-branch API. Do not overwrite a newer local document or commit on the user's behalf.

Use the existing C++20/CMake Windows ComputeLab shell. Reuse `src/timing`, build presets, test framework, environment/UUID acquisition and genuinely compatible input/result helpers. Preserve `src/ex1`, `Ex1Main.cpp`, old CUDA/Vulkan transform operations and all old v1 serializers. Add EX-2-specific experiment control, independent CPU oracles, native CUDA kernels/operations, native Vulkan shaders/resources/commands and tests. Avoid generic cross-backend operation interfaces and unnecessary refactors. The canonical local validation is ` .\scripts\test.ps1 ` (run without the surrounding backticks/spaces). Do not bypass GPU tests because a sandbox lacks Windows tools; report that environmental blocker.

Initial direct-comparison system: RTX 2060 SUPER/Turing. RTX 3060 Ti/Ampere and RTX 5060/Blackwell are later optional accessible hosts. Match the same physical GPU via CUDA/Vulkan UUID before every direct pair; GPU ordinal 0 is insufficient. Record a stable local UUID or unambiguous derived comparison identity alongside anonymous machine ID, driver, SDK/toolkit, compiler, source revision, binary/shader hashes and environment. Reject comparisons if UUID matching fails; do not silently select another adapter. Probe memory/dispatch/resource limits before allocating a selected condition.

## Canonical numeric and input contract

A–D operate on `uint32_t`; all explicitly arithmetic transformations use modulo 2^32 unsigned semantics. GPU and CPU use exact integer operations and identical logical inputs. Bounds guard padded GPU invocations (`i >= N` does no work). Core performance sizes are positive; N=0 and K=0 belong to isolated API/oracle correctness tests, not measured performance. All inputs are generated on CPU before timed regions and retained/reconstructible through seed, generator version and SHA-256 input digest. No undefined signed-overflow behavior, unprotected conflicting writes or uninitialized output is permitted.

**Generator `ex2-mix64-v1` (fixed, independent of C++ library distributions):** calculate in unsigned 64-bit arithmetic, with every operation modulo 2^64:

```text
Mix(seed, i):
    z = seed + uint64(i) + 0x9E3779B97F4A7C15
    z = (z XOR (z >> 30)) * 0xBF58476D1CE4E5B9
    z = (z XOR (z >> 27)) * 0x94D049BB133111EB
    return z XOR (z >> 31)
Value(seed,i) = uint32(Mix(seed,i) AND 0xFFFFFFFF)
Byte(seed,i)  = uint8(Mix(seed,i) AND 0xFF)
```

Core seed is hexadecimal `0x0123456789ABCDEF` (stored as the unsigned decimal integer or a fixed 16-digit hex representation, never silently signed). The independent known-answer fixtures for Mix and each transform must be checked in pure tests **before GPU work**; test fixtures must not be generated by invoking the production oracle under test. An independent fixture-generation calculation may be retained as review evidence. Every CUDA/Vulkan comparison uses the same generated buffer bytes; hash actual generated inputs to detect seed/generator drift. The existing EX-1 seeded-input generator remains unchanged; EX-2's explicit generator is separate because its byte-identical algorithm is defined here.

**Fixed known-answer fixtures (hexadecimal, seed above):** these literal expectations were separately calculated from the written formula and must be hard-coded in tests rather than regenerated by the oracle being tested.

| i | Mix(seed,i) | Value | A1 result | A2 result | E byte |
| ---: | --- | --- | --- | --- | --- |
| 0 | `157A3807A48FAA9D` | `A48FAA9D` | `3AB8D324` | `4579A7C6` | `9D` |
| 1 | `9804297CC374CA1A` | `C374CA1A` | `5D43B3A0` | `6D06CDCF` | `1A` |
| 2 | `AF8D95523BECCAA2` | `3BECCAA2` | `A5DBB319` | `3BE3E55E` | `A2` |
| 3 | `CB4E5F6A912DCEF8` | `912DCEF8` | `0F1AB744` | `271DAF20` | `F8` |

For a small B correctness fixture only, `N=4`, `structured-v1` produces `index=[1,0,3,2]`, gather output `[5D43B3A3,3AB8D327,0F1AB743,A5DBB31E]` and scatter output `[5D43B3A0,3AB8D324,0F1AB744,A5DBB319]` (all 32-bit hex). For D with N=4 and the four initial `Value` words above, final words after K=1 are `[D664E09C,27C0F020,3AC0DF49,62A16496]` and after K=2 are `[89BDA0DE,BE3BAF8C,1E3F5AC9,120E2194]`. This N=4 fixture is correctness-only and does not extend the performance matrix.

## Workload portfolio — exact core semantics

### A — linear contiguous transform

`input[N]` and `output[N]` are 32-bit unsigned arrays. Each invocation i loads exactly one logical input, computes the following transformation, then stores `output[i]` once. No input mutation. A1 and A2 use identical data/layout and output ownership; they differ only in declared arithmetic. Both use the seed above and independently checked CPU references.

```text
A1: output[i] = input[i] XOR (0x9E3779B9 + uint32(i))

A2:
    x = input[i] XOR (0x9E3779B9 + uint32(i))
    repeat exactly 16 times:
        x = (x XOR (x >> 16)) * 0x7FEB352D
        x = (x XOR (x >> 15)) * 0x846CA68B
    output[i] = x XOR (x >> 16)
```

All 32-bit additions and products wrap. The CPU oracle uses explicit unsigned arithmetic; GLSL/SPIR-V and CUDA must match bit-for-bit. Generated PTX and SPIR-V should be reviewed during implementation to confirm the input-dependent arithmetic is not eliminated; do not require identical instruction counts. A1 nominal useful traffic = 8N bytes (one 4-byte load and one 4-byte store); this is **not** actual DRAM traffic or proof of bandwidth saturation. A2 reports elements/s and host/device metrics only where qualified; no invented FLOPS.

A1 core N = 256, 262144, 16777216. A2 core N = 262144, 16777216. A 128-MiB-per-buffer A1 confirmation point is a **predeclared qualification-only extension**, not automatically a core evidence cell. A test with N=257 verifies guard behavior but is not a performance cell.

### B — indexed gather and collision-free scatter

Buffers `input[N]`, `index[N]`, `output[N]`; index is a permutation of `[0,N)`. The common transformation is the A1 operation on the selected value using **logical invocation index i**; the same index argument is used for B1 and B2 so semantics are explicit:

```text
B1 gather:  output[i]       = input[index[i]] XOR (0x9E3779B9 + uint32(i))
B2 scatter: output[index[i]] = input[i]        XOR (0x9E3779B9 + uint32(i))
```

B1's output is indexed by i; B2's result is indexed by the permutation. Each output destination is written exactly once, so B2 contains **no write races or atomics**. Preserve input and index buffers. Validate range, completeness and uniqueness before any timed scatter.

Two fixed index patterns use the same seed and data:

```text
structured-v1: index[i] = (8191 * uint64(i) + 17) modulo N;
               require gcd(8191, N) = 1 or reject the configuration.
shuffled-v1:   index initially [0,1,...,N-1].
               state = seed (uint64).
               For i = N-1 down through 1:
                   state = state + 0x9E3779B97F4A7C15;
                   r = SplitMixFinalizer(state) using the two XOR/multiply
                       rounds and last XOR above, without an extra addition;
                   j = r modulo (i+1);
                   swap(index[i], index[j]).
```

The shuffled modulo selection has a small known modulo bias; this is a reproducible synthetic locality input, not a claim of uniform random permutations. Store generator ID and digest. Selected core cells for each of gather and scatter: `(N=262144, structured)`, `(N=262144, shuffled)`, `(N=16777216, shuffled)`. The large structured case and other distributions are not in the initial matrix. Report indexed elements/s, not an unqualified bandwidth-equivalence claim between gather and scatter.

### C — controlled 32-bit atomic contention

Exactly N = 1048576 invocations. Input `targets[N]`; allocate a fixed `counters[N]` array of zeros regardless of active counter count B. Each invocation performs one atomic `+1` of `counters[targets[i]]`. Let `p(i) = (8191*uint64(i)+17) modulo N` and `targets[i] = p(i) modulo B`. B is one of N (one update per counter), N/32 (32 per active counter), or 64 (16384 per active counter). Because N and B divide evenly and p is a permutation, occupancy is exactly balanced; allocated counter-buffer size stays fixed while the active working set changes intentionally. All counters outside `[0,B)` must remain zero.

The reference histogram must match **every counter**, not only its sum. Also assert `sum(counters) == N` using at least 64-bit host accumulation. No counter can overflow because N is less than 2^32. CUDA uses a 32-bit atomic increment at device scope with no inter-thread ordering requirement beyond atomicity; Vulkan/SPIR-V must provide the corresponding 32-bit storage-buffer atomic semantics and appropriate scope. Inspect the compiled SPIR-V and document any API-language semantic difference before comparing. Do not insert unrelated fences inside the kernel.

Before each complete C operation, clear the full counter array and complete the reset-to-dispatch dependency. Both reset and preparation are **outside** the measured atomic interval, with their implementation and timing noted separately if needed. Validate the whole counter array after each completed measured sample. C reports N atomic updates per qualified operation; no unsupported universal atomics score.

### D — resident iterative ping-pong state

Buffers `state_a[N]`, `state_b[N]`; before each operation initialize A to `Value(seed,i)` and make the upload/initialization complete outside t0. B needs no initial contents because each pass writes every valid element. Pass p = 0..K-1 reads exclusively from the prior buffer and writes the opposite buffer. The transform is:

```text
v = previous[i] XOR (0x9E3779B9 + uint32(i) + uint32(p))
r = (v << 5) OR (v >> 27)       // 32-bit rotate left five bits
next[i] = r + 0x7F4A7C15       // wrap modulo 2^32
```

No CPU interaction or transfer is allowed **between passes**. After K passes, even K means A is final; odd K means B is final. K=0 is a correctness-only identity case. Exact CPU reference calculates K iterations **once per condition**, stores expected final bytes, and validates each resulting GPU sample; it is not recomputed in the measured interval.

D1 required core cells: `(N=262144,K=16)` and `(N=1048576,K=64)`; the latter is a **sustained candidate**, not proof of device domination. CUDA issues ordinary K launches in one explicitly selected nonblocking stream, waits once at end. Vulkan resets and records its command buffer and K dispatches for each ordinary operation, with required compute-shader dependencies between passes, submits, and waits once. For ping-pong reuse, synchronize both prior shader writes→next shader reads and prior shader reads→later shader writes as required; the initial conservative compute READ|WRITE → compute READ|WRITE barrier may be narrowed only by an explicitly reviewed correctness-preserving amendment. Preallocated pipeline/descriptors/resources are excluded from t0. D1's per-operation command recording belongs inside host submission, so D1 and any replay mode cannot be treated as interchangeable measurements.

D2 is **NOT included in the v1 core campaign**. It requires a separate documented amendment following a qualified D1 result showing a meaningful host-construction/replay question. A CUDA executable graph and pre-recorded reusable Vulkan command buffer are then separately prepared outside t0; compare only named replay intervals. Do not implement D2 in the first Codex assignment or infer graph advantage before measuring.

### E — host/device boundary

E1 = a single S-byte H2D copy; E2 = a single S-byte D2H copy. S core = 1024, 1048576, 67108864 bytes. Bytes are `Byte(seed,i)` from the fixed generator. Sources, destinations, streams, command pools, mapped memory and device buffers are preallocated. CPU filling, CUDA pinned registration/allocation, Vulkan mapping/flush/invalidate, validation and serialization are **outside the named transfer completion interval** unless a dependency is specifically part of submitted copy work.

CUDA primary path: `cudaMallocHost`-allocated pinned source/destination, `cudaMalloc` device buffer, `cudaMemcpyAsync(..., declared_nonblocking_stream)`, followed by `cudaStreamSynchronize`. No fallback to pageable memory in a comparable cell; allocation failure makes the cell unavailable and is preserved as such.

Vulkan primary path: host-visible persistently mapped staging/readback buffer and a device-local working buffer, `vkCmdCopyBuffer` on the selected compute-capable queue, then fence completion. Allocate a compatible memory type and record HOST_COHERENT versus noncoherent flags. For noncoherent mapped H2D writes flush the atom-aligned necessary range before queue use; for noncoherent D2H reads invalidate the atom-aligned necessary range after completed transfer, observing a correct transfer-write→host-read dependency. A known GPU source for E2 is prepared and completed before t0. Vulkan copy commands are recorded once outside t0 and safely resubmitted only after the previous completion; this is a **declared prepared single-copy path**, distinct from D1's deliberately per-operation command recording. Compare logical transfer-to-host completion, not allegedly identical memory allocations. Copy/compute overlap and dedicated-transfer-queue optimization are excluded.

E3 is a **view over every real operation**, never a separate no-op dispatch: split ordinary host submission, immediate wait and submission-to-completion. `host_wait_ns` is observed wait duration, not pure synchronization overhead. H2D/D2H nominal throughput is S divided by a named qualified interval; don't conflate it with GPU memory bandwidth. A round trip is not core.

## Frozen cell list, footprint and feasibility

The v1 **maximum predefined same-GPU screen** has 22 backend-neutral cells: A1 3, A2 2, B1 3, B2 3, C 3, D1 2, E1 3, E2 3. These are *eligible cells*, not instructions to execute all of them automatically. Stage 6 must record the distinct question for each retained cell and its admissible metric before execution. A1 tiny is a correctness/host-behavior qualification case only: tiny CUDA/Vulkan host comparison is initially excluded. Stage 5 qualification slice is tiny A1 and D1 `(1048576,64)` on RTX 2060 SUPER. E3 applies to real cells and adds no extra cell. D2 and 128-MiB A1 confirmation require separate predeclared qualification/amendment, not silent automatic expansion.

Core maximum logical payloads before runtime allocation overhead: A = 8N bytes; B = 12N; C = 8N (fixed target and counter arrays); D = 8N; E = 2S minimum (host transfer buffer plus device buffer), possibly an additional readback buffer for H2D validation. For A1 N=16777216 the two buffers total 128 MiB, B the three buffers total 192 MiB, C total 8 MiB, D sustained total 8 MiB, and E at S=64 MiB requires at least 128 MiB plus validation resources. All counts exclude runtime, staging, alignment, shader, command and auxiliary allocations. Preflight actual device memory, host-pinning capacity, `maxStorageBufferRange`, compute dispatch counts, workgroup-size limits, timestamp properties and other required capabilities. Native 256-thread/workgroup execution width is an **implementation candidate**, not part of logical semantics; any different width must keep every invocation's work identical, record its choice and pass edge-size tests. Reject infeasible cells before timing, not by downsizing without a new condition identity.

A1 bandwidth qualification may optionally compare 64-MiB versus 128-MiB per-buffer points after Gate 0. The former may be labeled `sustained` only if both fit, the per-backend process-level host-throughput median changes by at most 5%, no meaningful process trend is present and the qualified interval is device-work-relevant. Otherwise call it `large candidate` and do not silently assert saturation. If the larger point cannot fit, saturation is unqualified; do not fail correctness merely for lacking it.

## Correctness and operation lifecycle

Correctness is a hard gate independent of profiling declarations. Validate buffers and invariants against an independent CPU oracle and fixed known-answer cases. Preflight invalid indices, duplicates in scatter, unsupported atomics, overflow, zero/oversized parameters and memory limits. After **every complete measured operation**: check submission and wait success; retrieve/read back final output outside t2; compare every output byte or element and supplementary invariants; record explicit `validation_passed`. Readback and per-sample validation will influence subsequent process state; apply this identical *logical* validation policy to both APIs and retain it as a protocol limitation. A sample cannot be accepted merely because an earlier smoke test passed.

Before every separately measured C operation, reset and complete counter initialization. Before each D operation, restore initial A and complete upload. For other workloads, source data is immutable and output ownership covers all N elements. Run CPU oracle once per fully identified condition and retain its SHA-256 expected-output digest; verify CPU golden fixtures independently. Boundary and intentionally malformed tests are untimed. For E1, read back the destination in a separate post-t2 validation operation; for E2, compare the returned host bytes. A failed sample is retained and invalidates that condition's performance interpretation pending correction and a new campaign; never silently replace it.

An asynchronous API error may surface on submit, wait, timer retrieval or readback; retain its phase and native error. Timeouts are unsuccessful outcomes, never durations. A timed-out Vulkan/CUDA operation is not assumed safe for immediate resource reuse/destruction; a supervising process may terminate the child and retain independently written partial evidence.

## Gate 0 — historical v1.0 qualification procedure, amended by v1.2

The body of this section preserves the v1.0 bounded measurement procedure and is retained as historical protocol text. **For new Stage-5 qualification evidence, the scoped v1.2 amendment above governs wherever it explicitly differs.** v1.0 still supplies inherited H timing boundaries, fresh-process pairing, warm-up/sample mathematics, evidence rules and other compatible mechanics.

At v1.0 publication there was no Gate-0 PASS. Root-cause diagnosis of EX-1 CUDA/256 was out of scope. EX-1's distinct process state remained visible, and tiny host-visible cross-backend claims were excluded by default. v1.0 originally required Gate 0 to establish a representative comparison scope before Stage 5 rechecked applicability. v1.2 changes that sequencing: Stage 5 now supplies the bounded evidence used to decide Gate 0.

### Timing mode and exact boundaries

Mode **H** (ordinary host-only, validation/debug/profiling externally disabled and truthfully declared) is the initial decision-metric candidate. For a real, prepared complete operation:

```text
outside: source generation, allocation, setup, completed reset/upload
  t0 = steady_clock capture immediately before declared per-operation calls
      submit/enqueue all declared operation work and completion signal
  t1 = steady_clock capture immediately after submission returns
      immediately execute backend-native completion wait; no unrelated work
  t2 = steady_clock capture immediately when a successful wait returns
outside: readback, correctness validation, output recording, serialization

host_submission_ns = t1 - t0
host_wait_ns       = t2 - t1
host_completion_ns = t2 - t0
```

All captures use the same monotonic clock with integer nanosecond conversion. The exact timestamp differences must reconcile arithmetically; overflow or decreasing timestamps reject the sample. A Vulkan submit/fence or CUDA stream completion must refer to the operation under test. A failed submit or wait is recorded without fabricating t2. Do not call wait pure synchronization overhead. For A/B/C, Vulkan may use pre-recorded correctly reset/reused commands outside t0 and CUDA native per-operation kernel launch inside; this deliberate native-path distinction is recorded and limits any interpretation as pure kernel overhead. For D1, CUDA K launches and Vulkan ordinary per-operation command buffer reset/record/barriers/submit are *inside* t0→t1. For E, Vulkan prepared one-copy command buffer is outside t0 and CUDA native copy enqueue inside. Setup may not be opportunistically moved across t0 between runs.

Mode **N** (host plus device markers) records a separate instrument condition. Include **all per-operation** marker enqueue/API calls inside the declared submission scope; marker retrieval is after t2. For a reusable Vulkan command buffer, one-time recording of timestamp commands is preparation **outside** t0, while submitting that recorded command buffer is inside t0→t1. This is a declared difference in the H/N *whole execution condition*, not an assertion that only marker overhead changed. For CUDA, use same-stream start and stop events around exactly the named dispatch/sequence/copy, then completion wait. For Vulkan use supported queue timestamp queries with recorded start/stop stages and query readiness after fence. Metadata records stage masks, timestampPeriod, valid bits, conversion, rollover/precision and query availability; invalidate duration if no valid unwrapped interval can be established. Do not manufacture `device_execution_ns` from `host_completion_ns`. Do not treat H/N duration distributions as interchangeable without the perturbation check. Mode **P** is profiler/validation instrumentation, diagnostic only and separately declared; it never substitutes for ordinary evidence.

**Gate 0A initial scoped decision:** Native CUDA event and Vulkan timestamp intervals are **not admitted for direct cross-API scalar comparisons in v1.0**. They are optional backend-native explanatory/within-backend metrics only, subject to honest marker scopes and validity. A future explicit Gate-0A amendment can qualify a named identical-scope interval. The exclusion is a defensible comparison policy, not a claim that native clocks are broken. Host-only qualified operation completion is the first intended cross-API ruler; if it proves unusable for an infrastructure question, that question is inconclusive rather than automatically rescued by unqualified device timings.

### Fresh processes, ordering and independent samples

One fresh OS process owns **one** fully identified backend/cell/instrument condition. Five temporal paired blocks are the initial budget: one new CUDA process and one new Vulkan process per block on the same physical GPU, with fixed recorded order `CUDA,Vulkan; Vulkan,CUDA; CUDA,Vulkan; Vulkan,CUDA; CUDA,Vulkan`. Blocks are chronological; no overlap or simultaneous CUDA/Vulkan load. Process medians/ranges and paired block ratios are primary descriptive evidence; the 100 in-process samples are not 100 independent process replicates. Do not pool across fresh-process identities as primary inference. Record anonymous process index 0–4, block 0–4, order slot 0/1, timestamps, backend UUID matching and exact executable/shader hashes.

For H/N perturbation qualification, create **separate five-block pairs of H versus N within each backend** under a representative generic operation, with counterbalanced mode order; do not falsely pair CUDA H to Vulkan N as an instrument-control comparison. P runs are diagnostic-only and do not enter this comparison. A proposal to add five more CUDA/Vulkan blocks for a borderline cell is allowed only once by the trigger below, never to wash out a persistent distinct process state.

### Warm-up qualification (predeclared, nonoverlapping)

For each representative workload class, each fresh qualification process first retains **48 ordered complete-operation diagnostic timings**. The original discussion's 32 observations were insufficient to evaluate candidate warm-up 16 with two disjoint eight-operation windows and a disjoint late reference; 48 fixes this methodological overlap. Candidate W = 0, 1, 2, 4, 8, 16. For each W, compare medians for `[W,W+8)` and `[W+8,W+16)` with the completely disjoint reference `[40,48)`. Also compare `[32,40)` against `[40,48)` for late drift. A candidate qualifies within a process only if all three median differences are at most **5%** relative to the late reference, host timer resolution permits a meaningful 5% distinction and there is no clear persistent within-process trend or abrupt state switch. The first candidate meeting the rule is that process's W; the pair uses the maximum W found across both backends and all qualification processes. These thresholds are **a chosen screening policy to be validated, not evidence of stability**. If none W<=16 qualifies, stop that class as unqualified; do not arbitrarily add warm-ups. Retain all 48 diagnostic observations separately from accepted steady-state samples.

One *complete* operation means A/B one dispatch; C reset+atomic+wait; D the full K-pass sequence; E a one-way copy+wait. C/D setup is excluded from the measured dispatch interval but is included in the *complete warm-up operation*. Stable within-process timing does not establish stable *between-process* states. Tiny host comparisons stay excluded even if this test passes.

### Sample-count qualification

On representative accepted classes, use **separate fresh sample-qualification processes**, apply exactly the selected common W (not all 48 characterization operations), then collect **200 ordered complete measured operations** per process. Compute each process's prefix medians at 50, 100, 200. Candidate 100 is accepted for that class/metric only if `abs(median100/median200 - 1) <= 0.02`, `abs(median50/median200 - 1) <= 0.05`, and four consecutive nonoverlapping 50-sample windows show no systematic drift of more than 5% across the first and last window. These numerical thresholds are predeclared *engineering tolerances*, not proof of independent samples or exact population parameters. Require 200-sample result to be nonzero and resolved relative to the clock's effective precision. If any qualification process fails, 100 is not qualified: either explicitly approve 200 as a new ordinary count after showing adequate convergence or mark the class/metric unqualified. Do not keep doubling samples until the desired direction emerges. Ordinary cells use 100 measured operations **only where this procedure qualifies them**; otherwise no candidate campaign for that metric.

### Instrumentation qualification

For the representative operation, compare H and N in five fresh paired mode blocks *within each API*, using the same seed, logical operation, clock and completion primitive. Record differences in process-level host-completion and host-submission estimates, ordering, and mode-specific resource/command changes. Treat a consistent >=5% median shift or any new distinct process-state regime as materially perturbing for this screening policy; report H and N separately and admit only H-based host metrics if H itself qualifies. A smaller observed shift is **not** proof that N is perturbation-free in all workloads; N stays explanatory in v1.0 regardless. External profilers never supply ordinary timing. Do not run a separate CUDA/256 root-cause campaign.

### Practical effect, process uncertainty and extension

The v1.0 screen's predeclared **practically meaningful host-completion difference is 10%** for a named same-operation, same-GPU comparison; effects below that may be reported descriptively but cannot establish a provisional backend distinction. For a preliminary bounded effect, each of the five paired *process-median* completion ratios must show the same direction and at least a 10% ratio magnitude (`>=1.10` or `<=1/1.10`), with no separately observed persistent process-state split or failed warm-up/instrumentation qualification. Publish all five ratios and ranges; this strict criterion is descriptive consistency, **not** a confidence interval, p-value or guarantee against rare process states. If four of five ratios meet one directional threshold, one is inconclusive but not a separate multimodal regime, and no correctness/contract issue exists, one **predeclared extension of five paired blocks** may be run. To describe an extended effect require at least nine of ten ratios meeting the same threshold/direction and all leave-one-block-out medians on the same threshold side; disclose any dissent. If separate persistent states appear, a process fails, extension still disagrees or 10% separation is absent, report that metric/cell *inconclusive*. Never reject a process because its value is extreme. Submission and wait are reported separately; their direct comparison needs its own justification and cannot piggyback on a host-completion verdict.

For transfer or useful-throughput questions, define the effect on **the exact same admitted host-completion interval** with equal logical work; do not invent separate arbitrary thresholds. For native intervals, report only descriptive within-backend scaling, not a cross-API performance winner. No claim about tiny host latency is admitted at the initial gate. Requalification is required when driver/toolchain, hardware or timing/operation boundaries materially change.

### Gate-0 bounded execution and verdict

Only separately authorized generic **qualification campaigns** may collect qualification timing evidence before the gate, using representative non-neural arithmetic/transfer work. Correctness-only execution of A–E implementations is separately permitted by the sequencing decision; it is not qualification or candidate-performance evidence. The initial qualification instrument should reuse existing verified semantic operations where possible and add only the H/N host-timing capability needed; implementing the A–E suite does not itself qualify the measurement method. Fix exact representative conditions, source/build hashes and machine before launch. For each representative class, warm-up characterization uses five paired processes with 48 early operations; **new** five paired processes then use the selected W and up to 200 sample-count observations each. H/N adds separate five-mode-block controls within each backend. These process groups are distinct and their results cannot be pooled as interchangeable replicates. When a new A–E family is introduced after Gate 0, qualify its warm-up/sample applicability with the same bounded method as an explicitly labeled qualification task before its Stage-6 candidate cells; Stage 5 initially performs that confirmation for A1/D1. No CUDA/256 diagnostic escalation, broad parameter sweep or profiler campaign is authorized by this specification. Every qualification package is labeled `qualification` and **cannot later be relabeled candidate-performance**.

Gate-0 review must state one of **PASS**, **LIMITED PASS**, **FAIL**, with the precise admitted host metrics/operation classes/hardware/instrument mode and explicit excluded conditions. It requires exact correctness, physical-GPU matching, valid boundaries, adequately controlled process uncertainty, justified warm-up/sample count, and H/N policy. For this initial contract, Gate 0A's native cross-API comparison is intentionally excluded. If the representative interval is not interpretable, fail or issue a narrower evidence-supported LIMITED PASS; do not assert qualification by policy declaration alone. Later Stage-5 tiny A1 and sustained D1 qualification rechecks applicability and can revoke/narrow admission before Stage 6.

## Evidence: EX-2 schema v2 (EX-1 schema v1 remains immutable)

Keep the four-file package: `environment.json`, `initialization.csv`, `samples.csv`, `summary.json`. All EX-2 records use `schema_version: 2`; the EX-1 v1 schema, existing meanings and historical serializer are unchanged. Implement EX-2-specific record types/serializers rather than silently reusing EX-1 sample structs with changed meanings. File formats and statistics obey `docs/results-format.md`: integer nanoseconds, UTC timestamps, UTF-8 RFC 4180 CSV, lower-case booleans, explicit empty/null inapplicable values, deterministically regenerated stats over validation-passing rows, IEEE-754 binary64 accumulation in sample order, n-1 sample SD and nearest-rank p95. Do not include private hostnames, usernames, filesystem paths or credentials.

**Identity:** one backend-neutral `comparison_condition_id` is SHA-256 over canonical UTF-8 JSON with lexicographically sorted keys, no optional/implicit fields, ASCII lowercase names, integer decimal values without leading zeros, and no insignificant whitespace. Fields: protocol version, machine_id, verified device UUID identity, workload/variant, N or S, seed and generator revision, B/index pattern/K where applicable, **logical** execution mode (`ordinary` or `prepared`), named operation boundary and instrument mode. The two backends share this condition ID only when all common semantic fields match; backend-native API strategy/stream/queue choice is recorded separately as the declared paired implementation mapping. `series_id` is SHA-256 over the canonical condition JSON plus backend, process_index, block_index, order_slot, warmup_count, planned_sample_count, source revision and executable/shader hash. `run_id` is a collision-checked package-unique anonymous identifier. Reject duplicate IDs/path collisions; no truncation of identity hashes. Use explicit structured fields; don't bury independent variables in free-form `variant`.

`environment.json` includes all original required fields (schema version 2) plus `protocol_version`, `evidence_kind` (`qualification`, `correctness`, or `candidate-performance`), `comparison_condition_id`, `series_id`, `block_index`, `process_index`, `order_slot`, `instrument_mode`, `gpu_uuid_identity`, backend-native queue/stream and memory properties, exact executable and shader SHA-256 digests and input/expected-output digests. Values genuinely inapplicable are JSON null. Persist selected UUID identity consistently without exposing personal host identifiers. A qualification or dirty run remains under `results/local/`.

`initialization.csv` retains the baseline column set and records actual ordered first-use/setup observations per fresh process, separately from timing samples. At minimum write one truthful `setup_complete` qualitative row if initialization was not timed; do not invent zero durations or omit the required file. Diagnostic warm-up timings are stored in an additional `warmup.csv` with its own documented v2 header (`schema_version,run_id,series_id,process_index,sequence_index,host_submission_ns,host_wait_ns,host_completion_ns,status`) when a warm-up qualification is performed. This is an additive diagnostic artifact, not a fifth universally required standard file.

`samples.csv` v2 exact header (empty CSV fields represent null):

```csv
schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns
```

`sample_index` is zero-based; all execution parameters are explicitly populated or null as appropriate. `status` is `ok`, `validation_failed`, `submit_failed`, `wait_failed`, `timeout`, `device_lost`, `timestamp_invalid` or `incomplete`. `validation_passed` is `true`, `false` or empty when no result exists. `failure_phase`/`error_code` are empty when not applicable. Inapplicable native device timing is empty, never invented as zero. Transfer duration is the named E1/E2 host-completion interval, disambiguated by `transfer_direction` and `byte_count`, **not** an EX-1 `upload_ns`/`download_ns` reinterpretation. Only fully completed, correctness-passing `ok` rows enter the metric summaries. A timestamp-invalid row may be eligible for H host-only metrics only if recorded in a separate valid H condition; don't silently rescue a failed N sample by reassignment.

`summary.json` groups **one series/process at a time** using all of `comparison_condition_id`, `series_id`, `run_id`, backend, block/process identity and the full relevant independent variables. Each applicable metric (`host_submission_ns`, `host_wait_ns`, `host_completion_ns`, `native_device_interval_ns`) retains `sample_count`, minimum, median, mean, sample standard deviation, coefficient of variation and p95 with original algorithm/null semantics. Keep `recorded_sample_count`, `validation_failures`, `failed_sample_count`, and explicit metric-admission metadata. Never pool distinct processes into one primary sample group. Any subsequent cross-process paired-block analysis is a separate versioned derived `analysis.json` or report with disclosed formulas and exact referenced raw package hashes; it must be independently regenerable and cannot replace the four raw-evidence files.

**Curated evidence:** require a clean committed working tree *at run time* and run ` .\scripts\assert-curated-evidence-ready.ps1 ` immediately before curated collection. Record commit, build, binary and shader digests; qualified raw output in `results/local/` may only be promoted after independent evidence review and may never be retroactively promoted from a dirty run. Qualification remains labeled qualification even if technically clean. A protocol change requires an explicit new revision, new identities and a separate evidence campaign.

## Error, timeout, cancellation and bounded budget

All error outcomes are retained. Incorrect output, invalid index, atomic overflow, broken dependency, malformed schema, GPU UUID mismatch or incomplete provenance prevents comparative admission. Unsupported features/unavailable memory are separate capability outcomes, not timings. Each successful sample requires explicit completed operation and validation; failures mark the cell failed and suspend further candidate samples for that cell pending amendment. Do not silently replace missing samples, discard outliers, infer missing times by subtraction or treat elapsed watchdog time as a successful duration.

**Supervision budget:** default maximum 60 seconds for one complete operation, 20 minutes for one child process and 24 hours for the entire scheduled same-GPU screen. Implement operation timeout using a supervisor/child-process mechanism; a blocking `cudaStreamSynchronize` is not itself a cancellable timed wait. On deadline, preserve flushed raw rows plus an external failure record, terminate the child according to the approved operator procedure and do not reuse uncertain in-flight resources. Preflight the planned process count and budgets; exceeding them causes an explicitly inconclusive/incomplete campaign, not silent threshold expansion. Qualification's scheduled cells and N/H mode controls must be frozen separately in an execution manifest; no automatic retry of failed processes.

The 22-cell maximum corresponds to 220 fresh backend processes at five paired blocks, not including Stage-5 or qualification runs; a qualifying one-time extension adds at most five pairs to **only the triggered cell**. Stage-7 hardware repetitions require a new approved matrix and provenance, not unlimited cross-generation repetitions. No ad hoc dynamic sweeps, warm-up growth or additional CUDA/256 investigation are authorized.

## Implementation sequence and tests

The strategic Notion roadmap retains Stages 0–9; implementation milestones I0–I7 refine them. The correctness-first implementation path is now complete through I7: the semantic foundation, backend-native A–E workloads, correctness entry points, experiment-control support, fresh-process correctness supervisor, evidence publication and integrated Stage-4 acceptance have been implemented and reviewed. Stage 3 is complete and Stage 4 is PASS at source `8eb7f3654e83073ece4d7ef2a8a5c2301cafe793`; curated Stage-4 evidence is anchored by commit `24558d0d121a41e3d92f407f804ffbd1154c24b3`. Preserve EX-1 behavior and serializers. No implementation, smoke test or correctness run qualifies a metric, grants Stage-2 measurement-protocol authorization or permits candidate-performance collection.

Historical implementation order was: independent Mix/fixture/oracle and generator tests; config/identity/schema v2; A1 CPU->CUDA->Vulkan vertical slice; A2; B; C; D1; E; integrated runner/fresh-process control and full correctness qualification. The bounded Stage-5 v1.2 machinery subsequently completed through S5-I5 and the S5-E1 campaign at source `70859055bf4280f5aeacd8039e9d3c41c9487e84`, with curated evidence at `a5694b6afa918499607949ec0c60d63d21be1270`. Its valid scientific nonqualification closed that path with Gate-0 FAIL. D2 remains conditional and unimplemented. Preserve EX-1 tests. Do not prematurely add abstractions, dependencies or utility layers shared with consuming applications. Run ` .\scripts\test.ps1 ` locally after code changes; if host SDK or GPU access is unavailable, report rather than bypass. S6-C1 is documentation-only and runs no build, CMake, CTest, canonical test script or GPU workload.

The exact next sequence and future ownership are frozen in v1.3 above: **S6-C1 — Protocol 1.3 diagnostic-contract freeze -> S6-I1 — Diagnostic plan/evidence/analysis foundation -> S6-I2 — Full A–E Mode-H diagnostic child -> S6-I3 — Diagnostic supervisor/control + independent inspection -> S6-E1 — Full 22-cell / 220-process diagnostic campaign + acceptance**. C1 is currently READY FOR HUMAN REVIEW. After human acceptance, C1 is protocol frozen; I1/I2/I3 are NOT STARTED; E1 is NOT EXECUTED; the Stage-6 label is **PROTOCOL FROZEN / IMPLEMENTATION NOT STARTED**. No S6-E2 or native-Mode-N prompt is planned. Correction/continuation prompts require a real defect found by human review. Do not implement the later increments as part of C1.

Tests continue to require: exact generator/golden constants and independently calculated expected outputs; permutation uniqueness and malformed-index rejection; unsigned and atomic correctness/overflow; N=0, N=1, N=257 and workgroup-boundary correctness; D K=0/1/16/64 parity and read-after-write/write-after-read barriers; C reset, output invariants and source preservation; E byte-exact copy and coherent/noncoherent memory visibility; event/timestamp validity and scope metadata; t0/t1/t2 arithmetic, failure, timeout and null behavior; schema header/version/identity collisions, sample ordering and independent summary regeneration; same physical GPU enforcement, true instrumentation declarations, all historical EX-1 tests and canonical Windows smoke tests. Stage-5-specific support must additionally test the exact three-group process plan, counterbalanced ordering, 48-operation warm-up analysis, selected-common-W rule, 50/100/200 prefix analysis, `R_process<=1.10`, no-fallback/no-extension stop rules, qualification-only evidence identity and independently regenerable verdict analysis.

## Acceptance checkpoints and revision policy

- **Stage 0 design:** **COMPLETE.** A–E portfolio, formulas, exclusions and maximum initial cell ceiling are approved by the v1.0 design freeze. D2 remains conditional and unimplemented.
- **Stage 1 measurement Gate 0:** **COMPLETE — FAIL for proposed D1/H `host_completion_ns` comparative scope.** S5-E1 validly completed but D1-W did not qualify; D1-S was skipped by protocol. This scientific failure is distinct from INCOMPLETE execution/evidence.
- **Stage 2 measurement-execution freeze:** **NOT AUTHORIZED for comparative candidate-performance; NOT GRANTED.** The failed ruler admits no CUDA-versus-Vulkan comparative candidate-performance scope. DR-44 diagnostic authorization does not grant Stage 2.
- **Stage 3 implementation:** **COMPLETE.** EX-2 additive A–E semantics, backend-native implementations, correctness runner/control and evidence machinery are implemented; no candidate-performance authorization is implied.
- **Stage 4 correctness:** **PASS.** The accepted clean campaign at source `8eb7f3654e83073ece4d7ef2a8a5c2301cafe793` completed all 22 approved cells and 44 backend series; both the first historical INCOMPLETE attempt and the final PASS attempt are curated at evidence commit `24558d0d121a41e3d92f407f804ffbd1154c24b3`.
- **Stage 5 bounded same-GPU qualification:** **COMPLETE — SCIENTIFICALLY NONQUALIFIED.** S5-E1 source `70859055bf4280f5aeacd8039e9d3c41c9487e84`; curated evidence `a5694b6afa918499607949ec0c60d63d21be1270`. All ten D1-W inputs were valid; five had no qualifying `W<=16`; common W is null, warm-up/scope qualification false, and D1-S `skipped_by_protocol`. DR-43's bounded path completed as designed; qualification evidence remains qualification.
- **Stage 6 full same-GPU descriptive/diagnostic screen:** S6-C1 is **READY FOR HUMAN REVIEW**. After human acceptance: **PROTOCOL FROZEN / IMPLEMENTATION NOT STARTED**. Diagnostic execution is strategically authorized by Accepted DR-44; implementation and campaign have not been performed and require the separate v1.3 increments. Comparative candidate-performance collection/ranking remains prohibited.
- **Stage 7 cross-generation screen:** not started; any later RTX 3060 Ti / RTX 5060 subset requires a new approved matrix, machine-specific provenance and appropriate metric/family requalification.
- **Stages 8–9:** not started; tooling/engineering observations remain separate from performance evidence, and final synthesis stays bounded to tested scopes.
- **Step 10b:** **NOT PASSED.** No production backend selection or production-runtime conclusion is admitted by Stage-6 diagnostic work.

Existing EX-2 correctness results are authoritative only for correctness; historical Stage-5 v1.2 qualification remains authoritative for its complete negative result. New Stage-6 diagnostic evidence requires human acceptance and a clean committed v1.3 specification, a clean reviewed implementation source, exact new diagnostic identities and a separately reviewed immutable production manifest. A documented amendment is necessary for changes to qualification W, sample counts, effect tolerance, metrics, hardware, timing placement, shader/kernel formulas, schema or allowed cells, or to the frozen v1.3 diagnostic contract. Record old and new revisions; never rewrite historical evidence, relax thresholds after seeing results, or relabel qualification/diagnostic runs as candidate-performance. The operator controls acceptance, commit and push; C1 performs none of those transitions.

## References

Internal: `AGENTS.md`, `docs/charter.md`, `docs/methodology.md`, `docs/results-format.md`, `docs/ex1.md`, `docs/i7-correctness-supervisor.md`, `docs/stage5-qualification-supervisor.md`, the curated Stage-4 and S5-E1 evidence anchors above, and the EX-2 Notion Experiment/roadmap/DR-40/DR-41/DR-42/DR-43/DR-44 records. DR-41 is Retired. Accepted DR-43 owned the bounded Stage-5 Gate-0 path, now completed as designed. Accepted DR-44 owns the post-Gate-0 full descriptive/diagnostic Stage-6 branch. `docs/ex2.md` owns the exact executable scientific/protocol contract; supervisor documents own their implementation/control mechanics. Notion owns strategic decisions, experiment narrative, roadmap and progress state, without duplicating full technical decision bodies here. S6-C1 does not update Notion.

External API behavior, **not EX-2 qualification evidence**:

- NVIDIA CUDA Programming Guide, asynchronous execution and timing: https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/asynchronous-execution.html
- NVIDIA CUDA C++ Best Practices Guide, timing and effective bandwidth: https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/
- Khronos Vulkan synchronization examples: https://docs.vulkan.org/guide/latest/synchronization_examples.html
- Khronos Vulkan timestamp queries: https://docs.vulkan.org/spec/latest/chapters/queries.html
