#pragma once

#include "ex2/Ex2Configuration.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::vulkan
{

inline constexpr std::uint32_t Ex2VulkanD1LocalSizeX = 256U;
inline constexpr std::uint64_t Ex2VulkanD1DurationEnvelopeNanoseconds =
    30'000'000'000ULL;

enum class Ex2VulkanD1NativePhase
{
    InstanceCreation,
    DeviceEnumeration,
    DeviceSelection,
    DeviceProperties,
    QueueSelection,
    LogicalDeviceCreation,
    ResourcePreflight,
    BufferCreation,
    MemoryAllocation,
    MemoryBinding,
    MemoryMapping,
    DescriptorCreation,
    PipelineConstruction,
    CommandCreation,
    InitialUpload,
    CommandBufferReset,
    CommandRecording,
    Submission,
    CompletionWait,
    FinalReadback,
    QueryReset,
    NativeTimingRetrieval,
    NativeTimingConversion,
};

[[nodiscard]] std::string_view ToString(Ex2VulkanD1NativePhase phase) noexcept;

class Ex2VulkanD1NativeError final : public std::runtime_error
{
public:
    Ex2VulkanD1NativeError(
        Ex2VulkanD1NativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanD1NativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanD1NativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

enum class Ex2VulkanD1NativeTimingStatus
{
    NotApplicable,
    Valid,
    Unavailable,
    QueryUnavailable,
    RetrievalFailed,
    ConversionInvalid,
};

struct Ex2VulkanD1NativeTimingMetadata
{
    std::string_view method{"vkCmdWriteTimestamp2/vkGetQueryPoolResults"};
    long double timestampPeriodNanoseconds{};
    std::uint32_t timestampValidBits{};
    VkPipelineStageFlags2 startStage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    VkPipelineStageFlags2 stopStage{VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
    std::uint64_t durationEnvelopeNanoseconds{
        Ex2VulkanD1DurationEnvelopeNanoseconds};
};

namespace detail
{

struct Ex2VulkanD1DispatchShape
{
    std::uint32_t groupCountX{};
    std::uint32_t localSizeX{Ex2VulkanD1LocalSizeX};
};

struct Ex2VulkanD1CommandCounts
{
    std::uint32_t dispatchCount{};
    std::uint32_t interPassBarrierCount{};
};

void ValidateEx2VulkanD1Configuration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2VulkanD1DispatchShape ValidateEx2VulkanD1DispatchShape(
    const ex2::IterativeConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize);
[[nodiscard]] Ex2VulkanD1CommandCounts Ex2VulkanD1ExpectedCommandCounts(
    const ex2::IterativeConfiguration& configuration);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanD1BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount);
void ValidateEx2VulkanD1AllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount);
void ValidateEx2VulkanD1HeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize);
[[nodiscard]] std::uint32_t SelectEx2VulkanD1Queue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanD1MemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

struct Ex2VulkanD1MappedRange
{
    VkDeviceSize offset{};
    VkDeviceSize size{};
};

[[nodiscard]] Ex2VulkanD1MappedRange AlignEx2VulkanD1NoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

void ValidateEx2VulkanD1TimestampEnvelope(
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds);

struct Ex2VulkanD1TimestampQuery
{
    std::uint64_t ticks{};
    std::uint64_t available{};
};

[[nodiscard]] std::uint64_t DecodeEx2VulkanD1TimestampQueries(
    VkResult result,
    const std::array<Ex2VulkanD1TimestampQuery, 2U>& queries,
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds);

enum class Ex2VulkanD1OperationState
{
    Empty,
    InitialStateReady,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

enum class Ex2VulkanD1SubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

enum class Ex2VulkanD1ResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] constexpr Ex2VulkanD1SubmissionDisposition
ClassifyEx2VulkanD1SubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanD1SubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanD1SubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanD1SubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanD1OperationState
ClassifyEx2VulkanD1WaitResult(VkResult result) noexcept
{
    return result == VK_SUCCESS
        ? Ex2VulkanD1OperationState::Complete
        : Ex2VulkanD1OperationState::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanD1ResourceDisposition
ClassifyEx2VulkanD1ResourceDisposition(
    bool submittedOrCompletionUncertain,
    bool transferPending) noexcept
{
    return submittedOrCompletionUncertain || transferPending
        ? Ex2VulkanD1ResourceDisposition::PreserveForProcessTeardown
        : Ex2VulkanD1ResourceDisposition::DestroyNormally;
}

[[nodiscard]] constexpr bool IsEx2VulkanD1OperationStateReusable(
    Ex2VulkanD1OperationState state) noexcept
{
    return state != Ex2VulkanD1OperationState::Submitted &&
        state != Ex2VulkanD1OperationState::Failed &&
        state != Ex2VulkanD1OperationState::CompletionUncertain;
}

} // namespace detail

struct Ex2VulkanD1Diagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid{};
    std::uint32_t physicalDeviceIndex{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    bool hostQueryResetEnabled{};
    bool timestampQueryPoolCreated{};
    bool validationEnabled{};
    bool synchronizationValidationEnabled{};
    std::uint32_t validationErrorCount{};
    VkMemoryPropertyFlags stateAMemoryFlags{};
    VkMemoryPropertyFlags stateBMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
    std::uint32_t stateAMemoryTypeIndex{};
    std::uint32_t stateBMemoryTypeIndex{};
    std::uint32_t uploadMemoryTypeIndex{};
    std::uint32_t readbackMemoryTypeIndex{};
    VkDeviceSize stateAAllocationBytes{};
    VkDeviceSize stateBAllocationBytes{};
    VkDeviceSize uploadAllocationBytes{};
    VkDeviceSize readbackAllocationBytes{};
};

class Ex2VulkanD1Operation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    Ex2VulkanD1Operation(
        const ex2::IterativeConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanD1Operation() noexcept;

    Ex2VulkanD1Operation(const Ex2VulkanD1Operation&) = delete;
    Ex2VulkanD1Operation& operator=(const Ex2VulkanD1Operation&) = delete;
    Ex2VulkanD1Operation(Ex2VulkanD1Operation&&) = delete;
    Ex2VulkanD1Operation& operator=(Ex2VulkanD1Operation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t IterationCount() const noexcept;
    [[nodiscard]] ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanD1Diagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::string_view LoadedShaderName() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> LoadedSpirv() const noexcept;

    void UploadInitialState(std::span<const std::uint32_t> initialState);
    void SubmitSequence();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = Ex2VulkanD1DurationEnvelopeNanoseconds);
    [[nodiscard]] std::vector<std::uint32_t> RetrieveFinalState();

    [[nodiscard]] ex2::IterativeFinalBuffer CompletedFinalBuffer() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeDispatchCount() const;
    [[nodiscard]] std::uint32_t LastCompletionInterPassBarrierCount() const;
    [[nodiscard]] bool LastCompletionExecutedSequence() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

class Ex2VulkanD1DeviceTimedOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    Ex2VulkanD1DeviceTimedOperation(
        const ex2::IterativeConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanD1DeviceTimedOperation() noexcept;

    Ex2VulkanD1DeviceTimedOperation(const Ex2VulkanD1DeviceTimedOperation&) = delete;
    Ex2VulkanD1DeviceTimedOperation& operator=(const Ex2VulkanD1DeviceTimedOperation&) = delete;
    Ex2VulkanD1DeviceTimedOperation(Ex2VulkanD1DeviceTimedOperation&&) = delete;
    Ex2VulkanD1DeviceTimedOperation& operator=(Ex2VulkanD1DeviceTimedOperation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t IterationCount() const noexcept;
    [[nodiscard]] ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanD1Diagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::string_view LoadedShaderName() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> LoadedSpirv() const noexcept;

    void UploadInitialState(std::span<const std::uint32_t> initialState);
    void SubmitSequence();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = Ex2VulkanD1DurationEnvelopeNanoseconds);
    [[nodiscard]] std::vector<std::uint32_t> RetrieveFinalState();

    [[nodiscard]] ex2::IterativeFinalBuffer CompletedFinalBuffer() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeDispatchCount() const;
    [[nodiscard]] std::uint32_t LastCompletionInterPassBarrierCount() const;
    [[nodiscard]] bool LastCompletionExecutedSequence() const;
    [[nodiscard]] Ex2VulkanD1NativeTimingStatus NativeTimingStatus() const;
    [[nodiscard]] Ex2VulkanD1NativeTimingMetadata NativeTimingMetadata() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> NativeDeviceIntervalNanoseconds() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
