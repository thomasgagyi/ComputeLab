#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace computelab::cuda
{

[[nodiscard]] std::uint64_t DeviceMillisecondsToNanoseconds(
    float milliseconds);

class CudaTransformOperation final
{
public:
    CudaTransformOperation(int deviceOrdinal, std::size_t elementCount);
    ~CudaTransformOperation();

    CudaTransformOperation(const CudaTransformOperation&) = delete;
    CudaTransformOperation& operator=(const CudaTransformOperation&) = delete;
    CudaTransformOperation(CudaTransformOperation&&) = delete;
    CudaTransformOperation& operator=(CudaTransformOperation&&) = delete;

    [[nodiscard]] std::size_t ElementCount() const noexcept;

    void Upload(std::span<const std::uint32_t> input);
    void RecordDeviceStart();
    void SubmitTransform();
    void RecordDeviceStop();
    void WaitForCompletion();

    [[nodiscard]] std::uint64_t DeviceElapsedNanoseconds() const;
    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
