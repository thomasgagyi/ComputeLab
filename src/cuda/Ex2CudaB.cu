#include "cuda/Ex2CudaB.hpp"

#include "ex2/Ex2IndexPermutation.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace computelab::cuda
{
namespace
{

enum class OperationState
{
    Empty,
    Ready,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

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
    Ex2CudaBNativePhase phase,
    const char* operation)
{
    const std::string name = CudaErrorName(result);
    throw Ex2CudaBNativeError{
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
    Ex2CudaBNativePhase phase,
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

__global__ void Ex2CudaB1GatherKernel(
    const std::uint32_t* input,
    const std::uint32_t* indices,
    std::uint32_t* output,
    std::uint32_t elementCount)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    const std::uint32_t selected = indices[index];
    output[index] = input[selected] ^ (0x9E3779B9U + index);
}

__global__ void Ex2CudaB2ScatterKernel(
    const std::uint32_t* input,
    const std::uint32_t* indices,
    std::uint32_t* output,
    std::uint32_t elementCount)
{
    const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    const std::uint32_t selected = indices[index];
    output[selected] = input[index] ^ (0x9E3779B9U + index);
}

std::string_view ToString(Ex2CudaBNativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2CudaBNativePhase::DeviceSelection: return "device selection";
    case Ex2CudaBNativePhase::RuntimeInitialization:
        return "runtime initialization";
    case Ex2CudaBNativePhase::DeviceProperties: return "device properties";
    case Ex2CudaBNativePhase::ResourcePreflight: return "resource preflight";
    case Ex2CudaBNativePhase::StreamCreation: return "stream creation";
    case Ex2CudaBNativePhase::InputAllocation: return "input allocation";
    case Ex2CudaBNativePhase::IndexAllocation: return "index allocation";
    case Ex2CudaBNativePhase::OutputAllocation: return "output allocation";
    case Ex2CudaBNativePhase::InputUpload: return "input upload";
    case Ex2CudaBNativePhase::IndexUpload: return "index upload";
    case Ex2CudaBNativePhase::OutputInitialization:
        return "output initialization";
    case Ex2CudaBNativePhase::UploadCompletion: return "upload completion";
    case Ex2CudaBNativePhase::Submission: return "submission";
    case Ex2CudaBNativePhase::CompletionWait: return "completion wait";
    case Ex2CudaBNativePhase::OutputReadback: return "output readback";
    case Ex2CudaBNativePhase::InputDiagnosticReadback:
        return "input diagnostic readback";
    case Ex2CudaBNativePhase::IndexDiagnosticReadback:
        return "index diagnostic readback";
    }
    return "invalid";
}

Ex2CudaBNativeError::Ex2CudaBNativeError(
    Ex2CudaBNativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)}
{
}

Ex2CudaBNativePhase Ex2CudaBNativeError::Phase() const noexcept
{
    return phase_;
}

int Ex2CudaBNativeError::NativeErrorCode() const noexcept
{
    return nativeErrorCode_;
}

const std::string& Ex2CudaBNativeError::NativeErrorName() const noexcept
{
    return nativeErrorName_;
}

void detail::ValidateEx2CudaBConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    if (!std::holds_alternative<ex2::IndexedConfiguration>(
            configuration.parameters) ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA B configuration violates the I2-C semantic contract");
    }
}

detail::Ex2CudaBLaunchShape detail::ValidateEx2CudaBLaunchShape(
    const ex2::IndexedConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX)
{
    ValidateEx2CudaBConfiguration(ex2::MakeConfiguration(configuration));

    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (maximumThreadsPerBlock < Ex2CudaBThreadsPerBlock ||
        maximumThreadsDimensionX < Ex2CudaBThreadsPerBlock)
    {
        throw std::invalid_argument(
            "selected CUDA device cannot execute the EX-2 B 256-thread block");
    }

    const std::uint64_t requiredBlocks =
        configuration.elementCount / Ex2CudaBThreadsPerBlock +
        (configuration.elementCount % Ex2CudaBThreadsPerBlock == 0U
            ? 0U
            : 1U);
    if (requiredBlocks > maximumGridDimensionX ||
        requiredBlocks > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA B element count exceeds the selected device grid capacity");
    }

    return {
        static_cast<std::uint32_t>(requiredBlocks),
        Ex2CudaBThreadsPerBlock};
}

std::size_t detail::CalculateEx2CudaBBufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA B element count exceeds uint32 logical indexing");
    }
    if (elementCount > maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 CUDA B buffer byte count exceeds host addressability");
    }
    return static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2CudaBMemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes)
{
    if (singleBufferByteCount == 0U)
    {
        return;
    }
    if (singleBufferByteCount > totalGlobalMemoryBytes / 3U)
    {
        throw std::length_error(
            "EX-2 CUDA B input, index and output exceed selected-device global memory");
    }
    if (singleBufferByteCount > freeGlobalMemoryBytes / 3U)
    {
        throw std::length_error(
            "EX-2 CUDA B input, index and output exceed currently free device memory");
    }
}

class Ex2CudaBOperation::Impl final
{
public:
    Impl(
        int requestedDeviceOrdinal,
        ex2::IndexedConfiguration requestedConfiguration,
        std::uint64_t requestedSeed)
        : deviceOrdinal{requestedDeviceOrdinal},
          configuration{requestedConfiguration},
          seed{requestedSeed}
    {
    }

    ~Impl() noexcept
    {
        const auto disposition = detail::ClassifyEx2CudaBCompletionDisposition(
            state != OperationState::CompletionUncertain,
            hostTransferStorage != nullptr);
        if (disposition.hostStorage ==
            detail::Ex2CudaBHostStorageDisposition::PreserveForProcessTeardown)
        {
            // CUDA may still reference this allocation. Intentional process-
            // lifetime retention is safer than freeing potentially live DMA
            // storage after completion became uncertain.
            static_cast<void>(hostTransferStorage.release());
        }
        if (disposition.deviceResources ==
            detail::Ex2CudaBResourceDisposition::PreserveForProcessTeardown)
        {
            return;
        }
        if (input == nullptr && indices == nullptr && output == nullptr &&
            stream == nullptr)
        {
            return;
        }

        const cudaError_t selectionResult = cudaSetDevice(deviceOrdinal);
        if (selectionResult != cudaSuccess)
        {
            ReportCudaCleanupError(
                selectionResult,
                "cudaSetDevice during EX-2 CUDA B cleanup");
            return;
        }

        if (state == OperationState::Submitted)
        {
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                ReportCudaCleanupError(
                    waitResult,
                    "cudaStreamSynchronize during EX-2 CUDA B cleanup");
                return;
            }
        }

        if (output != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(output),
                "cudaFree for EX-2 CUDA B output buffer");
        }
        if (indices != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(indices),
                "cudaFree for EX-2 CUDA B index buffer");
        }
        if (input != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(input),
                "cudaFree for EX-2 CUDA B input buffer");
        }
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for EX-2 CUDA B stream");
        }
    }

    void RequireReusable(const char* operation) const
    {
        if (state == OperationState::Submitted)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA B "} + operation +
                " is invalid while execution is incomplete");
        }
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA B "} + operation +
                " is unavailable after uncertain native completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA B "} + operation +
                " is unavailable after a native failure");
        }
    }

    void ActivateDevice(
        Ex2CudaBNativePhase phase,
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

    void StageUploadInput(std::span<const std::uint32_t> source)
    {
        auto owned = std::make_unique<std::vector<std::uint32_t>>(
            source.begin(), source.end());
        hostTransferStorage = std::move(owned);
    }

    void ReplaceStagedUploadWithIndices(
        std::span<const std::uint32_t> source) noexcept
    {
        std::copy(
            source.begin(), source.end(), hostTransferStorage->begin());
    }

    void PrepareReadbackStorage()
    {
        auto owned = std::make_unique<std::vector<std::uint32_t>>(
            static_cast<std::size_t>(configuration.elementCount));
        hostTransferStorage = std::move(owned);
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
        Ex2CudaBNativePhase phase,
        const char* copyOperation,
        const char* waitOperation)
    {
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                "EX-2 CUDA B readback is unavailable after uncertain native completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                "EX-2 CUDA B readback is unavailable after a native failure");
        }
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                "EX-2 CUDA B readback requires explicit successful completion");
        }
        ActivateDevice(phase, "cudaSetDevice before EX-2 CUDA B readback");

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
    ex2::IndexedConfiguration configuration;
    std::uint64_t seed{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* input{nullptr};
    std::uint32_t* indices{nullptr};
    std::uint32_t* output{nullptr};
    cudaStream_t stream{nullptr};
    // One operation-owned host allocation is reused sequentially for input and
    // index upload. During readback it owns the destination until successful
    // completion transfers the vector to the caller. If completion becomes
    // uncertain, destruction intentionally retains the allocation for process
    // teardown.
    std::unique_ptr<std::vector<std::uint32_t>> hostTransferStorage;
    detail::Ex2CudaBLaunchShape launchShape;
    std::size_t bufferByteCount{};
    OperationState state{OperationState::Empty};
    bool submittedKernel{};
    bool lastCompletionExecutedKernel{};
};

Ex2CudaBOperation::Ex2CudaBOperation(
    int deviceOrdinal,
    const ex2::IndexedConfiguration& configuration,
    std::uint64_t seed)
    : impl_{std::make_unique<Impl>(deviceOrdinal, configuration, seed)}
{
    detail::ValidateEx2CudaBConfiguration(
        ex2::MakeConfiguration(configuration, seed));
    impl_->bufferByteCount = detail::CalculateEx2CudaBBufferByteCount(
        configuration.elementCount,
        std::numeric_limits<std::size_t>::max());
    if (configuration.elementCount > std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 CUDA B logical buffers exceed the host vector maximum");
    }

    CheckCuda(
        cudaSetDevice(deviceOrdinal),
        Ex2CudaBNativePhase::DeviceSelection,
        "cudaSetDevice for EX-2 CUDA B");
    CheckCuda(
        cudaFree(nullptr),
        Ex2CudaBNativePhase::RuntimeInitialization,
        "cudaFree(nullptr) for EX-2 CUDA B runtime initialization");

    cudaDeviceProp properties{};
    CheckCuda(
        cudaGetDeviceProperties(&properties, deviceOrdinal),
        Ex2CudaBNativePhase::DeviceProperties,
        "cudaGetDeviceProperties for EX-2 CUDA B");
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
    impl_->launchShape = detail::ValidateEx2CudaBLaunchShape(
        configuration,
        maximumGridDimensionX,
        maximumThreadsPerBlock,
        maximumThreadsDimensionX);

    std::size_t freeGlobalMemoryBytes = 0U;
    std::size_t totalGlobalMemoryBytes = 0U;
    CheckCuda(
        cudaMemGetInfo(&freeGlobalMemoryBytes, &totalGlobalMemoryBytes),
        Ex2CudaBNativePhase::ResourcePreflight,
        "cudaMemGetInfo for EX-2 CUDA B");
    detail::ValidateEx2CudaBMemoryFeasibility(
        impl_->bufferByteCount,
        totalGlobalMemoryBytes,
        freeGlobalMemoryBytes);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        Ex2CudaBNativePhase::StreamCreation,
        "cudaStreamCreateWithFlags for EX-2 CUDA B");

    if (configuration.elementCount == 0U)
    {
        return;
    }

    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->input),
            impl_->bufferByteCount),
        Ex2CudaBNativePhase::InputAllocation,
        "cudaMalloc for EX-2 CUDA B input");
    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->indices),
            impl_->bufferByteCount),
        Ex2CudaBNativePhase::IndexAllocation,
        "cudaMalloc for EX-2 CUDA B indices");
    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->output),
            impl_->bufferByteCount),
        Ex2CudaBNativePhase::OutputAllocation,
        "cudaMalloc for EX-2 CUDA B output");
}

Ex2CudaBOperation::~Ex2CudaBOperation() noexcept = default;

int Ex2CudaBOperation::SelectedDeviceOrdinal() const noexcept
{
    return impl_->deviceOrdinal;
}

const std::array<std::uint8_t, 16>&
Ex2CudaBOperation::SelectedDeviceUuid() const noexcept
{
    return impl_->deviceUuid;
}

std::uint64_t Ex2CudaBOperation::ElementCount() const noexcept
{
    return impl_->configuration.elementCount;
}

ex2::IndexedVariant Ex2CudaBOperation::Variant() const noexcept
{
    return impl_->configuration.variant;
}

ex2::IndexPattern Ex2CudaBOperation::Pattern() const noexcept
{
    return impl_->configuration.indexPattern;
}

void Ex2CudaBOperation::Upload(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices)
{
    impl_->RequireReusable("upload");
    impl_->state = OperationState::Empty;
    impl_->submittedKernel = false;
    impl_->lastCompletionExecutedKernel = false;

    if (input.size() != impl_->configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 CUDA B input upload size does not match the configured element count");
    }
    if (indices.size() != impl_->configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 CUDA B index upload size does not match the configured element count");
    }

    ex2::ValidateIndexPermutation(indices, impl_->configuration.elementCount);
    {
        const auto declaredIndices =
            impl_->configuration.indexPattern == ex2::IndexPattern::StructuredV1
            ? ex2::GenerateStructuredPermutation(
                impl_->configuration.elementCount)
            : ex2::GenerateShuffledPermutation(
                impl_->seed, impl_->configuration.elementCount);
        if (!std::equal(
                indices.begin(), indices.end(), declaredIndices.begin()))
        {
            throw std::invalid_argument(
                "EX-2 CUDA B indices do not match the declared pattern and seed");
        }
    }

    impl_->ActivateDevice(
        Ex2CudaBNativePhase::InputUpload,
        "cudaSetDevice before EX-2 CUDA B upload");
    if (!input.empty())
    {
        // Copy caller-owned data before any asynchronous CUDA work. No queued
        // transfer refers to either caller span after this point.
        impl_->StageUploadInput(input);
        const cudaError_t inputResult = cudaMemcpyAsync(
            impl_->input,
            impl_->hostTransferStorage->data(),
            impl_->bufferByteCount,
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (inputResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                inputResult,
                Ex2CudaBNativePhase::InputUpload,
                "cudaMemcpyAsync for EX-2 CUDA B input upload");
        }

        const cudaError_t inputWaitResult =
            cudaStreamSynchronize(impl_->stream);
        if (inputWaitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                inputWaitResult,
                Ex2CudaBNativePhase::UploadCompletion,
                "cudaStreamSynchronize after EX-2 CUDA B input upload");
        }

        // The input transfer is now known complete, so its owned storage can be
        // reused without a second N-word host allocation.
        impl_->ReplaceStagedUploadWithIndices(indices);

        const cudaError_t indexResult = cudaMemcpyAsync(
            impl_->indices,
            impl_->hostTransferStorage->data(),
            impl_->bufferByteCount,
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (indexResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                indexResult,
                Ex2CudaBNativePhase::IndexUpload,
                "cudaMemcpyAsync for EX-2 CUDA B index upload");
        }

        const cudaError_t initializationResult = cudaMemsetAsync(
            impl_->output,
            Ex2CudaBOutputSentinelByte,
            impl_->bufferByteCount,
            impl_->stream);
        if (initializationResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                initializationResult,
                Ex2CudaBNativePhase::OutputInitialization,
                "cudaMemsetAsync for EX-2 CUDA B output initialization");
        }

        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                waitResult,
                Ex2CudaBNativePhase::UploadCompletion,
                "cudaStreamSynchronize after EX-2 CUDA B upload");
        }
        impl_->hostTransferStorage.reset();
    }

    impl_->state = OperationState::Ready;
}

void Ex2CudaBOperation::Submit()
{
    impl_->RequireReusable("submission");
    if (impl_->state != OperationState::Ready)
    {
        throw std::logic_error(
            "EX-2 CUDA B submission requires a newly completed upload");
    }
    impl_->ActivateDevice(
        Ex2CudaBNativePhase::Submission,
        "cudaSetDevice before EX-2 CUDA B submission");

    impl_->submittedKernel = impl_->configuration.elementCount != 0U;
    if (impl_->submittedKernel)
    {
        const std::uint32_t elementCount =
            static_cast<std::uint32_t>(impl_->configuration.elementCount);
        if (impl_->configuration.variant == ex2::IndexedVariant::B1)
        {
            Ex2CudaB1GatherKernel<<<
                impl_->launchShape.blockCount,
                impl_->launchShape.threadsPerBlock,
                0U,
                impl_->stream>>>(
                    impl_->input, impl_->indices, impl_->output, elementCount);
        }
        else
        {
            Ex2CudaB2ScatterKernel<<<
                impl_->launchShape.blockCount,
                impl_->launchShape.threadsPerBlock,
                0U,
                impl_->stream>>>(
                    impl_->input, impl_->indices, impl_->output, elementCount);
        }

        const cudaError_t launchResult = cudaGetLastError();
        if (launchResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                launchResult,
                Ex2CudaBNativePhase::Submission,
                "EX-2 CUDA B kernel launch");
        }
    }
    impl_->state = OperationState::Submitted;
}

void Ex2CudaBOperation::WaitForCompletion()
{
    if (impl_->state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 CUDA B completion cannot be retried after uncertain native completion");
    }
    if (impl_->state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 CUDA B completion wait requires one submitted operation");
    }
    impl_->ActivateDevice(
        Ex2CudaBNativePhase::CompletionWait,
        "cudaSetDevice before EX-2 CUDA B completion wait");

    const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
    if (waitResult != cudaSuccess)
    {
        impl_->PreserveForProcessTeardown();
        ThrowCudaError(
            waitResult,
            Ex2CudaBNativePhase::CompletionWait,
            "cudaStreamSynchronize for EX-2 CUDA B completion");
    }
    impl_->state = OperationState::Complete;
    impl_->lastCompletionExecutedKernel = impl_->submittedKernel;
}

std::vector<std::uint32_t> Ex2CudaBOperation::RetrieveOutput()
{
    return impl_->ReadBack(
        impl_->output,
        Ex2CudaBNativePhase::OutputReadback,
        "cudaMemcpyAsync for EX-2 CUDA B output readback",
        "cudaStreamSynchronize after EX-2 CUDA B output readback");
}

std::vector<std::uint32_t> Ex2CudaBOperation::RetrieveDeviceInput()
{
    return impl_->ReadBack(
        impl_->input,
        Ex2CudaBNativePhase::InputDiagnosticReadback,
        "cudaMemcpyAsync for EX-2 CUDA B input diagnostic readback",
        "cudaStreamSynchronize after EX-2 CUDA B input diagnostic readback");
}

std::vector<std::uint32_t> Ex2CudaBOperation::RetrieveDeviceIndices()
{
    return impl_->ReadBack(
        impl_->indices,
        Ex2CudaBNativePhase::IndexDiagnosticReadback,
        "cudaMemcpyAsync for EX-2 CUDA B index diagnostic readback",
        "cudaStreamSynchronize after EX-2 CUDA B index diagnostic readback");
}

bool Ex2CudaBOperation::LastCompletionExecutedKernel() const
{
    if (impl_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 CUDA B dispatch status requires explicit successful completion");
    }
    return impl_->lastCompletionExecutedKernel;
}

} // namespace computelab::cuda
