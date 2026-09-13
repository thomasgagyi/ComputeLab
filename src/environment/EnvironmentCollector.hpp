#pragma once

#include "results/ResultRecords.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace computelab::environment
{

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
    std::array<std::uint8_t, 16> uuid;
    std::string name;
    std::uint64_t totalGlobalMemoryBytes{};
    int computeCapabilityMajor{};
    int computeCapabilityMinor{};
    std::string runtimeVersion;
};

struct VulkanDeviceMetadata
{
    std::array<std::uint8_t, 16> uuid;
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
    std::string deviceApiVersion;
};

[[nodiscard]] BuildMetadata GetConfiguredBuildMetadata();
[[nodiscard]] WindowsHostMetadata CollectWindowsHostMetadata();
[[nodiscard]] std::string CollectNvidiaDriverVersion();
[[nodiscard]] std::string FormatGpuDeviceId(std::uint32_t deviceId);
[[nodiscard]] results::EnvironmentRecord ComposeEnvironmentRecord(
    const EnvironmentRunContext& context,
    const WindowsHostMetadata& host,
    const BuildMetadata& build,
    const CudaDeviceMetadata& cudaDevice,
    const std::vector<VulkanDeviceMetadata>& vulkanDevices,
    const std::string& nvidiaDriverVersion);
[[nodiscard]] results::EnvironmentRecord CollectEnvironmentRecord(
    const EnvironmentRunContext& context);

} // namespace computelab::environment
