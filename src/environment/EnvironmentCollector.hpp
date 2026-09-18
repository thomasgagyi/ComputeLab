#pragma once

#include "results/ResultRecords.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace computelab::environment
{

using DeviceUuid = std::array<std::uint8_t, 16>;

struct EnvironmentRunContext
{
    std::string experimentId;
    std::string runId;
    std::string timestampUtc;
    std::string gitCommit;
    bool gitDirty{};
    std::string machineId;
    bool validationEnabled{};
    bool diagnosticInstrumentation{};
};

struct WindowsHostMetadata
{
    std::string osName;
    std::string osVersion;
    std::string cpuName;
    std::uint64_t systemMemoryBytes{};
};

struct BuildMetadata
{
    std::string compilerName;
    std::string compilerVersion;
    std::string cmakeVersion;
    std::string ninjaVersion;
    std::string configurePreset;
    std::string buildType;
    std::string cudaToolkitVersion;
    std::string vulkanSdkVersion;
};

struct CudaDeviceMetadata
{
    DeviceUuid uuid;
    std::string name;
    std::uint64_t totalGlobalMemoryBytes{};
    int computeCapabilityMajor{};
    int computeCapabilityMinor{};
    std::string runtimeVersion;
    std::uint32_t deviceId{};
};

struct VulkanDeviceMetadata
{
    DeviceUuid uuid;
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
    std::string deviceApiVersion;
    std::string name;
    std::uint64_t deviceLocalMemoryBytes{};
};

[[nodiscard]] std::vector<CudaDeviceMetadata> EnumerateCudaDeviceMetadata();
[[nodiscard]] std::vector<VulkanDeviceMetadata> EnumerateVulkanDeviceMetadata();

[[nodiscard]] BuildMetadata GetConfiguredBuildMetadata();
[[nodiscard]] WindowsHostMetadata CollectWindowsHostMetadata();
[[nodiscard]] std::string CollectNvidiaDriverVersion();
[[nodiscard]] std::string FormatGpuDeviceId(std::uint32_t deviceId);
[[nodiscard]] results::EnvironmentRecord ComposeEnvironmentRecord(
    const EnvironmentRunContext& context,
    const WindowsHostMetadata& host,
    const BuildMetadata& build,
    const std::optional<DeviceUuid>& measuredDeviceUuid,
    const std::vector<CudaDeviceMetadata>& cudaDevices,
    const std::vector<VulkanDeviceMetadata>& vulkanDevices,
    const std::optional<std::string>& nvidiaDriverVersion);
[[nodiscard]] results::EnvironmentRecord CollectEnvironmentRecord(
    const EnvironmentRunContext& context,
    const std::optional<DeviceUuid>& measuredDeviceUuid);
[[nodiscard]] results::EnvironmentRecord CollectCudaEnvironmentRecord(
    const EnvironmentRunContext& context,
    std::uint32_t deviceOrdinal,
    const DeviceUuid& measuredDeviceUuid);
[[nodiscard]] results::EnvironmentRecord CollectVulkanEnvironmentRecord(
    const EnvironmentRunContext& context,
    std::uint32_t physicalDeviceIndex,
    const DeviceUuid& measuredDeviceUuid);

} // namespace computelab::environment
