# ComputeLab agent instructions

ComputeLab is a standalone, generic compute-infrastructure experiment.

Before changing experiment behavior, read:

- `docs/charter.md`
- `docs/methodology.md`
- the active experiment specification under `docs/`

## Hard rules

- Do not introduce domain-specific simulation concepts.
- Do not create reusable production GPU/framework architecture.
- Do not model graphs, event runtimes, schedulers, learning systems,
  checkpoints, or domain-specific entities.
- Keep CUDA and Vulkan implementations backend-native.
- Do not create a generic `IGpuBackend` abstraction.
- Correctness must pass before performance data is accepted.
- CUDA and Vulkan variants of a workload must preserve identical
  workload semantics.
- Do not place initialization, result validation, logging, or file I/O
  inside timed regions unless the experiment explicitly measures them.
- Preserve raw samples; do not report only aggregates.
- Do not silently change experiment methodology.

## Build and validation

Run:

`scripts/test.ps1`

before considering a code change complete.

For smoke tests:

`ctest --preset x64-debug -L smoke --output-on-failure`

## Scope

Only measurements and conclusions are intended to leave this repository.