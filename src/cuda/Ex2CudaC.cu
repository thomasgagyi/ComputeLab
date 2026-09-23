#include "cuda/Ex2CudaC.hpp"

#include "ex2/Ex2ContentionTargets.hpp"

#include <cuda_runtime.h>

#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace computelab::cuda
{
namespace
{

using OperationState = detail::Ex2CudaCOperationState;

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
    Ex2CudaCNativePhase phase,
    const char* operation)
{
    const std::string name = CudaErrorName(result);
    throw Ex2CudaCNativeError{
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
    Ex2CudaCNativePhase phase,
    const char* operation)
{
    if (result != cudaSuccess)
    {
        ThrowCudaError(result, phase, operation);
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

} // namespace

__global__ void Ex2CudaCAtomicKernel(
    const std::uint32_t* targets,
    std::uint32_t* counters,
    std::uint32_t elementCount)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    const std::uint32_t target = targets[index];
    atomicAdd(&counters[target], 1U);
}

std::string_view ToString(Ex2CudaCNativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2CudaCNativePhase::DeviceSelection: return "device selection";
    case Ex2CudaCNativePhase::RuntimeInitialization:
        return "runtime initialization";
    case Ex2CudaCNativePhase::DeviceProperties: return "device properties";
    case Ex2CudaCNativePhase::ResourcePreflight: return "resource preflight";
    case Ex2CudaCNativePhase::StreamCreation: return "stream creation";
    case Ex2CudaCNativePhase::TargetAllocation: return "target allocation";
    case Ex2CudaCNativePhase::CounterAllocation: return "counter allocation";
    case Ex2CudaCNativePhase::TargetUpload: return "target upload";
    case Ex2CudaCNativePhase::TargetUploadCompletion:
        return "target upload completion";
    case Ex2CudaCNativePhase::CounterReset: return "counter reset";
    case Ex2CudaCNativePhase::ResetCompletion: return "reset completion";
    case Ex2CudaCNativePhase::Submission: return "submission";
    case Ex2CudaCNativePhase::CompletionWait: return "completion wait";
    case Ex2CudaCNativePhase::CounterReadback: return "counter readback";
    case Ex2CudaCNativePhase::TargetDiagnosticReadback:
        return "target diagnostic readback";
    }
    return "invalid";
}

Ex2CudaCNativeError::Ex2CudaCNativeError(
    Ex2CudaCNativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)}
{
}

Ex2CudaCNativePhase Ex2CudaCNativeError::Phase() const noexcept
{
    return phase_;
}

int Ex2CudaCNativeError::NativeErrorCode() const noexcept
{
    return nativeErrorCode_;
}

const std::string& Ex2CudaCNativeError::NativeErrorName() const noexcept
{
    return nativeErrorName_;
}

void detail::ValidateEx2CudaCConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    if (!std::holds_alternative<ex2::ContentionConfiguration>(
            configuration.parameters) ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA C configuration violates the I2-C semantic contract");
    }
}

detail::Ex2CudaCLaunchShape detail::ValidateEx2CudaCLaunchShape(
    const ex2::ContentionConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX)
{
    ValidateEx2CudaCConfiguration(ex2::MakeConfiguration(configuration));
    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (maximumThreadsPerBlock < Ex2CudaCThreadsPerBlock ||
        maximumThreadsDimensionX < Ex2CudaCThreadsPerBlock)
    {
        throw std::invalid_argument(
            "selected CUDA device cannot execute the EX-2 C 256-thread block");
    }

    const std::uint64_t requiredBlocks =
        configuration.elementCount / Ex2CudaCThreadsPerBlock +
        (configuration.elementCount % Ex2CudaCThreadsPerBlock == 0U
            ? 0U
            : 1U);
    if (maximumGridDimensionX == 0U ||
        requiredBlocks > maximumGridDimensionX ||
        requiredBlocks > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA C element count exceeds the selected device grid capacity");
    }
    return {
        static_cast<std::uint32_t>(requiredBlocks),
        Ex2CudaCThreadsPerBlock};
}

std::size_t detail::CalculateEx2CudaCBufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA C element count exceeds uint32 logical indexing");
    }
    if (elementCount > maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 CUDA C buffer byte count exceeds host addressability");
    }
    return static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2CudaCHostVectorCapacity(
    std::uint64_t elementCount,
    std::size_t maximumVectorElementCount)
{
    if (elementCount > maximumVectorElementCount)
    {
        throw std::length_error(
            "EX-2 CUDA C logical buffers exceed the host vector maximum");
    }
}

void detail::ValidateEx2CudaCMemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes)
{
    if (singleBufferByteCount == 0U)
    {
        return;
    }
    if (singleBufferByteCount > totalGlobalMemoryBytes / 2U)
    {
        throw std::length_error(
            "EX-2 CUDA C target and counter buffers exceed selected-device global memory");
    }
    if (singleBufferByteCount > freeGlobalMemoryBytes / 2U)
    {
        throw std::length_error(
            "EX-2 CUDA C target and counter buffers exceed currently free device memory");
    }
}

class Ex2CudaCOperation::Impl final
{
public:
    Impl(
        int requestedDeviceOrdinal,
        ex2::ContentionConfiguration requestedConfiguration)
        : deviceOrdinal{requestedDeviceOrdinal},
          configuration{requestedConfiguration}
    {
    }

    ~Impl() noexcept
    {
        const auto disposition = detail::ClassifyEx2CudaCCompletionDisposition(
            state != OperationState::CompletionUncertain,
            hostTransferStorage != nullptr);
        if (disposition.hostStorage ==
            detail::Ex2CudaCHostStorageDisposition::PreserveForProcessTeardown)
        {
            static_cast<void>(hostTransferStorage.release());
        }
        if (disposition.deviceResources ==
            detail::Ex2CudaCResourceDisposition::PreserveForProcessTeardown)
        {
            return;
        }
        if (targets == nullptr && counters == nullptr && stream == nullptr)
        {
            return;
        }

        const cudaError_t selectionResult = cudaSetDevice(deviceOrdinal);
        if (selectionResult != cudaSuccess)
        {
            ReportCudaCleanupError(
                selectionResult,
                "cudaSetDevice during EX-2 CUDA C cleanup");
            return;
        }
        if (state == OperationState::Submitted)
        {
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                ReportCudaCleanupError(
                    waitResult,
                    "cudaStreamSynchronize during EX-2 CUDA C cleanup");
                return;
            }
        }
        if (counters != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(counters),
                "cudaFree for EX-2 CUDA C counter buffer");
        }
        if (targets != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(targets),
                "cudaFree for EX-2 CUDA C target buffer");
        }
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for EX-2 CUDA C stream");
        }
    }

    void RequireReusable(const char* operation) const
    {
        if (!detail::IsEx2CudaCOperationStateReusable(state))
        {
            const char* reason = state == OperationState::Submitted
                ? " while execution is incomplete"
                : state == OperationState::CompletionUncertain
                    ? " after uncertain native completion"
                    : " after a native failure";
            throw std::logic_error(
                std::string{"EX-2 CUDA C "} + operation +
                " is unavailable" + reason);
        }
    }

    void ActivateDevice(
        Ex2CudaCNativePhase phase,
        const char* operation)
    {
        const cudaError_t result = cudaSetDevice(deviceOrdinal);
        if (result != cudaSuccess)
        {
            state = state == OperationState::Submitted
                ? OperationState::CompletionUncertain
                : OperationState::Failed;
            ThrowCudaError(result, phase, operation);
        }
    }

    void PreserveForProcessTeardown() noexcept
    {
        state = OperationState::CompletionUncertain;
    }

    void StageHostStorage(std::span<const std::uint32_t> source)
    {
        hostTransferStorage = std::make_unique<std::vector<std::uint32_t>>(
            source.begin(), source.end());
    }

    void PrepareReadbackStorage()
    {
        hostTransferStorage = std::make_unique<std::vector<std::uint32_t>>(
            static_cast<std::size_t>(configuration.elementCount));
    }

    std::vector<std::uint32_t> TakeCompletedReadback() noexcept
    {
        std::vector<std::uint32_t> result;
        result.swap(*hostTransferStorage);
        hostTransferStorage.reset();
        return result;
    }

    std::vector<std::uint32_t> ReadBack(
        const std::uint32_t* source,
        Ex2CudaCNativePhase phase,
        const char* copyOperation,
        const char* waitOperation)
    {
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                "EX-2 CUDA C readback is unavailable after uncertain native completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                "EX-2 CUDA C readback is unavailable after a native failure");
        }
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                "EX-2 CUDA C readback requires explicit successful completion");
        }
        ActivateDevice(phase, "cudaSetDevice before EX-2 CUDA C readback");
        if (configuration.elementCount == 0U)
        {
            return {};
        }

        PrepareReadbackStorage();
        const cudaError_t copyResult = cudaMemcpyAsync(
            hostTransferStorage->data(),
            source,
            bufferByteCount,
            cudaMemcpyDeviceToHost,
            stream);
        if (copyResult != cudaSuccess)
        {
            PreserveForProcessTeardown();
            ThrowCudaError(copyResult, phase, copyOperation);
        }
        const cudaError_t waitResult = cudaStreamSynchronize(stream);
        if (waitResult != cudaSuccess)
        {
            PreserveForProcessTeardown();
            ThrowCudaError(waitResult, phase, waitOperation);
        }
        return TakeCompletedReadback();
    }

    int deviceOrdinal{};
    const ex2::ContentionConfiguration configuration;
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* targets{nullptr};
    std::uint32_t* counters{nullptr};
    cudaStream_t stream{nullptr};
    std::unique_ptr<std::vector<std::uint32_t>> hostTransferStorage;
    detail::Ex2CudaCLaunchShape launchShape;
    std::size_t bufferByteCount{};
    OperationState state{OperationState::Empty};
    bool submittedKernel{};
    bool lastCompletionExecutedKernel{};
};

Ex2CudaCOperation::Ex2CudaCOperation(
    int deviceOrdinal,
    const ex2::ContentionConfiguration& configuration)
    : impl_{std::make_unique<Impl>(deviceOrdinal, configuration)}
{
    detail::ValidateEx2CudaCConfiguration(ex2::MakeConfiguration(configuration));
    impl_->bufferByteCount = detail::CalculateEx2CudaCBufferByteCount(
        configuration.elementCount,
        std::numeric_limits<std::size_t>::max());
    detail::ValidateEx2CudaCHostVectorCapacity(
        configuration.elementCount,
        std::vector<std::uint32_t>{}.max_size());

    CheckCuda(
        cudaSetDevice(deviceOrdinal),
        Ex2CudaCNativePhase::DeviceSelection,
        "cudaSetDevice for EX-2 CUDA C");
    CheckCuda(
        cudaFree(nullptr),
        Ex2CudaCNativePhase::RuntimeInitialization,
        "cudaFree(nullptr) for EX-2 CUDA C runtime initialization");

    cudaDeviceProp properties{};
    CheckCuda(
        cudaGetDeviceProperties(&properties, deviceOrdinal),
        Ex2CudaCNativePhase::DeviceProperties,
        "cudaGetDeviceProperties for EX-2 CUDA C");
    if (properties.major <= 0)
    {
        throw std::invalid_argument(
            "selected CUDA device does not report a usable compute capability");
    }
    for (std::size_t index = 0U; index < impl_->deviceUuid.size(); ++index)
    {
        impl_->deviceUuid[index] =
            static_cast<std::uint8_t>(properties.uuid.bytes[index]);
    }

    const std::uint64_t maximumGridDimensionX = properties.maxGridSize[0] > 0
        ? static_cast<std::uint64_t>(properties.maxGridSize[0])
        : 0U;
    const std::uint32_t maximumThreadsPerBlock =
        properties.maxThreadsPerBlock > 0
        ? static_cast<std::uint32_t>(properties.maxThreadsPerBlock)
        : 0U;
    const std::uint32_t maximumThreadsDimensionX =
        properties.maxThreadsDim[0] > 0
        ? static_cast<std::uint32_t>(properties.maxThreadsDim[0])
        : 0U;
    impl_->launchShape = detail::ValidateEx2CudaCLaunchShape(
        configuration,
        maximumGridDimensionX,
        maximumThreadsPerBlock,
        maximumThreadsDimensionX);

    std::size_t freeGlobalMemoryBytes = 0U;
    std::size_t totalGlobalMemoryBytes = 0U;
    CheckCuda(
        cudaMemGetInfo(&freeGlobalMemoryBytes, &totalGlobalMemoryBytes),
        Ex2CudaCNativePhase::ResourcePreflight,
        "cudaMemGetInfo for EX-2 CUDA C");
    detail::ValidateEx2CudaCMemoryFeasibility(
        impl_->bufferByteCount,
        totalGlobalMemoryBytes,
        freeGlobalMemoryBytes);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        Ex2CudaCNativePhase::StreamCreation,
        "cudaStreamCreateWithFlags for EX-2 CUDA C");
    if (configuration.elementCount == 0U)
    {
        return;
    }

    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->targets),
            impl_->bufferByteCount),
        Ex2CudaCNativePhase::TargetAllocation,
        "cudaMalloc for EX-2 CUDA C targets");
    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->counters),
            impl_->bufferByteCount),
        Ex2CudaCNativePhase::CounterAllocation,
        "cudaMalloc for EX-2 CUDA C counters");
}

Ex2CudaCOperation::~Ex2CudaCOperation() noexcept = default;

int Ex2CudaCOperation::SelectedDeviceOrdinal() const noexcept
{
    return impl_->deviceOrdinal;
}

const std::array<std::uint8_t, 16>&
Ex2CudaCOperation::SelectedDeviceUuid() const noexcept
{
    return impl_->deviceUuid;
}

std::uint64_t Ex2CudaCOperation::ElementCount() const noexcept
{
    return impl_->configuration.elementCount;
}

std::uint64_t Ex2CudaCOperation::ActiveCounterCount() const noexcept
{
    return impl_->configuration.activeCounterCount;
}

std::uint64_t Ex2CudaCOperation::AllocatedCounterCount() const noexcept
{
    return impl_->configuration.allocatedCounterCount;
}

void Ex2CudaCOperation::UploadTargets(
    std::span<const std::uint32_t> targets)
{
    impl_->RequireReusable("target upload");
    impl_->state = OperationState::Empty;
    impl_->submittedKernel = false;
    impl_->lastCompletionExecutedKernel = false;

    ex2::ValidateContentionTargets(
        targets,
        impl_->configuration.elementCount,
        impl_->configuration.activeCounterCount);
    impl_->ActivateDevice(
        Ex2CudaCNativePhase::TargetUpload,
        "cudaSetDevice before EX-2 CUDA C target upload");
    if (!targets.empty())
    {
        impl_->StageHostStorage(targets);
        const cudaError_t copyResult = cudaMemcpyAsync(
            impl_->targets,
            impl_->hostTransferStorage->data(),
            impl_->bufferByteCount,
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (copyResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                copyResult,
                Ex2CudaCNativePhase::TargetUpload,
                "cudaMemcpyAsync for EX-2 CUDA C target upload");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                waitResult,
                Ex2CudaCNativePhase::TargetUploadCompletion,
                "cudaStreamSynchronize after EX-2 CUDA C target upload");
        }
        impl_->hostTransferStorage.reset();
    }
    impl_->state = OperationState::TargetsReady;
}

void Ex2CudaCOperation::PrepareReset()
{
    impl_->RequireReusable("counter reset");
    if (impl_->state != OperationState::TargetsReady &&
        impl_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 CUDA C reset requires valid uploaded targets");
    }
    impl_->state = OperationState::TargetsReady;
    impl_->submittedKernel = false;
    impl_->lastCompletionExecutedKernel = false;
    impl_->ActivateDevice(
        Ex2CudaCNativePhase::CounterReset,
        "cudaSetDevice before EX-2 CUDA C counter reset");

    if (impl_->configuration.allocatedCounterCount != 0U)
    {
        const cudaError_t resetResult = cudaMemsetAsync(
            impl_->counters,
            0,
            impl_->bufferByteCount,
            impl_->stream);
        if (resetResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                resetResult,
                Ex2CudaCNativePhase::CounterReset,
                "cudaMemsetAsync for EX-2 CUDA C full counter reset");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                waitResult,
                Ex2CudaCNativePhase::ResetCompletion,
                "cudaStreamSynchronize after EX-2 CUDA C counter reset");
        }
    }
    impl_->state = OperationState::ResetReady;
}

void Ex2CudaCOperation::SubmitAtomic()
{
    impl_->RequireReusable("atomic submission");
    if (impl_->state != OperationState::ResetReady)
    {
        throw std::logic_error(
            "EX-2 CUDA C submission requires a newly completed full reset");
    }
    impl_->ActivateDevice(
        Ex2CudaCNativePhase::Submission,
        "cudaSetDevice before EX-2 CUDA C atomic submission");

    impl_->submittedKernel = impl_->configuration.elementCount != 0U;
    if (impl_->submittedKernel)
    {
        Ex2CudaCAtomicKernel<<<
            impl_->launchShape.blockCount,
            impl_->launchShape.threadsPerBlock,
            0U,
            impl_->stream>>>(
                impl_->targets,
                impl_->counters,
                static_cast<std::uint32_t>(
                    impl_->configuration.elementCount));
        const cudaError_t launchResult = cudaGetLastError();
        if (launchResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                launchResult,
                Ex2CudaCNativePhase::Submission,
                "EX-2 CUDA C atomic kernel launch");
        }
    }
    impl_->state = OperationState::Submitted;
}

void Ex2CudaCOperation::WaitForCompletion()
{
    if (impl_->state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 CUDA C completion cannot be retried after uncertain native completion");
    }
    if (impl_->state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 CUDA C completion wait requires one submitted operation");
    }
    impl_->ActivateDevice(
        Ex2CudaCNativePhase::CompletionWait,
        "cudaSetDevice before EX-2 CUDA C completion wait");
    const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
    if (waitResult != cudaSuccess)
    {
        impl_->PreserveForProcessTeardown();
        ThrowCudaError(
            waitResult,
            Ex2CudaCNativePhase::CompletionWait,
            "cudaStreamSynchronize for EX-2 CUDA C completion");
    }
    impl_->state = OperationState::Complete;
    impl_->lastCompletionExecutedKernel = impl_->submittedKernel;
}

std::vector<std::uint32_t> Ex2CudaCOperation::RetrieveCounters()
{
    return impl_->ReadBack(
        impl_->counters,
        Ex2CudaCNativePhase::CounterReadback,
        "cudaMemcpyAsync for EX-2 CUDA C counter readback",
        "cudaStreamSynchronize after EX-2 CUDA C counter readback");
}

std::vector<std::uint32_t> Ex2CudaCOperation::RetrieveDeviceTargets()
{
    return impl_->ReadBack(
        impl_->targets,
        Ex2CudaCNativePhase::TargetDiagnosticReadback,
        "cudaMemcpyAsync for EX-2 CUDA C target diagnostic readback",
        "cudaStreamSynchronize after EX-2 CUDA C target diagnostic readback");
}

bool Ex2CudaCOperation::LastCompletionExecutedKernel() const
{
    if (impl_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 CUDA C dispatch status requires explicit successful completion");
    }
    return impl_->lastCompletionExecutedKernel;
}

} // namespace computelab::cuda
