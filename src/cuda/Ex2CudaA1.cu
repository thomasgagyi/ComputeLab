#include "cuda/Ex2CudaA1.hpp"

#include <cuda_runtime.h>

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
    Ex2CudaA1NativePhase phase,
    const char* operation)
{
    const std::string name = CudaErrorName(result);
    throw Ex2CudaA1NativeError{
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
    Ex2CudaA1NativePhase phase,
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

__global__ void Ex2CudaA1Kernel(
    const std::uint32_t* input,
    std::uint32_t* output,
    std::uint32_t elementCount)
{
    const std::uint32_t index =
        blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    output[index] = input[index] ^ (0x9E3779B9U + index);
}

__global__ void Ex2CudaA2Kernel(
    const std::uint32_t* input,
    std::uint32_t* output,
    std::uint32_t elementCount)
{
    const std::uint32_t index =
        blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= elementCount)
    {
        return;
    }

    std::uint32_t value = input[index] ^ (0x9E3779B9U + index);
    for (std::uint32_t round = 0U; round < 16U; ++round)
    {
        value = (value ^ (value >> 16U)) * 0x7FEB352DU;
        value = (value ^ (value >> 15U)) * 0x846CA68BU;
    }
    output[index] = value ^ (value >> 16U);
}

} // namespace

std::string_view ToString(Ex2CudaA1NativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2CudaA1NativePhase::DeviceSelection: return "device selection";
    case Ex2CudaA1NativePhase::RuntimeInitialization:
        return "runtime initialization";
    case Ex2CudaA1NativePhase::DeviceProperties: return "device properties";
    case Ex2CudaA1NativePhase::ResourcePreflight: return "resource preflight";
    case Ex2CudaA1NativePhase::StreamCreation: return "stream creation";
    case Ex2CudaA1NativePhase::InputAllocation: return "input allocation";
    case Ex2CudaA1NativePhase::OutputAllocation: return "output allocation";
    case Ex2CudaA1NativePhase::Upload: return "upload";
    case Ex2CudaA1NativePhase::UploadCompletion: return "upload completion";
    case Ex2CudaA1NativePhase::Submission: return "submission";
    case Ex2CudaA1NativePhase::CompletionWait: return "completion wait";
    case Ex2CudaA1NativePhase::OutputReadback: return "output readback";
    case Ex2CudaA1NativePhase::InputDiagnosticReadback:
        return "input diagnostic readback";
    }
    return "invalid";
}

Ex2CudaA1NativeError::Ex2CudaA1NativeError(
    Ex2CudaA1NativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)}
{
}

Ex2CudaA1NativePhase Ex2CudaA1NativeError::Phase() const noexcept
{
    return phase_;
}

int Ex2CudaA1NativeError::NativeErrorCode() const noexcept
{
    return nativeErrorCode_;
}

const std::string& Ex2CudaA1NativeError::NativeErrorName() const noexcept
{
    return nativeErrorName_;
}

detail::Ex2CudaA1LaunchShape detail::ValidateEx2CudaA1LaunchShape(
    const ex2::LinearConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX)
{
    const auto semanticConfiguration = ex2::MakeConfiguration(configuration);
    if (!ex2::ValidateSemanticConfiguration(semanticConfiguration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != ex2::LinearVariant::A1)
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 operation requires the A1 linear variant");
    }

    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (maximumThreadsPerBlock < Ex2CudaA1ThreadsPerBlock ||
        maximumThreadsDimensionX < Ex2CudaA1ThreadsPerBlock)
    {
        throw std::invalid_argument(
            "selected CUDA device cannot execute the EX-2 A1 256-thread block");
    }

    const std::uint64_t requiredBlocks =
        configuration.elementCount / Ex2CudaA1ThreadsPerBlock +
        (configuration.elementCount % Ex2CudaA1ThreadsPerBlock == 0U
            ? 0U
            : 1U);
    if (requiredBlocks > maximumGridDimensionX ||
        requiredBlocks > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 element count exceeds the selected device grid capacity");
    }

    return {
        static_cast<std::uint32_t>(requiredBlocks),
        Ex2CudaA1ThreadsPerBlock};
}

std::size_t detail::CalculateEx2CudaA1BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 element count exceeds uint32 logical indexing");
    }
    if (elementCount >
        maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 CUDA A1 buffer byte count exceeds host addressability");
    }
    return static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2CudaA1MemoryFeasibility(
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
            "EX-2 CUDA A1 input and output exceed selected-device global memory");
    }
    if (singleBufferByteCount > freeGlobalMemoryBytes / 2U)
    {
        throw std::length_error(
            "EX-2 CUDA A1 input and output exceed currently free device memory");
    }
}

class Ex2CudaA1Operation::Impl final
{
public:
    Impl(int requestedDeviceOrdinal, ex2::LinearConfiguration requestedConfiguration)
        : deviceOrdinal{requestedDeviceOrdinal},
          configuration{requestedConfiguration}
    {
    }

    ~Impl() noexcept
    {
        if (state == OperationState::CompletionUncertain)
        {
            return;
        }
        if (input == nullptr && output == nullptr && stream == nullptr)
        {
            return;
        }

        const cudaError_t selectionResult = cudaSetDevice(deviceOrdinal);
        if (selectionResult != cudaSuccess)
        {
            ReportCudaCleanupError(
                selectionResult,
                "cudaSetDevice during EX-2 CUDA A1 cleanup");
            return;
        }

        // Destruction after submission may block to establish safe completion.
        // If that cannot be established, leave all resources for process teardown.
        if (state == OperationState::Submitted)
        {
            const cudaError_t waitResult = cudaStreamSynchronize(stream);
            if (waitResult != cudaSuccess)
            {
                ReportCudaCleanupError(
                    waitResult,
                    "cudaStreamSynchronize during EX-2 CUDA A1 cleanup");
                return;
            }
        }

        if (output != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(output),
                "cudaFree for EX-2 CUDA A1 output buffer");
        }
        if (input != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(input),
                "cudaFree for EX-2 CUDA A1 input buffer");
        }
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for EX-2 CUDA A1 stream");
        }
    }

    void RequireReusable(const char* operation) const
    {
        if (state == OperationState::Submitted)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA A1 "} + operation +
                " is invalid while execution is incomplete");
        }
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA A1 "} + operation +
                " is unavailable after uncertain native completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                std::string{"EX-2 CUDA A1 "} + operation +
                " is unavailable after a native failure");
        }
    }

    void ActivateDevice(
        Ex2CudaA1NativePhase phase,
        const char* operation)
    {
        const cudaError_t result = cudaSetDevice(deviceOrdinal);
        if (result != cudaSuccess)
        {
            if (state == OperationState::Submitted)
            {
                state = OperationState::CompletionUncertain;
            }
            else
            {
                state = OperationState::Failed;
            }
            ThrowCudaError(result, phase, operation);
        }
    }

    void PreserveForProcessTeardown() noexcept
    {
        state = OperationState::CompletionUncertain;
    }

    std::vector<std::uint32_t> ReadBack(
        const std::uint32_t* source,
        Ex2CudaA1NativePhase phase,
        const char* copyOperation,
        const char* waitOperation)
    {
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                "EX-2 CUDA A1 readback is unavailable after uncertain native completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                "EX-2 CUDA A1 readback is unavailable after a native failure");
        }
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                "EX-2 CUDA A1 readback requires explicit successful completion");
        }
        ActivateDevice(phase, "cudaSetDevice before EX-2 CUDA A1 readback");

        std::vector<std::uint32_t> result(
            static_cast<std::size_t>(configuration.elementCount));
        if (result.empty())
        {
            return result;
        }

        const cudaError_t copyResult = cudaMemcpyAsync(
            result.data(),
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
        return result;
    }

    int deviceOrdinal{};
    ex2::LinearConfiguration configuration;
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t* input{nullptr};
    std::uint32_t* output{nullptr};
    cudaStream_t stream{nullptr};
    detail::Ex2CudaA1LaunchShape launchShape;
    std::size_t bufferByteCount{};
    OperationState state{OperationState::Empty};
    bool submittedKernel{};
    bool lastCompletionExecutedKernel{};
};

Ex2CudaA1Operation::Ex2CudaA1Operation(
    int deviceOrdinal,
    const ex2::LinearConfiguration& configuration)
    : Ex2CudaA1Operation{
          deviceOrdinal, configuration, ex2::LinearVariant::A1}
{
}

Ex2CudaA1Operation::Ex2CudaA1Operation(
    int deviceOrdinal,
    const ex2::LinearConfiguration& configuration,
    ex2::LinearVariant requiredVariant)
    : impl_{std::make_unique<Impl>(deviceOrdinal, configuration)}
{
    // I2-C semantic validation deliberately precedes all CUDA interaction.
    const auto semanticConfiguration = ex2::MakeConfiguration(configuration);
    if (!ex2::ValidateSemanticConfiguration(semanticConfiguration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != requiredVariant)
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 operation requires the A1 linear variant");
    }
    impl_->bufferByteCount = detail::CalculateEx2CudaA1BufferByteCount(
        configuration.elementCount,
        std::numeric_limits<std::size_t>::max());
    if (configuration.elementCount > std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 CUDA A1 output exceeds the host vector maximum");
    }

    CheckCuda(
        cudaSetDevice(deviceOrdinal),
        Ex2CudaA1NativePhase::DeviceSelection,
        "cudaSetDevice for EX-2 CUDA A1");
    CheckCuda(
        cudaFree(nullptr),
        Ex2CudaA1NativePhase::RuntimeInitialization,
        "cudaFree(nullptr) for EX-2 CUDA A1 runtime initialization");

    cudaDeviceProp properties{};
    CheckCuda(
        cudaGetDeviceProperties(&properties, deviceOrdinal),
        Ex2CudaA1NativePhase::DeviceProperties,
        "cudaGetDeviceProperties for EX-2 CUDA A1");
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

    const std::uint64_t maximumGridDimensionX =
        properties.maxGridSize[0] > 0
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
    auto launchConfiguration = configuration;
    launchConfiguration.variant = ex2::LinearVariant::A1;
    impl_->launchShape = detail::ValidateEx2CudaA1LaunchShape(
        launchConfiguration,
        maximumGridDimensionX,
        maximumThreadsPerBlock,
        maximumThreadsDimensionX);

    std::size_t freeGlobalMemoryBytes = 0U;
    std::size_t totalGlobalMemoryBytes = 0U;
    CheckCuda(
        cudaMemGetInfo(&freeGlobalMemoryBytes, &totalGlobalMemoryBytes),
        Ex2CudaA1NativePhase::ResourcePreflight,
        "cudaMemGetInfo for EX-2 CUDA A1");
    detail::ValidateEx2CudaA1MemoryFeasibility(
        impl_->bufferByteCount,
        totalGlobalMemoryBytes,
        freeGlobalMemoryBytes);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        Ex2CudaA1NativePhase::StreamCreation,
        "cudaStreamCreateWithFlags for EX-2 CUDA A1");

    if (configuration.elementCount == 0U)
    {
        return;
    }

    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->input),
            impl_->bufferByteCount),
        Ex2CudaA1NativePhase::InputAllocation,
        "cudaMalloc for EX-2 CUDA A1 input");
    CheckCuda(
        cudaMalloc(
            reinterpret_cast<void**>(&impl_->output),
            impl_->bufferByteCount),
        Ex2CudaA1NativePhase::OutputAllocation,
        "cudaMalloc for EX-2 CUDA A1 output");
}

Ex2CudaA1Operation::~Ex2CudaA1Operation() noexcept = default;

int Ex2CudaA1Operation::SelectedDeviceOrdinal() const noexcept
{
    return impl_->deviceOrdinal;
}

const std::array<std::uint8_t, 16>&
Ex2CudaA1Operation::SelectedDeviceUuid() const noexcept
{
    return impl_->deviceUuid;
}

std::uint64_t Ex2CudaA1Operation::ElementCount() const noexcept
{
    return impl_->configuration.elementCount;
}

void Ex2CudaA1Operation::Upload(std::span<const std::uint32_t> input)
{
    impl_->RequireReusable("upload");
    if (input.size() != impl_->configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 CUDA A1 upload size does not match the configured element count");
    }
    impl_->ActivateDevice(
        Ex2CudaA1NativePhase::Upload,
        "cudaSetDevice before EX-2 CUDA A1 upload");

    if (!input.empty())
    {
        const cudaError_t uploadResult = cudaMemcpyAsync(
            impl_->input,
            input.data(),
            impl_->bufferByteCount,
            cudaMemcpyHostToDevice,
            impl_->stream);
        if (uploadResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                uploadResult,
                Ex2CudaA1NativePhase::Upload,
                "cudaMemcpyAsync for EX-2 CUDA A1 upload");
        }
        const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
        if (waitResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                waitResult,
                Ex2CudaA1NativePhase::UploadCompletion,
                "cudaStreamSynchronize after EX-2 CUDA A1 upload");
        }
    }

    impl_->state = OperationState::Ready;
    impl_->submittedKernel = false;
    impl_->lastCompletionExecutedKernel = false;
}

void Ex2CudaA1Operation::SubmitA1()
{
    impl_->RequireReusable("submission");
    if (impl_->state != OperationState::Ready)
    {
        throw std::logic_error(
            "EX-2 CUDA A1 submission requires a newly completed upload");
    }
    impl_->ActivateDevice(
        Ex2CudaA1NativePhase::Submission,
        "cudaSetDevice before EX-2 CUDA A1 submission");

    impl_->submittedKernel = impl_->configuration.elementCount != 0U;
    if (impl_->submittedKernel)
    {
        if (impl_->configuration.variant == ex2::LinearVariant::A1)
        {
            Ex2CudaA1Kernel<<<
                impl_->launchShape.blockCount,
                impl_->launchShape.threadsPerBlock,
                0U,
                impl_->stream>>>(
                    impl_->input,
                    impl_->output,
                    static_cast<std::uint32_t>(
                        impl_->configuration.elementCount));
        }
        else
        {
            Ex2CudaA2Kernel<<<
                impl_->launchShape.blockCount,
                impl_->launchShape.threadsPerBlock,
                0U,
                impl_->stream>>>(
                    impl_->input,
                    impl_->output,
                    static_cast<std::uint32_t>(
                        impl_->configuration.elementCount));
        }
        const cudaError_t launchResult = cudaGetLastError();
        if (launchResult != cudaSuccess)
        {
            impl_->PreserveForProcessTeardown();
            ThrowCudaError(
                launchResult,
                Ex2CudaA1NativePhase::Submission,
                "EX-2 CUDA A1 kernel launch");
        }
    }
    impl_->state = OperationState::Submitted;
}

void Ex2CudaA1Operation::WaitForCompletion()
{
    if (impl_->state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 CUDA A1 completion cannot be retried after uncertain native completion");
    }
    if (impl_->state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 CUDA A1 completion wait requires one submitted operation");
    }
    impl_->ActivateDevice(
        Ex2CudaA1NativePhase::CompletionWait,
        "cudaSetDevice before EX-2 CUDA A1 completion wait");

    const cudaError_t waitResult = cudaStreamSynchronize(impl_->stream);
    if (waitResult != cudaSuccess)
    {
        impl_->PreserveForProcessTeardown();
        ThrowCudaError(
            waitResult,
            Ex2CudaA1NativePhase::CompletionWait,
            "cudaStreamSynchronize for EX-2 CUDA A1 completion");
    }
    impl_->state = OperationState::Complete;
    impl_->lastCompletionExecutedKernel = impl_->submittedKernel;
}

std::vector<std::uint32_t> Ex2CudaA1Operation::RetrieveOutput()
{
    return impl_->ReadBack(
        impl_->output,
        Ex2CudaA1NativePhase::OutputReadback,
        "cudaMemcpyAsync for EX-2 CUDA A1 output readback",
        "cudaStreamSynchronize after EX-2 CUDA A1 output readback");
}

std::vector<std::uint32_t> Ex2CudaA1Operation::RetrieveDeviceInput()
{
    return impl_->ReadBack(
        impl_->input,
        Ex2CudaA1NativePhase::InputDiagnosticReadback,
        "cudaMemcpyAsync for EX-2 CUDA A1 input diagnostic readback",
        "cudaStreamSynchronize after EX-2 CUDA A1 input diagnostic readback");
}

bool Ex2CudaA1Operation::LastCompletionExecutedKernel() const
{
    if (impl_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 CUDA A1 dispatch status requires explicit successful completion");
    }
    return impl_->lastCompletionExecutedKernel;
}

} // namespace computelab::cuda
