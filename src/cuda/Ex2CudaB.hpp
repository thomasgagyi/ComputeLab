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

inline constexpr std::uint32_t Ex2CudaBThreadsPerBlock = 256U;
inline constexpr std::uint8_t Ex2CudaBOutputSentinelByte = 0xA5U;

enum class Ex2CudaBNativePhase
{
    DeviceSelection,
    RuntimeInitialization,
    DeviceProperties,
    ResourcePreflight,
    StreamCreation,
    InputAllocation,
    IndexAllocation,
    OutputAllocation,
    InputUpload,
    IndexUpload,
    OutputInitialization,
    UploadCompletion,
    Submission,
    CompletionWait,
    OutputReadback,
    InputDiagnosticReadback,
    IndexDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(Ex2CudaBNativePhase phase) noexcept;

class Ex2CudaBNativeError final : public std::runtime_error
{
public:
    Ex2CudaBNativeError(
        Ex2CudaBNativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message);

    [[nodiscard]] Ex2CudaBNativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;

private:
    Ex2CudaBNativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
};

namespace detail
{

struct Ex2CudaBLaunchShape
{
    std::uint32_t blockCount{};
    std::uint32_t threadsPerBlock{Ex2CudaBThreadsPerBlock};
};

// Pure validation seams cover the complete I2-C contract and native resource
// arithmetic without allocating host or device buffers.
void ValidateEx2CudaBConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2CudaBLaunchShape ValidateEx2CudaBLaunchShape(
    const ex2::IndexedConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX);
[[nodiscard]] std::size_t CalculateEx2CudaBBufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaBMemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);

enum class Ex2CudaBResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

enum class Ex2CudaBHostStorageDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

struct Ex2CudaBCompletionDisposition
{
    Ex2CudaBResourceDisposition deviceResources;
    Ex2CudaBHostStorageDisposition hostStorage;
    bool operationReusable{};
};

[[nodiscard]] constexpr Ex2CudaBResourceDisposition
ClassifyEx2CudaBResourceDisposition(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? Ex2CudaBResourceDisposition::DestroyNormally
        : Ex2CudaBResourceDisposition::PreserveForProcessTeardown;
}

// A native failure before asynchronous work can reference owned host storage is
// known-safe for that storage. Once such a reference is possible, only
// established completion permits release and reuse.
[[nodiscard]] constexpr Ex2CudaBCompletionDisposition
ClassifyEx2CudaBCompletionDisposition(
    bool safeCompletionEstablished,
    bool asynchronousHostReferencePossible) noexcept
{
    return {
        ClassifyEx2CudaBResourceDisposition(safeCompletionEstablished),
        !safeCompletionEstablished && asynchronousHostReferencePossible
            ? Ex2CudaBHostStorageDisposition::PreserveForProcessTeardown
            : Ex2CudaBHostStorageDisposition::DestroyNormally,
        safeCompletionEstablished};
}

} // namespace detail

// Backend-native EX-2 B operation. The variant, element count, index pattern
// and seed are immutable. Upload validates the complete declared permutation,
// copies both logical inputs, initializes output and establishes completion
// before returning. Submit enqueues exactly one selected kernel.
class Ex2CudaBOperation final
{
public:
    Ex2CudaBOperation(
        int deviceOrdinal,
        const ex2::IndexedConfiguration& configuration,
        std::uint64_t seed);
    ~Ex2CudaBOperation() noexcept;

    Ex2CudaBOperation(const Ex2CudaBOperation&) = delete;
    Ex2CudaBOperation& operator=(const Ex2CudaBOperation&) = delete;
    Ex2CudaBOperation(Ex2CudaBOperation&&) = delete;
    Ex2CudaBOperation& operator=(Ex2CudaBOperation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] ex2::IndexedVariant Variant() const noexcept;
    [[nodiscard]] ex2::IndexPattern Pattern() const noexcept;

    void Upload(
        std::span<const std::uint32_t> input,
        std::span<const std::uint32_t> indices);
    void Submit();
    void WaitForCompletion();

    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceIndices();
    [[nodiscard]] bool LastCompletionExecutedKernel() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
