#pragma once

#include "ex2/Ex2Configuration.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::vulkan
{

inline constexpr std::uint32_t Ex2VulkanCLocalSizeX = 256U;

enum class Ex2VulkanCNativePhase
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
    TargetUpload,
    CounterReset,
    ResetCompletion,
    Submission,
    CompletionWait,
    CounterReadback,
    TargetDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(Ex2VulkanCNativePhase phase) noexcept;

class Ex2VulkanCNativeError final : public std::runtime_error
{
public:
    Ex2VulkanCNativeError(
        Ex2VulkanCNativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanCNativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanCNativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

namespace detail
{

struct Ex2VulkanCDispatchShape
{
    std::uint32_t groupCountX{};
    std::uint32_t localSizeX{Ex2VulkanCLocalSizeX};
};

void ValidateEx2VulkanCConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2VulkanCDispatchShape ValidateEx2VulkanCDispatchShape(
    const ex2::ContentionConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanCBufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount);
void ValidateEx2VulkanCAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount = 4U);
void ValidateEx2VulkanCHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize);
[[nodiscard]] std::uint32_t SelectEx2VulkanCQueue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanCMemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

struct Ex2VulkanCMappedRange
{
    VkDeviceSize offset{};
    VkDeviceSize size{};
};

[[nodiscard]] Ex2VulkanCMappedRange AlignEx2VulkanCNoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

enum class Ex2VulkanCResourceDisposition
{
    DestroyNormally,
    DrainAtomicThenDestroy,
    PreserveForProcessTeardown,
};

enum class Ex2VulkanCSubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

enum class Ex2VulkanCOperationState
{
    Empty,
    TargetsReady,
    ResetReady,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

[[nodiscard]] constexpr Ex2VulkanCSubmissionDisposition
ClassifyEx2VulkanCSubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanCSubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanCSubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanCSubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanCOperationState
ClassifyEx2VulkanCWaitResult(VkResult result) noexcept
{
    return result == VK_SUCCESS
        ? Ex2VulkanCOperationState::Complete
        : Ex2VulkanCOperationState::CompletionUncertain;
}

[[nodiscard]] constexpr bool IsEx2VulkanCOperationStateReusable(
    Ex2VulkanCOperationState state) noexcept
{
    return state != Ex2VulkanCOperationState::Submitted &&
        state != Ex2VulkanCOperationState::Failed &&
        state != Ex2VulkanCOperationState::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanCResourceDisposition
ClassifyEx2VulkanCResourceDisposition(
    bool completionUncertain,
    bool atomicPending,
    bool transferPending) noexcept
{
    if (completionUncertain || transferPending)
        return Ex2VulkanCResourceDisposition::PreserveForProcessTeardown;
    if (atomicPending)
        return Ex2VulkanCResourceDisposition::DrainAtomicThenDestroy;
    return Ex2VulkanCResourceDisposition::DestroyNormally;
}

} // namespace detail

struct Ex2VulkanCDiagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid{};
    std::uint32_t physicalDeviceIndex{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    VkMemoryPropertyFlags targetMemoryFlags{};
    VkMemoryPropertyFlags counterMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
    std::uint32_t targetMemoryTypeIndex{};
    std::uint32_t counterMemoryTypeIndex{};
    std::uint32_t uploadMemoryTypeIndex{};
    std::uint32_t readbackMemoryTypeIndex{};
    VkDeviceSize targetAllocationBytes{};
    VkDeviceSize counterAllocationBytes{};
    VkDeviceSize uploadAllocationBytes{};
    VkDeviceSize readbackAllocationBytes{};
};

class Ex2VulkanCOperation final
{
public:
    Ex2VulkanCOperation(
        const ex2::ContentionConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanCOperation() noexcept;

    Ex2VulkanCOperation(const Ex2VulkanCOperation&) = delete;
    Ex2VulkanCOperation& operator=(const Ex2VulkanCOperation&) = delete;
    Ex2VulkanCOperation(Ex2VulkanCOperation&&) = delete;
    Ex2VulkanCOperation& operator=(Ex2VulkanCOperation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] std::uint64_t ActiveCounterCount() const noexcept;
    [[nodiscard]] std::uint64_t AllocatedCounterCount() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanCDiagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::string_view LoadedShaderName() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> LoadedSpirv() const noexcept;

    void UploadTargets(std::span<const std::uint32_t> targets);
    void PrepareReset();
    void SubmitAtomic();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);

    [[nodiscard]] std::vector<std::uint32_t> RetrieveCounters();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceTargets();
    [[nodiscard]] bool LastCompletionExecutedShader() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
