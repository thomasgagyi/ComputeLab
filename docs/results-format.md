# ComputeLab result format

## Purpose

ComputeLab preserves experiment evidence in machine-readable files.

The initial result package contains:

- `environment.json`
- `initialization.csv`
- `samples.csv`
- `summary.json`

Raw samples are authoritative evidence.

Summary data is derived from raw samples.

## General conventions

Schema versions are positive integers.

Timestamps use UTC ISO 8601.

Durations use integer nanoseconds in persisted raw records unless an experiment
explicitly requires another representation.

Byte counts are integers.

Identifiers are stable strings within a result package.

CSV files use UTF-8, include exactly one header row, and follow RFC 4180 quoting
rules. Boolean values are lowercase `true` or `false`.

Missing or not-applicable measurements are represented as empty CSV fields or
JSON `null` according to the file format.

Machine identifiers are anonymous project identifiers rather than hostnames or
usernames.

Examples:

- `q0-turing`
- `q0-ampere`
- `q0-blackwell`

## `environment.json`

One environment manifest is written for each experiment session.

Required fields:

```text
schema_version

experiment_id
run_id
timestamp_utc

git_commit
git_dirty

machine_id

os_name
os_version

cpu_name
system_memory_bytes

gpu_name
gpu_vendor
gpu_device_id
gpu_memory_bytes

nvidia_driver_version

cuda_toolkit_version
cuda_runtime_version
cuda_compute_capability

vulkan_sdk_version
vulkan_device_api_version

compiler_name
compiler_version

cmake_version
ninja_version

configure_preset
build_type

validation_enabled
diagnostic_instrumentation
```

Fields that do not apply to a backend remain `null` rather than being invented.

`git_dirty` is `true` if staged changes, unstaged changes, or untracked files
were present when the run began. Ignored build and local-result files do not by
themselves make the source tree dirty.

The environment manifest must not include:

- username
- home directory
- machine hostname
- private repository credentials
- access tokens
- unrelated process information

## `initialization.csv`

Initialization and first-use evidence is recorded separately from steady-state
samples. One row represents one ordered initialization measurement or
observation from a fresh experiment process.

Initial columns:

```csv
schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation
```

`process_index` identifies the fresh process within the initialization study.
`sequence_index` is zero-based within that process. `category` and `metric` are
stable experiment-defined identifiers for the initialization operation and its
measurement. `workload`, `variant`, and `element_count` provide workload context
when initialization depends on them and are otherwise empty. `duration_ns` is
empty for a qualitative observation, while `observation` is empty for a purely
numeric measurement. At least one of those two fields must be populated.

Initialization rows are raw evidence. They are never mixed into steady-state
sample summaries.

## `samples.csv`

One row represents one accepted or rejected measured sample.

Initial columns:

```csv
schema_version,run_id,experiment_id,series_id,backend,workload,variant,seed,element_count,warmup_count,planned_sample_count,sample_index,validation_passed,upload_ns,host_submission_ns,device_execution_ns,end_to_end_ns,download_ns
```

### Field semantics

#### `series_id`

Stable identifier for one measured sequence. Rows with different warm-up or
planned sample counts use different series identifiers even when every other
parameter is equal.

#### `backend`

Initial values may include:

- `cpu`
- `cuda`
- `vulkan`

#### `workload`

Stable experiment-defined workload identifier.

#### `variant`

Optional implementation or measurement variant identifier.

#### `seed`

Input-generation seed.

#### `element_count`

Number of logical workload elements.

#### `warmup_count`

Number of warm-up iterations completed before measured sampling.

#### `planned_sample_count`

Number of measured samples declared for the sequence before collection begins.

#### `sample_index`

Zero-based index within the measured sample sequence.

#### `validation_passed`

Boolean indicating whether the sample's result satisfied the declared
correctness contract.

#### `upload_ns`

Upload duration when the workload declares upload as a separately relevant
measurement.

#### `host_submission_ns`

Host-side submission duration when meaningful.

#### `device_execution_ns`

GPU device execution duration when meaningful.

#### `end_to_end_ns`

Declared end-to-end operation duration.

#### `download_ns`

Download duration when the workload declares download as a separately relevant
measurement.

A performance sample whose correctness validation fails remains recorded but is
not included in accepted performance summaries.

## `summary.json`

A summary is generated from raw samples rather than measured independently.

The top-level object has this structure:

```json
{
  "schema_version": 1,
  "run_id": "run-id",
  "experiment_id": "EX-1",
  "sample_groups": []
}
```

Each `sample_groups` entry contains a `group` object, `recorded_sample_count`,
`validation_failures`, and a `metrics` object. The group object contains these
keys:

```text
series_id
backend
workload
variant
seed
element_count
warmup_count
planned_sample_count
```

The metrics object uses the applicable timing column name as its key. Initial
metric keys are `upload_ns`, `host_submission_ns`, `device_execution_ns`,
`end_to_end_ns`, and `download_ns`. A metric that is not applicable to the
group has a JSON `null` value.

`recorded_sample_count` counts every row in the series, including rejected rows.
`validation_failures` counts validation-failing rows in the series. Those rows
are excluded from every performance statistic.

For every applicable metric preserve:

```text
sample_count
minimum
median
mean
standard_deviation
coefficient_of_variation
p95
```

`sample_count` is the number of validation-passing rows with a nonempty value
for that metric.

Statistics use the following deterministic rules over the accepted metric
values:

- Convert integer nanoseconds to IEEE 754 binary64 values in ascending
  `sample_index` order.
- `minimum` is the least value.
- `median` is the middle sorted value for an odd count and the binary64 average
  of the two middle values for an even count.
- `mean` is the arithmetic mean calculated by a sequential sum in ascending
  `sample_index` order.
- `standard_deviation` is the sample standard deviation with denominator
  `sample_count - 1`, calculated in a second pass in ascending `sample_index`
  order. It is `null` when `sample_count` is less than two.
- `coefficient_of_variation` is `standard_deviation / mean`, expressed as a
  ratio rather than a percentage. It is `null` when the standard deviation is
  `null` or the mean is zero.
- `p95` uses the nearest-rank rule: after sorting ascending, select one-based
  rank `ceil(0.95 * sample_count)`.

When an applicable metric has no accepted values, its `sample_count` and
all six distribution values are `null`. No intermediate value is rounded.
Binary64 values are serialized using the shortest decimal representation that
round-trips to the same binary64 value.

The order of `sample_groups` entries and JSON object members is not significant.

## Evidence directory

Ordinary local runs belong under:

`results/local/`

and are ignored by Git.

A deliberately reviewed evidence package may later be copied into:

`results/evidence/`

only after its correctness, metadata, and interpretation have been reviewed.

Curated evidence additionally requires a clean committed source tree when the
run is performed. Immediately before a curated run, execute:

```powershell
.\scripts\assert-curated-evidence-ready.ps1
```

The check requires a committed `HEAD` and no staged, unstaged, or untracked
files. A curated environment manifest therefore has `git_dirty` set to `false`.
Dirty-tree runs are valid for exploration only, remain under `results/local/`,
and cannot later be promoted as curated evidence.

## Schema evolution

Any incompatible interpretation change increments `schema_version`.

Do not silently change the meaning or units of an existing field.

New optional fields may be introduced when they do not change the meaning of
existing records.
