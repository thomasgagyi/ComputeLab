#include "cuda/Ex2CudaE.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace computelab::cuda
{
namespace
{

using OperationState = detail::Ex2CudaEOperationState;

std::string CudaErrorName(cudaError_t result)
{
    const char* name = cudaGetErrorName(result);
    return name == nullptr ? "unknown CUDA error" : name;
}

std::string CudaErrorDescription(cudaError_t result)
{
    const char* description = cudaGetErrorString(result);
    return description == nullptr ? "no CUDA error description" : description;
}

[[noreturn]] void ThrowCudaError(
    cudaError_t result,
    Ex2CudaENativePhase phase,
    const char* operation)
{
    const std::string name = CudaErrorName(result);
    throw Ex2CudaENativeError{
        phase,
        static_cast<int>(result),
        name,
        std::string{operation} + " failed during " +
            std::string{ToString(phase)} + " with " + name + " (code " +
            std::to_string(static_cast<int>(result)) + "): " +
            CudaErrorDescription(result)};
}

void CheckCuda(
    cudaError_t result,
    Ex2CudaENativePhase phase,
    const char* operation)
{
    if (result != cudaSuccess)
        ThrowCudaError(result, phase, operation);
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

class ECore
{
public:
    ECore(
        int requestedDeviceOrdinal,
        const ex2::TransferConfiguration& requestedConfiguration,
        bool enableTiming)
        : deviceOrdinal{requestedDeviceOrdinal},
          configuration{requestedConfiguration},
          timingEnabled{enableTiming}
    {
    }

    void Initialize()
    {
        detail::ValidateEx2CudaEConfiguration(
            ex2::MakeConfiguration(configuration));
        byteCount = detail::CalculateEx2CudaEByteCount(
            configuration.byteCount,
            std::numeric_limits<std::size_t>::max());
        detail::ValidateEx2CudaEHostVectorCapacity(
            configuration.byteCount,
            std::vector<std::uint8_t>{}.max_size());
        resourceBytes = detail::CalculateEx2CudaEResourceBytes(
            byteCount,
            std::numeric_limits<std::size_t>::max());

        CheckCuda(
            cudaSetDevice(deviceOrdinal),
            Ex2CudaENativePhase::DeviceSelection,
            "cudaSetDevice for EX-2 CUDA E");
        CheckCuda(
            cudaFree(nullptr),
            Ex2CudaENativePhase::RuntimeInitialization,
            "cudaFree(nullptr) for EX-2 CUDA E runtime initialization");

        cudaDeviceProp properties{};
        CheckCuda(
            cudaGetDeviceProperties(&properties, deviceOrdinal),
            Ex2CudaENativePhase::DeviceProperties,
            "cudaGetDeviceProperties for EX-2 CUDA E");
        if (properties.major <= 0)
            throw std::invalid_argument(
                "selected CUDA device does not report a usable compute capability");
        for (std::size_t index = 0U; index < deviceUuid.size(); ++index)
            deviceUuid[index] =
                static_cast<std::uint8_t>(properties.uuid.bytes[index]);

        std::size_t freeGlobalMemoryBytes{};
        std::size_t totalGlobalMemoryBytes{};
        CheckCuda(
            cudaMemGetInfo(&freeGlobalMemoryBytes, &totalGlobalMemoryBytes),
            Ex2CudaENativePhase::ResourcePreflight,
            "cudaMemGetInfo for EX-2 CUDA E");
        detail::ValidateEx2CudaEDeviceMemoryFeasibility(
            resourceBytes.deviceBytes,
            totalGlobalMemoryBytes,
            freeGlobalMemoryBytes);

        CheckCuda(
            cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
            Ex2CudaENativePhase::StreamCreation,
            "cudaStreamCreateWithFlags for EX-2 CUDA E");
        if (timingEnabled)
        {
            CheckCuda(
                cudaEventCreateWithFlags(&startEvent, cudaEventDefault),
                Ex2CudaENativePhase::StartMarker,
                "cudaEventCreateWithFlags for EX-2 CUDA E start event");
            CheckCuda(
                cudaEventCreateWithFlags(&stopEvent, cudaEventDefault),
                Ex2CudaENativePhase::StopMarker,
                "cudaEventCreateWithFlags for EX-2 CUDA E stop event");
        }
        if (byteCount == 0U)
            return;

        CheckCuda(
            cudaMallocHost(
                reinterpret_cast<void**>(&pinnedSource), byteCount),
            Ex2CudaENativePhase::PinnedSourceAllocation,
            "cudaMallocHost for EX-2 CUDA E pinned source");
        CheckCuda(
            cudaMallocHost(
                reinterpret_cast<void**>(&pinnedDestination), byteCount),
            Ex2CudaENativePhase::PinnedDestinationAllocation,
            "cudaMallocHost for EX-2 CUDA E pinned destination");
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&deviceBuffer), byteCount),
            Ex2CudaENativePhase::DeviceAllocation,
            "cudaMalloc for EX-2 CUDA E device buffer");
    }

    ~ECore() noexcept
    {
        const bool uncertain =
            state == OperationState::Submitted ||
            state == OperationState::CompletionUncertain;
        const auto disposition = detail::ClassifyEx2CudaECompletionDisposition(
            uncertain,
            pinnedSource != nullptr || pinnedDestination != nullptr);
        if (disposition.deviceResources ==
            detail::Ex2CudaEResourceDisposition::PreserveForProcessTeardown)
        {
            return;
        }
        if (stream == nullptr && deviceBuffer == nullptr &&
            pinnedSource == nullptr && pinnedDestination == nullptr)
        {
            return;
        }
        const cudaError_t selectionResult = cudaSetDevice(deviceOrdinal);
        if (selectionResult != cudaSuccess)
        {
            ReportCudaCleanupError(
                selectionResult,
                "cudaSetDevice during EX-2 CUDA E cleanup");
            return;
        }
        if (stopEvent != nullptr)
            ReportCudaCleanupError(cudaEventDestroy(stopEvent), "cudaEventDestroy for EX-2 CUDA E stop event");
        if (startEvent != nullptr)
            ReportCudaCleanupError(cudaEventDestroy(startEvent), "cudaEventDestroy for EX-2 CUDA E start event");
        if (deviceBuffer != nullptr)
            ReportCudaCleanupError(cudaFree(deviceBuffer), "cudaFree for EX-2 CUDA E device buffer");
        if (pinnedDestination != nullptr)
            ReportCudaCleanupError(cudaFreeHost(pinnedDestination), "cudaFreeHost for EX-2 CUDA E pinned destination");
        if (pinnedSource != nullptr)
            ReportCudaCleanupError(cudaFreeHost(pinnedSource), "cudaFreeHost for EX-2 CUDA E pinned source");
        if (stream != nullptr)
            ReportCudaCleanupError(cudaStreamDestroy(stream), "cudaStreamDestroy for EX-2 CUDA E stream");
    }

    void RequireReusable(const char* operation) const
    {
        if (detail::IsEx2CudaEOperationStateReusable(state))
            return;
        const char* reason = state == OperationState::Submitted
            ? " while transfer work is pending"
            : state == OperationState::CompletionUncertain
                ? " after uncertain native completion"
                : " after a native failure";
        throw std::logic_error(
            std::string{"EX-2 CUDA E "} + operation + " is unavailable" + reason);
    }

    void ActivateDevice(
        Ex2CudaENativePhase phase,
        const char* operation,
        bool asynchronousOperationAttempted)
    {
        const cudaError_t result = cudaSetDevice(deviceOrdinal);
        if (result != cudaSuccess)
        {
            state = detail::ClassifyEx2CudaEAsyncFailure(
                asynchronousOperationAttempted, false);
            ThrowCudaError(result, phase, operation);
        }
    }

    void ResetCompletionDiagnostics() noexcept
    {
        lastCompletionExecutedCopy = false;
        lastCompletionCopyCount = 0U;
        timingStatus = Ex2CudaENativeTimingStatus::NotApplicable;
        timingRetrieved = false;
        nativeIntervalNanoseconds.reset();
    }

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource)
    {
        RequireReusable("preparation");
        state = OperationState::Failed;
        ResetCompletionDiagnostics();
        detail::ValidateEx2CudaEConfiguration(
            ex2::MakeConfiguration(configuration));
        if (canonicalSource.size() != byteCount)
            throw std::invalid_argument(
                "EX-2 CUDA E source length does not match byte count");
        ActivateDevice(
            Ex2CudaENativePhase::HostSourcePreparation,
            "cudaSetDevice before EX-2 CUDA E preparation",
            false);
        if (byteCount == 0U)
        {
            state = OperationState::Prepared;
            return;
        }

        std::memcpy(pinnedSource, canonicalSource.data(), byteCount);
        std::memset(pinnedDestination, 0xA5, byteCount);
        if (configuration.direction == ex2::TransferDirection::DeviceToHost)
        {
            const cudaError_t copyResult = cudaMemcpyAsync(
                deviceBuffer,
                pinnedSource,
                byteCount,
                cudaMemcpyHostToDevice,
                stream);
            if (copyResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    copyResult,
                    Ex2CudaENativePhase::DeviceSourcePreparation,
                    "cudaMemcpyAsync for EX-2 CUDA E2 source preparation");
            }
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    waitResult,
                    Ex2CudaENativePhase::PreparationCompletion,
                    "cudaStreamSynchronize after EX-2 CUDA E2 source preparation");
            }
        }
        state = OperationState::Prepared;
    }

    void SubmitTransfer()
    {
        RequireReusable("submission");
        if (state != OperationState::Prepared)
            throw std::logic_error(
                "EX-2 CUDA E submission requires a fresh successful preparation");
        ActivateDevice(
            Ex2CudaENativePhase::TransferSubmission,
            "cudaSetDevice before EX-2 CUDA E submission",
            false);
        if (byteCount == 0U)
        {
            timingStatus = Ex2CudaENativeTimingStatus::NotApplicable;
            state = OperationState::Submitted;
            return;
        }

        if (timingEnabled)
        {
            const cudaError_t startResult = cudaEventRecord(startEvent, stream);
            if (startResult != cudaSuccess)
            {
                state = OperationState::Failed;
                timingStatus = Ex2CudaENativeTimingStatus::MarkerFailed;
                ThrowCudaError(
                    startResult,
                    Ex2CudaENativePhase::StartMarker,
                    "cudaEventRecord for EX-2 CUDA E start event");
            }
        }

        // Isolate this operation's immediate API result from stale per-thread
        // CUDA Runtime error state, while retaining the copy call's own result.
        static_cast<void>(cudaGetLastError());
        const bool hostToDevice =
            configuration.direction == ex2::TransferDirection::HostToDevice;
        const cudaError_t copyResult = cudaMemcpyAsync(
            hostToDevice ? deviceBuffer : pinnedDestination,
            hostToDevice ? pinnedSource : deviceBuffer,
            byteCount,
            hostToDevice ? cudaMemcpyHostToDevice : cudaMemcpyDeviceToHost,
            stream);
        if (copyResult != cudaSuccess)
        {
            state = OperationState::CompletionUncertain;
            ThrowCudaError(
                copyResult,
                Ex2CudaENativePhase::TransferSubmission,
                "cudaMemcpyAsync for EX-2 CUDA E named transfer");
        }

        if (timingEnabled)
        {
            const cudaError_t stopResult = cudaEventRecord(stopEvent, stream);
            if (stopResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                timingStatus = Ex2CudaENativeTimingStatus::MarkerFailed;
                ThrowCudaError(
                    stopResult,
                    Ex2CudaENativePhase::StopMarker,
                    "cudaEventRecord for EX-2 CUDA E stop event");
            }
            timingStatus = Ex2CudaENativeTimingStatus::Unavailable;
        }
        state = OperationState::Submitted;
    }

    void WaitForCompletion()
    {
        if (state == OperationState::CompletionUncertain)
            throw std::logic_error(
                "EX-2 CUDA E completion cannot be retried after uncertain native completion");
        if (state != OperationState::Submitted)
            throw std::logic_error(
                "EX-2 CUDA E completion wait requires one submitted transfer");
        if (byteCount != 0U)
        {
            ActivateDevice(
                Ex2CudaENativePhase::CompletionWait,
                "cudaSetDevice before EX-2 CUDA E completion wait",
                true);
            const cudaError_t result = cudaStreamSynchronize(stream);
            if (result != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    result,
                    Ex2CudaENativePhase::CompletionWait,
                    "cudaStreamSynchronize for EX-2 CUDA E named transfer");
            }
        }
        lastCompletionExecutedCopy = byteCount != 0U;
        lastCompletionCopyCount = byteCount == 0U ? 0U : 1U;
        state = OperationState::Complete;
    }

    std::vector<std::uint8_t> RetrieveDestinationForValidation()
    {
        RequireComplete("destination retrieval");
        if (byteCount == 0U)
            return {};
        ActivateDevice(
            Ex2CudaENativePhase::ValidationReadback,
            "cudaSetDevice before EX-2 CUDA E validation retrieval",
            false);
        if (configuration.direction == ex2::TransferDirection::HostToDevice)
        {
            const cudaError_t copyResult = cudaMemcpyAsync(
                pinnedDestination,
                deviceBuffer,
                byteCount,
                cudaMemcpyDeviceToHost,
                stream);
            if (copyResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    copyResult,
                    Ex2CudaENativePhase::ValidationReadback,
                    "cudaMemcpyAsync for EX-2 CUDA E1 validation readback");
            }
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    waitResult,
                    Ex2CudaENativePhase::ValidationReadbackCompletion,
                    "cudaStreamSynchronize for EX-2 CUDA E1 validation readback");
            }
        }
        return {pinnedDestination, pinnedDestination + byteCount};
    }

    void RetrieveNativeTiming()
    {
        RequireComplete("native timing retrieval");
        if (!timingEnabled)
            throw std::logic_error(
                "EX-2 CUDA E ordinary operation has no native timing resources");
        if (byteCount == 0U)
        {
            timingStatus = Ex2CudaENativeTimingStatus::NotApplicable;
            timingRetrieved = true;
            return;
        }
        if (timingRetrieved)
            return;
        float milliseconds{};
        const cudaError_t result = cudaEventElapsedTime(
            &milliseconds, startEvent, stopEvent);
        if (result != cudaSuccess)
        {
            timingStatus = Ex2CudaENativeTimingStatus::RetrievalFailed;
            ThrowCudaError(
                result,
                Ex2CudaENativePhase::NativeTimingRetrieval,
                "cudaEventElapsedTime for EX-2 CUDA E");
        }
        try
        {
            nativeIntervalNanoseconds =
                detail::Ex2CudaEMillisecondsToNanoseconds(milliseconds);
            timingStatus = Ex2CudaENativeTimingStatus::Valid;
            timingRetrieved = true;
        }
        catch (const std::exception& error)
        {
            timingStatus = Ex2CudaENativeTimingStatus::ConversionInvalid;
            throw Ex2CudaENativeError{
                Ex2CudaENativePhase::NativeTimingConversion,
                static_cast<int>(cudaSuccess),
                "timing conversion",
                error.what()};
        }
    }

    void RequireComplete(const char* diagnostic) const
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                std::string{"EX-2 CUDA E "} + diagnostic +
                " requires explicit successful completion");
    }

    int deviceOrdinal{};
    const ex2::TransferConfiguration configuration;
    const bool timingEnabled{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint8_t* pinnedSource{nullptr};
    std::uint8_t* pinnedDestination{nullptr};
    std::uint8_t* deviceBuffer{nullptr};
    cudaStream_t stream{nullptr};
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    std::size_t byteCount{};
    detail::Ex2CudaEResourceBytes resourceBytes{};
    OperationState state{OperationState::Empty};
    bool lastCompletionExecutedCopy{};
    std::uint32_t lastCompletionCopyCount{};
    Ex2CudaENativeTimingStatus timingStatus{
        Ex2CudaENativeTimingStatus::NotApplicable};
    bool timingRetrieved{};
    std::optional<std::uint64_t> nativeIntervalNanoseconds;
};

} // namespace

std::string_view ToString(Ex2CudaENativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2CudaENativePhase::DeviceSelection: return "device selection";
    case Ex2CudaENativePhase::RuntimeInitialization: return "runtime initialization";
    case Ex2CudaENativePhase::DeviceProperties: return "device properties";
    case Ex2CudaENativePhase::ResourcePreflight: return "resource preflight";
    case Ex2CudaENativePhase::StreamCreation: return "stream creation";
    case Ex2CudaENativePhase::PinnedSourceAllocation: return "pinned source allocation";
    case Ex2CudaENativePhase::PinnedDestinationAllocation: return "pinned destination allocation";
    case Ex2CudaENativePhase::DeviceAllocation: return "device allocation";
    case Ex2CudaENativePhase::HostSourcePreparation: return "host source preparation";
    case Ex2CudaENativePhase::DeviceSourcePreparation: return "device source preparation";
    case Ex2CudaENativePhase::PreparationCompletion: return "preparation completion";
    case Ex2CudaENativePhase::StartMarker: return "start marker";
    case Ex2CudaENativePhase::TransferSubmission: return "transfer submission";
    case Ex2CudaENativePhase::StopMarker: return "stop marker";
    case Ex2CudaENativePhase::CompletionWait: return "completion wait";
    case Ex2CudaENativePhase::ValidationReadback: return "validation readback";
    case Ex2CudaENativePhase::ValidationReadbackCompletion: return "validation readback completion";
    case Ex2CudaENativePhase::NativeTimingRetrieval: return "native timing retrieval";
    case Ex2CudaENativePhase::NativeTimingConversion: return "native timing conversion";
    }
    return "invalid";
}

Ex2CudaENativeError::Ex2CudaENativeError(
    Ex2CudaENativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)}
{
}

Ex2CudaENativePhase Ex2CudaENativeError::Phase() const noexcept { return phase_; }
int Ex2CudaENativeError::NativeErrorCode() const noexcept { return nativeErrorCode_; }
const std::string& Ex2CudaENativeError::NativeErrorName() const noexcept { return nativeErrorName_; }

void detail::ValidateEx2CudaEConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    const auto* transfer = std::get_if<ex2::TransferConfiguration>(
        &configuration.parameters);
    if (transfer == nullptr ||
        configuration.common.executionMode != ex2::LogicalExecutionMode::Prepared ||
        configuration.common.operationBoundary !=
            ex2::OperationBoundary::PreparedSingleCopyCompletion ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA E configuration violates the frozen transfer contract");
    }
}

std::size_t detail::CalculateEx2CudaEByteCount(
    std::uint64_t requestedByteCount,
    std::size_t maximumAddressableByteCount)
{
    if (requestedByteCount > maximumAddressableByteCount)
        throw std::length_error(
            "EX-2 CUDA E byte count exceeds host/native copy addressability");
    return static_cast<std::size_t>(requestedByteCount);
}

void detail::ValidateEx2CudaEHostVectorCapacity(
    std::uint64_t requestedByteCount,
    std::size_t maximumVectorElementCount)
{
    if (requestedByteCount > maximumVectorElementCount)
        throw std::length_error(
            "EX-2 CUDA E byte count exceeds the host vector maximum");
}

detail::Ex2CudaEResourceBytes detail::CalculateEx2CudaEResourceBytes(
    std::size_t requestedByteCount,
    std::size_t maximumAddressableByteCount)
{
    if (requestedByteCount > maximumAddressableByteCount / 3U)
        throw std::length_error(
            "EX-2 CUDA E aggregate resource byte count overflows size_t");
    return {
        requestedByteCount,
        requestedByteCount * 2U,
        requestedByteCount * 3U};
}

void detail::ValidateEx2CudaEDeviceMemoryFeasibility(
    std::size_t deviceBytes,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes)
{
    if (deviceBytes > totalGlobalMemoryBytes)
        throw std::length_error(
            "EX-2 CUDA E device buffer exceeds selected-device global memory");
    if (deviceBytes > freeGlobalMemoryBytes)
        throw std::length_error(
            "EX-2 CUDA E device buffer exceeds currently free device memory");
}

std::uint64_t detail::Ex2CudaEMillisecondsToNanoseconds(float milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
        throw std::invalid_argument(
            "EX-2 CUDA E elapsed milliseconds must be finite and nonnegative");
    const long double nanoseconds =
        static_cast<long double>(milliseconds) * 1'000'000.0L;
    const long double rounded = std::round(nanoseconds);
    if (!std::isfinite(rounded) || rounded < 0.0L ||
        rounded >= std::ldexp(1.0L, 64))
    {
        throw std::overflow_error(
            "EX-2 CUDA E elapsed time is outside uint64 nanoseconds");
    }
    return static_cast<std::uint64_t>(rounded);
}

class Ex2CudaEOperation::Impl final : public ECore
{
public:
    Impl(int ordinal, const ex2::TransferConfiguration& configuration)
        : ECore{ordinal, configuration, false}
    {
        Initialize();
    }
};

class Ex2CudaEDeviceTimedOperation::Impl final : public ECore
{
public:
    Impl(int ordinal, const ex2::TransferConfiguration& configuration)
        : ECore{ordinal, configuration, true}
    {
        Initialize();
    }
};

#define COMPUTELAB_CUDA_E_COMMON_METHODS(ClassName) \
ClassName::ClassName(int ordinal, const ex2::TransferConfiguration& config) \
    : impl_{std::make_unique<Impl>(ordinal, config)} {} \
ClassName::~ClassName() noexcept = default; \
int ClassName::SelectedDeviceOrdinal() const noexcept { return impl_->deviceOrdinal; } \
const std::array<std::uint8_t, 16>& ClassName::SelectedDeviceUuid() const noexcept { return impl_->deviceUuid; } \
std::uint64_t ClassName::ByteCount() const noexcept { return impl_->configuration.byteCount; } \
ex2::TransferDirection ClassName::Direction() const noexcept { return impl_->configuration.direction; } \
std::uint32_t ClassName::ExpectedNativeCopyCount() const noexcept { return impl_->byteCount == 0U ? 0U : 1U; } \
void ClassName::PrepareTransfer(std::span<const std::uint8_t> source) { impl_->PrepareTransfer(source); } \
void ClassName::SubmitTransfer() { impl_->SubmitTransfer(); } \
void ClassName::WaitForCompletion() { impl_->WaitForCompletion(); } \
std::vector<std::uint8_t> ClassName::RetrieveDestinationForValidation() { return impl_->RetrieveDestinationForValidation(); } \
bool ClassName::LastCompletionExecutedCopy() const { impl_->RequireComplete("execution diagnostic"); return impl_->lastCompletionExecutedCopy; } \
std::uint32_t ClassName::LastCompletionNativeCopyCount() const { impl_->RequireComplete("copy-count diagnostic"); return impl_->lastCompletionCopyCount; }

COMPUTELAB_CUDA_E_COMMON_METHODS(Ex2CudaEOperation)
COMPUTELAB_CUDA_E_COMMON_METHODS(Ex2CudaEDeviceTimedOperation)

#undef COMPUTELAB_CUDA_E_COMMON_METHODS

void Ex2CudaEDeviceTimedOperation::RetrieveNativeTiming()
{
    impl_->RetrieveNativeTiming();
}

Ex2CudaENativeTimingStatus
Ex2CudaEDeviceTimedOperation::NativeTimingStatus() const
{
    impl_->RequireComplete("native timing status");
    return impl_->timingStatus;
}

Ex2CudaENativeTimingMetadata
Ex2CudaEDeviceTimedOperation::NativeTimingMetadata() const noexcept
{
    return {};
}

std::optional<std::uint64_t>
Ex2CudaEDeviceTimedOperation::NativeDeviceIntervalNanoseconds() const
{
    impl_->RequireComplete("native device interval");
    return impl_->nativeIntervalNanoseconds;
}

} // namespace computelab::cuda
