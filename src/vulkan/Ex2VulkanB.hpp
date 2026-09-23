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

inline constexpr std::uint32_t Ex2VulkanBLocalSizeX = 256U;
inline constexpr std::uint32_t Ex2VulkanBOutputSentinel = 0xA5A5A5A5U;

enum class Ex2VulkanBNativePhase
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
    InputUpload,
    IndexUpload,
    OutputInitialization,
    Preparation,
    Submission,
    CompletionWait,
    OutputReadback,
    InputDiagnosticReadback,
    IndexDiagnosticReadback,
};

[[nodiscard]] std::string_view ToString(
    Ex2VulkanBNativePhase phase) noexcept;

class Ex2VulkanBNativeError final : public std::runtime_error
{
public:
    Ex2VulkanBNativeError(
        Ex2VulkanBNativePhase phase,
        VkResult nativeResult,
        std::string operation,
        std::string message);

    [[nodiscard]] Ex2VulkanBNativePhase Phase() const noexcept;
    [[nodiscard]] VkResult NativeResult() const noexcept;
    [[nodiscard]] const std::string& Operation() const noexcept;

private:
    Ex2VulkanBNativePhase phase_;
    VkResult nativeResult_;
    std::string operation_;
};

namespace detail
{

struct Ex2VulkanBDispatchShape
{
    std::uint32_t groupCountX{};
    std::uint32_t localSizeX{Ex2VulkanBLocalSizeX};
};

void ValidateEx2VulkanBConfiguration(
    const ex2::WorkloadConfiguration& configuration);
[[nodiscard]] Ex2VulkanBDispatchShape ValidateEx2VulkanBDispatchShape(
    const ex2::IndexedConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize);
[[nodiscard]] VkDeviceSize CalculateEx2VulkanBBufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount);
void ValidateEx2VulkanBAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount = 5U);
void ValidateEx2VulkanBHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize);
[[nodiscard]] std::uint32_t SelectEx2VulkanBQueue(
    std::span<const VkQueueFamilyProperties> families);
[[nodiscard]] std::uint32_t SelectEx2VulkanBMemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties);

struct Ex2VulkanBMappedRange
{
    VkDeviceSize offset{};
    VkDeviceSize size{};
};

[[nodiscard]] Ex2VulkanBMappedRange AlignEx2VulkanBNoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize);

enum class Ex2VulkanBResourceDisposition
{
    DestroyNormally,
    DrainComputeThenDestroy,
    PreserveForProcessTeardown,
};

enum class Ex2VulkanBSubmissionDisposition
{
    Submitted,
    FailedWithoutSubmission,
    CompletionUncertain,
};

[[nodiscard]] constexpr Ex2VulkanBSubmissionDisposition
ClassifyEx2VulkanBSubmissionResult(VkResult result) noexcept
{
    if (result == VK_SUCCESS)
        return Ex2VulkanBSubmissionDisposition::Submitted;
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY ||
        result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    {
        return Ex2VulkanBSubmissionDisposition::FailedWithoutSubmission;
    }
    return Ex2VulkanBSubmissionDisposition::CompletionUncertain;
}

[[nodiscard]] constexpr Ex2VulkanBResourceDisposition
ClassifyEx2VulkanBResourceDisposition(
    bool completionUncertain,
    bool computePending,
    bool transferPending) noexcept
{
    if (completionUncertain || transferPending)
        return Ex2VulkanBResourceDisposition::PreserveForProcessTeardown;
    if (computePending)
        return Ex2VulkanBResourceDisposition::DrainComputeThenDestroy;
    return Ex2VulkanBResourceDisposition::DestroyNormally;
}

} // namespace detail

struct Ex2VulkanBDiagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid{};
    std::uint32_t physicalDeviceIndex{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    VkMemoryPropertyFlags inputMemoryFlags{};
    VkMemoryPropertyFlags indexMemoryFlags{};
    VkMemoryPropertyFlags outputMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
};

// Backend-native EX-2 B operation. The selected variant, element count,
// pattern, seed and physical device are immutable. The operation loads only
// the shader for its declared variant and owns every Vulkan resource it uses.
class Ex2VulkanBOperation final
{
public:
    Ex2VulkanBOperation(
        const ex2::IndexedConfiguration& configuration,
        std::uint64_t seed,
        const std::filesystem::path& b1SpirvPath,
        const std::filesystem::path& b2SpirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~Ex2VulkanBOperation() noexcept;

    Ex2VulkanBOperation(const Ex2VulkanBOperation&) = delete;
    Ex2VulkanBOperation& operator=(const Ex2VulkanBOperation&) = delete;
    Ex2VulkanBOperation(Ex2VulkanBOperation&&) = delete;
    Ex2VulkanBOperation& operator=(Ex2VulkanBOperation&&) = delete;

    [[nodiscard]] std::uint64_t ElementCount() const noexcept;
    [[nodiscard]] ex2::IndexedVariant Variant() const noexcept;
    [[nodiscard]] ex2::IndexPattern Pattern() const noexcept;
    [[nodiscard]] std::uint64_t Seed() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, VK_UUID_SIZE>&
    SelectedDeviceUuid() const noexcept;
    [[nodiscard]] const Ex2VulkanBDiagnostics& Diagnostics() const noexcept;
    [[nodiscard]] std::string_view LoadedShaderName() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> LoadedSpirv() const noexcept;

    void Upload(
        std::span<const std::uint32_t> input,
        std::span<const std::uint32_t> indices);
    void Prepare();
    void Submit();
    void WaitForCompletion(
        std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);

    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceInput();
    [[nodiscard]] std::vector<std::uint32_t> RetrieveDeviceIndices();
    [[nodiscard]] bool LastCompletionExecutedShader() const;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
