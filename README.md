# ComputeLab

ComputeLab is a standalone C++ laboratory for evaluating low-level GPU compute,
systems, and measurement infrastructure.

The project is intentionally domain-neutral. It exists to answer general
engineering questions about technologies such as CUDA, Vulkan Compute,
measurement methodology, memory behavior, synchronization, and optional
compute-to-graphics interoperability.

ComputeLab is designed to remain independently understandable and publishable.

## Current scope

The current phase is Q0: generic infrastructure qualification.

The first active experiment is EX-1, whose purpose is to validate the benchmark
and timing methodology before comparative CUDA/Vulkan measurements are trusted.

ComputeLab is not a production framework and is not intended to become one.

## Repository boundaries

Only measurements, compatibility observations, experimental limitations, and
engineering conclusions are intended to leave this repository.

The project must not reproduce or anticipate the architecture, schemas,
scheduling model, or domain semantics of another application.

See:

- `docs/charter.md`
- `docs/methodology.md`
- `docs/ex1.md`
- `docs/results-format.md`

## Toolchain

The initial Windows development environment uses:

- C++20
- CMake
- Ninja
- Visual Studio 2026 / MSVC
- CUDA Toolkit 13.4
- Vulkan SDK 1.4
- vcpkg manifest mode
- GoogleTest / CTest

CUDA and Vulkan are machine-installed SDKs.

Ordinary C++ dependencies are declared through `vcpkg.json`.

## Build and test

Set `VCPKG_ROOT` to the standalone vcpkg checkout on the current machine.

Then run:

```powershell
.\scripts\test.ps1
```

The script initializes the Visual Studio x64 developer environment, configures
the project, builds it, and runs the test suite.

The current bootstrap must preserve these smoke checks:

- GoogleTest integration
- CUDA compilation and device execution
- Vulkan loader/device discovery, device timestamp support, and logical compute
  queue creation
- CUDA/Vulkan physical-device identity matching

## Evidence

Ordinary local experimental output belongs under `results/local/` and is not
tracked by Git.

Exploratory runs may use dirty or untracked source state, but they must remain
local-only.

Only deliberately reviewed evidence packages may be placed under
`results/evidence/`. Curated runs require a clean committed working tree,
checked immediately before the run with:

```powershell
.\scripts\assert-curated-evidence-ready.ps1
```

Benchmark results are not accepted solely because an executable completed.

Correctness validation and experiment metadata are required by the methodology.

## Status

Environment and repository bootstrap are complete.

EX-1 is ready for human approval. Its workloads and timing infrastructure must
not be implemented until that approval is recorded in Git history.
