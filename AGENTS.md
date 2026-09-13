# ComputeLab agent instructions

ComputeLab is a standalone, generic compute-infrastructure experiment.

Before modifying experiment behavior, read:

- `docs/charter.md`
- `docs/methodology.md`
- the active experiment specification under `docs/`
- `docs/results-format.md`

These documents are authoritative.

## Hard boundaries

Do not introduce domain-specific simulation concepts.

Do not create or anticipate production application architecture, schemas,
runtime semantics, checkpoints, persistence formats, or domain-specific data
structures.

Do not create disguised domain simulations using generic names.

Do not turn ComputeLab into a reusable GPU framework.

Keep CUDA and Vulkan implementations backend-native.

Do not introduce a generic `IGpuBackend` abstraction.

Do not silently change experiment semantics, timing boundaries, controls,
sample methodology, or acceptance criteria.

Only measurements, compatibility observations, experimental limitations, and
engineering conclusions are intended to leave this repository.

## Correctness and measurement

Correctness must pass before performance measurements are accepted.

Equivalent CUDA and Vulkan experiments must preserve the same semantic workload.

Initialization, input generation, validation, logging, file output, and result
serialization must remain outside timed regions unless an experiment explicitly
measures them.

Retain raw samples.

Do not report only aggregates.

Curated evidence requires a clean committed working tree. Results produced from
a dirty or untracked source state are exploratory and must remain under
`results/local/`.

Do not infer production-application performance from ComputeLab results.

## Build and validation

The canonical Windows validation command is:

```powershell
.\scripts\test.ps1
```

Run it after code changes before declaring work complete.

Smoke-only tests may be run with:

```powershell
ctest --preset x64-debug -L smoke --output-on-failure
```

Do not bypass failing tests.

If canonical build or validation is blocked because the sandbox cannot access
the installed compiler, GPU, SDKs, vcpkg checkout, or other required host
resources, request the necessary local execution permission rather than
modifying project code, build configuration, or methodology to work around the
sandbox restriction.

## Scope discipline

Implement only the requested task.

Do not add adjacent infrastructure, abstractions, dependencies, refactors, or
optimizations unless they are necessary to satisfy the task or explicitly
approved.

Do not commit or push changes unless the task explicitly requests it.
