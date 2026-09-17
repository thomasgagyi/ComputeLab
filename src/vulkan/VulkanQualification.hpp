#pragma once

#include "ex2/Ex2HostTiming.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::vulkan
{

namespace detail
{

enum class VulkanResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] constexpr VulkanResourceDisposition
ClassifyVulkanResourceDisposition(
    bool computePending,
    bool transferPending) noexcept
{
    return computePending || transferPending
        ? VulkanResourceDisposition::PreserveForProcessTeardown
        : VulkanResourceDisposition::DestroyNormally;
}

} // namespace detail

inline constexpr std::uint32_t QualificationLocalSizeX = 256U;

[[nodiscard]] std::uint32_t SelectComputeQualificationQueue(
    std::span<const VkQueueFamilyProperties> families);

enum class VulkanQualificationStatus
{
    Ok,
    SubmitFailed,
    WaitFailed,
    Timeout,
    ReadbackFailed,
    ValidationFailed,
    TimingInvalid,
};

enum class VulkanQualificationFailurePhase
{
    None,
    Submission,
    CompletionWait,
    Readback,
    Validation,
    HostTiming,
};

struct VulkanQualificationExecution
{
    VulkanQualificationStatus status{VulkanQualificationStatus::SubmitFailed};
    VulkanQualificationFailurePhase failurePhase{VulkanQualificationFailurePhase::None};
    ex2::HostTimingIntervals hostTiming;
    std::optional<VkResult> nativeResult;
    std::string errorMessage;
    bool validationPassed{};
    std::vector<std::uint32_t> output;
};

struct VulkanQualificationDiagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    VkMemoryPropertyFlags inputMemoryFlags{};
    VkMemoryPropertyFlags outputMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
};

// A separate, timestamp-free diagnostic operation for EX-2 Gate-0
// qualification. It reuses the validated EX-1 shader without modifying the
// timestamp-bearing EX-1 command buffer or TransformDispatch.
// Uncertain fence completion retains native resources for later child-process
// termination rather than blocking or destroying potentially referenced state.
class VulkanQualificationOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "H";
    static constexpr bool EnqueuesDeviceTimestamps = false;

    VulkanQualificationOperation(
        std::size_t elementCount,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~VulkanQualificationOperation() noexcept;

    VulkanQualificationOperation(const VulkanQualificationOperation&) = delete;
    VulkanQualificationOperation& operator=(const VulkanQualificationOperation&) = delete;
    VulkanQualificationOperation(VulkanQualificationOperation&&) = delete;
    VulkanQualificationOperation& operator=(VulkanQualificationOperation&&) = delete;

    [[nodiscard]] std::size_t ElementCount() const noexcept;
    [[nodiscard]] const VulkanQualificationDiagnostics& Diagnostics() const noexcept;

    // Upload, its fence completion, and CPU-oracle construction are untimed.
    void Upload(std::span<const std::uint32_t> input);

    // Resets only the operation's proven-complete fence, outside measurement.
    void PrepareHostOnly();

    // Submits the timestamp-free command buffer with vkQueueSubmit2, waits on
    // its specific fence, then performs readback/visibility/validation after t2.
    // The measured Vulkan submission interval is exactly the native queue-submit
    // call: t0 is captured immediately before vkQueueSubmit2 and t1 immediately
    // after that call returns.
    [[nodiscard]] VulkanQualificationExecution ExecuteHostOnly(
        std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};

} // namespace computelab::vulkan
