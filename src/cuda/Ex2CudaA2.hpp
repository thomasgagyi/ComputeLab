#pragma once

#include "cuda/Ex2CudaA1.hpp"

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

inline constexpr std::uint32_t Ex2CudaA2ThreadsPerBlock = 256U;

enum class Ex2CudaA2NativePhase
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
    Ex2CudaA2NativePhase phase) noexcept;

class Ex2CudaA2NativeError final : public std::runtime_error
{
public:
    Ex2CudaA2NativeError(
        Ex2CudaA2NativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message);

    [[nodiscard]] Ex2CudaA2NativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;

private:
    Ex2CudaA2NativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
};

namespace detail
{

[[nodiscard]] Ex2CudaA2NativePhase ConvertEx2CudaA1PhaseForA2(
    Ex2CudaA1NativePhase phase);
[[nodiscard]] Ex2CudaA1NativePhase ConvertEx2CudaA2PhaseForA1(
    Ex2CudaA2NativePhase phase);
[[nodiscard]] std::string RelabelEx2CudaA1DiagnosticForA2(std::string text);

struct Ex2CudaA2LaunchShape
{
    std::uint32_t blockCount{};
    std::uint32_t threadsPerBlock{Ex2CudaA2ThreadsPerBlock};
};

[[nodiscard]] Ex2CudaA2LaunchShape ValidateEx2CudaA2LaunchShape(
    const ex2::LinearConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX);
[[nodiscard]] std::size_t CalculateEx2CudaA2BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaA2MemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);

enum class Ex2CudaA2ResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] constexpr Ex2CudaA2ResourceDisposition
ClassifyEx2CudaA2ResourceDisposition(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? Ex2CudaA2ResourceDisposition::DestroyNormally
        : Ex2CudaA2ResourceDisposition::PreserveForProcessTeardown;
}

} // namespace detail

// A2-only public operation. It reuses the established CUDA-local A1 resource
// lifecycle but selects the dedicated A2 kernel and rejects every non-A2
// configuration before native interaction.
class Ex2CudaA2Operation final
{
public:
    Ex2CudaA2Operation(
        int deviceOrdinal,
        const ex2::LinearConfiguration& configuration);
    ~Ex2CudaA2Operation() noexcept;

    Ex2CudaA2Operation(const Ex2CudaA2Operation&) = delete;
    Ex2CudaA2Operation& operator=(const Ex2CudaA2Operation&) = delete;
    Ex2CudaA2Operation(Ex2CudaA2Operation&&) = delete;
    Ex2CudaA2Operation& operator=(Ex2CudaA2Operation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;

    void Upload(std::span<const std::uint32_t> input);
    void SubmitA2();
    void WaitForCompletion();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] bool LastCompletionExecutedKernel() const;

private:
    std::unique_ptr<Ex2CudaA1Operation> operation_;
};

} // namespace computelab::cuda
