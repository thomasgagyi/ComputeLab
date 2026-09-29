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

inline constexpr std::uint32_t Ex2CudaD1ThreadsPerBlock = 256U;

enum class Ex2CudaD1NativePhase
{
    DeviceSelection,
    RuntimeInitialization,
    DeviceProperties,
    ResourcePreflight,
    StreamCreation,
    StateAAllocation,
    StateBAllocation,
    InitialUpload,
    UploadCompletion,
    StartMarker,
    SequenceSubmission,
    StopMarker,
    CompletionWait,
    FinalReadback,
    NativeTimingRetrieval,
    NativeTimingConversion,
};

[[nodiscard]] std::string_view ToString(Ex2CudaD1NativePhase phase) noexcept;

class Ex2CudaD1NativeError final : public std::runtime_error
{
public:
    Ex2CudaD1NativeError(
        Ex2CudaD1NativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message,
        std::optional<std::uint32_t> attemptedPass = std::nullopt,
        std::uint32_t acceptedDispatchCount = 0U);

    [[nodiscard]] Ex2CudaD1NativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> AttemptedPass() const noexcept;
    [[nodiscard]] std::uint32_t AcceptedDispatchCount() const noexcept;

private:
    Ex2CudaD1NativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
    std::optional<std::uint32_t> attemptedPass_;
    std::uint32_t acceptedDispatchCount_{};
};

enum class Ex2CudaD1NativeTimingStatus
{
    NotApplicable,
    Valid,
    Unavailable,
    MarkerFailed,
    RetrievalFailed,
    ConversionInvalid,
};

struct Ex2CudaD1NativeTimingMetadata
{
    std::string_view method{"cudaEventElapsedTime"};
    std::string_view eventCreateFlags{"cudaEventDefault (timing enabled)"};
    std::string_view elapsedValueRepresentation{"float milliseconds"};
    std::uint64_t approximateResolutionNanoseconds{500U};
};

namespace detail
{

struct Ex2CudaD1LaunchShape
{
    std::uint32_t blockCount{};
    std::uint32_t threadsPerBlock{Ex2CudaD1ThreadsPerBlock};
};

void ValidateEx2CudaD1Configuration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2CudaD1LaunchShape ValidateEx2CudaD1LaunchShape(
    const ex2::IterativeConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX);
[[nodiscard]] std::size_t CalculateEx2CudaD1BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaD1HostVectorCapacity(
    std::uint64_t elementCount,
    std::size_t maximumVectorElementCount);
void ValidateEx2CudaD1MemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);
[[nodiscard]] std::uint64_t Ex2CudaD1MillisecondsToNanoseconds(
    float milliseconds);

enum class Ex2CudaD1OperationState
{
    Empty,
    InitialStateReady,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

enum class Ex2CudaD1ResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

enum class Ex2CudaD1HostStorageDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

struct Ex2CudaD1CompletionDisposition
{
    Ex2CudaD1ResourceDisposition deviceResources;
    Ex2CudaD1HostStorageDisposition hostStorage;
    bool operationReusable{};
};

[[nodiscard]] constexpr Ex2CudaD1OperationState
ClassifyEx2CudaD1AsyncFailure(
    bool positiveLaunchAttempted,
    bool safeCompletionEstablished) noexcept
{
    return positiveLaunchAttempted && !safeCompletionEstablished
        ? Ex2CudaD1OperationState::CompletionUncertain
        : Ex2CudaD1OperationState::Failed;
}

struct Ex2CudaD1PassFailureClassification
{
    Ex2CudaD1OperationState state;
    std::uint32_t attemptedPass{};
    std::uint32_t acceptedDispatchCount{};
};

[[nodiscard]] constexpr Ex2CudaD1PassFailureClassification
ClassifyEx2CudaD1PassFailure(
    std::uint32_t attemptedPass,
    std::uint32_t acceptedDispatchCount,
    bool safeCompletionEstablished) noexcept
{
    return {
        ClassifyEx2CudaD1AsyncFailure(true, safeCompletionEstablished),
        attemptedPass,
        acceptedDispatchCount};
}

[[nodiscard]] constexpr Ex2CudaD1CompletionDisposition
ClassifyEx2CudaD1CompletionDisposition(
    bool completionUncertain,
    bool asynchronousHostReferencePossible) noexcept
{
    return {
        completionUncertain
            ? Ex2CudaD1ResourceDisposition::PreserveForProcessTeardown
            : Ex2CudaD1ResourceDisposition::DestroyNormally,
        completionUncertain && asynchronousHostReferencePossible
            ? Ex2CudaD1HostStorageDisposition::PreserveForProcessTeardown
            : Ex2CudaD1HostStorageDisposition::DestroyNormally,
        !completionUncertain};
}

[[nodiscard]] constexpr bool IsEx2CudaD1OperationStateReusable(
    Ex2CudaD1OperationState state) noexcept
{
    return state != Ex2CudaD1OperationState::Submitted &&
        state != Ex2CudaD1OperationState::Failed &&
        state != Ex2CudaD1OperationState::CompletionUncertain;
}

} // namespace detail

class Ex2CudaD1Operation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    Ex2CudaD1Operation(
        int deviceOrdinal,
        const ex2::IterativeConfiguration& configuration);
    ~Ex2CudaD1Operation() noexcept;

    Ex2CudaD1Operation(const Ex2CudaD1Operation&) = delete;
    Ex2CudaD1Operation& operator=(const Ex2CudaD1Operation&) = delete;
    Ex2CudaD1Operation(Ex2CudaD1Operation&&) = delete;
    Ex2CudaD1Operation& operator=(Ex2CudaD1Operation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t IterationCount() const noexcept;
    [[nodiscard]] ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept;

    void UploadInitialState(std::span<const std::uint32_t> initialState);
    void SubmitSequence();
    void WaitForCompletion();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveFinalState();

    [[nodiscard]] ex2::IterativeFinalBuffer CompletedFinalBuffer() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeDispatchCount() const;
    [[nodiscard]] bool LastCompletionExecutedSequence() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Ex2CudaD1DeviceTimedOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    Ex2CudaD1DeviceTimedOperation(
        int deviceOrdinal,
        const ex2::IterativeConfiguration& configuration);
    ~Ex2CudaD1DeviceTimedOperation() noexcept;

    Ex2CudaD1DeviceTimedOperation(const Ex2CudaD1DeviceTimedOperation&) = delete;
    Ex2CudaD1DeviceTimedOperation& operator=(const Ex2CudaD1DeviceTimedOperation&) = delete;
    Ex2CudaD1DeviceTimedOperation(Ex2CudaD1DeviceTimedOperation&&) = delete;
    Ex2CudaD1DeviceTimedOperation& operator=(Ex2CudaD1DeviceTimedOperation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t IterationCount() const noexcept;
    [[nodiscard]] ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept;

    void UploadInitialState(std::span<const std::uint32_t> initialState);
    void SubmitSequence();
    void WaitForCompletion();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveFinalState();

    [[nodiscard]] ex2::IterativeFinalBuffer CompletedFinalBuffer() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeDispatchCount() const;
    [[nodiscard]] bool LastCompletionExecutedSequence() const;
    [[nodiscard]] Ex2CudaD1NativeTimingStatus NativeTimingStatus() const;
    [[nodiscard]] Ex2CudaD1NativeTimingMetadata NativeTimingMetadata() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> NativeDeviceIntervalNanoseconds() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
