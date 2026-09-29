#pragma once

#include "ex2/Ex2Configuration.hpp"

#include <vulkan/vulkan.h>

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

namespace computelab::vulkan
{

inline constexpr std::uint64_t Ex2VulkanEDurationEnvelopeNanoseconds =
    30'000'000'000ULL;

enum class Ex2VulkanENativePhase
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
    HostSourcePreparation,
    NoncoherentFlush,
    DeviceSourcePreparation,
    PreparationCompletion,
    CommandCreation,
    CommandRecording,
    QueryReset,
    Submission,
    CompletionWait,
    ValidationReadback,
    HostReadVisibility,
    NoncoherentInvalidate,
    NativeTimingRetrieval,
    NativeTimingConversion,
};

[[nodiscard]] std::string_view ToString(Ex2VulkanENativePhase phase) noexcept;

class Ex2VulkanENativeError final : public std::runtime_error
{
public:
    Ex2VulkanENativeError(
        Ex2VulkanENativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanENativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanENativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

enum class Ex2VulkanENativeTimingStatus
{
    NotApplicable,
    Valid,
    Unavailable,
    QueryUnavailable,
    RetrievalFailed,
    ConversionInvalid,
};

struct Ex2VulkanENativeTimingMetadata
{
    std::string_view method{"vkCmdWriteTimestamp2/vkGetQueryPoolResults"};
    long double timestampPeriodNanoseconds{};
    std::uint32_t timestampValidBits{};
    VkPipelineStageFlags2 startStage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
    VkPipelineStageFlags2 stopStage{VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
    std::uint64_t durationEnvelopeNanoseconds{
        Ex2VulkanEDurationEnvelopeNanoseconds};
};

namespace detail
{

void ValidateEx2VulkanEConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanEByteCount(
    std::uint64_t byteCount,
    VkDeviceSize maximumAddressableByteCount,
    std::size_t maximumVectorElementCount);
void ValidateEx2VulkanEAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount);
void ValidateEx2VulkanEHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize);
[[nodiscard]] std::uint32_t SelectEx2VulkanEQueue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanEMemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

struct Ex2VulkanEMappedRange
{
    VkDeviceSize offset{};
    VkDeviceSize size{};
};

[[nodiscard]] constexpr bool RequiresEx2VulkanEHostCacheMaintenance(
    VkMemoryPropertyFlags memoryFlags) noexcept
{
    return (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0U;
}

[[nodiscard]] Ex2VulkanEMappedRange AlignEx2VulkanENoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

void ValidateEx2VulkanETimestampEnvelope(
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds);

struct Ex2VulkanETimestampQuery
{
    std::uint64_t ticks{};
    std::uint64_t available{};
};

[[nodiscard]] std::uint64_t DecodeEx2VulkanETimestampQueries(
    VkResult result,
    const std::array<Ex2VulkanETimestampQuery, 2U>& queries,
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds);

enum class Ex2VulkanEOperationState
{
    Empty,
    Prepared,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

enum class Ex2VulkanESubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

enum class Ex2VulkanEResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] constexpr Ex2VulkanESubmissionDisposition
ClassifyEx2VulkanESubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanESubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanESubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanESubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanEOperationState
ClassifyEx2VulkanEWaitResult(VkResult result) noexcept
{
    return result == VK_SUCCESS
        ? Ex2VulkanEOperationState::Complete
        : Ex2VulkanEOperationState::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanEResourceDisposition
ClassifyEx2VulkanEResourceDisposition(
    bool submittedOrCompletionUncertain,
    bool auxiliaryTransferPending) noexcept
{
    return submittedOrCompletionUncertain || auxiliaryTransferPending
        ? Ex2VulkanEResourceDisposition::PreserveForProcessTeardown
        : Ex2VulkanEResourceDisposition::DestroyNormally;
}

[[nodiscard]] constexpr bool IsEx2VulkanEOperationStateReusable(
    Ex2VulkanEOperationState state) noexcept
{
    return state != Ex2VulkanEOperationState::Submitted &&
        state != Ex2VulkanEOperationState::Failed &&
        state != Ex2VulkanEOperationState::CompletionUncertain;
}

} // namespace detail

struct Ex2VulkanEDiagnostics
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
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags deviceMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
    std::uint32_t uploadMemoryTypeIndex{};
    std::uint32_t deviceMemoryTypeIndex{};
    std::uint32_t readbackMemoryTypeIndex{};
    VkDeviceSize uploadAllocationBytes{};
    VkDeviceSize deviceAllocationBytes{};
    VkDeviceSize readbackAllocationBytes{};
    std::uint32_t preparedCommandRecordCount{};
    std::uint32_t preparedNativeCopyCount{};
};

class Ex2VulkanEOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    Ex2VulkanEOperation(
        const ex2::TransferConfiguration& configuration,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanEOperation() noexcept;

    Ex2VulkanEOperation(const Ex2VulkanEOperation&) = delete;
    Ex2VulkanEOperation& operator=(const Ex2VulkanEOperation&) = delete;
    Ex2VulkanEOperation(Ex2VulkanEOperation&&) = delete;
    Ex2VulkanEOperation& operator=(Ex2VulkanEOperation&&) = delete;

    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanEDiagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::uint64_t ByteCount() const noexcept;
    [[nodiscard]] ex2::TransferDirection Direction() const noexcept;
    [[nodiscard]] std::uint32_t ExpectedNativeCopyCount() const noexcept;

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource);
    void SubmitTransfer();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = Ex2VulkanEDurationEnvelopeNanoseconds);
    [[nodiscard]] std::vector<std::uint8_t> RetrieveDestinationForValidation();

    [[nodiscard]] bool LastCompletionExecutedCopy() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeCopyCount() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

class Ex2VulkanEDeviceTimedOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    Ex2VulkanEDeviceTimedOperation(
        const ex2::TransferConfiguration& configuration,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanEDeviceTimedOperation() noexcept;

    Ex2VulkanEDeviceTimedOperation(const Ex2VulkanEDeviceTimedOperation&) = delete;
    Ex2VulkanEDeviceTimedOperation& operator=(const Ex2VulkanEDeviceTimedOperation&) = delete;
    Ex2VulkanEDeviceTimedOperation(Ex2VulkanEDeviceTimedOperation&&) = delete;
    Ex2VulkanEDeviceTimedOperation& operator=(Ex2VulkanEDeviceTimedOperation&&) = delete;

    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>& SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanEDiagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::uint64_t ByteCount() const noexcept;
    [[nodiscard]] ex2::TransferDirection Direction() const noexcept;
    [[nodiscard]] std::uint32_t ExpectedNativeCopyCount() const noexcept;

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource);
    void SubmitTransfer();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = Ex2VulkanEDurationEnvelopeNanoseconds);
    [[nodiscard]] std::vector<std::uint8_t> RetrieveDestinationForValidation();

    [[nodiscard]] bool LastCompletionExecutedCopy() const;
    [[nodiscard]] std::uint32_t LastCompletionNativeCopyCount() const;
    void RetrieveNativeTiming();
    [[nodiscard]] Ex2VulkanENativeTimingStatus NativeTimingStatus() const;
    [[nodiscard]] Ex2VulkanENativeTimingMetadata NativeTimingMetadata() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> NativeDeviceIntervalNanoseconds() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
