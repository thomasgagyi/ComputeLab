# EX-1 — Measurement validity and reproducibility

## Status

Approved.

EX-1 implementation may begin under the committed specification.
Any change to experiment semantics, timing boundaries, controls, or
acceptance criteria requires human review before execution.

## Question

Can ComputeLab produce correct, stable, interpretable, and reproducible timing
measurements whose uncertainty is understood well enough to support later
infrastructure comparisons?

## Purpose

EX-1 validates the measuring instrument before ComputeLab compares CUDA and
Vulkan infrastructure.

EX-1 does not attempt to determine which GPU API is faster.

## Hypothesis

After separating initialization from steady-state work and selecting an
evidence-based warm-up and sample methodology, repeated measurements under
materially unchanged conditions will be sufficiently stable and interpretable
to distinguish meaningful differences in later Q0 experiments.

## Non-goals

EX-1 does not:

- select the production GPU backend
- model a production workload
- benchmark neural or domain-specific behavior
- optimize CUDA against Vulkan
- test final memory layouts
- test graph/event scheduling
- establish multi-GPU behavior

## Correctness workload

EX-1 uses a small deterministic numeric transform.

Input is generated from an explicitly recorded seed.

A CPU implementation is the correctness oracle.

CUDA and Vulkan implementations must produce the same declared result as the
CPU oracle before their samples are accepted.

Integer operations are preferred initially so correctness can be exact rather
than tolerance-based.

## Initial workload classes

The EX-1 discovery phase includes:

- CPU deterministic operation
- CUDA tiny deterministic operation
- Vulkan tiny deterministic operation
- a small GPU workload
- a larger GPU workload sufficient to distinguish tiny-dispatch behavior from
  more sustained execution

The exact element counts become experiment parameters rather than hidden
constants.

## Independent variables

EX-1 initially varies:

### Warm-up count

- 0
- 1
- 3
- 5
- 10
- 20

### Measured sample count

- 10
- 30
- 100

### Workload size

- small discovery case
- larger discovery case

### Backend

Where the metric applies:

- CPU
- CUDA
- Vulkan

### Instrumentation

Ordinary measurement versus selected diagnostic conditions where relevant.

## Controlled variables

Within a comparison, preserve:

- physical machine
- GPU
- source revision
- compiler/build preset
- input seed
- workload semantics
- workload size
- backend configuration
- power/driver/toolkit environment as recorded in metadata

## Procedure

### Phase 1 — Initialization characterization

Run each GPU path from a fresh process and record first-use behavior separately
from steady-state dispatches.

Identify costs caused by backend/context/device/pipeline initialization.

### Phase 2 — Warm-up study

For each selected workload size, execute the declared warm-up sweep.

Inspect timing versus sample position and determine whether early executions
systematically differ from the later steady state.

Establish the minimum justified warm-up procedure for subsequent experiments.

### Phase 3 — Sample-count study

Using the selected warm-up procedure, collect 10, 30, and 100 measured samples.

Compare the resulting distribution summaries and determine whether additional
samples materially alter interpretation.

### Phase 4 — Repeatability

Repeat the complete selected measurement under materially unchanged conditions.

Compare the independent runs.

### Phase 5 — Instrumentation check

Where applicable, compare ordinary measurement against relevant diagnostic or
validation instrumentation to determine whether instrumentation materially
changes timing.

## Required measurements

Record:

- host submission time where meaningful
- device execution time for GPU operations
- end-to-end time
- correctness result
- raw sample sequence
- initialization observations

## Acceptance criteria

EX-1 is complete only when all of the following are true.

1. Every timed GPU operation has a deterministic correctness check against the
   CPU oracle.

2. Host submission, device execution, and end-to-end timing are independently
   defined and implemented.

3. Initialization effects are identified and separated from steady-state
   samples.

4. The selected warm-up method is justified by observed data.

5. The selected sample count is justified by observed data.

6. Measurement and diagnostic instrumentation overhead is characterized where
   it could affect interpretation.

7. The environment record contains enough information to identify the run
   context.

8. Raw samples are preserved and derived summaries can be regenerated from
   them.

9. Independent repeat runs under materially unchanged conditions produce
   compatible interpretations.

10. No result depends on domain-specific or production-application semantics.

## Failure / inconclusive outcome

EX-1 is allowed to conclude that the current harness is not sufficiently stable
or understood.

In that case, later comparative experiments remain blocked while the
measurement method is corrected.

A failed or inconclusive EX-1 is retained as experiment history.

## Downstream effect

EX-2 may begin only after EX-1 establishes a credible measurement methodology
or an explicit subsequent experiment resolves the outstanding measurement
problem.
