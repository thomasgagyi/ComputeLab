# EX-2 Stage-5 qualification supervisor/control

This document describes S5-I5 implementation and control mechanics. It does not
amend the authoritative v1.2 scientific contract in `docs/ex2.md`. Implementing
or testing this instrument does not execute Stage 5, issue a Gate-0 verdict,
authorize Stage 2, admit candidate-performance collection, or support a
CUDA/Vulkan performance ranking. I7 remains complete, Stage 4 remains PASS,
Gate 0 remains NOT PASSED, DR-41 remains Proposed, DR-43 remains Accepted,
Stage 2 remains NOT GRANTED, D2 remains conditional/unimplemented, and Step 10b
remains NOT PASSED.

## Ownership

`ComputeLabEx2Stage5Control` owns strict manifest parsing, Windows process
ownership, progress validation, independent disk inspection, scheduling, and
durable control/analysis publication. It links the existing evidence, CPU
oracle, environment and qualification layers. It has no dependency on the
application execution library and contains no GPU workload execution.

`ComputeLabEx2Stage5Execution` adds only an app-owned observer around existing
physical attempts. Native CUDA/Vulkan code and the H t0/t1/t2 placement are
unchanged. The pure qualification, operational-fact, evidence, configuration,
and common result-schema implementations remain independently owned.

## Immutable manifest v1

The production interface is exactly:

```text
ComputeLabEx2Stage5Supervisor --manifest <repository-relative-json>
```

`--help` prints this interface. There is no resume, retry, subset, backend,
warm-up, sample-count, tolerance, output-directory or historical-exception
option. The manifest path must use normalized forward slashes, remain relative
to the configured repository, and name a JSON file.

The control-owned `LoadManifestFromRepository` applies those lexical rules and
rejects a reparse manifest or any reparse ancestor before reading bytes. It
requires a regular file, performs a bounded read of at most 1 MiB, rejects
incomplete or changed reads, and passes the bytes to the existing strict
hash-validating parser. Main has no alternate manifest reader.

The manifest has exactly these top-level members:

```text
manifest_version = 1
manifest_type = "ex2-stage5-qualification"
manifest_id
manifest_sha256
protocol_version = "1.2"
evidence_kind = "qualification"
machine_id
child_executable_path
expected_source_revision
expected_git_dirty = false
expected_child_executable_sha256
expected_supervisor_executable_sha256
expected_gpu_uuid
cuda_device_ordinal
vulkan_physical_device_index
expected_a1_vulkan_shader_sha256
expected_d1_vulkan_shader_sha256
operation_timeout_ms
child_timeout_ms
campaign_timeout_ms
declared_max_child_count = 30
groups
```

Source identity is 40 lower-case hex characters, SHA-256 identities are 64,
and the physical GPU UUID is canonical lower-case and nonzero. Anonymous
identifiers use bounded ASCII letters/digits/hyphen/underscore; Windows device
names are rejected. The child path must name `ComputeLabEx2Stage5.exe`.
Operation timeout is 1..60000 ms; child timeout is at least operation timeout
and at most 1200000 ms; campaign timeout is at least child timeout and at most
86400000 ms. These are external control limits, not scientific parameters.

Each of exactly three groups has only `group_index`, `phase`, `launch_rule`,
`selected_w_policy`, `declared_child_count`, and `children`:

| Group | Phase | Launch rule | Selected-W policy | Children |
|---|---|---|---|---|
| 0 | a1-sentinel | after-campaign-preflight | none | 10 |
| 1 | d1-warmup | after-a1-integrity-complete | none | 10 |
| 2 | d1-sample | after-d1-warmup-qualified | max-qualified-d1-warmup-w | 10 |

Each child has only `sequence_index`, `plan_index`, and `session_id`. Global
sequence is exactly 0..29, and each group has plan indices 0..9. Backend,
process, block, and order slot come from `FrozenProcessPlan()`. D1-S identities
are reserved from the beginning; numeric common W is absent from the manifest.

Parsing rejects missing/unknown/duplicate members, wrong types, numeric
overflow, topology/order drift and case-insensitive namespace overlaps.
Canonical JSON sorts object members lexicographically, preserves array order,
uses locale-independent numbers and JSON escaping, and excludes only
`manifest_sha256` from the hashed UTF-8 payload. The test suite pins a literal
digest independently calculated using Python's `json` and `hashlib`.
Execution reparses the hash-validated canonical JSON, so mutation of the public C++
manifest value cannot replace its schedule.

## Preflight and namespaces

Before child zero, preflight requires the expected clean committed HEAD, a
resolvable branch, exact child and currently running supervisor executable
hashes, both selected native GPU UUIDs equal to the manifest identity, and
both exact Vulkan shader hashes. It checks all 30 declared final directories,
`.incomplete` directories and `.failure.json` paths, the control ledger and
its staging/temporary paths, and the analysis directory. Reparse ancestors
and collisions are rejected. No existing child package can satisfy a slot.

Provenance and the next child's namespace are checked again before every
launch. The supervisor does not rebuild, substitute devices, adopt evidence,
discard processes, retry, resume, or overwrite an existing namespace.

The six preexisting `s5-smoke-86864a8f-*` S5-I4 direct-child smoke packages are
historical local material, preserved unchanged under the human clarification.
Their unrelated presence does not block implementation. Their names remain
occupied production namespaces; declaring one in a future manifest fails
preflight. Their D1-S W=2 is never used by scheduling or analysis. S5-I5 tests
create no new real Stage-5 qualification package or campaign evidence.

## Windows ownership and progress

Each child is launched sequentially using an exact absolute executable path,
`CreateProcessW`, Windows argument quoting, `CREATE_SUSPENDED`,
`CREATE_NO_WINDOW`, and an explicit inherited-handle list containing only the
progress pipe writer. A kill-on-close Job Object owns the child before resume;
descendants inherit job ownership. The parent closes its writer and waits for
owned termination before considering another launch. Launch/job/wait failure,
timeout, malformed progress, abnormal exit or nonzero exit stops the campaign.

The hidden `--supervisor-progress-handle` option is stripped before the child's
unchanged eight-option public parser. It is optional for direct-child usage,
absent from help and evidence identity, and rejects missing/duplicate/invalid
handles. Pipe publication failure terminates the child fail-closed.

The Windows-only wire record is 40 bytes with explicit 32-bit fields for magic
`0x53355043`, version 1, event, phase, backend, kind, plan index, and attempt
index, followed by a signed 64-bit QPC capture at offset 32. Events are
Started=1/Returned=2; kinds are diagnostic observation=1, selected-W
preparation=2, measured observation=3. Phase/backend numeric values are the
existing Stage-5 enum values. QPC is used only by the external watchdog;
neither QPC captures nor derived QPC durations are persisted as evidence.

Every A1 and D1-W process emits 48 Started/Returned pairs for indices 0..47.
D1-S emits exactly W preparation pairs followed by 200 measured pairs,
restarting the measured index at zero. W=0 emits no preparation pair. The
watchdog covers preparation/upload, the original H operation, completed
readback, exact validation and row retention. A1's initial upload remains
setup; retained-input preparation before observations 1..47 is enclosed by
progress. D1-S preparation is watchdog-visible but never an evidence row.
Failures and exceptions do not fabricate Returned.

Validation rejects magic/version/enum/identity/order/kind/index drift,
duplicates, gaps, extra/truncated records and nonpositive/backward/future QPC.
Exit zero requires the complete sequence; nonzero exit may retain a truthful
prefix but never permits continuation. Operation deadlines use the child's
Started QPC including delivery/drain delay. Child deadlines bound each launch;
the campaign deadline bounds the complete execution. The I7 production timeout
precedence is preserved: protocol, operation, campaign, then child timeout.
Only event counts and active/last completed attempt identity enter the ledger.

## Independent disk inspection

The inspector classifies complete, incomplete-retained, sidecar-only,
partial-retained, missing and contradictory states, hashes retained regular
files, and rejects reparse artifacts and unexpected topology. Successful
packages require exactly the six standard files and no staging/sidecar.
Failure context remains separate control evidence, including phase, native
error and timing nullability. Failed completion waits cannot establish t2;
post-completion readback failure may retain valid H components.

Inspection reopens JSON and CSV from disk. It rebuilds typed frozen
configuration, deterministic logical input, CPU oracle and foundation identity;
checks environment/source/binary/GPU/shader/condition/series/process identity;
requires exact ordered 4095 clock rows and phase-specific 48 diagnostic or
200 sample rows; validates status/correctness claims, H arithmetic and null
native intervals; and regenerates the existing per-series summary. JSON member
order and ordinary CSV quoting do not alter semantic comparison. Unknown JSON
fields, duplicate members, malformed numeric cells, row-order/count changes,
identity drift and inconsistent summaries fail inspection.

The retained schema does not store GPU output bytes. The inspector does not
claim to recompare unretained GPU output; correctness eligibility combines the
clean child implementation's validation claims, completed progress and retained
row/status evidence. Logical input and expected-output identities are
independently regenerated. Raw observations, clock zero/null values and extreme
observations are never dropped or normalized.

Each reconstructed `ProcessInput` contains ordered host-completion values and
all retained calibration deltas. Existing analysis determines operational
input validity and scientific facts. Missing/malformed calibration causes an
incomplete stop; valid coarse clock or state structure reaches science and
can yield valid nonqualification.

## Group barriers and derived analysis

A1 completes ten valid fresh processes before `AssessA1Sentinel`; descriptive
instability or coarse clock does not gate D1. Integrity/correctness failure
stops immediately. D1-W completes ten further valid processes even when an
early process scientifically does not qualify, then uses existing
`AssessD1Qualification`/common-W assessment. No qualifying common W causes
D1-S `skipped_by_protocol` and complete scientific nonqualification.

Qualified D1-W analysis is durably finalized and hashed, and its exact path,
hash and derived common W enter the durable ledger before D1-S. Every D1-S
launch rechecks the finalized analysis bytes and durable ledger anchor. All
ten new D1-S children receive that same independently derived W; valid
scientific sample nonqualification does not stop the group early. The existing
sample-count and within-backend process-stability calculations own the result.
No thresholds, medians, tolerances or R_process formulas are duplicated in
control code. Group barriers reinspect raw files and compare their hashes.

Derived artifacts live only at `results/local/<manifest_id>-stage5-analysis/`:
`a1-analysis.json`, `d1-warmup-analysis.json`, conditional completed
`d1-sample-analysis.json`, and `campaign-analysis.json`. Wrapper version is 1;
the reused derived analysis schema remains 2, evidence schema remains 2 and
protocol remains 1.2. Each group wrapper records manifest/source identity,
ordered package inventories and hashes, and existing immutable assessment
facts/reasons. A1 serialization is explicitly sentinel/diagnostic.

Campaign analysis records group path/hash anchors or explicit nulls, selected
common W or null, `d1_scope_qualified` true/false/null, completeness, terminal
control reason, `gate0_verdict="pending_human_review"`, and
`stage2_authorization="not_granted_by_this_artifact"`. These are qualification
facts, never a human verdict, performance comparison or execution authorization.

## Durability, ledger and exits

The control ledger path is
`results/local/<manifest_id>-stage5-control.json`. Its record version is 1 and
type is `ex2-stage5-qualification-supervisor-execution`. It retains the hash-validated
manifest, observed provenance, all 30 declared identities, actual arguments,
UTC launch/exit timestamps, PID/exit/termination reason, progress prefix,
inspection/failure facts, artifact hashes, continuation and group states, and
analysis anchors. It contains no scientific statistics or Gate-0/Stage-2 verdict.

Each update uses CREATE_NEW `.incomplete.tmp`, flush, close, byte readback and
write-through rename to owned `.incomplete`. Only the current run's matching
manifest/start identity permits replacement of staging. Normal control
termination renames `.incomplete` to final, including valid scientific
nonqualification or a controlled incomplete stop. Unexpected supervisor death
leaves the last `.incomplete` record. A final ledger means the control record
terminated; it does not mean scientific qualification succeeded.

Ledger updates precede launch and follow termination, inspection, group
barriers, analysis finalization and terminal disposition. Analysis publication
uses CREATE_NEW `.tmp`, flush/close/readback, non-overwriting final rename,
then hashing of the actual final file. Existing temporary/final artifacts are
preserved on failure; no cleanup implements a hidden retry.

| Exit | Meaning |
|---|---|
| 0 | Completed qualification analysis, scientifically qualified or nonqualified |
| 2 | Preflight/control incomplete, including a thrown process-control exception |
| 3 | Returned child/progress/evidence result is incomplete |
| 4 | Supervisor publication failure |

If process control throws, the supervisor records `process_control_failure`,
stops without retry, and finalizes an incomplete control record with a null
scientific qualification result. This is exit 2. A returned nonzero child,
progress failure, timeout, or invalid evidence remains exit 3. Completed
scientific nonqualification remains exit 0; publication failure remains exit 4.

Synthetic tests use test-owned TEMP roots and a dedicated non-GPU child helper.
They exercise strict parsing/hashes/collisions, raw package tampering, progress,
owned process/descendant termination, barriers, analysis anchors, failure and
durability. Existing GPU correctness and Vulkan validation regressions remain
allowed. No production Stage-5 supervisor or child GPU smoke is part of S5-I5.
After implementation validation, human review precedes an isolated commit,
documentation synchronization and a separately authorized S5-E1 campaign on a
new clean SHA and new namespace. No such later action is authorized here.
