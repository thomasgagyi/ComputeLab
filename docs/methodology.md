SETUP
excluded

INPUT GENERATION
excluded

UPLOAD
measured separately if relevant

HOST SUBMISSION
host timer

DEVICE EXECUTION
CUDA events / Vulkan timestamps

COMPLETION
part of end-to-end timing

DOWNLOAD
measured separately if relevant

VALIDATION
excluded

RESULT WRITING
excluded

Correctness before timing.

Same semantic workload for candidate implementations.

Same physical GPU for direct CUDA-vs-Vulkan comparisons.

Raw samples retained.

Initialization separated from steady state.

No performance result accepted without environment metadata.

Validation/debug instrumentation excluded from ordinary performance
numbers unless explicitly being measured.

No single-run architectural conclusions.