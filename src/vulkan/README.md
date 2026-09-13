# EX-1 A7 Vulkan instrument

`ComputeLabVulkanTransform` implements only the A3 unsigned-integer transform
and the A7 device-timeline dispatch bracket. It links Vulkan alone; the tests
link the existing A2 generator and A3 oracle. It does not select or correlate
CUDA devices, collect samples, or make performance/backend-selection claims.

## Setup and ownership

`TransformDispatch(elementCount, spirvPath, physicalDeviceIndex)` owns one
fixed-size operation. The default device index is zero in Vulkan's enumeration;
selection does not search other devices on feature failure. The component
requires core Vulkan 1.3, queries Vulkan 1.2/1.3 feature structures, and explicitly
enables only `hostQueryReset` and `synchronization2`. It selects the first queue
family with an available compute queue and nonzero timestamp width, requires
36 through 64 valid bits, and uses exactly one queue. Device/queue properties,
enabled features and memory flags remain available through `Diagnostics()`.

The private resource owner holds the instance, logical device, command pool
(three primary command buffers), two-query timestamp pool, compute and transfer
fences, descriptors, pipeline layout, shader module, pipeline, and four separate
buffer/memory pairs. Device-local input/output have storage usage; mapped
host-visible upload/readback buffers have transfer usage only. All allocations,
mapping, descriptor updates and command recording occur in construction.
Workgroup, dispatch and storage-buffer limits are checked before pipeline use.

Partial construction is error safe. Destruction first drains any pending work
using its specific fence, destroys command buffers with their pool, then pipeline
and descriptor/query/fence objects, unmaps and destroys buffers/frees their
allocations, and finally destroys device and instance. Destructors do not throw.
Teardown waits indefinitely if the caller abandons submitted work; device loss
permits cleanup. An unexpected teardown wait failure terminates rather than
destroying resources with unknown completion. No queue/device idle waits are
used. The object is neither copyable nor movable and requires serial host use.

## Shader build

CMake discovers `Vulkan::glslc` through the installed SDK. Its custom command
builds `Ex1Transform.comp` into `Ex1Transform.comp.spv` in this target's binary
directory. The Vulkan 1.3 environment with SPIR-V 1.3 uses literal `LocalSize`
without adding a `maintenance4`/`LocalSizeId` requirement. CMake passes the generated
path to tests; callers supply the path at setup. There is no runtime compiler or
machine-specific source path. The shader uses 256 x 1 x 1 invocations, a uint32
element count push constant, and a logical-index bounds guard.

## Invocation lifecycle

1. `Upload(input)` copies host data into staging, flushes if noncoherent, submits
   the pre-recorded untimed upload commands and waits on the transfer fence.
2. `PrepareMeasurement()` host-resets both queries with `vkResetQueryPool` and
   resets the compute fence. This phase belongs outside host submission timing.
3. `SubmitTransform()` submits the pre-recorded compute commands using
   `vkQueueSubmit2` and returns without any completion wait.
4. `WaitForCompletion()` waits on that compute submission's fence. Its default
   timeout is 30 seconds. Timeout throws and leaves the operation pending; the
   caller can explicitly wait again, but cannot reset or reuse pending work.
5. `DeviceElapsedNanoseconds()` is allowed only after explicit completion and
   never synchronizes. `RetrieveOutput()` separately submits the untimed readback
   commands and waits on the transfer fence before copying mapped data to a vector.

Timing and output retrieval can occur in either order after completion. Callers
must validate output against A3 before accepting a duration; correctness tests
perform that gate first. A duration of zero is valid. Reuse requires another
`Upload`, including for identical input. Upload invalidates access to the prior
duration. Repeated output retrieval before the next upload is allowed. Invalid
phases throw `logic_error`; wrong input size throws `invalid_argument`.
Transfer/setup failures invalidate the operation, and Vulkan failures retain
the operation name and result code. No timeout is treated as success.

The compute command buffer contains exactly:

```
bind compute pipeline
bind descriptor set
push elementCount
write timestamp 0 at TOP_OF_PIPE
dispatch ceil(elementCount / 256) workgroups (one guarded group for zero)
write timestamp 1 at BOTTOM_OF_PIPE
```

This is a broad device-timeline dispatch bracket, not exact shader-core
execution time. No queries are reset on the GPU. There are no copies, memory
barriers, allocations, recording, logging or serialization in submission's
steady-state path. Command buffers have neither simultaneous-use nor one-time
flags and cannot be resubmitted while pending.

## Transfers and timestamps

All transfer work uses separate untimed submissions on the same queue.
Synchronization2 buffer dependencies use COPY/TRANSFER_WRITE to
COMPUTE_SHADER/SHADER_STORAGE_READ after upload, and
COMPUTE_SHADER/SHADER_STORAGE_WRITE to COPY/TRANSFER_READ before readback.
Additional precise dependencies cover reuse of input, output and readback;
readback ends with COPY/TRANSFER_WRITE to HOST/HOST_READ. TOP/BOTTOM stages are
used only for the two timestamp writes.

Staging types prefer coherent memory; readback also prefers cached memory.
Noncoherent upload is flushed before transfer, and readback is invalidated after
transfer completion. Each buffer has a separate allocation mapped in full, so
offset zero plus `VK_WHOLE_SIZE` respects atom alignment and the allocation-end
exception without affecting other allocations. Input/output are never mapped.
Zero elements allocate a four-byte dummy capacity, omit copies, dispatch one
fully guarded group, and return the A3 empty result.

Query retrieval uses exactly `64_BIT | WITH_AVAILABILITY_BIT`, never `WAIT_BIT`.
`VK_NOT_READY`, any other unsuccessful result, or either zero availability value
throws immediately. Availability may be any nonzero value. For widths below 64,
both timestamps are masked and the difference is masked to that width. Width 64
uses unsigned subtraction without shifting by 64. This handles an ordinary
rollover; it cannot detect multiple complete wraps.

Ticks multiply the device's finite positive `timestampPeriod` using a
`long double` intermediate and round to the nearest integer nanosecond (half up
for nonnegative values). Unit-period conversion retains the exact integer delta.
Invalid metadata, nonfinite/negative intermediates and overflow are rejected,
never clamped. The exclusive 2^64 range check avoids an unsafe float-to-integer
cast on MSVC, where `long double` has binary64 precision. Integer nanoseconds
are storage units, not a measurement-precision claim.
