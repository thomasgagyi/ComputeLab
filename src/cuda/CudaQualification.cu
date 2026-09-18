#include "cuda/CudaQualification.hpp"

#include "oracle/DeterministicTransform.hpp"
#include "timing/HostTiming.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace computelab::cuda
{
namespace
{

constexpr unsigned int ThreadsPerBlock = 256U;

std::string CudaErrorMessage(cudaError_t result, const char* operation)
{
    return std::string{operation} + " failed with " + cudaGetErrorName(result) +
        " (code " + std::to_string(static_cast<int>(result)) + "): " +
        cudaGetErrorString(result);
}

[[noreturn]] void ThrowCudaError(cudaError_t result, const char* operation)
{
    throw std::runtime_error(CudaErrorMessage(result, operation));
}

void CheckCuda(cudaError_t result, const char* operation)
{
    if (result != cudaSuccess)
    {
        ThrowCudaError(result, operation);
    }
}

void ReportCudaCleanupError(cudaError_t result, const char* operation) noexcept
{
    if (result != cudaSuccess)
    {
        std::fprintf(
            stderr,
            "%s failed with %s: %s\n",
            operation,
            cudaGetErrorName(result),
            cudaGetErrorString(result));
    }
}

__global__ void QualificationTransformKernel(
    const std::uint32_t* input,
    std::uint32_t* output,
    std::size_t elementCount)
{
    const std::size_t index =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    std::uint32_t value = input[index];
    const std::uint32_t logicalIndex = static_cast<std::uint32_t>(index);
    value ^= logicalIndex * 0x9E3779B9U;
    value ^= value >> 16U;
    value *= 0x85EBCA6BU;
    value ^= value >> 13U;
    value *= 0xC2B2AE35U;
    value ^= value >> 16U;
    output[index] = value;
}

} // namespace

std::uint64_t detail::QualificationDeviceMillisecondsToNanoseconds(
    float milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
    {
        throw std::invalid_argument(
            "CUDA qualification event elapsed time must be finite and nonnegative");
    }

    const long double nanoseconds =
        static_cast<long double>(milliseconds) * 1'000'000.0L;
    const long double roundedNanoseconds = std::round(nanoseconds);
    if (!std::isfinite(roundedNanoseconds) || roundedNanoseconds < 0.0L ||
        roundedNanoseconds >= std::ldexp(1.0L, 64))
    {
        throw std::overflow_error(
            "CUDA qualification event elapsed time is outside uint64 nanoseconds");
    }
    return static_cast<std::uint64_t>(roundedNanoseconds);
}

class CudaQualificationOperation::Impl final
{
public:
    enum class Phase
    {
        Empty,
        Ready,
        Complete,
        Failed,
        CompletionUncertain,
    };

    explicit Impl(std::size_t requestedElementCount)
        : elementCount{requestedElementCount}
    {
    }

    ~Impl() noexcept
    {
        // A failed synchronization does not prove that queued work stopped using
        // these resources. Leave them for process teardown rather than freeing or
        // resetting them under possibly in-flight work.
        if (phase == Phase::CompletionUncertain)
        {
            return;
        }

        if (output != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(output),
                "cudaFree for CUDA qualification output buffer");
        }
        if (input != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(input),
                "cudaFree for CUDA qualification input buffer");
        }
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for CUDA qualification stream");
        }
    }

    void RequireExecutable() const
    {
        if (phase != Phase::Ready && phase != Phase::Complete)
        {
            throw std::logic_error(
                "CUDA qualification execution requires completed input upload and no failed or pending work");
        }
    }

    void ApplyResourceDisposition(
        detail::CudaResourceDisposition disposition) noexcept
    {
        phase = disposition ==
                detail::CudaResourceDisposition::PreserveForProcessTeardown
            ? Phase::CompletionUncertain
            : Phase::Failed;
    }

    std::size_t elementCount{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* input{nullptr};
    std::uint32_t* output{nullptr};
    cudaStream_t stream{nullptr};
    unsigned int blockCount{1U};
    std::vector<std::uint32_t> expected;
    Phase phase{Phase::Empty};
};

CudaQualificationOperation::CudaQualificationOperation(
    int deviceOrdinal,
    std::size_t elementCount)
    : impl_{std::make_unique<Impl>(elementCount)}
{
    CheckCuda(cudaSetDevice(deviceOrdinal), "cudaSetDevice during CUDA qualification setup");
    CheckCuda(
        cudaFree(nullptr),
        "cudaFree(nullptr) during CUDA qualification runtime initialization");

    cudaDeviceProp properties{};
    CheckCuda(
        cudaGetDeviceProperties(&properties, deviceOrdinal),
        "cudaGetDeviceProperties during CUDA qualification setup");
    for (std::size_t index = 0; index < impl_->deviceUuid.size(); ++index)
    {
        impl_->deviceUuid[index] =
            static_cast<std::uint8_t>(properties.uuid.bytes[index]);
    }

    const std::size_t requiredBlocks = std::max<std::size_t>(
        1U,
        elementCount / ThreadsPerBlock +
            (elementCount % ThreadsPerBlock == 0U ? 0U : 1U));
    if (requiredBlocks > static_cast<std::size_t>(properties.maxGridSize[0]))
    {
        throw std::invalid_argument(
            "CUDA qualification element count exceeds the selected device's one-dimensional grid capacity");
    }
    if (elementCount >
        std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "CUDA qualification element count exceeds addressable byte capacity");
    }
    impl_->blockCount = static_cast<unsigned int>(requiredBlocks);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        "cudaStreamCreateWithFlags for CUDA qualification stream");

    if (elementCount != 0U)
    {
        const std::size_t byteCount = elementCount * sizeof(std::uint32_t);
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&impl_->input), byteCount),
            "cudaMalloc for CUDA qualification input buffer");
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&impl_->output), byteCount),
            "cudaMalloc for CUDA qualification output buffer");
    }
}

CudaQualificationOperation::~CudaQualificationOperation() noexcept = default;

std::size_t CudaQualificationOperation::ElementCount() const noexcept
{
    return impl_->elementCount;
}

const std::array<std::uint8_t, 16>&
CudaQualificationOperation::SelectedDeviceUuid() const noexcept
{
    return impl_->deviceUuid;
}

void CudaQualificationOperation::Upload(std::span<const std::uint32_t> input)
{
    if (input.size() != impl_->elementCount)
    {
        throw std::invalid_argument(
            "CUDA qualification upload size does not match the declared element count");
    }
    if (impl_->phase == Impl::Phase::Failed ||
        impl_->phase == Impl::Phase::CompletionUncertain)
    {
        throw std::logic_error(
            "CUDA qualification resources cannot be reused after a native failure");
    }

    impl_->expected = TransformSequence(
        std::vector<std::uint32_t>{input.begin(), input.end()});
    if (!input.empty())
    {
        const cudaError_t uploadResult = cudaMemcpyAsync(
            impl_->input,
            input.data(),
            input.size_bytes(),
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (uploadResult != cudaSuccess)
        {
            impl_->ApplyResourceDisposition(
                detail::ClassifyCudaResourceDisposition(false));
            ThrowCudaError(
                uploadResult,
                "cudaMemcpyAsync for CUDA qualification input upload");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->phase = Impl::Phase::CompletionUncertain;
            ThrowCudaError(
                waitResult,
                "cudaStreamSynchronize after CUDA qualification input upload");
        }
    }
    impl_->phase = Impl::Phase::Ready;
}

CudaQualificationExecution CudaQualificationOperation::ExecuteHostOnly()
{
    impl_->RequireExecutable();

    const timing::HostTimePoint t0 = timing::CaptureHostTime();
    QualificationTransformKernel<<<
        impl_->blockCount,
        ThreadsPerBlock,
        0U,
        impl_->stream>>>(impl_->input, impl_->output, impl_->elementCount);
    const cudaError_t submissionResult = cudaGetLastError();
    const timing::HostTimePoint t1 = timing::CaptureHostTime();

    if (submissionResult != cudaSuccess)
    {
        impl_->ApplyResourceDisposition(
            detail::ClassifyCudaResourceDisposition(false));
        return {
            CudaQualificationStatus::SubmitFailed,
            CudaQualificationFailurePhase::Submission,
            ex2::CalculateHostTimingIntervals(
                ex2::HostTimingStatus::SubmitFailed, t0, t1, std::nullopt),
            static_cast<int>(submissionResult),
            cudaGetErrorName(submissionResult),
            CudaErrorMessage(
                submissionResult,
                "CUDA qualification kernel submission"),
            false,
            {}};
    }

    const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
    if (waitResult != cudaSuccess)
    {
        impl_->phase = Impl::Phase::CompletionUncertain;
        return {
            CudaQualificationStatus::WaitFailed,
            CudaQualificationFailurePhase::CompletionWait,
            ex2::CalculateHostTimingIntervals(
                ex2::HostTimingStatus::WaitFailed, t0, t1, std::nullopt),
            static_cast<int>(waitResult),
            cudaGetErrorName(waitResult),
            CudaErrorMessage(
                waitResult,
                "cudaStreamSynchronize for CUDA qualification completion"),
            false,
            {}};
    }
    const timing::HostTimePoint t2 = timing::CaptureHostTime();
    const auto hostTiming = ex2::CalculateHostTimingIntervals(
        ex2::HostTimingStatus::Ok, t0, t1, t2);

    std::vector<std::uint32_t> output(impl_->elementCount);
    if (!output.empty())
    {
        const cudaError_t readbackResult = cudaMemcpyAsync(
            output.data(),
            impl_->output,
            output.size() * sizeof(std::uint32_t),
            cudaMemcpyDeviceToHost,
            impl_->stream);
        if (readbackResult != cudaSuccess)
        {
            impl_->ApplyResourceDisposition(
                detail::ClassifyCudaResourceDisposition(false));
            return {
                CudaQualificationStatus::ReadbackFailed,
                CudaQualificationFailurePhase::Readback,
                hostTiming,
                static_cast<int>(readbackResult),
                cudaGetErrorName(readbackResult),
                CudaErrorMessage(
                    readbackResult,
                    "cudaMemcpyAsync for CUDA qualification output retrieval"),
                false,
                {}};
        }

        const cudaError_t readbackWaitResult = cudaStreamSynchronize(impl_->stream);
        if (readbackWaitResult != cudaSuccess)
        {
            impl_->phase = Impl::Phase::CompletionUncertain;
            return {
                CudaQualificationStatus::ReadbackFailed,
                CudaQualificationFailurePhase::Readback,
                hostTiming,
                static_cast<int>(readbackWaitResult),
                cudaGetErrorName(readbackWaitResult),
                CudaErrorMessage(
                    readbackWaitResult,
                    "cudaStreamSynchronize after CUDA qualification output retrieval"),
                false,
                {}};
        }
    }

    if (output != impl_->expected)
    {
        impl_->phase = Impl::Phase::Failed;
        return {
            CudaQualificationStatus::ValidationFailed,
            CudaQualificationFailurePhase::Validation,
            hostTiming,
            std::nullopt,
            {},
            "CUDA qualification output differs from the exact CPU oracle",
            false,
            std::move(output)};
    }

    if (hostTiming.status != ex2::HostTimingStatus::Ok)
    {
        impl_->phase = Impl::Phase::Failed;
        return {
            CudaQualificationStatus::TimingInvalid,
            CudaQualificationFailurePhase::HostTiming,
            hostTiming,
            std::nullopt,
            {},
            "CUDA qualification host timestamps failed the G0-01 contract",
            true,
            std::move(output)};
    }

    impl_->phase = Impl::Phase::Complete;
    return {
        CudaQualificationStatus::Ok,
        CudaQualificationFailurePhase::None,
        hostTiming,
        std::nullopt,
        {},
        {},
        true,
        std::move(output)};
}

class CudaDeviceTimedQualificationOperation::Impl final
{
public:
    enum class Phase
    {
        Empty,
        Ready,
        Complete,
        Failed,
        CompletionUncertain,
    };

    explicit Impl(std::size_t requestedElementCount)
        : elementCount{requestedElementCount}
    {
    }

    ~Impl() noexcept
    {
        if (phase == Phase::CompletionUncertain)
        {
            return;
        }
        if (output != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(output),
                "cudaFree for CUDA mode-N qualification output buffer");
        }
        if (input != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(input),
                "cudaFree for CUDA mode-N qualification input buffer");
        }
        if (stopEvent != nullptr)
        {
            ReportCudaCleanupError(
                cudaEventDestroy(stopEvent),
                "cudaEventDestroy for CUDA mode-N stop event");
        }
        if (startEvent != nullptr)
        {
            ReportCudaCleanupError(
                cudaEventDestroy(startEvent),
                "cudaEventDestroy for CUDA mode-N start event");
        }
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for CUDA mode-N qualification stream");
        }
    }

    void RequireExecutable() const
    {
        if (phase != Phase::Ready && phase != Phase::Complete)
        {
            throw std::logic_error(
                "CUDA mode-N qualification execution requires completed input upload and no failed or pending work");
        }
    }

    void PreserveForProcessTeardown() noexcept
    {
        phase = Phase::CompletionUncertain;
    }

    std::size_t elementCount{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* input{nullptr};
    std::uint32_t* output{nullptr};
    cudaStream_t stream{nullptr};
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    unsigned int blockCount{1U};
    std::vector<std::uint32_t> expected;
    Phase phase{Phase::Empty};
};

CudaDeviceTimedQualificationOperation::CudaDeviceTimedQualificationOperation(
    int deviceOrdinal,
    std::size_t elementCount)
    : impl_{std::make_unique<Impl>(elementCount)}
{
    CheckCuda(
        cudaSetDevice(deviceOrdinal),
        "cudaSetDevice during CUDA mode-N qualification setup");
    CheckCuda(
        cudaFree(nullptr),
        "cudaFree(nullptr) during CUDA mode-N runtime initialization");

    cudaDeviceProp properties{};
    CheckCuda(
        cudaGetDeviceProperties(&properties, deviceOrdinal),
        "cudaGetDeviceProperties during CUDA mode-N qualification setup");
    for (std::size_t index = 0; index < impl_->deviceUuid.size(); ++index)
    {
        impl_->deviceUuid[index] =
            static_cast<std::uint8_t>(properties.uuid.bytes[index]);
    }

    const std::size_t requiredBlocks = std::max<std::size_t>(
        1U,
        elementCount / ThreadsPerBlock +
            (elementCount % ThreadsPerBlock == 0U ? 0U : 1U));
    if (requiredBlocks > static_cast<std::size_t>(properties.maxGridSize[0]))
    {
        throw std::invalid_argument(
            "CUDA mode-N qualification element count exceeds the selected device's one-dimensional grid capacity");
    }
    if (elementCount >
        std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "CUDA mode-N qualification element count exceeds addressable byte capacity");
    }
    impl_->blockCount = static_cast<unsigned int>(requiredBlocks);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        "cudaStreamCreateWithFlags for CUDA mode-N qualification stream");
    CheckCuda(
        cudaEventCreateWithFlags(&impl_->startEvent, cudaEventDefault),
        "cudaEventCreateWithFlags for CUDA mode-N start timing event");
    CheckCuda(
        cudaEventCreateWithFlags(&impl_->stopEvent, cudaEventDefault),
        "cudaEventCreateWithFlags for CUDA mode-N stop timing event");

    if (elementCount != 0U)
    {
        const std::size_t byteCount = elementCount * sizeof(std::uint32_t);
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&impl_->input), byteCount),
            "cudaMalloc for CUDA mode-N qualification input buffer");
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&impl_->output), byteCount),
            "cudaMalloc for CUDA mode-N qualification output buffer");
    }
}

CudaDeviceTimedQualificationOperation::~CudaDeviceTimedQualificationOperation() noexcept =
    default;

std::size_t CudaDeviceTimedQualificationOperation::ElementCount() const noexcept
{
    return impl_->elementCount;
}

const std::array<std::uint8_t, 16>&
CudaDeviceTimedQualificationOperation::SelectedDeviceUuid() const noexcept
{
    return impl_->deviceUuid;
}

void CudaDeviceTimedQualificationOperation::Upload(
    std::span<const std::uint32_t> input)
{
    if (input.size() != impl_->elementCount)
    {
        throw std::invalid_argument(
            "CUDA mode-N qualification upload size does not match the declared element count");
    }
    if (impl_->phase == Impl::Phase::Failed ||
        impl_->phase == Impl::Phase::CompletionUncertain)
    {
        throw std::logic_error(
            "CUDA mode-N qualification resources cannot be reused after a native failure");
    }

    impl_->expected = TransformSequence(
        std::vector<std::uint32_t>{input.begin(), input.end()});
    if (!input.empty())
    {
        const cudaError_t uploadResult = cudaMemcpyAsync(
            impl_->input,
            input.data(),
            input.size_bytes(),
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (uploadResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                uploadResult,
                "cudaMemcpyAsync for CUDA mode-N qualification input upload");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                waitResult,
                "cudaStreamSynchronize after CUDA mode-N qualification input upload");
        }
    }
    impl_->phase = Impl::Phase::Ready;
}

CudaQualificationExecution
CudaDeviceTimedQualificationOperation::ExecuteDeviceTimed()
{
    impl_->RequireExecutable();

    CudaQualificationExecution execution;
    execution.instrumentMode = InstrumentMode;
    execution.nativeTimingStatus = CudaNativeTimingStatus::Unavailable;
    execution.nativeTimingMetadata.emplace();

    const timing::HostTimePoint t0 = timing::CaptureHostTime();
    const cudaError_t startResult =
        cudaEventRecord(impl_->startEvent, impl_->stream);
    if (startResult != cudaSuccess)
    {
        const timing::HostTimePoint t1 = timing::CaptureHostTime();
        impl_->PreserveForProcessTeardown();
        execution.status = CudaQualificationStatus::SubmitFailed;
        execution.failurePhase = CudaQualificationFailurePhase::StartMarker;
        execution.hostTiming = ex2::CalculateHostTimingIntervals(
            ex2::HostTimingStatus::SubmitFailed, t0, t1, std::nullopt);
        execution.nativeErrorCode = static_cast<int>(startResult);
        execution.nativeErrorName = cudaGetErrorName(startResult);
        execution.errorMessage = CudaErrorMessage(
            startResult,
            "cudaEventRecord for CUDA mode-N start timing event");
        execution.nativeTimingStatus = CudaNativeTimingStatus::MarkerFailed;
        return execution;
    }

    QualificationTransformKernel<<<
        impl_->blockCount,
        ThreadsPerBlock,
        0U,
        impl_->stream>>>(impl_->input, impl_->output, impl_->elementCount);
    const cudaError_t launchResult = cudaGetLastError();
    if (launchResult != cudaSuccess)
    {
        const timing::HostTimePoint t1 = timing::CaptureHostTime();
        impl_->PreserveForProcessTeardown();
        execution.status = CudaQualificationStatus::SubmitFailed;
        execution.failurePhase = CudaQualificationFailurePhase::Submission;
        execution.hostTiming = ex2::CalculateHostTimingIntervals(
            ex2::HostTimingStatus::SubmitFailed, t0, t1, std::nullopt);
        execution.nativeErrorCode = static_cast<int>(launchResult);
        execution.nativeErrorName = cudaGetErrorName(launchResult);
        execution.errorMessage = CudaErrorMessage(
            launchResult,
            "CUDA mode-N qualification kernel submission");
        return execution;
    }

    const cudaError_t stopResult =
        cudaEventRecord(impl_->stopEvent, impl_->stream);
    const timing::HostTimePoint t1 = timing::CaptureHostTime();
    if (stopResult != cudaSuccess)
    {
        impl_->PreserveForProcessTeardown();
        execution.status = CudaQualificationStatus::SubmitFailed;
        execution.failurePhase = CudaQualificationFailurePhase::StopMarker;
        execution.hostTiming = ex2::CalculateHostTimingIntervals(
            ex2::HostTimingStatus::SubmitFailed, t0, t1, std::nullopt);
        execution.nativeErrorCode = static_cast<int>(stopResult);
        execution.nativeErrorName = cudaGetErrorName(stopResult);
        execution.errorMessage = CudaErrorMessage(
            stopResult,
            "cudaEventRecord for CUDA mode-N stop timing event");
        execution.nativeTimingStatus = CudaNativeTimingStatus::MarkerFailed;
        return execution;
    }

    const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
    if (waitResult != cudaSuccess)
    {
        impl_->PreserveForProcessTeardown();
        execution.status = CudaQualificationStatus::WaitFailed;
        execution.failurePhase =
            CudaQualificationFailurePhase::CompletionWait;
        execution.hostTiming = ex2::CalculateHostTimingIntervals(
            ex2::HostTimingStatus::WaitFailed, t0, t1, std::nullopt);
        execution.nativeErrorCode = static_cast<int>(waitResult);
        execution.nativeErrorName = cudaGetErrorName(waitResult);
        execution.errorMessage = CudaErrorMessage(
            waitResult,
            "cudaStreamSynchronize for CUDA mode-N qualification completion");
        return execution;
    }
    const timing::HostTimePoint t2 = timing::CaptureHostTime();
    execution.hostTiming = ex2::CalculateHostTimingIntervals(
        ex2::HostTimingStatus::Ok, t0, t1, t2);

    float elapsedMilliseconds = 0.0F;
    const cudaError_t elapsedResult = cudaEventElapsedTime(
        &elapsedMilliseconds, impl_->startEvent, impl_->stopEvent);
    if (elapsedResult != cudaSuccess)
    {
        execution.failurePhase =
            CudaQualificationFailurePhase::NativeTimingRetrieval;
        execution.nativeErrorCode = static_cast<int>(elapsedResult);
        execution.nativeErrorName = cudaGetErrorName(elapsedResult);
        execution.errorMessage = CudaErrorMessage(
            elapsedResult,
            "cudaEventElapsedTime for CUDA mode-N qualification");
        execution.nativeTimingStatus =
            CudaNativeTimingStatus::RetrievalFailed;
    }
    else
    {
        try
        {
            execution.nativeDeviceIntervalNanoseconds =
                detail::QualificationDeviceMillisecondsToNanoseconds(
                    elapsedMilliseconds);
            execution.nativeTimingStatus = CudaNativeTimingStatus::Valid;
        }
        catch (const std::exception& error)
        {
            execution.failurePhase =
                CudaQualificationFailurePhase::NativeTimingConversion;
            execution.errorMessage = error.what();
            execution.nativeTimingStatus =
                CudaNativeTimingStatus::ConversionInvalid;
        }
    }

    std::vector<std::uint32_t> output(impl_->elementCount);
    if (!output.empty())
    {
        const cudaError_t readbackResult = cudaMemcpyAsync(
            output.data(),
            impl_->output,
            output.size() * sizeof(std::uint32_t),
            cudaMemcpyDeviceToHost,
            impl_->stream);
        if (readbackResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            execution.status = CudaQualificationStatus::ReadbackFailed;
            execution.failurePhase = CudaQualificationFailurePhase::Readback;
            execution.nativeErrorCode = static_cast<int>(readbackResult);
            execution.nativeErrorName = cudaGetErrorName(readbackResult);
            execution.errorMessage = CudaErrorMessage(
                readbackResult,
                "cudaMemcpyAsync for CUDA mode-N qualification output retrieval");
            return execution;
        }
        const cudaError_t readbackWaitResult =
            cudaStreamSynchronize(impl_->stream);
        if (readbackWaitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            execution.status = CudaQualificationStatus::ReadbackFailed;
            execution.failurePhase = CudaQualificationFailurePhase::Readback;
            execution.nativeErrorCode = static_cast<int>(readbackWaitResult);
            execution.nativeErrorName = cudaGetErrorName(readbackWaitResult);
            execution.errorMessage = CudaErrorMessage(
                readbackWaitResult,
                "cudaStreamSynchronize after CUDA mode-N qualification output retrieval");
            return execution;
        }
    }

    execution.output = std::move(output);
    execution.validationPassed = execution.output == impl_->expected;
    if (!execution.validationPassed)
    {
        impl_->phase = Impl::Phase::Failed;
        execution.status = CudaQualificationStatus::ValidationFailed;
        execution.failurePhase = CudaQualificationFailurePhase::Validation;
        execution.errorMessage =
            "CUDA mode-N qualification output differs from the exact CPU oracle";
        return execution;
    }
    if (execution.hostTiming.status != ex2::HostTimingStatus::Ok)
    {
        impl_->phase = Impl::Phase::Failed;
        execution.status = CudaQualificationStatus::TimingInvalid;
        execution.failurePhase = CudaQualificationFailurePhase::HostTiming;
        execution.errorMessage =
            "CUDA mode-N qualification host timestamps failed the G0-01 contract";
        return execution;
    }
    if (execution.nativeTimingStatus != CudaNativeTimingStatus::Valid)
    {
        impl_->phase = Impl::Phase::Failed;
        execution.status = CudaQualificationStatus::TimingInvalid;
        return execution;
    }

    impl_->phase = Impl::Phase::Complete;
    execution.status = CudaQualificationStatus::Ok;
    execution.failurePhase = CudaQualificationFailurePhase::None;
    return execution;
}

} // namespace computelab::cuda
