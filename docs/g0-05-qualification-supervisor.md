# G0-05 qualification manifest and supervisor

This document describes the execution-control contract implemented by
`ComputeLabEx2Gate0Supervisor`. It does not amend `docs/ex2.md`, authorize an
EX-2 campaign, select scientific parameters, or define a Gate-0 verdict.

## Manifest envelope

The manifest is JSON. Version 1 accepts exactly these top-level fields:

```text
manifest_version                 integer, exactly 1
manifest_type                    warmup-characterization,
                                 sample-count-qualification, or
                                 instrumentation-control
manifest_id                      anonymous ASCII identifier
manifest_sha256                  lowercase SHA-256
machine_id                       anonymous ASCII identifier
protocol_version                 exactly 1.0
child_executable_path            normalized repository-relative path
vulkan_shader_path               normalized repository-relative path
supervisor_record_path           results/local/<file>
continue_after_fatal_failure     explicit boolean
operation_timeout_ms             1..60000
child_timeout_ms                 operation timeout..1200000
campaign_timeout_ms              child timeout..86400000
declared_child_count             exact children array size
declared_operation_count         exact sum implied by all children
prerequisite                     null or the declared prerequisite object
children                         ordered child array
```

Unknown, missing, duplicate, ambiguously typed, negative, fractional, or
overflowing values are rejected. Paths use forward slashes and may not be
absolute or traverse above the repository root.

`manifest_sha256` is SHA-256 over canonical UTF-8 JSON after removing the
`manifest_sha256` member. Canonical JSON sorts every object member
lexicographically, preserves array order, uses decimal unsigned integers with
no leading zeros, and contains no insignificant whitespace. The supervisor's
`--print-manifest-hash` command calculates this value; it does not execute the
manifest.

## Prerequisite object

Warm-up characterization requires `prerequisite: null`. A downstream manifest
requires exactly:

```text
authorization_id
evidence_id
evidence_sha256
selected_warmup_count
qualified_sample_count
```

Sample-count qualification requires a declared selected W and a null qualified
sample count. Instrumentation control requires both a declared selected W and
qualified sample count. The supervisor checks their syntax and consistency
against every child but does not open, hash, authenticate, review, calculate,
or qualify the referenced prerequisite evidence. `authorization_id`,
`evidence_id`, and `evidence_sha256` are therefore declarations, not proof of
verification; a syntactically valid placeholder remains unverified.

Review and SHA-256 verification of the referenced authorization/evidence is an
external human-review boundary that must be completed before a downstream
manifest is executed. The supervisor record states
`external_human_review_required_not_machine_verified` for downstream manifests
and `not_applicable` for warm-up characterization.

## Child entries

Every child contains exactly:

```text
sequence_index
pair_id
phase
backend
instrument_mode
device_index
expected_gpu_uuid
element_count
seed
warmup_count
planned_sample_count
process_index
block_index
order_slot
run_id
output_package_path
protocol_version
expected_source_revision
expected_executable_sha256
expected_shader_sha256
```

The shader digest is required for Vulkan and must be JSON null for CUDA.
Output paths are `results/local/<run_id>`. Run IDs and normalized paths are
unique. Process and block indices remain limited to 0 through 4; order slots
remain 0 or 1.

Warm-up and sample-count manifests contain exactly five adjacent CUDA/Vulkan
pairs in the frozen `C,V; V,C; C,V; V,C; C,V` block order and use mode H.
Warm-up children declare W=0, zero samples, and execute 48 diagnostic
operations. Sample-count children are separate processes, use the supplied W,
and retain the frozen 200 characterization observations.

Instrumentation manifests contain five adjacent H/N pairs for each backend,
20 children total. H and N stay within the same backend and block, match every
logical variable other than instrument mode, and alternate first mode across
that backend's five blocks. They are never cross-paired as CUDA H/Vulkan N.

All entries in one manifest target the same expected physical GPU UUID,
source revision, executable identity, machine identity, workload size, seed,
and protocol. Device indices are checked independently against each API's
enumerated UUID; ordinal zero is never treated as physical identity.

## Preflight and execution

Before the first child, the supervisor validates the complete manifest,
canonical hash, prerequisite structure, pairings, declared counts, checked
budget arithmetic, current Git revision, executable and shader hashes, device
availability and UUIDs, and all final/incomplete output collisions. A failure
starts no child.

Children are launched sequentially with `CreateProcessW`, an exact application
path, Windows-compatible per-argument quoting, and an explicit inherited-handle
list. Each child is created suspended, assigned to a kill-on-close job object,
and only then resumed. The supervisor waits for termination before considering
the next entry. There is no automatic retry.

The child writes fixed versioned progress messages to an inherited one-way
pipe immediately before and after each complete operation. Both writes are
outside the existing measured host interval. For Vulkan they also surround
the existing outside-timing command preparation. Thus `t0`, `t1`, `t2`, native
marker scope, warm-up procedure, and correctness work are unchanged. The pipe
writes and polling do add explicitly declared inter-operation pacing to the
whole supervised execution condition. Each message carries a Windows
performance-counter value, so the operation deadline is anchored at the child
start boundary rather than when the supervisor happens to drain the pipe.

The supervisor enforces the declared limits up to the frozen maxima: 60 seconds
per operation, 20 minutes per child, and 24 hours per campaign. An operation,
child, campaign, or progress-protocol timeout terminates the owned job, records
the actual exit outcome and reason, retains any flushed `.incomplete` child
package, and never fabricates an operation duration. A fatal outcome stops the
schedule unless `continue_after_fatal_failure` was explicitly true in the
hashed manifest.

## Package and supervisor records

After every exit, the supervisor checks package state, required files, schema
and evidence kind, manifest/process/condition identities, source/executable/
shader provenance, exact CSV headers, row ordering and declared counts, timing
arithmetic, every summary statistic regenerated from eligible sample rows using
the canonical binary64/null rules, physical UUID, H null native intervals, and
N backend-specific timing metadata. It hashes every retained package file. Raw
child evidence is never changed.

The separately versioned supervisor record is updated durably at
`<supervisor_record_path>.incomplete` after each child and renamed to the
declared path when supervision terminates normally. It records the manifest
identity and hash, scheduled and actual order, launch/exit timestamps, exit
codes, timeout or termination reason, progress counts, package paths and file
hashes, pairing/integrity status, admission eligibility and rejection reasons,
and overall execution status. A structurally valid completed package is still
rejected from admission after any supervisor timeout, progress failure, or
nonzero child exit. An unexpected
supervisor death leaves the durable incomplete record and closes the job,
terminating its child where Windows job semantics permit.

The record is execution-control evidence only. It contains no cross-process
statistics, backend-performance analysis, selected W, sample qualification,
instrumentation-perturbation conclusion, or Gate-0 verdict.
