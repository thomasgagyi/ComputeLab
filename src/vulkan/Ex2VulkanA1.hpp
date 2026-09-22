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

class Ex2VulkanA2Operation;

inline constexpr std::uint32_t Ex2VulkanA1LocalSizeX = 256U;

enum class Ex2VulkanA1NativePhase
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
    Ex2VulkanA1NativePhase phase) noexcept;

class Ex2VulkanA1NativeError final : public std::runtime_error
{
public:
    Ex2VulkanA1NativeError(
        Ex2VulkanA1NativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanA1NativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanA1NativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

namespace detail
{

struct Ex2VulkanA1DispatchShape
{
    std::uint32_t groupCountX{};
    std::uint32_t localSizeX{Ex2VulkanA1LocalSizeX};
};

[[nodiscard]] Ex2VulkanA1DispatchShape ValidateEx2VulkanA1DispatchShape(
    const ex2::LinearConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanA1BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount);
[[nodiscard]] std::uint32_t SelectEx2VulkanA1Queue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanA1MemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

struct Ex2VulkanA1MappedRange
{
    VkDeviceSize offset{};
    VkDeviceSize size{};
};

[[nodiscard]] Ex2VulkanA1MappedRange AlignEx2VulkanA1NoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

enum class Ex2VulkanA1ResourceDisposition
{
    DestroyNormally,
    DrainComputeThenDestroy,
    PreserveForProcessTeardown,
};

enum class Ex2VulkanA1SubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

// vkQueueSubmit2 guarantees that these two allocation failures leave the
// referenced resources and synchronization primitives unaffected. Device loss
// and any result outside the documented successful/allocation-failure set are
// treated conservatively because completion cannot be established here.
[[nodiscard]] constexpr Ex2VulkanA1SubmissionDisposition
ClassifyEx2VulkanA1SubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanA1SubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanA1SubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanA1SubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanA1ResourceDisposition
ClassifyEx2VulkanA1ResourceDisposition(
    bool completionUncertain,
    bool computePending,
    bool transferPending) noexcept
{
    if (completionUncertain || transferPending)
        return Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown;
    if (computePending)
        return Ex2VulkanA1ResourceDisposition::DrainComputeThenDestroy;
    return Ex2VulkanA1ResourceDisposition::DestroyNormally;
}

} // namespace detail

struct Ex2VulkanA1Diagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid{};
    std::uint32_t physicalDeviceIndex{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    VkMemoryPropertyFlags inputMemoryFlags{};
    VkMemoryPropertyFlags outputMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
};

// Backend-native EX-2 A1 operation. Upload and readbacks use distinct untimed
// transfer submissions. Prepare records the next A1 command buffer. SubmitA1
// only submits that prepared work, and WaitForCompletion waits on its fence.
// A timeout or device-loss result makes completion uncertain and permanently
// disables reuse; native resources are then retained for process teardown.
class Ex2VulkanA1Operation final
{
public:
    Ex2VulkanA1Operation(
        const ex2::LinearConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanA1Operation() noexcept;

    Ex2VulkanA1Operation(const Ex2VulkanA1Operation&) = delete;
    Ex2VulkanA1Operation& operator=(const Ex2VulkanA1Operation&) = delete;
    Ex2VulkanA1Operation(Ex2VulkanA1Operation&&) = delete;
    Ex2VulkanA1Operation& operator=(Ex2VulkanA1Operation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanA1Diagnostics& Diagnostics() const noexcept;

    void Upload(std::span<const std::uint32_t> input);
    void Prepare();
    void SubmitA1();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);

    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    // Correctness diagnostic only. It is a separate post-completion transfer
    // and is never included in ordinary output retrieval.
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] bool LastCompletionExecutedShader() const;

private:
    friend class Ex2VulkanA2Operation;

    Ex2VulkanA1Operation(
        const ex2::LinearConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex,
        ex2::LinearVariant requiredVariant);

    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
