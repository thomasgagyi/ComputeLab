#pragma once

#include "ex2/Ex2HostTiming.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::cuda
{

namespace detail
{

enum class CudaResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

// CUDA enqueue/launch status APIs may surface an earlier asynchronous error.
// Only separately established safe completion permits ordinary destruction;
// otherwise this operation may still have work referencing its resources.
[[nodiscard]] constexpr CudaResourceDisposition
ClassifyCudaResourceDisposition(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? CudaResourceDisposition::DestroyNormally
        : CudaResourceDisposition::PreserveForProcessTeardown;
}

} // namespace detail

enum class CudaQualificationStatus
{
    Ok,
    SubmitFailed,
    WaitFailed,
    ReadbackFailed,
    ValidationFailed,
    TimingInvalid,
};

enum class CudaNativeTimingStatus
{
    NotApplicable,
    Valid,
    Unavailable,
    MarkerFailed,
    RetrievalFailed,
    ConversionInvalid,
};

enum class CudaQualificationFailurePhase
{
    None,
    Submission,
    CompletionWait,
    Readback,
    Validation,
    HostTiming,
    StartMarker,
    StopMarker,
    NativeTimingRetrieval,
    NativeTimingConversion,
};

struct CudaNativeTimingMetadata
{
    // cudaEventElapsedTime returns single-precision milliseconds. CUDA timing
    // events have approximately 0.5 microsecond resolution; integer-nanosecond
    // storage below does not imply nanosecond measurement resolution.
    std::string_view method{"cudaEventElapsedTime"};
    std::string_view eventCreateFlags{"cudaEventDefault (timing enabled)"};
    std::string_view elapsedValueRepresentation{"float milliseconds"};
    std::uint64_t approximateResolutionNanoseconds{500U};
};

struct CudaQualificationExecution
{
    CudaQualificationStatus status{CudaQualificationStatus::SubmitFailed};
    CudaQualificationFailurePhase failurePhase{CudaQualificationFailurePhase::None};
    ex2::HostTimingIntervals hostTiming;
    std::optional<int> nativeErrorCode;
    std::string nativeErrorName;
    std::string errorMessage;
    bool validationPassed{};
    std::vector<std::uint32_t> output;
    std::string_view instrumentMode{"H"};
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
    CudaNativeTimingStatus nativeTimingStatus{
        CudaNativeTimingStatus::NotApplicable};
    std::optional<CudaNativeTimingMetadata> nativeTimingMetadata;
};

namespace detail
{

// Checked conversion for the native cudaEventElapsedTime result. The returned
// integer is a storage representation, not a claim of nanosecond resolution.
[[nodiscard]] std::uint64_t QualificationDeviceMillisecondsToNanoseconds(
    float milliseconds);

} // namespace detail

// A separate, fixed diagnostic operation for EX-2 Gate-0 qualification.
// It intentionally does not modify or wrap CudaTransformOperation.
// Uncertain asynchronous completion retains native resources for later child-
// process termination rather than freeing memory that may still be referenced.
class CudaQualificationOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    CudaQualificationOperation(int deviceOrdinal, std::size_t elementCount);
    ~CudaQualificationOperation() noexcept;

    CudaQualificationOperation(const CudaQualificationOperation&) = delete;
    CudaQualificationOperation& operator=(const CudaQualificationOperation&) = delete;
    CudaQualificationOperation(CudaQualificationOperation&&) = delete;
    CudaQualificationOperation& operator=(CudaQualificationOperation&&) = delete;

    [[nodiscard]] std::size_t ElementCount() const noexcept;

    // Upload, its completion wait, and CPU-oracle construction are untimed.
    void Upload(std::span<const std::uint32_t> input);

    // Enqueues exactly one kernel in the owned nonblocking stream, then waits
    // on that same stream. The measured CUDA submission interval is the kernel
    // launch plus its immediate cudaGetLastError launch-status check: t0 is
    // captured before the launch and t1 immediately after the check returns.
    // Readback and exact validation occur after t2.
    [[nodiscard]] CudaQualificationExecution ExecuteHostOnly();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// A distinct timing-enabled qualification operation. Unlike mode H, its two
// reusable cudaEventDefault resources and every per-operation cudaEventRecord
// call are part of the declared mode-N instrument condition.
class CudaDeviceTimedQualificationOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    CudaDeviceTimedQualificationOperation(
        int deviceOrdinal,
        std::size_t elementCount);
    ~CudaDeviceTimedQualificationOperation() noexcept;

    CudaDeviceTimedQualificationOperation(
        const CudaDeviceTimedQualificationOperation&) = delete;
    CudaDeviceTimedQualificationOperation& operator=(
        const CudaDeviceTimedQualificationOperation&) = delete;
    CudaDeviceTimedQualificationOperation(
        CudaDeviceTimedQualificationOperation&&) = delete;
    CudaDeviceTimedQualificationOperation& operator=(
        CudaDeviceTimedQualificationOperation&&) = delete;

    [[nodiscard]] std::size_t ElementCount() const noexcept;
    void Upload(std::span<const std::uint32_t> input);

    // t0 precedes start-event recording. t1 follows the start record, exactly
    // one kernel launch, its immediate launch-status check, and stop-event
    // record. t2 is captured only after successful stream synchronization.
    // Native elapsed-time retrieval, readback, and validation follow t2.
    [[nodiscard]] CudaQualificationExecution ExecuteDeviceTimed();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
