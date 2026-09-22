#pragma once

#include "ex2/Ex2Configuration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::cuda
{

class Ex2CudaA2Operation;

inline constexpr std::uint32_t Ex2CudaA1ThreadsPerBlock = 256U;

enum class Ex2CudaA1NativePhase
{
    DeviceSelection,
    RuntimeInitialization,
    DeviceProperties,
    ResourcePreflight,
    StreamCreation,
    InputAllocation,
    OutputAllocation,
    Upload,
    UploadCompletion,
    Submission,
    CompletionWait,
    OutputReadback,
    InputDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(
    Ex2CudaA1NativePhase phase) noexcept;

class Ex2CudaA1NativeError final : public std::runtime_error
{
public:
    Ex2CudaA1NativeError(
        Ex2CudaA1NativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message);

    [[nodiscard]] Ex2CudaA1NativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;

private:
    Ex2CudaA1NativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
};

namespace detail
{

struct Ex2CudaA1LaunchShape
{
    std::uint32_t blockCount{};
    std::uint32_t threadsPerBlock{Ex2CudaA1ThreadsPerBlock};
};

// Pure, allocation-free validation seams keep oversized and unsupported
// configurations testable without asking the host or device for huge buffers.
[[nodiscard]] Ex2CudaA1LaunchShape ValidateEx2CudaA1LaunchShape(
    const ex2::LinearConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX);
[[nodiscard]] std::size_t CalculateEx2CudaA1BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaA1MemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);

enum class Ex2CudaA1ResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] constexpr Ex2CudaA1ResourceDisposition
ClassifyEx2CudaA1ResourceDisposition(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? Ex2CudaA1ResourceDisposition::DestroyNormally
        : Ex2CudaA1ResourceDisposition::PreserveForProcessTeardown;
}

} // namespace detail

// Backend-native EX-2 A1 operation. Upload and both readbacks complete before
// returning. SubmitA1 only enqueues the kernel; WaitForCompletion establishes
// successful stream completion. A failed wait leaves resource completion
// uncertain and the object permanently non-reusable.
class Ex2CudaA1Operation final
{
public:
    Ex2CudaA1Operation(
        int deviceOrdinal,
        const ex2::LinearConfiguration& configuration);
    ~Ex2CudaA1Operation() noexcept;

    Ex2CudaA1Operation(const Ex2CudaA1Operation&) = delete;
    Ex2CudaA1Operation& operator=(const Ex2CudaA1Operation&) = delete;
    Ex2CudaA1Operation(Ex2CudaA1Operation&&) = delete;
    Ex2CudaA1Operation& operator=(Ex2CudaA1Operation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;

    void Upload(std::span<const std::uint32_t> input);
    void SubmitA1();
    void WaitForCompletion();

    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    // Correctness diagnostic only. This copy is outside the A1 operation
    // boundary and is never performed by the ordinary output path.
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] bool LastCompletionExecutedKernel() const;

private:
    friend class Ex2CudaA2Operation;

    Ex2CudaA1Operation(
        int deviceOrdinal,
        const ex2::LinearConfiguration& configuration,
        ex2::LinearVariant requiredVariant);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
