# EX-2 I7 correctness supervisor (Prompt 3)

This correctness-only execution-control instrument does not amend the measurement
protocol, pass Gate 0, authorize candidate-performance collection, or issue the
final Stage-4 verdict. Prompt 4 and a full 22-cell acceptance campaign remain
separate human-controlled work. Protocol 1.1, schema-v2 child evidence, native
CUDA/Vulkan semantics, and the qualification supervisor remain unchanged.

## Manifest v1

The CLI is `ComputeLabEx2CorrectnessSupervisor --manifest <manifest.json>`.
The separate `--print-manifest-hash <manifest.json>` command only prints the
canonical payload SHA-256; it performs no preflight, launch, or publication.
`--help` documents both forms.

Exactly these top-level members are accepted:

| Member | Contract |
| --- | --- |
| `manifest_version` | unsigned integer 1 |
| `manifest_type` | `i7-correctness` |
| `manifest_id`, `machine_id` | anonymous identifiers |
| `manifest_sha256` | lower-case 64-hex digest of canonical payload |
| `protocol_version` | `1.1` |
| `child_executable_path` | normalized repository-relative path, leaf `ComputeLabEx2Correctness.exe` |
| `supervisor_record_path` | one normalized file directly under `results/local` |
| `expected_source_revision` | lower-case 40-hex commit |
| `expected_git_dirty` | boolean, not an integer |
| `expected_child_executable_sha256`, `expected_supervisor_executable_sha256` | lower-case 64-hex digests of actual executables |
| `expected_gpu_uuid` | nonzero canonical lower-case dashed UUID |
| `cuda_device_ordinal`, `vulkan_physical_device_index` | unsigned, within respective native index bounds |
| `operation_timeout_ms` | 1..60000 |
| `child_timeout_ms` | operation timeout..1200000 |
| `campaign_timeout_ms` | child timeout..86400000 |
| `continuation_policy` | `completed-validation-only` |
| `declared_child_count` | 1..22, exactly the array size |
| `children` | ordered subset, no repeated core cells |

Every child has exactly `sequence_index` (contiguous from zero), `core_cell_index`
(0..21 in the frozen `ApprovedCoreCells()`), `session_id`, and
`expected_vulkan_shader_sha256`. The shader is required for A1/A2/B1/B2/C/D1
and must be JSON null for E1/E2. Session identifiers and their derived CUDA/Vulkan
run identifiers satisfy Prompt-2 identifier validation. Final, incomplete,
sidecar and ledger paths must not collide, including case-only collisions.
Paths reject traversal, absolute/drive paths, unsafe components, and existing
reparse points. No workload parameters, order, timing mode, or arbitrary child
arguments are accepted from the manifest.

Canonicalization removes `manifest_sha256`, sorts object members lexicographically,
preserves array order, uses canonical JSON string escaping, literal booleans/null,
minimal decimal unsigned integers, and no insignificant whitespace. Existing
EX-2 SHA-256 hashes the UTF-8 payload. Execution strictly rejects unknown,
missing, duplicate, incorrectly typed, negative, fractional, or overflowing
members. Parsing has a bounded nesting depth. The hash-print command calculates
a payload hash only, not a claim that the manifest is executable.

## Preflight and schedule

Before child zero, the supervisor verifies actual Git HEAD/dirty state, child
and running supervisor binary hashes, independently enumerated CUDA/Vulkan UUIDs,
and every applicable built shader hash. Both device selections must match the
declared nonzero UUID. All session siblings and final/incomplete/tmp ledger paths
must be absent. A rejected preflight launches zero children and publishes no
ledger. Before each subsequent launch it rechecks provenance and that child's
paths. Only one child is live at a time; no retry exists.

The fixed public child invocation retains its five options: core cell, CUDA
device, Vulkan device, anonymous machine, and session. The supervisor adds one
hidden `--supervisor-progress-handle` option. The child removes and validates it
before parsing the unchanged public option set. A missing observer remains a
disabled/default value for direct integration callers and direct child runs.

## Attempt progress and process ownership

The one-way inherited pipe carries a 24-byte standard-layout, trivially copyable
POD: uint32 magic `0x49374350`, uint32 version 1, uint32 event (Started 1 or
Returned 2), uint32 backend (CUDA 1 or Vulkan 2), int64 QPC counter. Exactly four
events are valid: CUDA Started, CUDA Returned, Vulkan Started, Vulkan Returned.
App-level emission immediately surrounds existing execution attempts. Exceptions
do not fabricate Returned; native resources, kernels, synchronization, E3
t0/t1/t2, and H/N scopes are not changed. Progress write failure terminates the
child fail-closed without unwinding uncertain native resources.

The operation deadline starts at the child-emitted Started counter, including
pipe-drain latency, and is enforced externally. QPC is monotonic, nonzero, not
future-dated, and never persisted as timing data. Incomplete nonzero-exit prefixes
remain recorded prefixes; exit zero requires all four events. Bad magic/version,
order, duplicates, backwards counters, truncation or surplus data fail closed.

The runner uses the exact absolute executable with `CreateProcessW`, explicit
Windows argument quoting, `CREATE_SUSPENDED | CREATE_NO_WINDOW`, and
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` containing only the pipe write handle. It
assigns a kill-on-close Job Object before resuming. The supervisor closes its
write handle, drains progress, enforces operation/child/campaign deadlines, and
terminates and waits for an owned timed-out child. Exceptions also trigger owned
child termination. The child deadline starts at launch/resume; the campaign
deadline is checked before each launch and during the child. QPC does not replace
the whole-child or campaign bound.

## Independent disk inspection

The control library does not link Prompt-2 execution code and never calls its
`VerifyStagedSession`, `PreparedFoundationPair`, or `SerializedPair` for expected
truth. The inspector regenerates input and output from the manifest's approved
cell, `CoreInputSeed`, deterministic generators, and independent CPU oracles.
B uses the complete primary/permutation composite; C uses targets and the full
zero-counter state. A/D word and E byte meanings remain frozen.

Actual disk JSON/CSV is parsed after process exit. Independently reconstructed
plans constrain run/condition/series identities, protocol 1.1, schema 2, P,
zero warm-ups, one sample, fixed order slots, source/binary/GPU identities,
workload parameters, logical digests and shader applicability. Existing schema
validators and serializers check exact member/column sets and reconstruct the
summary from parsed raw sample status. All timing data must remain null.
Initialization requires the existing truthful setup row; pre-setup native failure
rows cannot be paired with fabricated setup completion. No child file is rewritten.

The inspector reconciles final, `.incomplete`, and `.failure.json` siblings:

- `complete-pass`: exit zero, final only, both exact four-file series pass.
- `validation-failure`: child exit 3, incomplete and strict validation sidecar,
  foundation established, both backend series represent completed comparisons.
- `incomplete` or `missing`: an unsafe/partial outcome, never promoted.
- `collision` or `contradictory`: mutually inconsistent topology, identities,
  rows, summaries, sidecar facts, or exit/package states.

The sidecar has exactly the eleven existing Prompt-2 members: record version/type,
session, failure phase/code, foundation flag, source revision, CUDA/Vulkan run
IDs, retained-staging flag, and detail. Version/type/session are exact; phase is
from Prompt 2; token/detail bounds match Prompt 2; any source/run identity must
agree with the manifest and foundation truth. `staging_retained` is compared with
the filesystem. Every recorded regular artifact, including the sidecar, has its
relative path, size, and SHA-256 retained. Reparse artifacts are rejected.

The inspector can establish disk structural and semantic-identity consistency;
it cannot independently recover wrong observed GPU bytes because schema v2 does
not retain them. Status rows are claims made by the child under the established
native lifecycle, not raw-output reconstructions.

## Continuation and ledger

Only a complete pass or a structurally valid completed validation failure with
all four events, no supervisor termination, and a validation sidecar may continue.
Pair-level validation can leave successful individual backend rows unchanged.
Timeout/crash, early/single-backend failure, invalid progress, native uncertainty,
provenance drift, or evidence/control failure stops. Native/package errors are
never relabeled validation to continue.

Ledger version 1 has kind `ex2-i7-correctness-supervisor-execution` and scope
`correctness_control_only_no_stage4_verdict`. It stores expected/observed provenance,
declared deadlines, UTC start/end, the scheduled order, actual launch order,
exit/termination, four progress booleans, package classifications, integrity errors,
artifact hashes and continuation decisions. It does not contain QPC, attempt
durations, performance statistics, or Gate-0/Stage-4 verdicts.

The initial record uses a new `<record>.incomplete.tmp`, durable write/flush,
then rename to `<record>.incomplete`. Only the running supervisor's owned
incomplete file may be replaced. Updates occur initially, before each launch
(`launch_pending`), after inspection and after the decision. Normal termination
renames the final snapshot to `<record>`, including failed/stopped outcomes.
Unexpected death leaves the last incomplete snapshot and closes the owned job.
Preexisting final/incomplete/tmp files are never adopted or cleaned up.

| Exit | Meaning | Final status |
| --- | --- | --- |
| 0 | all scheduled subset children pass | `execution_complete` |
| 2 | manifest/preflight rejected before execution | no ledger |
| 3 | only safely retained completed validation failures | `correctness_failed` |
| 4 | unsafe/incomplete/timeout/crash/progress outcome | `stopped_incomplete_or_unsafe` |
| 5 | provenance/evidence/publication/control failure | `control_or_evidence_failed` |

A final ledger means that the supervisor record is complete, not that correctness
passed. The latest durable incomplete snapshot remains if final publication fails.

## Validation scope

The canonical build and `scripts/test.ps1` include the pure inspector/manifest
tests and test-only synthetic processes. Synthetic tests cover hangs, both backend
deadlines, malformed progress, crashes, quoting, job assignment, restricted
inheritance, delayed-marker suppression and an exhausted campaign. They are not
GPU correctness evidence.

The separately built `out/build/x64-debug/tests/ComputeLabEx2CorrectnessSupervisorSmokeTests.exe`
is an explicit one-shot real smoke, intentionally not discovered by CTest so
focused validation followed by the canonical suite cannot execute it twice.
It creates one A1 N=256 manifest, requires baseline
`3642edd1301dded0b44c6fef89643f14352b2722` with dirty=true, uses actual executable/
shader hashes and matching device UUIDs, runs the supervisor, independently
checks the final two-backend package and ledger, and cleans only its unique
owned test paths. It prints artifact identities before cleanup. The existing
Prompt-2 direct smoke remains in the canonical suite unchanged.

This subset smoke is control regression evidence from an uncommitted tree,
not curated results, a full 22-cell run, a Stage-4 acceptance decision, a Gate-0
decision, or authorization for measurement execution or candidate performance.
