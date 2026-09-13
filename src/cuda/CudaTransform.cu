#include "cuda/CudaTransform.hpp"

#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace computelab::cuda
{
namespace
{

constexpr unsigned int threadsPerBlock = 256U;

[[noreturn]] void ThrowCudaError(cudaError_t result, const char* operation)
{
    throw std::runtime_error(
        std::string{operation} + " failed with " + cudaGetErrorName(result) +
        ": " + cudaGetErrorString(result));
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

__global__ void TransformKernel(
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

class CudaTransformOperation::Impl final
{
public:
    explicit Impl(std::size_t requestedElementCount)
        : elementCount{requestedElementCount}
    {
    }

    ~Impl()
    {
        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamSynchronize(stream),
                "cudaStreamSynchronize during CUDA transform cleanup");
        }

        if (output != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(output),
                "cudaFree for CUDA transform output buffer");
        }

        if (input != nullptr)
        {
            ReportCudaCleanupError(
                cudaFree(input),
                "cudaFree for CUDA transform input buffer");
        }

        if (stopEvent != nullptr)
        {
            ReportCudaCleanupError(
                cudaEventDestroy(stopEvent),
                "cudaEventDestroy for CUDA transform stop event");
        }

        if (startEvent != nullptr)
        {
            ReportCudaCleanupError(
                cudaEventDestroy(startEvent),
                "cudaEventDestroy for CUDA transform start event");
        }

        if (stream != nullptr)
        {
            ReportCudaCleanupError(
                cudaStreamDestroy(stream),
                "cudaStreamDestroy for CUDA transform stream");
        }
    }

    std::size_t elementCount;
    std::uint32_t* input{nullptr};
    std::uint32_t* output{nullptr};
    cudaStream_t stream{nullptr};
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    unsigned int blockCount{0U};
    bool uploadCompleted{false};
    bool startRecorded{false};
    bool submitted{false};
    bool stopRecorded{false};
    bool completionObserved{false};
};

std::uint64_t DeviceMillisecondsToNanoseconds(float milliseconds)
{
    if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
    {
        throw std::invalid_argument(
            "CUDA event elapsed time must be finite and nonnegative");
    }

    const long double nanoseconds =
        static_cast<long double>(milliseconds) * 1'000'000.0L;
    const long double roundedNanoseconds = std::round(nanoseconds);
    const long double exclusiveUpperBound = std::ldexp(1.0L, 64);

    if (roundedNanoseconds >= exclusiveUpperBound)
    {
        throw std::overflow_error(
            "CUDA event elapsed time is out of range for integer nanoseconds");
    }

    return static_cast<std::uint64_t>(roundedNanoseconds);
}

CudaTransformOperation::CudaTransformOperation(
    int deviceOrdinal,
    std::size_t elementCount)
    : impl_{std::make_unique<Impl>(elementCount)}
{
    CheckCuda(cudaSetDevice(deviceOrdinal), "cudaSetDevice during CUDA setup");
    CheckCuda(cudaFree(nullptr), "cudaFree(nullptr) during CUDA runtime initialization");

    cudaDeviceProp deviceProperties{};
    CheckCuda(
        cudaGetDeviceProperties(&deviceProperties, deviceOrdinal),
        "cudaGetDeviceProperties during CUDA setup");

    const std::size_t requiredBlocks =
        elementCount / threadsPerBlock +
        (elementCount % threadsPerBlock == 0U ? 0U : 1U);

    if (requiredBlocks >
        static_cast<std::size_t>(deviceProperties.maxGridSize[0]))
    {
        throw std::invalid_argument(
            "CUDA transform element count exceeds the selected device's one-dimensional grid capacity");
    }

    impl_->blockCount = static_cast<unsigned int>(requiredBlocks);

    CheckCuda(
        cudaStreamCreateWithFlags(&impl_->stream, cudaStreamNonBlocking),
        "cudaStreamCreateWithFlags for CUDA transform stream");
    CheckCuda(
        cudaEventCreateWithFlags(&impl_->startEvent, cudaEventDefault),
        "cudaEventCreateWithFlags for CUDA transform start event");
    CheckCuda(
        cudaEventCreateWithFlags(&impl_->stopEvent, cudaEventDefault),
        "cudaEventCreateWithFlags for CUDA transform stop event");

    if (elementCount == 0U)
    {
        return;
    }

    if (elementCount >
        std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "CUDA transform element count exceeds addressable byte capacity");
    }

    const std::size_t byteCount = elementCount * sizeof(std::uint32_t);
    CheckCuda(
        cudaMalloc(reinterpret_cast<void**>(&impl_->input), byteCount),
        "cudaMalloc for CUDA transform input buffer");
    CheckCuda(
        cudaMalloc(reinterpret_cast<void**>(&impl_->output), byteCount),
        "cudaMalloc for CUDA transform output buffer");
}

CudaTransformOperation::~CudaTransformOperation() = default;

std::size_t CudaTransformOperation::ElementCount() const noexcept
{
    return impl_->elementCount;
}

void CudaTransformOperation::Upload(std::span<const std::uint32_t> input)
{
    if (input.size() != impl_->elementCount)
    {
        throw std::invalid_argument(
            "CUDA transform upload size does not match the declared element count");
    }

    if (impl_->submitted && !impl_->completionObserved)
    {
        throw std::logic_error(
            "CUDA transform upload cannot replace input while execution is incomplete");
    }

    if (!input.empty())
    {
        CheckCuda(
            cudaMemcpyAsync(
                impl_->input,
                input.data(),
                input.size_bytes(),
                cudaMemcpyHostToDevice,
                impl_->stream),
            "cudaMemcpyAsync for CUDA transform input upload");
        CheckCuda(
            cudaStreamSynchronize(impl_->stream),
            "cudaStreamSynchronize after CUDA transform input upload");
    }

    impl_->uploadCompleted = true;
    impl_->startRecorded = false;
    impl_->submitted = false;
    impl_->stopRecorded = false;
    impl_->completionObserved = false;
}

void CudaTransformOperation::RecordDeviceStart()
{
    if (!impl_->uploadCompleted)
    {
        throw std::logic_error(
            "CUDA transform input must be uploaded before recording device start");
    }

    if (impl_->submitted && !impl_->completionObserved)
    {
        throw std::logic_error(
            "CUDA transform execution is already incomplete");
    }

    CheckCuda(
        cudaEventRecord(impl_->startEvent, impl_->stream),
        "cudaEventRecord for CUDA transform start event");

    impl_->startRecorded = true;
    impl_->submitted = false;
    impl_->stopRecorded = false;
    impl_->completionObserved = false;
}

void CudaTransformOperation::SubmitTransform()
{
    if (!impl_->startRecorded || impl_->submitted)
    {
        throw std::logic_error(
            "CUDA transform submission requires a new recorded device start");
    }

    if (impl_->elementCount != 0U)
    {
        TransformKernel<<<impl_->blockCount, threadsPerBlock, 0U, impl_->stream>>>(
            impl_->input,
            impl_->output,
            impl_->elementCount);
        CheckCuda(
            cudaGetLastError(),
            "CUDA transform kernel launch");
    }

    impl_->submitted = true;
}

void CudaTransformOperation::RecordDeviceStop()
{
    if (!impl_->submitted || impl_->stopRecorded)
    {
        throw std::logic_error(
            "CUDA transform stop event requires one submitted transform");
    }

    CheckCuda(
        cudaEventRecord(impl_->stopEvent, impl_->stream),
        "cudaEventRecord for CUDA transform stop event");
    impl_->stopRecorded = true;
}

void CudaTransformOperation::WaitForCompletion()
{
    if (!impl_->stopRecorded)
    {
        throw std::logic_error(
            "CUDA transform completion wait requires a recorded stop event");
    }

    CheckCuda(
        cudaStreamSynchronize(impl_->stream),
        "cudaStreamSynchronize for CUDA transform completion");
    impl_->completionObserved = true;
}

std::uint64_t CudaTransformOperation::DeviceElapsedNanoseconds() const
{
    if (!impl_->completionObserved)
    {
        throw std::logic_error(
            "CUDA transform device elapsed time requires explicit completion");
    }

    float elapsedMilliseconds = 0.0F;
    CheckCuda(
        cudaEventElapsedTime(
            &elapsedMilliseconds,
            impl_->startEvent,
            impl_->stopEvent),
        "cudaEventElapsedTime for CUDA transform");

    return DeviceMillisecondsToNanoseconds(elapsedMilliseconds);
}

std::vector<std::uint32_t> CudaTransformOperation::RetrieveOutput() const
{
    if (!impl_->completionObserved)
    {
        throw std::logic_error(
            "CUDA transform output retrieval requires explicit completion");
    }

    std::vector<std::uint32_t> output(impl_->elementCount);
    if (output.empty())
    {
        return output;
    }

    CheckCuda(
        cudaMemcpyAsync(
            output.data(),
            impl_->output,
            output.size() * sizeof(std::uint32_t),
            cudaMemcpyDeviceToHost,
            impl_->stream),
        "cudaMemcpyAsync for CUDA transform output retrieval");
    CheckCuda(
        cudaStreamSynchronize(impl_->stream),
        "cudaStreamSynchronize after CUDA transform output retrieval");

    return output;
}

} // namespace computelab::cuda
