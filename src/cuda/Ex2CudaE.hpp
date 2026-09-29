#pragma once

#include "ex2/Ex2Configuration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::cuda
{

enum class Ex2CudaENativePhase
{
    DeviceSelection,
    RuntimeInitialization,
    DeviceProperties,
    ResourcePreflight,
    StreamCreation,
    PinnedSourceAllocation,
    PinnedDestinationAllocation,
    DeviceAllocation,
    HostSourcePreparation,
    DeviceSourcePreparation,
    PreparationCompletion,
    StartMarker,
    TransferSubmission,
    StopMarker,
    CompletionWait,
    ValidationReadback,
    ValidationReadbackCompletion,
    NativeTimingRetrieval,
    NativeTimingConversion,
};

[[nodiscard]] std::string_view ToString(Ex2CudaENativePhase phase) noexcept;

class Ex2CudaENativeError final : public std::runtime_error
{
public:
    Ex2CudaENativeError(
        Ex2CudaENativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message);

    [[nodiscard]] Ex2CudaENativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;

private:
    Ex2CudaENativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
};

enum class Ex2CudaENativeTimingStatus
{
    NotApplicable,
    Valid,
    Unavailable,
    MarkerFailed,
    RetrievalFailed,
    ConversionInvalid,
};

struct Ex2CudaENativeTimingMetadata
{
    std::string_view method{"cudaEventElapsedTime"};
    std::string_view eventCreateFlags{"cudaEventDefault (timing enabled)"};
    std::string_view elapsedValueRepresentation{"float milliseconds"};
    std::uint64_t approximateResolutionNanoseconds{500U};
};

namespace detail
{

void ValidateEx2CudaEConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] std::size_t CalculateEx2CudaEByteCount(
    std::uint64_t byteCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaEHostVectorCapacity(
    std::uint64_t byteCount,
    std::size_t maximumVectorElementCount);

struct Ex2CudaEResourceBytes
{
    std::size_t deviceBytes{};
    std::size_t pinnedHostBytes{};
    std::size_t totalBytes{};
};

[[nodiscard]] Ex2CudaEResourceBytes CalculateEx2CudaEResourceBytes(
    std::size_t byteCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaEDeviceMemoryFeasibility(
    std::size_t deviceBytes,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);
[[nodiscard]] std::uint64_t Ex2CudaEMillisecondsToNanoseconds(
    float milliseconds);

enum class Ex2CudaEOperationState
{
    Empty,
    Prepared,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

enum class Ex2CudaEResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

enum class Ex2CudaEHostStorageDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

struct Ex2CudaECompletionDisposition
{
    Ex2CudaEResourceDisposition deviceResources;
    Ex2CudaEHostStorageDisposition hostStorage;
    bool operationReusable{};
};

[[nodiscard]] constexpr Ex2CudaEOperationState ClassifyEx2CudaEAsyncFailure(
    bool asynchronousOperationAttempted,
    bool safeCompletionEstablished) noexcept
{
    return asynchronousOperationAttempted && !safeCompletionEstablished
        ? Ex2CudaEOperationState::CompletionUncertain
        : Ex2CudaEOperationState::Failed;
}

[[nodiscard]] constexpr Ex2CudaECompletionDisposition
ClassifyEx2CudaECompletionDisposition(
    bool completionUncertain,
    bool asynchronousHostReferencePossible) noexcept
{
    return {
        completionUncertain
            ? Ex2CudaEResourceDisposition::PreserveForProcessTeardown
            : Ex2CudaEResourceDisposition::DestroyNormally,
        completionUncertain && asynchronousHostReferencePossible
            ? Ex2CudaEHostStorageDisposition::PreserveForProcessTeardown
            : Ex2CudaEHostStorageDisposition::DestroyNormally,
        !completionUncertain};
}

[[nodiscard]] constexpr bool IsEx2CudaEOperationStateReusable(
    Ex2CudaEOperationState state) noexcept
{
    return state != Ex2CudaEOperationState::Submitted &&
        state != Ex2CudaEOperationState::Failed &&
        state != Ex2CudaEOperationState::CompletionUncertain;
}

} // namespace detail

class Ex2CudaEOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    Ex2CudaEOperation(
        int deviceOrdinal,
        const ex2::TransferConfiguration& configuration);
    ~Ex2CudaEOperation() noexcept;

    Ex2CudaEOperation(const Ex2CudaEOperation&) = delete;
    Ex2CudaEOperation& operator=(const Ex2CudaEOperation&) = delete;
    Ex2CudaEOperation(Ex2CudaEOperation&&) = delete;
    Ex2CudaEOperation& operator=(Ex2CudaEOperation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ByteCount() const noexcept;
    [[nodiscard]] ex2::TransferDirection Direction() const noexcept;
    [[nodiscard]] std::uint32_t ExpectedNativeCopyCount() const noexcept;

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource);
    void SubmitTransfer();
    void WaitForCompletion();
    [[nodiscard]] std::vector<std::uint8_t> RetrieveDestinationForValidation();

    [[nodiscard]] bool LastCompletionExecutedCopy() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeCopyCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Ex2CudaEDeviceTimedOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    Ex2CudaEDeviceTimedOperation(
        int deviceOrdinal,
        const ex2::TransferConfiguration& configuration);
    ~Ex2CudaEDeviceTimedOperation() noexcept;

    Ex2CudaEDeviceTimedOperation(const Ex2CudaEDeviceTimedOperation&) = delete;
    Ex2CudaEDeviceTimedOperation& operator=(const Ex2CudaEDeviceTimedOperation&) = delete;
    Ex2CudaEDeviceTimedOperation(Ex2CudaEDeviceTimedOperation&&) = delete;
    Ex2CudaEDeviceTimedOperation& operator=(Ex2CudaEDeviceTimedOperation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ByteCount() const noexcept;
    [[nodiscard]] ex2::TransferDirection Direction() const noexcept;
    [[nodiscard]] std::uint32_t ExpectedNativeCopyCount() const noexcept;

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource);
    void SubmitTransfer();
    void WaitForCompletion();
    [[nodiscard]] std::vector<std::uint8_t> RetrieveDestinationForValidation();

    [[nodiscard]] bool LastCompletionExecutedCopy() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeCopyCount() const;
    void RetrieveNativeTiming();
    [[nodiscard]] Ex2CudaENativeTimingStatus NativeTimingStatus() const;
    [[nodiscard]] Ex2CudaENativeTimingMetadata NativeTimingMetadata() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> NativeDeviceIntervalNanoseconds() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
