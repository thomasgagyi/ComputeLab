#include "cuda/Ex2CudaD1.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace computelab::cuda
{
namespace
{

using OperationState = detail::Ex2CudaD1OperationState;

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
    Ex2CudaD1NativePhase phase,
    const char* operation,
    std::optional<std::uint32_t> attemptedPass = std::nullopt,
    std::uint32_t acceptedDispatchCount = 0U)
{
    const std::string name = CudaErrorName(result);
    throw Ex2CudaD1NativeError{
        phase,
        static_cast<int>(result),
        name,
        std::string{operation} + " failed during " +
            std::string{ToString(phase)} + " with " + name + " (code " +
            std::to_string(static_cast<int>(result)) + "): " +
            CudaErrorDescription(result),
        attemptedPass,
        acceptedDispatchCount};
}

void CheckCuda(
    cudaError_t result,
    Ex2CudaD1NativePhase phase,
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

__global__ void Ex2CudaD1Kernel(
    const std::uint32_t* previous,
    std::uint32_t* next,
    std::uint32_t elementCount,
    std::uint32_t pass)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
        return;

    const std::uint32_t value =
        previous[index] ^ (0x9E3779B9U + index + pass);
    const std::uint32_t rotated = (value << 5U) | (value >> 27U);
    next[index] = rotated + 0x7F4A7C15U;
}

class D1Core
{
public:
    D1Core(
        int requestedDeviceOrdinal,
        const ex2::IterativeConfiguration& requestedConfiguration,
        bool enableTiming)
        : deviceOrdinal{requestedDeviceOrdinal},
          configuration{requestedConfiguration},
          timingEnabled{enableTiming}
    {
    }

    void Initialize()
    {
        detail::ValidateEx2CudaD1Configuration(
            ex2::MakeConfiguration(configuration));
        bufferByteCount = detail::CalculateEx2CudaD1BufferByteCount(
            configuration.elementCount,
            std::numeric_limits<std::size_t>::max());
        detail::ValidateEx2CudaD1HostVectorCapacity(
            configuration.elementCount,
            std::vector<std::uint32_t>{}.max_size());

        CheckCuda(
            cudaSetDevice(deviceOrdinal),
            Ex2CudaD1NativePhase::DeviceSelection,
            "cudaSetDevice for EX-2 CUDA D1");
        CheckCuda(
            cudaFree(nullptr),
            Ex2CudaD1NativePhase::RuntimeInitialization,
            "cudaFree(nullptr) for EX-2 CUDA D1 runtime initialization");

        cudaDeviceProp properties{};
        CheckCuda(
            cudaGetDeviceProperties(&properties, deviceOrdinal),
            Ex2CudaD1NativePhase::DeviceProperties,
            "cudaGetDeviceProperties for EX-2 CUDA D1");
        if (properties.major <= 0)
            throw std::invalid_argument(
                "selected CUDA device does not report a usable compute capability");
        for (std::size_t index = 0U; index < deviceUuid.size(); ++index)
            deviceUuid[index] =
                static_cast<std::uint8_t>(properties.uuid.bytes[index]);

        launchShape = detail::ValidateEx2CudaD1LaunchShape(
            configuration,
            properties.maxGridSize[0] > 0
                ? static_cast<std::uint64_t>(properties.maxGridSize[0])
                : 0U,
            properties.maxThreadsPerBlock > 0
                ? static_cast<std::uint32_t>(properties.maxThreadsPerBlock)
                : 0U,
            properties.maxThreadsDim[0] > 0
                ? static_cast<std::uint32_t>(properties.maxThreadsDim[0])
                : 0U);

        std::size_t freeGlobalMemoryBytes{};
        std::size_t totalGlobalMemoryBytes{};
        CheckCuda(
            cudaMemGetInfo(&freeGlobalMemoryBytes, &totalGlobalMemoryBytes),
            Ex2CudaD1NativePhase::ResourcePreflight,
            "cudaMemGetInfo for EX-2 CUDA D1");
        detail::ValidateEx2CudaD1MemoryFeasibility(
            bufferByteCount, totalGlobalMemoryBytes, freeGlobalMemoryBytes);

        CheckCuda(
            cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
            Ex2CudaD1NativePhase::StreamCreation,
            "cudaStreamCreateWithFlags for EX-2 CUDA D1");
        if (timingEnabled)
        {
            CheckCuda(
                cudaEventCreateWithFlags(&startEvent, cudaEventDefault),
                Ex2CudaD1NativePhase::StartMarker,
                "cudaEventCreateWithFlags for EX-2 CUDA D1 start event");
            CheckCuda(
                cudaEventCreateWithFlags(&stopEvent, cudaEventDefault),
                Ex2CudaD1NativePhase::StopMarker,
                "cudaEventCreateWithFlags for EX-2 CUDA D1 stop event");
        }
        if (configuration.elementCount == 0U)
            return;

        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&stateA), bufferByteCount),
            Ex2CudaD1NativePhase::StateAAllocation,
            "cudaMalloc for EX-2 CUDA D1 state A");
        CheckCuda(
            cudaMalloc(reinterpret_cast<void**>(&stateB), bufferByteCount),
            Ex2CudaD1NativePhase::StateBAllocation,
            "cudaMalloc for EX-2 CUDA D1 state B");
    }

    ~D1Core() noexcept
    {
        const bool uncertain =
            state == OperationState::Submitted ||
            state == OperationState::CompletionUncertain;
        const auto disposition = detail::ClassifyEx2CudaD1CompletionDisposition(
            uncertain, hostTransferStorage != nullptr);
        if (disposition.hostStorage ==
            detail::Ex2CudaD1HostStorageDisposition::PreserveForProcessTeardown)
        {
            static_cast<void>(hostTransferStorage.release());
        }
        if (disposition.deviceResources ==
            detail::Ex2CudaD1ResourceDisposition::PreserveForProcessTeardown)
        {
            return;
        }
        if (stateA == nullptr && stateB == nullptr && stream == nullptr)
            return;
        const cudaError_t selectionResult = cudaSetDevice(deviceOrdinal);
        if (selectionResult != cudaSuccess)
        {
            ReportCudaCleanupError(
                selectionResult,
                "cudaSetDevice during EX-2 CUDA D1 cleanup");
            return;
        }
        if (stateB != nullptr)
            ReportCudaCleanupError(cudaFree(stateB), "cudaFree for EX-2 CUDA D1 state B");
        if (stateA != nullptr)
            ReportCudaCleanupError(cudaFree(stateA), "cudaFree for EX-2 CUDA D1 state A");
        if (stopEvent != nullptr)
            ReportCudaCleanupError(cudaEventDestroy(stopEvent), "cudaEventDestroy for EX-2 CUDA D1 stop event");
        if (startEvent != nullptr)
            ReportCudaCleanupError(cudaEventDestroy(startEvent), "cudaEventDestroy for EX-2 CUDA D1 start event");
        if (stream != nullptr)
            ReportCudaCleanupError(cudaStreamDestroy(stream), "cudaStreamDestroy for EX-2 CUDA D1 stream");
    }

    void RequireReusable(const char* operation) const
    {
        if (detail::IsEx2CudaD1OperationStateReusable(state))
            return;
        const char* reason = state == OperationState::Submitted
            ? " while sequence execution is incomplete"
            : state == OperationState::CompletionUncertain
                ? " after uncertain native completion"
                : " after a native failure";
        throw std::logic_error(
            std::string{"EX-2 CUDA D1 "} + operation + " is unavailable" + reason);
    }

    void ActivateDevice(Ex2CudaD1NativePhase phase, const char* operation)
    {
        const cudaError_t result = cudaSetDevice(deviceOrdinal);
        if (result != cudaSuccess)
        {
            state = detail::ClassifyEx2CudaD1AsyncFailure(
                anyPositiveLaunchAttempted || state == OperationState::Submitted,
                false);
            ThrowCudaError(
                result, phase, operation, attemptedPass, acceptedDispatchCount);
        }
    }

    ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept
    {
        return (configuration.iterationCount & 1U) == 0U
            ? ex2::IterativeFinalBuffer::StateA
            : ex2::IterativeFinalBuffer::StateB;
    }

    void UploadInitialState(std::span<const std::uint32_t> initialState)
    {
        RequireReusable("initial-state upload");
        state = OperationState::Failed;
        attemptedPass.reset();
        acceptedDispatchCount = 0U;
        lastCompletionDispatchCount = 0U;
        lastCompletionExecutedSequence = false;
        timingStatus = Ex2CudaD1NativeTimingStatus::NotApplicable;
        nativeIntervalNanoseconds.reset();

        detail::ValidateEx2CudaD1Configuration(
            ex2::MakeConfiguration(configuration));
        if (initialState.size() != configuration.elementCount)
            throw std::invalid_argument(
                "EX-2 CUDA D1 initial-state length does not match element count");
        ActivateDevice(
            Ex2CudaD1NativePhase::InitialUpload,
            "cudaSetDevice before EX-2 CUDA D1 initial upload");
        if (!initialState.empty())
        {
            hostTransferStorage = std::make_unique<std::vector<std::uint32_t>>(
                initialState.begin(), initialState.end());
            const cudaError_t copyResult = cudaMemcpyAsync(
                stateA,
                hostTransferStorage->data(),
                bufferByteCount,
                cudaMemcpyHostToDevice,
                stream);
            if (copyResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    copyResult,
                    Ex2CudaD1NativePhase::InitialUpload,
                    "cudaMemcpyAsync for EX-2 CUDA D1 initial-state upload");
            }
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    waitResult,
                    Ex2CudaD1NativePhase::UploadCompletion,
                    "cudaStreamSynchronize after EX-2 CUDA D1 initial upload");
            }
            hostTransferStorage.reset();
        }
        anyPositiveLaunchAttempted = false;
        state = OperationState::InitialStateReady;
    }

    void SubmitSequence()
    {
        RequireReusable("sequence submission");
        if (state != OperationState::InitialStateReady)
            throw std::logic_error(
                "EX-2 CUDA D1 sequence submission requires a fresh completed initial-state upload");
        ActivateDevice(
            Ex2CudaD1NativePhase::SequenceSubmission,
            "cudaSetDevice before EX-2 CUDA D1 sequence submission");

        attemptedPass.reset();
        acceptedDispatchCount = 0U;
        anyPositiveLaunchAttempted = false;
        const bool executes = configuration.elementCount != 0U &&
            configuration.iterationCount != 0U;
        if (!executes)
        {
            timingStatus = Ex2CudaD1NativeTimingStatus::NotApplicable;
            state = OperationState::Submitted;
            return;
        }

        if (timingEnabled)
        {
            const cudaError_t startResult = cudaEventRecord(startEvent, stream);
            if (startResult != cudaSuccess)
            {
                state = OperationState::Failed;
                timingStatus = Ex2CudaD1NativeTimingStatus::MarkerFailed;
                ThrowCudaError(
                    startResult,
                    Ex2CudaD1NativePhase::StartMarker,
                    "cudaEventRecord for EX-2 CUDA D1 start event");
            }
        }

        for (std::uint32_t pass = 0U;
             pass < static_cast<std::uint32_t>(configuration.iterationCount);
             ++pass)
        {
            attemptedPass = pass;
            static_cast<void>(cudaGetLastError());
            anyPositiveLaunchAttempted = true;
            const std::uint32_t* previous = (pass & 1U) == 0U ? stateA : stateB;
            std::uint32_t* next = (pass & 1U) == 0U ? stateB : stateA;
            Ex2CudaD1Kernel<<<
                launchShape.blockCount,
                launchShape.threadsPerBlock,
                0U,
                stream>>>(
                    previous,
                    next,
                    static_cast<std::uint32_t>(configuration.elementCount),
                    pass);
            const cudaError_t launchResult = cudaGetLastError();
            if (launchResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    launchResult,
                    Ex2CudaD1NativePhase::SequenceSubmission,
                    "EX-2 CUDA D1 kernel launch",
                    pass,
                    acceptedDispatchCount);
            }
            ++acceptedDispatchCount;
        }

        if (timingEnabled)
        {
            const cudaError_t stopResult = cudaEventRecord(stopEvent, stream);
            if (stopResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                timingStatus = Ex2CudaD1NativeTimingStatus::MarkerFailed;
                ThrowCudaError(
                    stopResult,
                    Ex2CudaD1NativePhase::StopMarker,
                    "cudaEventRecord for EX-2 CUDA D1 stop event",
                    attemptedPass,
                    acceptedDispatchCount);
            }
            timingStatus = Ex2CudaD1NativeTimingStatus::Unavailable;
        }
        state = OperationState::Submitted;
    }

    void WaitForCompletion()
    {
        if (state == OperationState::CompletionUncertain)
            throw std::logic_error(
                "EX-2 CUDA D1 completion cannot be retried after uncertain native completion");
        if (state != OperationState::Submitted)
            throw std::logic_error(
                "EX-2 CUDA D1 completion wait requires one submitted sequence");

        const bool executes = configuration.elementCount != 0U &&
            configuration.iterationCount != 0U;
        if (executes)
        {
            ActivateDevice(
                Ex2CudaD1NativePhase::CompletionWait,
                "cudaSetDevice before EX-2 CUDA D1 completion wait");
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                state = OperationState::CompletionUncertain;
                ThrowCudaError(
                    waitResult,
                    Ex2CudaD1NativePhase::CompletionWait,
                    "cudaStreamSynchronize for EX-2 CUDA D1 sequence completion",
                    attemptedPass,
                    acceptedDispatchCount);
            }
        }

        lastCompletionDispatchCount = executes
            ? static_cast<std::uint32_t>(configuration.iterationCount)
            : 0U;
        lastCompletionExecutedSequence = executes;
        completedFinalBuffer = ExpectedFinalBuffer();

        if (timingEnabled && executes)
        {
            float milliseconds{};
            const cudaError_t elapsedResult = cudaEventElapsedTime(
                &milliseconds, startEvent, stopEvent);
            if (elapsedResult != cudaSuccess)
            {
                state = OperationState::Failed;
                timingStatus = Ex2CudaD1NativeTimingStatus::RetrievalFailed;
                ThrowCudaError(
                    elapsedResult,
                    Ex2CudaD1NativePhase::NativeTimingRetrieval,
                    "cudaEventElapsedTime for EX-2 CUDA D1");
            }
            try
            {
                nativeIntervalNanoseconds =
                    detail::Ex2CudaD1MillisecondsToNanoseconds(milliseconds);
                timingStatus = Ex2CudaD1NativeTimingStatus::Valid;
            }
            catch (const std::exception& error)
            {
                state = OperationState::Failed;
                timingStatus = Ex2CudaD1NativeTimingStatus::ConversionInvalid;
                throw Ex2CudaD1NativeError{
                    Ex2CudaD1NativePhase::NativeTimingConversion,
                    static_cast<int>(cudaSuccess),
                    "timing conversion",
                    error.what(),
                    attemptedPass,
                    acceptedDispatchCount};
            }
        }
        state = OperationState::Complete;
    }

    std::vector<std::uint32_t> RetrieveFinalState()
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                "EX-2 CUDA D1 final readback requires explicit successful completion");
        if (configuration.elementCount == 0U)
            return {};
        ActivateDevice(
            Ex2CudaD1NativePhase::FinalReadback,
            "cudaSetDevice before EX-2 CUDA D1 final readback");
        hostTransferStorage = std::make_unique<std::vector<std::uint32_t>>(
            static_cast<std::size_t>(configuration.elementCount));
        const std::uint32_t* source =
            ExpectedFinalBuffer() == ex2::IterativeFinalBuffer::StateA
                ? stateA
                : stateB;
        const cudaError_t copyResult = cudaMemcpyAsync(
            hostTransferStorage->data(),
            source,
            bufferByteCount,
            cudaMemcpyDeviceToHost,
            stream);
        if (copyResult != cudaSuccess)
        {
            state = OperationState::CompletionUncertain;
            ThrowCudaError(
                copyResult,
                Ex2CudaD1NativePhase::FinalReadback,
                "cudaMemcpyAsync for EX-2 CUDA D1 final readback");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(stream);
        if (waitResult != cudaSuccess)
        {
            state = OperationState::CompletionUncertain;
            ThrowCudaError(
                waitResult,
                Ex2CudaD1NativePhase::FinalReadback,
                "cudaStreamSynchronize after EX-2 CUDA D1 final readback");
        }
        std::vector<std::uint32_t> result;
        result.swap(*hostTransferStorage);
        hostTransferStorage.reset();
        return result;
    }

    void RequireComplete(const char* diagnostic) const
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                std::string{"EX-2 CUDA D1 "} + diagnostic +
                " requires explicit successful completion");
    }

    int deviceOrdinal{};
    const ex2::IterativeConfiguration configuration;
    const bool timingEnabled{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* stateA{nullptr};
    std::uint32_t* stateB{nullptr};
    cudaStream_t stream{nullptr};
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    std::unique_ptr<std::vector<std::uint32_t>> hostTransferStorage;
    detail::Ex2CudaD1LaunchShape launchShape;
    std::size_t bufferByteCount{};
    OperationState state{OperationState::Empty};
    bool anyPositiveLaunchAttempted{};
    std::optional<std::uint32_t> attemptedPass;
    std::uint32_t acceptedDispatchCount{};
    ex2::IterativeFinalBuffer completedFinalBuffer{ex2::IterativeFinalBuffer::StateA};
    std::uint32_t lastCompletionDispatchCount{};
    bool lastCompletionExecutedSequence{};
    Ex2CudaD1NativeTimingStatus timingStatus{Ex2CudaD1NativeTimingStatus::NotApplicable};
    std::optional<std::uint64_t> nativeIntervalNanoseconds;
};

} // namespace

std::string_view ToString(Ex2CudaD1NativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2CudaD1NativePhase::DeviceSelection: return "device selection";
    case Ex2CudaD1NativePhase::RuntimeInitialization: return "runtime initialization";
    case Ex2CudaD1NativePhase::DeviceProperties: return "device properties";
    case Ex2CudaD1NativePhase::ResourcePreflight: return "resource preflight";
    case Ex2CudaD1NativePhase::StreamCreation: return "stream creation";
    case Ex2CudaD1NativePhase::StateAAllocation: return "state A allocation";
    case Ex2CudaD1NativePhase::StateBAllocation: return "state B allocation";
    case Ex2CudaD1NativePhase::InitialUpload: return "initial upload";
    case Ex2CudaD1NativePhase::UploadCompletion: return "upload completion";
    case Ex2CudaD1NativePhase::StartMarker: return "start marker";
    case Ex2CudaD1NativePhase::SequenceSubmission: return "sequence submission";
    case Ex2CudaD1NativePhase::StopMarker: return "stop marker";
    case Ex2CudaD1NativePhase::CompletionWait: return "completion wait";
    case Ex2CudaD1NativePhase::FinalReadback: return "final readback";
    case Ex2CudaD1NativePhase::NativeTimingRetrieval: return "native timing retrieval";
    case Ex2CudaD1NativePhase::NativeTimingConversion: return "native timing conversion";
    }
    return "invalid";
}

Ex2CudaD1NativeError::Ex2CudaD1NativeError(
    Ex2CudaD1NativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message,
    std::optional<std::uint32_t> attemptedPass,
    std::uint32_t acceptedDispatchCount)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)},
      attemptedPass_{attemptedPass},
      acceptedDispatchCount_{acceptedDispatchCount}
{
}

Ex2CudaD1NativePhase Ex2CudaD1NativeError::Phase() const noexcept { return phase_; }
int Ex2CudaD1NativeError::NativeErrorCode() const noexcept { return nativeErrorCode_; }
const std::string& Ex2CudaD1NativeError::NativeErrorName() const noexcept { return nativeErrorName_; }
std::optional<std::uint32_t> Ex2CudaD1NativeError::AttemptedPass() const noexcept { return attemptedPass_; }
std::uint32_t Ex2CudaD1NativeError::AcceptedDispatchCount() const noexcept { return acceptedDispatchCount_; }

void detail::ValidateEx2CudaD1Configuration(
    const ex2::WorkloadConfiguration& configuration)
{
    const auto* iterative = std::get_if<ex2::IterativeConfiguration>(
        &configuration.parameters);
    if (iterative == nullptr ||
        iterative->variant != ex2::IterativeVariant::D1 ||
        configuration.common.executionMode != ex2::LogicalExecutionMode::Ordinary ||
        configuration.common.operationBoundary !=
            ex2::OperationBoundary::OrdinaryIterationSequenceCompletion ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA D1 configuration violates the frozen D1 semantic contract");
    }
}

detail::Ex2CudaD1LaunchShape detail::ValidateEx2CudaD1LaunchShape(
    const ex2::IterativeConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX)
{
    ValidateEx2CudaD1Configuration(ex2::MakeConfiguration(configuration));
    if (configuration.elementCount == 0U)
        return {};
    if (maximumThreadsPerBlock < Ex2CudaD1ThreadsPerBlock ||
        maximumThreadsDimensionX < Ex2CudaD1ThreadsPerBlock)
    {
        throw std::invalid_argument(
            "selected CUDA device cannot execute the EX-2 D1 256-thread block");
    }
    const std::uint64_t requiredBlocks =
        configuration.elementCount / Ex2CudaD1ThreadsPerBlock +
        (configuration.elementCount % Ex2CudaD1ThreadsPerBlock == 0U ? 0U : 1U);
    if (maximumGridDimensionX == 0U ||
        requiredBlocks > maximumGridDimensionX ||
        requiredBlocks > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA D1 element count exceeds selected-device grid capacity");
    }
    return {static_cast<std::uint32_t>(requiredBlocks), Ex2CudaD1ThreadsPerBlock};
}

std::size_t detail::CalculateEx2CudaD1BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(
            "EX-2 CUDA D1 element count exceeds uint32 logical indexing");
    if (elementCount > maximumAddressableByteCount / sizeof(std::uint32_t))
        throw std::length_error(
            "EX-2 CUDA D1 buffer byte count exceeds host addressability");
    return static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2CudaD1HostVectorCapacity(
    std::uint64_t elementCount,
    std::size_t maximumVectorElementCount)
{
    if (elementCount > maximumVectorElementCount)
        throw std::length_error(
            "EX-2 CUDA D1 logical state exceeds the host vector maximum");
}

void detail::ValidateEx2CudaD1MemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes)
{
    if (singleBufferByteCount == 0U)
        return;
    if (singleBufferByteCount > totalGlobalMemoryBytes / 2U)
        throw std::length_error(
            "EX-2 CUDA D1 state buffers exceed selected-device global memory");
    if (singleBufferByteCount > freeGlobalMemoryBytes / 2U)
        throw std::length_error(
            "EX-2 CUDA D1 state buffers exceed currently free device memory");
}

std::uint64_t detail::Ex2CudaD1MillisecondsToNanoseconds(float milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
        throw std::invalid_argument(
            "EX-2 CUDA D1 elapsed milliseconds must be finite and nonnegative");
    const long double nanoseconds =
        static_cast<long double>(milliseconds) * 1'000'000.0L;
    const long double rounded = std::round(nanoseconds);
    if (!std::isfinite(rounded) || rounded < 0.0L ||
        rounded >= std::ldexp(1.0L, 64))
    {
        throw std::overflow_error(
            "EX-2 CUDA D1 elapsed time is outside uint64 nanoseconds");
    }
    return static_cast<std::uint64_t>(rounded);
}

class Ex2CudaD1Operation::Impl final : public D1Core
{
public:
    Impl(int deviceOrdinal, const ex2::IterativeConfiguration& configuration)
        : D1Core{deviceOrdinal, configuration, false}
    {
        Initialize();
    }
};

class Ex2CudaD1DeviceTimedOperation::Impl final : public D1Core
{
public:
    Impl(int deviceOrdinal, const ex2::IterativeConfiguration& configuration)
        : D1Core{deviceOrdinal, configuration, true}
    {
        Initialize();
    }
};

#define COMPUTELAB_CUDA_D1_COMMON_METHODS(ClassName) \
ClassName::ClassName(int ordinal, const ex2::IterativeConfiguration& config) \
    : impl_{std::make_unique<Impl>(ordinal, config)} {} \
ClassName::~ClassName() noexcept = default; \
int ClassName::SelectedDeviceOrdinal() const noexcept { return impl_->deviceOrdinal; } \
const std::array<std::uint8_t, 16>& ClassName::SelectedDeviceUuid() const noexcept { return impl_->deviceUuid; } \
std::uint64_t ClassName::ElementCount() const noexcept { return impl_->configuration.elementCount; } \
std::uint64_t ClassName::IterationCount() const noexcept { return impl_->configuration.iterationCount; } \
ex2::IterativeFinalBuffer ClassName::ExpectedFinalBuffer() const noexcept { return impl_->ExpectedFinalBuffer(); } \
void ClassName::UploadInitialState(std::span<const std::uint32_t> input) { impl_->UploadInitialState(input); } \
void ClassName::SubmitSequence() { impl_->SubmitSequence(); } \
void ClassName::WaitForCompletion() { impl_->WaitForCompletion(); } \
std::vector<std::uint32_t> ClassName::RetrieveFinalState() { return impl_->RetrieveFinalState(); } \
ex2::IterativeFinalBuffer ClassName::CompletedFinalBuffer() const { impl_->RequireComplete("completed final-buffer diagnostic"); return impl_->completedFinalBuffer; } \
std::uint32_t ClassName::LastCompletionNativeDispatchCount() const { impl_->RequireComplete("dispatch-count diagnostic"); return impl_->lastCompletionDispatchCount; } \
bool ClassName::LastCompletionExecutedSequence() const { impl_->RequireComplete("execution diagnostic"); return impl_->lastCompletionExecutedSequence; }

COMPUTELAB_CUDA_D1_COMMON_METHODS(Ex2CudaD1Operation)
COMPUTELAB_CUDA_D1_COMMON_METHODS(Ex2CudaD1DeviceTimedOperation)

#undef COMPUTELAB_CUDA_D1_COMMON_METHODS

Ex2CudaD1NativeTimingStatus
Ex2CudaD1DeviceTimedOperation::NativeTimingStatus() const
{
    impl_->RequireComplete("native timing status");
    return impl_->timingStatus;
}

Ex2CudaD1NativeTimingMetadata
Ex2CudaD1DeviceTimedOperation::NativeTimingMetadata() const noexcept
{
    return {};
}

std::optional<std::uint64_t>
Ex2CudaD1DeviceTimedOperation::NativeDeviceIntervalNanoseconds() const
{
    impl_->RequireComplete("native device interval");
    return impl_->nativeIntervalNanoseconds;
}

} // namespace computelab::cuda
