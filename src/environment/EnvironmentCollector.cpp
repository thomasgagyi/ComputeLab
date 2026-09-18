#include "environment/EnvironmentCollector.hpp"

#include <cuda_runtime_api.h>
#include <nvml.h>
#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace computelab::environment
{
namespace
{

constexpr std::uint32_t kEnvironmentSchemaVersion = 1U;
constexpr std::uint32_t kNvidiaVendorId = 0x10DEU;

[[noreturn]] void ThrowAcquisitionFailure(const std::string& detail)
{
    throw std::runtime_error("environment metadata acquisition failed: " + detail);
}

void RequireNonEmpty(std::string_view value, std::string_view field)
{
    if (value.empty())
    {
        ThrowAcquisitionFailure(std::string(field) + " is empty");
    }
}

void CheckCuda(cudaError_t result, const char* operation)
{
    if (result != cudaSuccess)
    {
        ThrowAcquisitionFailure(std::string(operation) + ": " + cudaGetErrorString(result));
    }
}

void CheckVulkan(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        ThrowAcquisitionFailure(std::string(operation) + " returned " + std::to_string(result));
    }
}

void CheckNvml(nvmlReturn_t result, const char* operation)
{
    if (result != NVML_SUCCESS)
    {
        ThrowAcquisitionFailure(std::string(operation) + ": " + nvmlErrorString(result));
    }
}

std::string WideStringToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (requiredSize <= 0)
    {
        ThrowAcquisitionFailure("WideCharToMultiByte size query failed");
    }

    std::string converted(static_cast<std::size_t>(requiredSize), '\0');
    const int convertedSize = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        converted.data(),
        requiredSize,
        nullptr,
        nullptr);
    if (convertedSize != requiredSize)
    {
        ThrowAcquisitionFailure("WideCharToMultiByte conversion failed");
    }

    return converted;
}

std::wstring ReadRegistryString(const wchar_t* subKey, const wchar_t* valueName)
{
    DWORD type = 0U;
    DWORD byteCount = 0U;
    const LSTATUS sizeResult = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subKey,
        valueName,
        RRF_RT_REG_SZ,
        &type,
        nullptr,
        &byteCount);
    if (sizeResult != ERROR_SUCCESS || byteCount < sizeof(wchar_t))
    {
        ThrowAcquisitionFailure("RegGetValueW size query failed");
    }

    std::vector<wchar_t> value(byteCount / sizeof(wchar_t));
    const LSTATUS valueResult = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subKey,
        valueName,
        RRF_RT_REG_SZ,
        &type,
        value.data(),
        &byteCount);
    if (valueResult != ERROR_SUCCESS)
    {
        ThrowAcquisitionFailure("RegGetValueW string query failed");
    }

    return std::wstring(value.data());
}

std::uint32_t ReadRegistryDword(const wchar_t* subKey, const wchar_t* valueName)
{
    DWORD type = 0U;
    DWORD value = 0U;
    DWORD byteCount = sizeof(value);
    const LSTATUS result = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subKey,
        valueName,
        RRF_RT_REG_DWORD,
        &type,
        &value,
        &byteCount);
    if (result != ERROR_SUCCESS || byteCount != sizeof(value))
    {
        ThrowAcquisitionFailure("RegGetValueW DWORD query failed");
    }

    return value;
}

std::array<std::uint8_t, 16> ToUuid(const cudaUUID_t& uuid)
{
    std::array<std::uint8_t, 16> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        result[index] = static_cast<std::uint8_t>(uuid.bytes[index]);
    }
    return result;
}

std::array<std::uint8_t, 16> ToUuid(const std::uint8_t (&uuid)[VK_UUID_SIZE])
{
    std::array<std::uint8_t, 16> result{};
    std::copy_n(uuid, result.size(), result.begin());
    return result;
}

std::string FormatCudaRuntimeVersion(int version)
{
    if (version <= 0)
    {
        ThrowAcquisitionFailure("cudaRuntimeGetVersion returned a nonpositive version");
    }

    return std::to_string(version / 1000) + "." + std::to_string((version % 1000) / 10);
}

std::string FormatComputeCapability(int major, int minor)
{
    if (major < 0 || minor < 0)
    {
        ThrowAcquisitionFailure("CUDA compute capability is negative");
    }

    return std::to_string(major) + "." + std::to_string(minor);
}

std::string FormatVulkanApiVersion(std::uint32_t version)
{
    return std::to_string(VK_VERSION_MAJOR(version)) + "."
        + std::to_string(VK_VERSION_MINOR(version)) + "."
        + std::to_string(VK_VERSION_PATCH(version));
}

std::vector<CudaDeviceMetadata> CollectCudaDeviceMetadata()
{
    int deviceCount = 0;
    CheckCuda(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount");
    if (deviceCount <= 0)
    {
        ThrowAcquisitionFailure("no CUDA devices are available");
    }

    int runtimeVersion = 0;
    CheckCuda(cudaRuntimeGetVersion(&runtimeVersion), "cudaRuntimeGetVersion");

    std::vector<CudaDeviceMetadata> devices;
    devices.reserve(static_cast<std::size_t>(deviceCount));
    for (int deviceOrdinal = 0; deviceOrdinal < deviceCount; ++deviceOrdinal)
    {
        cudaDeviceProp properties{};
        CheckCuda(
            cudaGetDeviceProperties(&properties, deviceOrdinal),
            "cudaGetDeviceProperties");
        if (properties.pciDeviceID < 0)
            ThrowAcquisitionFailure("CUDA PCI device identifier is negative");
        CudaDeviceMetadata metadata{
            ToUuid(properties.uuid),
            properties.name,
            static_cast<std::uint64_t>(properties.totalGlobalMem),
            properties.major,
            properties.minor,
            FormatCudaRuntimeVersion(runtimeVersion),
            static_cast<std::uint32_t>(properties.pciDeviceID)};
        RequireNonEmpty(metadata.name, "CUDA device name");
        if (metadata.totalGlobalMemoryBytes == 0U)
        {
            ThrowAcquisitionFailure("CUDA device totalGlobalMem is zero");
        }
        devices.push_back(std::move(metadata));
    }
    return devices;
}

std::vector<VulkanDeviceMetadata> CollectVulkanDeviceMetadata()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "ComputeLab environment metadata";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "None";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = VK_NULL_HANDLE;
    CheckVulkan(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");

    try
    {
        std::uint32_t deviceCount = 0U;
        CheckVulkan(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices count");
        if (deviceCount == 0U)
        {
            ThrowAcquisitionFailure("no Vulkan physical devices are available");
        }

        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()),
            "vkEnumeratePhysicalDevices values");

        std::vector<VulkanDeviceMetadata> metadata;
        metadata.reserve(physicalDevices.size());
        for (const VkPhysicalDevice physicalDevice : physicalDevices)
        {
            VkPhysicalDeviceIDProperties idProperties{};
            idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

            VkPhysicalDeviceProperties2 properties{};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &idProperties;
            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(
                physicalDevice, &memoryProperties);
            std::uint64_t deviceLocalMemoryBytes = 0U;
            for (std::uint32_t heapIndex = 0U;
                 heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
            {
                if ((memoryProperties.memoryHeaps[heapIndex].flags
                        & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0U)
                {
                    if (deviceLocalMemoryBytes
                        > (std::numeric_limits<std::uint64_t>::max)()
                            - memoryProperties.memoryHeaps[heapIndex].size)
                    {
                        ThrowAcquisitionFailure(
                            "Vulkan device-local memory total overflowed");
                    }
                    deviceLocalMemoryBytes +=
                        memoryProperties.memoryHeaps[heapIndex].size;
                }
            }

            metadata.push_back({
                ToUuid(idProperties.deviceUUID),
                properties.properties.vendorID,
                properties.properties.deviceID,
                FormatVulkanApiVersion(properties.properties.apiVersion),
                properties.properties.deviceName,
                deviceLocalMemoryBytes});
        }

        vkDestroyInstance(instance, nullptr);
        return metadata;
    }
    catch (...)
    {
        vkDestroyInstance(instance, nullptr);
        throw;
    }
}

} // namespace

BuildMetadata GetConfiguredBuildMetadata()
{
    BuildMetadata metadata{
        COMPUTELAB_BUILD_COMPILER_NAME,
        COMPUTELAB_BUILD_COMPILER_VERSION,
        COMPUTELAB_BUILD_CMAKE_VERSION,
        COMPUTELAB_BUILD_NINJA_VERSION,
        COMPUTELAB_BUILD_CONFIGURE_PRESET,
        COMPUTELAB_BUILD_TYPE,
        COMPUTELAB_BUILD_CUDA_TOOLKIT_VERSION,
        COMPUTELAB_BUILD_VULKAN_SDK_VERSION};

    RequireNonEmpty(metadata.compilerName, "compiler_name");
    RequireNonEmpty(metadata.compilerVersion, "compiler_version");
    RequireNonEmpty(metadata.cmakeVersion, "cmake_version");
    RequireNonEmpty(metadata.ninjaVersion, "ninja_version");
    RequireNonEmpty(metadata.configurePreset, "configure_preset");
    RequireNonEmpty(metadata.buildType, "build_type");
    RequireNonEmpty(metadata.cudaToolkitVersion, "cuda_toolkit_version");
    RequireNonEmpty(metadata.vulkanSdkVersion, "vulkan_sdk_version");
    return metadata;
}

WindowsHostMetadata CollectWindowsHostMetadata()
{
    constexpr wchar_t kWindowsCurrentVersionKey[] =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    constexpr wchar_t kCpuKey[] =
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

    const std::uint32_t majorVersion =
        ReadRegistryDword(kWindowsCurrentVersionKey, L"CurrentMajorVersionNumber");
    const std::uint32_t minorVersion =
        ReadRegistryDword(kWindowsCurrentVersionKey, L"CurrentMinorVersionNumber");
    const std::string buildNumber =
        WideStringToUtf8(ReadRegistryString(kWindowsCurrentVersionKey, L"CurrentBuildNumber"));
    const std::string cpuName =
        WideStringToUtf8(ReadRegistryString(kCpuKey, L"ProcessorNameString"));

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) == FALSE)
    {
        ThrowAcquisitionFailure("GlobalMemoryStatusEx failed");
    }

    WindowsHostMetadata metadata{
        "Windows",
        std::to_string(majorVersion) + "." + std::to_string(minorVersion) + "." + buildNumber,
        cpuName,
        memoryStatus.ullTotalPhys};
    RequireNonEmpty(metadata.osVersion, "os_version");
    RequireNonEmpty(metadata.cpuName, "cpu_name");
    if (metadata.systemMemoryBytes == 0U)
    {
        ThrowAcquisitionFailure("system_memory_bytes is zero");
    }
    return metadata;
}

std::string CollectNvidiaDriverVersion()
{
    CheckNvml(nvmlInit_v2(), "nvmlInit_v2");
    try
    {
        std::array<char, NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE> version{};
        CheckNvml(
            nvmlSystemGetDriverVersion(version.data(), static_cast<unsigned int>(version.size())),
            "nvmlSystemGetDriverVersion");

        const std::string result(version.data());
        RequireNonEmpty(result, "nvidia_driver_version");
        CheckNvml(nvmlShutdown(), "nvmlShutdown");
        return result;
    }
    catch (...)
    {
        static_cast<void>(nvmlShutdown());
        throw;
    }
}

std::string FormatGpuDeviceId(std::uint32_t deviceId)
{
    std::array<char, 8> hexadecimal{};
    const auto [end, error] = std::to_chars(
        hexadecimal.data(),
        hexadecimal.data() + hexadecimal.size(),
        deviceId,
        16);
    if (error != std::errc{})
    {
        ThrowAcquisitionFailure("GPU device identifier formatting failed");
    }

    const std::size_t digitCount = static_cast<std::size_t>(end - hexadecimal.data());
    return "0x" + std::string(8U - digitCount, '0')
        + std::string(hexadecimal.data(), digitCount);
}

results::EnvironmentRecord ComposeEnvironmentRecord(
    const EnvironmentRunContext& context,
    const WindowsHostMetadata& host,
    const BuildMetadata& build,
    const std::optional<DeviceUuid>& measuredDeviceUuid,
    const std::vector<CudaDeviceMetadata>& cudaDevices,
    const std::vector<VulkanDeviceMetadata>& vulkanDevices,
    const std::optional<std::string>& nvidiaDriverVersion)
{
    RequireNonEmpty(context.experimentId, "experiment_id");
    RequireNonEmpty(context.runId, "run_id");
    RequireNonEmpty(context.timestampUtc, "timestamp_utc");
    RequireNonEmpty(context.gitCommit, "git_commit");
    RequireNonEmpty(context.machineId, "machine_id");
    RequireNonEmpty(host.osName, "os_name");
    RequireNonEmpty(host.osVersion, "os_version");
    RequireNonEmpty(host.cpuName, "cpu_name");
    if (host.systemMemoryBytes == 0U)
    {
        ThrowAcquisitionFailure("system_memory_bytes is zero");
    }

    RequireNonEmpty(build.compilerName, "compiler_name");
    RequireNonEmpty(build.compilerVersion, "compiler_version");
    RequireNonEmpty(build.cmakeVersion, "cmake_version");
    RequireNonEmpty(build.ninjaVersion, "ninja_version");
    RequireNonEmpty(build.configurePreset, "configure_preset");
    RequireNonEmpty(build.buildType, "build_type");
    std::optional<std::string> gpuName;
    std::optional<std::string> gpuVendor;
    std::optional<std::string> gpuDeviceId;
    std::optional<std::uint64_t> gpuMemoryBytes;
    std::optional<std::string> driverVersion;
    std::optional<std::string> cudaToolkitVersion;
    std::optional<std::string> cudaRuntimeVersion;
    std::optional<std::string> cudaComputeCapability;
    std::optional<std::string> vulkanSdkVersion;
    std::optional<std::string> vulkanDeviceApiVersion;

    if (measuredDeviceUuid.has_value())
    {
        const auto matchedCudaDevice = std::find_if(
            cudaDevices.begin(), cudaDevices.end(),
            [&measuredDeviceUuid](const CudaDeviceMetadata& device) {
                return device.uuid == *measuredDeviceUuid;
            });
        if (matchedCudaDevice == cudaDevices.end())
        {
            ThrowAcquisitionFailure(
                "the measured GPU UUID is not present in CUDA devices");
        }
        const auto matchedVulkanDevice = std::find_if(
            vulkanDevices.begin(), vulkanDevices.end(),
            [&measuredDeviceUuid](const VulkanDeviceMetadata& device) {
                return device.uuid == *measuredDeviceUuid;
            });
        if (matchedVulkanDevice == vulkanDevices.end())
        {
            ThrowAcquisitionFailure(
                "the measured GPU UUID is not present in Vulkan physical devices");
        }
        if (matchedVulkanDevice->vendorId != kNvidiaVendorId)
        {
            ThrowAcquisitionFailure("the measured UUID-matched Vulkan physical device is not NVIDIA");
        }
        if (!nvidiaDriverVersion.has_value())
        {
            ThrowAcquisitionFailure("nvidia_driver_version is unavailable for the measured GPU");
        }
        RequireNonEmpty(matchedCudaDevice->name, "gpu_name");
        RequireNonEmpty(matchedCudaDevice->runtimeVersion, "cuda_runtime_version");
        RequireNonEmpty(matchedVulkanDevice->deviceApiVersion, "vulkan_device_api_version");
        RequireNonEmpty(*nvidiaDriverVersion, "nvidia_driver_version");
        RequireNonEmpty(build.cudaToolkitVersion, "cuda_toolkit_version");
        RequireNonEmpty(build.vulkanSdkVersion, "vulkan_sdk_version");
        if (matchedCudaDevice->totalGlobalMemoryBytes == 0U)
        {
            ThrowAcquisitionFailure("gpu_memory_bytes is zero");
        }

        gpuName = matchedCudaDevice->name;
        gpuVendor = "NVIDIA";
        gpuDeviceId = FormatGpuDeviceId(matchedVulkanDevice->deviceId);
        gpuMemoryBytes = matchedCudaDevice->totalGlobalMemoryBytes;
        driverVersion = *nvidiaDriverVersion;
        cudaToolkitVersion = build.cudaToolkitVersion;
        cudaRuntimeVersion = matchedCudaDevice->runtimeVersion;
        cudaComputeCapability = FormatComputeCapability(
            matchedCudaDevice->computeCapabilityMajor,
            matchedCudaDevice->computeCapabilityMinor);
        vulkanSdkVersion = build.vulkanSdkVersion;
        vulkanDeviceApiVersion = matchedVulkanDevice->deviceApiVersion;
    }

    return {
        kEnvironmentSchemaVersion,
        context.experimentId,
        context.runId,
        context.timestampUtc,
        context.gitCommit,
        context.gitDirty,
        context.machineId,
        host.osName,
        host.osVersion,
        host.cpuName,
        host.systemMemoryBytes,
        gpuName,
        gpuVendor,
        gpuDeviceId,
        gpuMemoryBytes,
        driverVersion,
        cudaToolkitVersion,
        cudaRuntimeVersion,
        cudaComputeCapability,
        vulkanSdkVersion,
        vulkanDeviceApiVersion,
        build.compilerName,
        build.compilerVersion,
        build.cmakeVersion,
        build.ninjaVersion,
        build.configurePreset,
        build.buildType,
        context.validationEnabled,
        context.diagnosticInstrumentation};
}

results::EnvironmentRecord CollectEnvironmentRecord(
    const EnvironmentRunContext& context,
    const std::optional<DeviceUuid>& measuredDeviceUuid)
{
    const bool hasMeasuredGpu = measuredDeviceUuid.has_value();
    return ComposeEnvironmentRecord(
        context,
        CollectWindowsHostMetadata(),
        GetConfiguredBuildMetadata(),
        measuredDeviceUuid,
        hasMeasuredGpu ? CollectCudaDeviceMetadata()
                       : std::vector<CudaDeviceMetadata>{},
        hasMeasuredGpu ? CollectVulkanDeviceMetadata()
                       : std::vector<VulkanDeviceMetadata>{},
        hasMeasuredGpu ? std::optional<std::string>{CollectNvidiaDriverVersion()}
                       : std::nullopt);
}

results::EnvironmentRecord CollectCudaEnvironmentRecord(
    const EnvironmentRunContext& context,
    std::uint32_t deviceOrdinal,
    const DeviceUuid& measuredDeviceUuid)
{
    const BuildMetadata build = GetConfiguredBuildMetadata();
    results::EnvironmentRecord record = ComposeEnvironmentRecord(
        context,
        CollectWindowsHostMetadata(),
        build,
        std::nullopt,
        {},
        {},
        std::nullopt);
    const auto devices = CollectCudaDeviceMetadata();
    if (deviceOrdinal >= devices.size())
        ThrowAcquisitionFailure("selected CUDA device ordinal is out of range");
    const CudaDeviceMetadata& device = devices[deviceOrdinal];
    if (device.uuid != measuredDeviceUuid)
        ThrowAcquisitionFailure(
            "selected CUDA device UUID changed between operation and environment collection");
    RequireNonEmpty(device.name, "gpu_name");
    RequireNonEmpty(device.runtimeVersion, "cuda_runtime_version");
    RequireNonEmpty(build.cudaToolkitVersion, "cuda_toolkit_version");
    if (device.totalGlobalMemoryBytes == 0U)
        ThrowAcquisitionFailure("gpu_memory_bytes is zero");

    record.gpuName = device.name;
    record.gpuVendor = "NVIDIA";
    record.gpuDeviceId = FormatGpuDeviceId(device.deviceId);
    record.gpuMemoryBytes = device.totalGlobalMemoryBytes;
    record.nvidiaDriverVersion = CollectNvidiaDriverVersion();
    record.cudaToolkitVersion = build.cudaToolkitVersion;
    record.cudaRuntimeVersion = device.runtimeVersion;
    record.cudaComputeCapability = FormatComputeCapability(
        device.computeCapabilityMajor, device.computeCapabilityMinor);
    return record;
}

results::EnvironmentRecord CollectVulkanEnvironmentRecord(
    const EnvironmentRunContext& context,
    std::uint32_t physicalDeviceIndex,
    const DeviceUuid& measuredDeviceUuid)
{
    const BuildMetadata build = GetConfiguredBuildMetadata();
    results::EnvironmentRecord record = ComposeEnvironmentRecord(
        context,
        CollectWindowsHostMetadata(),
        build,
        std::nullopt,
        {},
        {},
        std::nullopt);
    const auto devices = CollectVulkanDeviceMetadata();
    if (physicalDeviceIndex >= devices.size())
        ThrowAcquisitionFailure("selected Vulkan physical-device index is out of range");
    const VulkanDeviceMetadata& device = devices[physicalDeviceIndex];
    if (device.uuid != measuredDeviceUuid)
        ThrowAcquisitionFailure(
            "selected Vulkan device UUID changed between operation and environment collection");
    if (device.vendorId != kNvidiaVendorId)
        ThrowAcquisitionFailure("selected Vulkan physical device is not NVIDIA");
    RequireNonEmpty(device.name, "gpu_name");
    RequireNonEmpty(device.deviceApiVersion, "vulkan_device_api_version");
    RequireNonEmpty(build.vulkanSdkVersion, "vulkan_sdk_version");
    if (device.deviceLocalMemoryBytes == 0U)
        ThrowAcquisitionFailure("gpu_memory_bytes is zero");

    record.gpuName = device.name;
    record.gpuVendor = "NVIDIA";
    record.gpuDeviceId = FormatGpuDeviceId(device.deviceId);
    record.gpuMemoryBytes = device.deviceLocalMemoryBytes;
    record.nvidiaDriverVersion = CollectNvidiaDriverVersion();
    record.vulkanSdkVersion = build.vulkanSdkVersion;
    record.vulkanDeviceApiVersion = device.deviceApiVersion;
    return record;
}

} // namespace computelab::environment
