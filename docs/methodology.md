# ComputeLab measurement methodology

## Purpose

This document defines measurement rules shared by ComputeLab experiments.

Experiment-specific documents may narrow these rules but must not silently
contradict them.

## Correctness precedes performance

A performance sample is valid only if the corresponding operation satisfies the
experiment's correctness contract.

Incorrect output invalidates the associated performance data.

A faster incorrect implementation is not a successful result.

## Timing domains

ComputeLab distinguishes at least three timing domains.

### Host submission time

Measures CPU-side work required to submit the operation under test.

Use a monotonic host clock.

For C++, the default host timing source is
`std::chrono::steady_clock`.

### Device execution time

Measures GPU-side execution of the operation under test.

CUDA workloads use CUDA timing events.

Vulkan workloads use Vulkan device timestamp queries.

### End-to-end time

Measures the declared host-visible operation interval, including required
submission and completion synchronization.

The exact start and stop boundaries must be documented per workload.

The three timing domains must not be substituted for one another.

## Default measurement boundaries

Unless an experiment explicitly states otherwise:

### Setup

Excluded.

### Input generation

Excluded.

### Resource creation

Excluded from steady-state timing.

### Upload

Excluded from device execution timing and measured separately when relevant.

### Host submission

Measured by the host-submission metric.

### Device execution

Measured by the backend's device timing mechanism.

### Completion / required synchronization

Included in end-to-end timing.

### Download

Measured separately when relevant.

### Correctness validation

Excluded.

### Logging

Excluded.

### Result serialization

Excluded.

### File I/O

Excluded.

## Initialization versus steady state

First-use costs must not be silently mixed into steady-state measurements.

Examples include:

- CUDA context initialization
- device or module initialization
- Vulkan instance/device creation
- pipeline creation
- shader/module creation
- memory allocation
- descriptor/resource setup

Where initial startup cost is relevant, it must be recorded as a separate
measurement category.

## Warm-up

No universal warm-up count is assumed.

EX-1 determines whether warm-up is required and establishes the procedure used
by later Q0 experiments.

The selected warm-up procedure must be documented and applied consistently to
comparable candidate implementations.

## Samples and repetition

No architecture conclusion may rely on a single timed sample.

Raw samples must be retained.

Derived statistics must be reproducible from the retained samples.

The exact formulas, null handling, calculation precision, and grouping rules
are defined by `docs/results-format.md` and are part of the result schema.

The initial statistical vocabulary is:

- sample count
- minimum
- median
- arithmetic mean
- standard deviation
- coefficient of variation
- 95th percentile

The methodology may be refined when EX-1 evidence justifies doing so.

## Backend comparability

Direct CUDA-versus-Vulkan claims require the same physical GPU.

Equivalent candidate implementations must preserve:

- workload semantics
- input data
- output semantics
- precision
- workload size
- correctness requirements
- declared timing boundaries

Implementation details may remain backend-native.

Fair comparison does not require Vulkan to look like CUDA or CUDA to look like
Vulkan.

## Instrumentation

Debugging, validation, tracing, profiling, or diagnostic instrumentation that
materially alters timing must not be silently enabled for ordinary performance
measurements.

Instrumentation state is part of the environment record.

Diagnostic measurements may deliberately enable such tools, but they must be
identified as a different measurement condition.

## Result preservation

Raw measurement samples are the primary evidence.

Summary statistics are derived artifacts and must not replace raw samples.

The Git commit and environment metadata associated with a result must be
preserved.

Exploratory runs may use a dirty working tree, but their environment manifest
must record `git_dirty` as `true` and their results must remain under
`results/local/`.

Curated evidence requires that `HEAD` resolve to a commit and that the working
tree, including staged changes and untracked files, be clean when the run is
performed. A result produced from dirty or untracked source state cannot be
promoted later merely by cleaning or committing the tree. Run
`scripts/assert-curated-evidence-ready.ps1` immediately before producing a
curated evidence package.

## Interpretation

A benchmark winner is not automatically an architecture decision.

ComputeLab reports only what the named workload, machine, software environment,
and measurement contract support.

Results must not be generalized to unrelated hardware, vendors, workloads, or
production applications without separate evidence.
