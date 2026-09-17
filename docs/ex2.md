# EX-2 — Generic GPU infrastructure qualification

## Status and authorization

**Specification v1.0 — approved design baseline; measurement qualification pending.** Design freeze date: 2026-09-17. This version fixes the generic workload definitions, implementation boundary, initial experiment matrix, record contract and proposed qualification procedure. It is suitable for bounded Stage-1 measurement-support implementation and for Codex to design later work against. A future amendment must document changes to the protocol before affected evidence is collected.

**EX-2 experiment state: Proposed. Gate 0: NOT PASSED. Stage-2 execution-protocol authorization: NOT GRANTED. EX-2 candidate-performance collection: PROHIBITED.** Design approval does not turn uncollected qualification evidence into a PASS. The Stage-2 completion checkpoint is marked complete only after Gate 0, review of resulting applicability, clean committed specification and explicit execution authorization. A Gate-0 FAIL blocks comparative collection; a LIMITED PASS admits only named conditions/metrics. Codex may implement separately authorized qualification support, but must not execute candidate-performance campaigns or silently change this contract.

EX-1's final outcome was **measurement methodology not qualified for downstream comparison**. Its correctness/provenance evidence is useful; fresh-process host-visible CUDA/256 states, universally justified warm-up, and instrumentation-perturbation control are unresolved. We do **not** investigate the CUDA/256 root cause as a prerequisite. Tiny host-visible CUDA/Vulkan comparisons are excluded from the initial authorized scope; a later qualification can amend that exclusion. A fresh set of tidy results alone does not resolve the anomaly.

**Authority:** `AGENTS.md`, `docs/charter.md`, `docs/methodology.md`, and `docs/results-format.md` define common rules; this approved EX-2 design narrows them without changing EX-1's historical interpretation. EX-1 source, frozen executable/SPIR-V, evidence and schema v1 remain intact. A contradiction with a common rule blocks implementation until reviewed, not silently overridden.

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

## Gate 0 — frozen qualification *procedure*, not a verdict

This section prespecifies the bounded method. **There is no Gate-0 PASS at v1.0 publication.** Root-cause diagnosis of EX-1 CUDA/256 is out of scope. EX-1's distinct process state remains visible, and tiny host-visible cross-backend claims are excluded by default. Gate 0 must independently establish a viable comparison scope on a representative generic operation or issue FAIL. Stage 5 must then confirm scope on actual EX-2 tiny/large cases before Stage 6.

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

Only a separately authorized generic **qualification** instrument may run before the gate, using representative non-neural arithmetic/transfer work. The initial instrument should reuse existing verified semantic operations where possible and add only the H/N host-timing capability needed; do not implement the A–E suite to 'qualify' it first. Fix exact representative conditions, source/build hashes and machine before launch. For each representative class, warm-up characterization uses five paired processes with 48 early operations; **new** five paired processes then use the selected W and up to 200 sample-count observations each. H/N adds separate five-mode-block controls within each backend. These process groups are distinct and their results cannot be pooled as interchangeable replicates. When a new A–E family is introduced after Gate 0, qualify its warm-up/sample applicability with the same bounded method as an explicitly labeled qualification task before its Stage-6 candidate cells; Stage 5 initially performs that confirmation for A1/D1. No CUDA/256 diagnostic escalation, broad parameter sweep or profiler campaign is authorized by this specification. Every qualification package is labeled `qualification` and **cannot later be relabeled candidate-performance**.

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

The strategic Notion roadmap retains Stages 0–9; implementation milestones I0–I7 refine them. **Allowed first code task while Gate 0 is pending:** add a separate pure host-timing-decomposition contract and unit tests, plus minimal EX-2 qualification-support types if necessary; keep EX-1 behavior and serializers unchanged, collect no candidate-performance data and do not claim a gate verdict. It is acceptable to prepare a bounded qualification instrument following a separately approved task. **Do not implement A–E performance runners until the Stage-1 gate and Stage-2 freeze/authorization have been recorded.**

After authorization: independent Mix/fixture/oracle and generator tests; config/identity/schema v2; A1 CPU→CUDA→Vulkan vertical slice; A2; B; C; D1; E; conditional D2 only by amendment; integrated runner/fresh-process control and full correctness qualification. Preserve EX-1 tests. Do not prematurely add abstractions, dependencies or utility layers shared with consuming applications. Run ` .\scripts\test.ps1 ` locally after code changes; if host SDK or GPU access is unavailable, report rather than bypass.

Tests require: exact generator/golden constants and independently calculated expected outputs; permutation uniqueness and malformed-index rejection; unsigned and atomic correctness/overflow; N=0, N=1, N=257 and workgroup-boundary correctness; D K=0/1/16/64 parity and read-after-write/write-after-read barriers; C reset, output invariants and source preservation; E byte-exact copy and coherent/noncoherent memory visibility; event/timestamp validity and scope metadata; t0/t1/t2 arithmetic, failure, timeout and null behavior; schema header/version/identity collisions, sample ordering and independent summary regeneration; same physical GPU enforcement, true instrumentation declarations, all historical EX-1 tests and canonical Windows smoke tests.

## Acceptance checkpoints and revision policy

- **Stage 0 design:** A–E portfolio, formulas, exclusions and initial cell ceiling are approved by the v1.0 design freeze. Retain D2 conditional.
- **Stage 1 measurement Gate 0:** remains **PENDING** until reviewed qualification evidence yields PASS/LIMITED PASS/FAIL with exact admitted/excluded scopes. A formally reviewed exclusion is allowed; claiming exclusion alone qualifies the rest is not.
- **Stage 2 protocol freeze:** requires Gate 0, verified applicability of chosen measurements, clean committed `docs/ex2.md` with any required gate-driven amendment and explicit human authorization. This document is the approved **design baseline**, not a premature claim of Stage-2 completion.
- **Stage 3 implementation:** EX-2 additive workloads/runner, no EX-1 semantic changes, all tests passed.
- **Stage 4 correctness:** independently verified exact CPU/CUDA/Vulkan outputs and valid resource/synchronization behavior before performance interpretation.
- **Stage 5:** RTX 2060 SUPER tiny A1/sustained D1 checks the qualified contract; failure stops Stage 6 and may revoke Gate-0 admission.
- **Stages 6–9:** selected eligible cells, process-resolved raw evidence, accessible cross-generation subset, separate tooling observations, correctness-first bounded synthesis and provisional starting-path discussion only within tested scope.

No EX-2 results exist in this design release. A documented amendment is necessary if measured qualifications require different W, sample counts, effect tolerance, metrics, hardware set, timing placement, shader/kernel formulas, schema or allowed cells. Record both the old and new revision; do not silently rewrite historical evidence or relabel qualification runs as performance runs. The operator must explicitly approve any actual commit or push.

## References

Internal: `AGENTS.md`, `docs/charter.md`, `docs/methodology.md`, `docs/results-format.md`, `docs/ex1.md`, and the EX-2 Notion Experiment/roadmap/DR-40/DR-41 records. This file contains the executable design details for agents without Notion access; Notion owns strategic decisions, not duplicate technical implementation text.

External API behavior, **not EX-2 qualification evidence**:

- NVIDIA CUDA Programming Guide, asynchronous execution and timing: https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/asynchronous-execution.html
- NVIDIA CUDA C++ Best Practices Guide, timing and effective bandwidth: https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/
- Khronos Vulkan synchronization examples: https://docs.vulkan.org/guide/latest/synchronization_examples.html
- Khronos Vulkan timestamp queries: https://docs.vulkan.org/spec/latest/chapters/queries.html
