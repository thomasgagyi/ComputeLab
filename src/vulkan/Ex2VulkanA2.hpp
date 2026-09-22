#pragma once

#include "vulkan/Ex2VulkanA1.hpp"

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

inline constexpr std::uint32_t Ex2VulkanA2LocalSizeX = 256U;

enum class Ex2VulkanA2NativePhase
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
    PipelineCreation,
    CommandCreation,
    Upload,
    Preparation,
    Submission,
    CompletionWait,
    OutputReadback,
    InputDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(
    Ex2VulkanA2NativePhase phase) noexcept;

class Ex2VulkanA2NativeError final : public std::runtime_error
{
public:
    Ex2VulkanA2NativeError(
        Ex2VulkanA2NativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanA2NativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanA2NativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

namespace detail
{

struct Ex2VulkanA2DispatchShape
{
    std::uint32_t groupCountX{};
    std::uint32_t localSizeX{Ex2VulkanA2LocalSizeX};
};

[[nodiscard]] Ex2VulkanA2DispatchShape ValidateEx2VulkanA2DispatchShape(
    const ex2::LinearConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanA2BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount);
[[nodiscard]] std::uint32_t SelectEx2VulkanA2Queue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanA2MemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

using Ex2VulkanA2MappedRange = Ex2VulkanA1MappedRange;

[[nodiscard]] Ex2VulkanA2MappedRange AlignEx2VulkanA2NoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

enum class Ex2VulkanA2ResourceDisposition
{
    DestroyNormally,
    DrainComputeThenDestroy,
    PreserveForProcessTeardown,
};

enum class Ex2VulkanA2SubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

[[nodiscard]] constexpr Ex2VulkanA2SubmissionDisposition
ClassifyEx2VulkanA2SubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanA2SubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanA2SubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanA2SubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanA2ResourceDisposition
ClassifyEx2VulkanA2ResourceDisposition(
    bool completionUncertain,
    bool computePending,
    bool transferPending) noexcept
{
    if (completionUncertain || transferPending)
        return Ex2VulkanA2ResourceDisposition::PreserveForProcessTeardown;
    if (computePending)
        return Ex2VulkanA2ResourceDisposition::DrainComputeThenDestroy;
    return Ex2VulkanA2ResourceDisposition::DestroyNormally;
}

} // namespace detail

using Ex2VulkanA2Diagnostics = Ex2VulkanA1Diagnostics;

// A2-only public operation. The Vulkan-local A1 lifecycle implementation is
// reused with a separately compiled A2 SPIR-V module and pipeline.
class Ex2VulkanA2Operation final
{
public:
    Ex2VulkanA2Operation(
        const ex2::LinearConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanA2Operation() noexcept;

    Ex2VulkanA2Operation(const Ex2VulkanA2Operation&) = delete;
    Ex2VulkanA2Operation& operator=(const Ex2VulkanA2Operation&) = delete;
    Ex2VulkanA2Operation(Ex2VulkanA2Operation&&) = delete;
    Ex2VulkanA2Operation& operator=(Ex2VulkanA2Operation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanA2Diagnostics& Diagnostics() const noexcept;

    void Upload(std::span<const std::uint32_t> input);
    void Prepare();
    void SubmitA2();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);
    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] bool LastCompletionExecutedShader() const;

private:
    std::unique_ptr<Ex2VulkanA1Operation> operation_;
};

} // namespace computelab::vulkan
