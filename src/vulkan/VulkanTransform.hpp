#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace computelab::vulkan
{

inline constexpr std::uint32_t Ex1LocalSizeX = 256;

// Pure setup validation, shared with deterministic tests. Zero work uses one
// guarded workgroup and nonzero dummy buffer storage, preserving empty output.
[[nodiscard]] std::uint32_t ValidateDispatch(
    std::size_t elementCount, const VkPhysicalDeviceLimits& limits);
[[nodiscard]] std::uint32_t SelectComputeTimestampQueue(
    std::span<const VkQueueFamilyProperties> families);

struct DeviceDiagnostics
{
    VkPhysicalDeviceProperties properties{};
    std::uint32_t queueFamilyIndex{};
    VkQueueFamilyProperties queueFamily{};
    bool synchronization2Enabled{};
    bool hostQueryResetEnabled{};
    VkMemoryPropertyFlags inputMemoryFlags{};
    VkMemoryPropertyFlags outputMemoryFlags{};
    VkMemoryPropertyFlags uploadMemoryFlags{};
    VkMemoryPropertyFlags readbackMemoryFlags{};
};

// One fixed-size EX-1 operation, one Vulkan queue. Not thread safe. Owns its
// instance/device and all dependent resources; no CUDA or cross-API identity.
// physicalDeviceIndex is an index in Vulkan's own enumeration (default first).
// Every invocation follows Upload -> Prepare -> Submit -> Wait -> timing/output.
// Upload is required again for reuse, even when the input is identical.
class TransformDispatch final
{
public:
    TransformDispatch(std::size_t elementCount, const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex = 0);
    ~TransformDispatch() noexcept;
    TransformDispatch(const TransformDispatch&) = delete;
    TransformDispatch& operator=(const TransformDispatch&) = delete;
    TransformDispatch(TransformDispatch&&) = delete;
    TransformDispatch& operator=(TransformDispatch&&) = delete;

    void Upload(std::span<const std::uint32_t> input);
    // Host query reset and fence reset only; outside both measured boundaries.
    void PrepareMeasurement();
    // Asynchronous vkQueueSubmit2 only, plus lifecycle/error bookkeeping.
    void SubmitTransform();
    // Specific fence wait; timeout is an error and leaves work pending. The
    // caller may explicitly wait again, but cannot reset/reuse pending resources.
    void WaitForCompletion(std::uint64_t timeoutNanoseconds = 30'000'000'000ULL);
    // No hidden synchronization. A broad device-timeline dispatch bracket,
    // not exact shader-core execution time. Accept only after A3 correctness.
    [[nodiscard]] std::uint64_t DeviceElapsedNanoseconds() const;
    [[nodiscard]] std::vector<std::uint32_t> RetrieveOutput();
    [[nodiscard]] const DeviceDiagnostics& Diagnostics() const noexcept;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
};
} // namespace computelab::vulkan
