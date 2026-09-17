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

// Mode N must use the exact family selected by mode H. It is unavailable when
// that paired family cannot write valid compute timestamps.
[[nodiscard]] std::uint32_t ValidatePairedTimestampQualificationQueue(
    std::span<const VkQueueFamilyProperties> families,
    std::uint32_t hostOnlyQueueFamilyIndex);

inline constexpr std::uint64_t QualificationNativeDurationEnvelopeNanoseconds =
    30'000'000'000ULL;
inline constexpr VkPipelineStageFlags2 QualificationTimestampStartStage =
    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
inline constexpr VkPipelineStageFlags2 QualificationTimestampStopStage =
    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

enum class VulkanNativeTimingStatus
{
    NotApplicable,
    Valid,
    QueryUnavailable,
    QueryRetrievalFailed,
    ConversionInvalid,
};

struct VulkanQualificationTimestampQuery
{
    std::uint64_t ticks{};
    std::uint64_t available{};
};

struct VulkanNativeTimingResult
{
    VulkanNativeTimingStatus status{VulkanNativeTimingStatus::QueryUnavailable};
    std::optional<std::uint64_t> intervalNanoseconds;
};

// The duration envelope must be shorter than one timestamp-counter wrap. This
// makes modular subtraction capable of representing zero or one wrap without
// silently accepting an ambiguous multi-wrap duration.
[[nodiscard]] std::uint64_t QualificationTimestampNanoseconds(
    std::uint64_t start,
    std::uint64_t stop,
    std::uint32_t validBits,
    long double timestampPeriod,
    std::uint64_t durationEnvelopeNanoseconds =
        QualificationNativeDurationEnvelopeNanoseconds);

[[nodiscard]] VulkanNativeTimingResult DecodeQualificationTimestampQueries(
    VkResult result,
    const std::array<VulkanQualificationTimestampQuery, 2U>& queries,
    std::uint32_t validBits,
    long double timestampPeriod,
    std::uint64_t durationEnvelopeNanoseconds =
        QualificationNativeDurationEnvelopeNanoseconds) noexcept;

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
    NativeTiming,
};

struct VulkanNativeTimingMetadata
{
    std::string_view method{"vkCmdWriteTimestamp2/vkGetQueryPoolResults"};
    long double timestampPeriodNanoseconds{};
    std::uint32_t timestampValidBits{};
    VkPipelineStageFlags2 startStage{QualificationTimestampStartStage};
    VkPipelineStageFlags2 stopStage{QualificationTimestampStopStage};
    std::uint64_t durationEnvelopeNanoseconds{
        QualificationNativeDurationEnvelopeNanoseconds};
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
    std::string_view instrumentMode{"H"};
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
    VulkanNativeTimingStatus nativeTimingStatus{
        VulkanNativeTimingStatus::NotApplicable};
    std::optional<VulkanNativeTimingMetadata> nativeTimingMetadata;
};

struct VulkanQualificationDiagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::array<std::uint8_t, 16> deviceUuid{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    bool hostQueryResetEnabled{};
    bool timestampCommandsRecorded{};
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
    friend class VulkanDeviceTimedQualificationOperation;

    VulkanQualificationOperation(
        std::size_t elementCount,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex,
        bool deviceTimed);
    void PrepareDeviceTimed();
    [[nodiscard]] VulkanQualificationExecution ExecuteDeviceTimedInternal(
        std::uint64_t timeoutNanoseconds);

    struct Resources;
    std::unique_ptr<Resources> resources_;
};

// Separate public mode-N condition. Its private underlying operation instance
// creates a query pool and records a timestamp-bearing command buffer; a mode-H
// instance creates neither.
class VulkanDeviceTimedQualificationOperation final
{
public:
    static constexpr std::string_view InstrumentMode = "N";
    static constexpr bool EnqueuesDeviceTimestamps = true;

    VulkanDeviceTimedQualificationOperation(
        std::size_t elementCount,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0U);
    ~VulkanDeviceTimedQualificationOperation() noexcept;

    VulkanDeviceTimedQualificationOperation(
        const VulkanDeviceTimedQualificationOperation&) = delete;
    VulkanDeviceTimedQualificationOperation& operator=(
        const VulkanDeviceTimedQualificationOperation&) = delete;
    VulkanDeviceTimedQualificationOperation(
        VulkanDeviceTimedQualificationOperation&&) = delete;
    VulkanDeviceTimedQualificationOperation& operator=(
        VulkanDeviceTimedQualificationOperation&&) = delete;

    [[nodiscard]] std::size_t ElementCount() const noexcept;
    [[nodiscard]] const VulkanQualificationDiagnostics& Diagnostics() const noexcept;
    void Upload(std::span<const std::uint32_t> input);
    void PrepareDeviceTimed();
    [[nodiscard]] VulkanQualificationExecution ExecuteDeviceTimed(
        std::uint64_t timeoutNanoseconds =
            QualificationNativeDurationEnvelopeNanoseconds);

private:
    std::unique_ptr<VulkanQualificationOperation> operation_;
};

} // namespace computelab::vulkan
