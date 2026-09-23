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

inline constexpr std::uint32_t Ex2CudaCThreadsPerBlock = 256U;

enum class Ex2CudaCNativePhase
{
    DeviceSelection,
    RuntimeInitialization,
    DeviceProperties,
    ResourcePreflight,
    StreamCreation,
    TargetAllocation,
    CounterAllocation,
    TargetUpload,
    TargetUploadCompletion,
    CounterReset,
    ResetCompletion,
    Submission,
    CompletionWait,
    CounterReadback,
    TargetDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(Ex2CudaCNativePhase phase) noexcept;

class Ex2CudaCNativeError final : public std::runtime_error
{
public:
    Ex2CudaCNativeError(
        Ex2CudaCNativePhase phase,
        int nativeErrorCode,
        std::string nativeErrorName,
        std::string message);

    [[nodiscard]] Ex2CudaCNativePhase Phase() const noexcept;
    [[nodiscard]] int NativeErrorCode() const noexcept;
    [[nodiscard]] const std::string& NativeErrorName() const noexcept;

private:
    Ex2CudaCNativePhase phase_;
    int nativeErrorCode_;
    std::string nativeErrorName_;
};

namespace detail
{

struct Ex2CudaCLaunchShape
{
    std::uint32_t blockCount{};
    std::uint32_t threadsPerBlock{Ex2CudaCThreadsPerBlock};
};

void ValidateEx2CudaCConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2CudaCLaunchShape ValidateEx2CudaCLaunchShape(
    const ex2::ContentionConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX);
[[nodiscard]] std::size_t CalculateEx2CudaCBufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount);
void ValidateEx2CudaCHostVectorCapacity(
    std::uint64_t elementCount,
    std::size_t maximumVectorElementCount);
void ValidateEx2CudaCMemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes);

enum class Ex2CudaCResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

enum class Ex2CudaCHostStorageDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

struct Ex2CudaCCompletionDisposition
{
    Ex2CudaCResourceDisposition deviceResources;
    Ex2CudaCHostStorageDisposition hostStorage;
    bool operationReusable{};
};

enum class Ex2CudaCOperationState
{
    Empty,
    TargetsReady,
    ResetReady,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

[[nodiscard]] constexpr Ex2CudaCResourceDisposition
ClassifyEx2CudaCResourceDisposition(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? Ex2CudaCResourceDisposition::DestroyNormally
        : Ex2CudaCResourceDisposition::PreserveForProcessTeardown;
}

[[nodiscard]] constexpr Ex2CudaCCompletionDisposition
ClassifyEx2CudaCCompletionDisposition(
    bool safeCompletionEstablished,
    bool asynchronousHostReferencePossible) noexcept
{
    return {
        ClassifyEx2CudaCResourceDisposition(safeCompletionEstablished),
        !safeCompletionEstablished && asynchronousHostReferencePossible
            ? Ex2CudaCHostStorageDisposition::PreserveForProcessTeardown
            : Ex2CudaCHostStorageDisposition::DestroyNormally,
        safeCompletionEstablished};
}

[[nodiscard]] constexpr Ex2CudaCOperationState
ClassifyEx2CudaCAsyncFailure(bool safeCompletionEstablished) noexcept
{
    return safeCompletionEstablished
        ? Ex2CudaCOperationState::Failed
        : Ex2CudaCOperationState::CompletionUncertain;
}

[[nodiscard]] constexpr bool IsEx2CudaCOperationStateReusable(
    Ex2CudaCOperationState state) noexcept
{
    return state != Ex2CudaCOperationState::Submitted
        && state != Ex2CudaCOperationState::Failed
        && state != Ex2CudaCOperationState::CompletionUncertain;
}

} // namespace detail

// Backend-native EX-2 C operation. Configuration and selected device are
// immutable. Target upload, full reset, atomic submission, completion and
// readback remain distinct lifecycle phases.
class Ex2CudaCOperation final
{
public:
    Ex2CudaCOperation(
        int deviceOrdinal,
        const ex2::ContentionConfiguration& configuration);
    ~Ex2CudaCOperation() noexcept;

    Ex2CudaCOperation(const Ex2CudaCOperation&) = delete;
    Ex2CudaCOperation& operator=(const Ex2CudaCOperation&) = delete;
    Ex2CudaCOperation(Ex2CudaCOperation&&) = delete;
    Ex2CudaCOperation& operator=(Ex2CudaCOperation&&) = delete;

    [[nodiscard]] int SelectedDeviceOrdinal() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t ActiveCounterCount() const noexcept;
    [[nodiscard]] std::uint64_t AllocatedCounterCount() const noexcept;

    void UploadTargets(std::span<const std::uint32_t> targets);
    void PrepareReset();
    void SubmitAtomic();
    void WaitForCompletion();

    [[nodiscard]] std::vector<std::uint32_t> RetrieveCounters();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceTargets();
    [[nodiscard]] bool LastCompletionExecutedKernel() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace computelab::cuda
