#include "vulkan/Ex2VulkanB.hpp"

#include "ex2/Ex2IndexPermutation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace computelab::vulkan
{
namespace
{

enum class OperationState
{
    Empty,
    Uploaded,
    Prepared,
    Submitted,
    Complete,
    Failed,
    CompletionUncertain,
};

[[noreturn]] void ThrowVulkanError(
    VkResult result,
    Ex2VulkanBNativePhase phase,
    std::string operation)
{
    throw Ex2VulkanBNativeError{
        phase,
        result,
        operation,
        operation + " failed during " + std::string{ToString(phase)} +
            " with VkResult " + std::to_string(static_cast<int>(result))};
}

void CheckVulkan(
    VkResult result,
    Ex2VulkanBNativePhase phase,
    const char* operation)
{
    if (result != VK_SUCCESS)
    {
        ThrowVulkanError(result, phase, operation);
    }
}

std::vector<std::uint32_t> ReadSpirv(
    const std::filesystem::path& path,
    std::string_view shaderName)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        throw std::runtime_error(
            "EX-2 Vulkan B cannot open " + std::string{shaderName} +
            " SPIR-V: " + path.string());
    }
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
    {
        throw std::runtime_error(
            "EX-2 Vulkan B " + std::string{shaderName} +
            " SPIR-V has an invalid byte length: " + path.string());
    }
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(length) / sizeof(std::uint32_t));
    stream.seekg(0);
    if (!stream.read(
            reinterpret_cast<char*>(words.data()),
            static_cast<std::streamsize>(length)) ||
        words.front() != 0x07230203U)
    {
        throw std::runtime_error(
            "EX-2 Vulkan B " + std::string{shaderName} +
            " SPIR-V data is malformed: " + path.string());
    }
    return words;
}

void BufferDependency(
    VkCommandBuffer commands,
    VkBuffer buffer,
    VkPipelineStageFlags2 sourceStage,
    VkAccessFlags2 sourceAccess,
    VkPipelineStageFlags2 destinationStage,
    VkAccessFlags2 destinationAccess)
{
    VkBufferMemoryBarrier2 barrier{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0U;
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1U;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace

std::string_view ToString(Ex2VulkanBNativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2VulkanBNativePhase::InstanceCreation:
        return "instance creation";
    case Ex2VulkanBNativePhase::DeviceEnumeration:
        return "device enumeration";
    case Ex2VulkanBNativePhase::DeviceSelection:
        return "device selection";
    case Ex2VulkanBNativePhase::DeviceProperties:
        return "device properties";
    case Ex2VulkanBNativePhase::QueueSelection:
        return "queue selection";
    case Ex2VulkanBNativePhase::LogicalDeviceCreation:
        return "logical device creation";
    case Ex2VulkanBNativePhase::ResourcePreflight:
        return "resource preflight";
    case Ex2VulkanBNativePhase::BufferCreation:
        return "buffer creation";
    case Ex2VulkanBNativePhase::MemoryAllocation:
        return "memory allocation";
    case Ex2VulkanBNativePhase::MemoryBinding:
        return "memory binding";
    case Ex2VulkanBNativePhase::MemoryMapping:
        return "memory mapping";
    case Ex2VulkanBNativePhase::DescriptorCreation:
        return "descriptor creation";
    case Ex2VulkanBNativePhase::PipelineConstruction:
        return "pipeline construction";
    case Ex2VulkanBNativePhase::CommandCreation:
        return "command creation";
    case Ex2VulkanBNativePhase::InputUpload:
        return "input upload";
    case Ex2VulkanBNativePhase::IndexUpload:
        return "index upload";
    case Ex2VulkanBNativePhase::OutputInitialization:
        return "output initialization";
    case Ex2VulkanBNativePhase::Preparation:
        return "preparation";
    case Ex2VulkanBNativePhase::Submission:
        return "submission";
    case Ex2VulkanBNativePhase::CompletionWait:
        return "completion wait";
    case Ex2VulkanBNativePhase::OutputReadback:
        return "output readback";
    case Ex2VulkanBNativePhase::InputDiagnosticReadback:
        return "input diagnostic readback";
    case Ex2VulkanBNativePhase::IndexDiagnosticReadback:
        return "index diagnostic readback";
    }
    return "invalid";
}

Ex2VulkanBNativeError::Ex2VulkanBNativeError(
    Ex2VulkanBNativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanBNativePhase Ex2VulkanBNativeError::Phase() const noexcept
{
    return phase_;
}

VkResult Ex2VulkanBNativeError::NativeResult() const noexcept
{
    return nativeResult_;
}

const std::string& Ex2VulkanBNativeError::Operation() const noexcept
{
    return operation_;
}

void detail::ValidateEx2VulkanBConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    if (!ex2::ValidateSemanticConfiguration(configuration).IsValid() ||
        !std::holds_alternative<ex2::IndexedConfiguration>(
            configuration.parameters))
    {
        throw std::invalid_argument(
            "EX-2 Vulkan B configuration violates the I2-C indexed semantic contract");
    }
}

detail::Ex2VulkanBDispatchShape detail::ValidateEx2VulkanBDispatchShape(
    const ex2::IndexedConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize)
{
    ValidateEx2VulkanBConfiguration(ex2::MakeConfiguration(configuration));
    const VkDeviceSize bytes = CalculateEx2VulkanBBufferByteCount(
        configuration.elementCount, maximumBufferSize);
    if (bytes > limits.maxStorageBufferRange)
    {
        throw std::length_error(
            "EX-2 Vulkan B logical buffer exceeds maxStorageBufferRange");
    }
    if (limits.maxPushConstantsSize < sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the B element-count push constant");
    }
    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (limits.maxComputeWorkGroupInvocations < Ex2VulkanBLocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < Ex2VulkanBLocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1U ||
        limits.maxComputeWorkGroupSize[2] < 1U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot execute the EX-2 B 256 x 1 x 1 workgroup");
    }

    const std::uint64_t groupCount =
        configuration.elementCount / Ex2VulkanBLocalSizeX +
        (configuration.elementCount % Ex2VulkanBLocalSizeX == 0U
            ? 0U
            : 1U);
    if (groupCount > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1U ||
        limits.maxComputeWorkGroupCount[2] < 1U ||
        groupCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan B element count exceeds maxComputeWorkGroupCount");
    }
    return {
        static_cast<std::uint32_t>(groupCount),
        Ex2VulkanBLocalSizeX};
}

VkDeviceSize detail::CalculateEx2VulkanBBufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan B element count exceeds uint32 logical indexing");
    }
    if (elementCount >
        maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 Vulkan B buffer byte count exceeds the supported size");
    }
    return static_cast<VkDeviceSize>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2VulkanBAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount)
{
    if (requiredAllocationCount > maximumAllocationCount)
    {
        throw std::length_error(
            "selected Vulkan device cannot support the required EX-2 B buffer allocations");
    }
}

void detail::ValidateEx2VulkanBHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize)
{
    if (plannedHeapBytes > heapSize ||
        requirementSize > heapSize - plannedHeapBytes)
    {
        throw std::length_error(
            "EX-2 Vulkan B memory requirements exceed the selected Vulkan heap");
    }
}

std::uint32_t detail::SelectEx2VulkanBQueue(
    std::span<const VkQueueFamilyProperties> families)
{
    for (std::size_t index = 0U; index < families.size(); ++index)
    {
        if (families[index].queueCount != 0U &&
            (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    throw std::invalid_argument(
        "selected Vulkan device has no compute-capable queue");
}

std::uint32_t detail::SelectEx2VulkanBMemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties)
{
    for (unsigned pass = 0U; pass < 2U; ++pass)
    {
        const VkMemoryPropertyFlags requested =
            required | (pass == 0U ? preferred : 0U);
        for (std::uint32_t index = 0U;
            index < properties.memoryTypeCount;
            ++index)
        {
            const bool compatible =
                (compatibleTypeBits & (std::uint32_t{1U} << index)) != 0U;
            const auto available = properties.memoryTypes[index].propertyFlags;
            if (compatible && (available & requested) == requested)
            {
                return index;
            }
        }
    }
    throw std::invalid_argument(
        "EX-2 Vulkan B found no compatible memory type");
}

detail::Ex2VulkanBMappedRange detail::AlignEx2VulkanBNoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize)
{
    if (nonCoherentAtomSize == 0U)
    {
        throw std::invalid_argument(
            "Vulkan nonCoherentAtomSize must be positive");
    }
    if (offset > allocationSize || size > allocationSize - offset)
    {
        throw std::out_of_range(
            "EX-2 Vulkan B mapped range exceeds its allocation");
    }
    if (size == 0U)
    {
        return {offset, 0U};
    }

    const VkDeviceSize alignedOffset =
        offset - (offset % nonCoherentAtomSize);
    const VkDeviceSize end = offset + size;
    if (end == allocationSize)
    {
        return {alignedOffset, VK_WHOLE_SIZE};
    }
    const VkDeviceSize remainder = end % nonCoherentAtomSize;
    VkDeviceSize alignedEnd = end;
    if (remainder != 0U)
    {
        const VkDeviceSize increment = nonCoherentAtomSize - remainder;
        if (end > std::numeric_limits<VkDeviceSize>::max() - increment)
        {
            throw std::overflow_error(
                "EX-2 Vulkan B noncoherent range alignment overflow");
        }
        alignedEnd += increment;
    }
    if (alignedEnd >= allocationSize)
    {
        return {alignedOffset, VK_WHOLE_SIZE};
    }
    return {alignedOffset, alignedEnd - alignedOffset};
}

struct Ex2VulkanBOperation::Resources
{
    struct Buffer
    {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize allocationSize{};
        std::uint32_t memoryTypeIndex{};
        VkMemoryPropertyFlags memoryFlags{};
        void* mapped{};
    } input, indices, output, upload, readback;

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer inputUploadCommands{VK_NULL_HANDLE};
    VkCommandBuffer indexUploadCommands{VK_NULL_HANDLE};
    VkCommandBuffer outputInitializationCommands{VK_NULL_HANDLE};
    VkCommandBuffer computeCommands{VK_NULL_HANDLE};
    VkCommandBuffer outputReadbackCommands{VK_NULL_HANDLE};
    VkCommandBuffer inputReadbackCommands{VK_NULL_HANDLE};
    VkCommandBuffer indexReadbackCommands{VK_NULL_HANDLE};
    VkFence computeFence{VK_NULL_HANDLE};
    VkFence transferFence{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkShaderModule shader{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDeviceSize maximumMemoryAllocationSize{};
    VkDeviceSize maximumBufferSize{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> plannedHeapBytes{};
    ex2::IndexedConfiguration configuration{};
    std::uint64_t seed{};
    detail::Ex2VulkanBDispatchShape dispatchShape{};
    VkDeviceSize logicalBufferBytes{};
    VkDeviceSize storageBufferBytes{};
    Ex2VulkanBDiagnostics diagnostics{};
    std::string loadedShaderName;
    std::vector<std::uint32_t> loadedSpirv;
    OperationState state{OperationState::Empty};
    bool computePending{};
    bool transferPending{};
    bool lastCompletionExecutedShader{};

    ~Resources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            const auto disposition =
                detail::ClassifyEx2VulkanBResourceDisposition(
                    state == OperationState::CompletionUncertain,
                    computePending,
                    transferPending);
            if (disposition ==
                detail::Ex2VulkanBResourceDisposition::PreserveForProcessTeardown)
            {
                return;
            }
            if (disposition ==
                detail::Ex2VulkanBResourceDisposition::DrainComputeThenDestroy)
            {
                const VkResult result = vkWaitForFences(
                    device,
                    1U,
                    &computeFence,
                    VK_TRUE,
                    std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                {
                    return;
                }
            }
            if (commandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, commandPool, nullptr);
            if (pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, pipeline, nullptr);
            if (shader != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, shader, nullptr);
            if (pipelineLayout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorPool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            if (computeFence != VK_NULL_HANDLE)
                vkDestroyFence(device, computeFence, nullptr);
            if (transferFence != VK_NULL_HANDLE)
                vkDestroyFence(device, transferFence, nullptr);
            DestroyBuffer(readback);
            DestroyBuffer(upload);
            DestroyBuffer(output);
            DestroyBuffer(indices);
            DestroyBuffer(input);
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE)
            vkDestroyInstance(instance, nullptr);
    }

    void DestroyBuffer(Buffer& buffer) noexcept
    {
        if (buffer.mapped != nullptr)
            vkUnmapMemory(device, buffer.memory);
        if (buffer.handle != VK_NULL_HANDLE)
            vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, buffer.memory, nullptr);
    }

    void RequireReusable(const char* operation) const
    {
        if (state == OperationState::Submitted)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan B "} + operation +
                " is invalid while work is pending");
        }
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan B "} + operation +
                " is unavailable after uncertain completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan B "} + operation +
                " is unavailable after a native failure");
        }
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 Vulkan B";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        CheckVulkan(
            vkCreateInstance(&instanceInfo, nullptr, &instance),
            Ex2VulkanBNativePhase::InstanceCreation,
            "vkCreateInstance for EX-2 Vulkan B");

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            Ex2VulkanBNativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices count for EX-2 Vulkan B");
        if (physicalDeviceIndex >= count)
        {
            throw std::invalid_argument(
                "requested Vulkan physical-device index is unavailable");
        }
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            Ex2VulkanBNativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices data for EX-2 Vulkan B");
        if (physicalDeviceIndex >= count)
        {
            throw std::runtime_error(
                "Vulkan physical-device enumeration changed during EX-2 B setup");
        }
        physicalDevice = devices[physicalDeviceIndex];
        diagnostics.physicalDeviceIndex = physicalDeviceIndex;

        VkPhysicalDeviceMaintenance4Properties maintenance4{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
        VkPhysicalDeviceMaintenance3Properties maintenance3{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
        maintenance3.pNext = &maintenance4;
        VkPhysicalDeviceIDProperties idProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        idProperties.pNext = &maintenance3;
        VkPhysicalDeviceProperties2 properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &idProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
        diagnostics.properties = properties.properties;
        std::copy_n(
            idProperties.deviceUUID,
            diagnostics.deviceUuid.size(),
            diagnostics.deviceUuid.begin());
        maximumMemoryAllocationSize = maintenance3.maxMemoryAllocationSize;
        maximumBufferSize = maintenance4.maxBufferSize;
        if (diagnostics.properties.apiVersion < VK_API_VERSION_1_3)
        {
            throw std::invalid_argument(
                "selected Vulkan device does not support core Vulkan 1.3");
        }
        if (storageBufferBytes > maximumBufferSize ||
            storageBufferBytes > diagnostics.properties.limits.maxStorageBufferRange)
        {
            throw std::length_error(
                "EX-2 Vulkan B physical buffer exceeds selected device limits");
        }

        VkPhysicalDeviceVulkan13Features supported13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 supported{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
        if (supported13.synchronization2 != VK_TRUE)
        {
            throw std::invalid_argument(
                "selected Vulkan device lacks required synchronization2 support");
        }

        vkGetPhysicalDeviceMemoryProperties(
            physicalDevice, &memoryProperties);
        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex =
            detail::SelectEx2VulkanBQueue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];

        dispatchShape = detail::ValidateEx2VulkanBDispatchShape(
            configuration,
            diagnostics.properties.limits,
            maximumBufferSize);
        detail::ValidateEx2VulkanBAllocationCount(
            diagnostics.properties.limits.maxMemoryAllocationCount);

        VkPhysicalDeviceVulkan13Features enabled13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled13.synchronization2 = VK_TRUE;
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        queueInfo.queueCount = 1U;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &enabled13;
        deviceInfo.queueCreateInfoCount = 1U;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        CheckVulkan(
            vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
            Ex2VulkanBNativePhase::LogicalDeviceCreation,
            "vkCreateDevice for EX-2 Vulkan B");
        diagnostics.synchronization2Enabled = true;
        vkGetDeviceQueue(
            device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "vkGetDeviceQueue returned a null EX-2 Vulkan B queue");
        }
    }

    void CreateBuffer(
        Buffer& buffer,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        const char* name)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = storageBufferBytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVulkan(
            vkCreateBuffer(device, &info, nullptr, &buffer.handle),
            Ex2VulkanBNativePhase::BufferCreation,
            name);

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        if (requirements.size == 0U ||
            requirements.size > maximumMemoryAllocationSize)
        {
            throw std::length_error(
                std::string{name} +
                " memory requirements exceed maxMemoryAllocationSize");
        }
        buffer.memoryTypeIndex = detail::SelectEx2VulkanBMemoryType(
            requirements.memoryTypeBits,
            required,
            preferred,
            memoryProperties);
        const std::uint32_t heapIndex =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].heapIndex;
        detail::ValidateEx2VulkanBHeapFeasibility(
            requirements.size,
            plannedHeapBytes[heapIndex],
            memoryProperties.memoryHeaps[heapIndex].size);
        plannedHeapBytes[heapIndex] += requirements.size;
        buffer.allocationSize = requirements.size;
        buffer.memoryFlags =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].propertyFlags;

        VkMemoryAllocateInfo allocation{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = buffer.memoryTypeIndex;
        CheckVulkan(
            vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
            Ex2VulkanBNativePhase::MemoryAllocation,
            name);
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            Ex2VulkanBNativePhase::MemoryBinding,
            name);
        if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U)
        {
            CheckVulkan(
                vkMapMemory(
                    device,
                    buffer.memory,
                    0U,
                    buffer.allocationSize,
                    0U,
                    &buffer.mapped),
                Ex2VulkanBNativePhase::MemoryMapping,
                name);
        }
    }

    void MaintainHostCache(
        const Buffer& buffer,
        VkDeviceSize bytes,
        bool flush,
        Ex2VulkanBNativePhase phase)
    {
        if (bytes == 0U ||
            (buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U)
        {
            return;
        }
        const auto aligned = detail::AlignEx2VulkanBNoncoherentRange(
            0U,
            bytes,
            buffer.allocationSize,
            diagnostics.properties.limits.nonCoherentAtomSize);
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.offset = aligned.offset;
        range.size = aligned.size;
        CheckVulkan(
            flush
                ? vkFlushMappedMemoryRanges(device, 1U, &range)
                : vkInvalidateMappedMemoryRanges(device, 1U, &range),
            phase,
            flush
                ? "vkFlushMappedMemoryRanges for EX-2 Vulkan B upload"
                : "vkInvalidateMappedMemoryRanges for EX-2 Vulkan B readback");
    }

    void SetupPipeline()
    {
        std::array<VkDescriptorSetLayoutBinding, 3U> bindings{};
        for (std::uint32_t index = 0U; index < bindings.size(); ++index)
        {
            bindings[index].binding = index;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[index].descriptorCount = 1U;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        CheckVulkan(
            vkCreateDescriptorSetLayout(
                device, &layoutInfo, nullptr, &descriptorLayout),
            Ex2VulkanBNativePhase::DescriptorCreation,
            "vkCreateDescriptorSetLayout for EX-2 Vulkan B");

        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3U};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1U;
        poolInfo.poolSizeCount = 1U;
        poolInfo.pPoolSizes = &poolSize;
        CheckVulkan(
            vkCreateDescriptorPool(
                device, &poolInfo, nullptr, &descriptorPool),
            Ex2VulkanBNativePhase::DescriptorCreation,
            "vkCreateDescriptorPool for EX-2 Vulkan B");

        VkDescriptorSetAllocateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1U;
        setInfo.pSetLayouts = &descriptorLayout;
        CheckVulkan(
            vkAllocateDescriptorSets(device, &setInfo, &descriptorSet),
            Ex2VulkanBNativePhase::DescriptorCreation,
            "vkAllocateDescriptorSets for EX-2 Vulkan B");

        const std::array<VkDescriptorBufferInfo, 3U> buffers{{
            {input.handle, 0U, storageBufferBytes},
            {indices.handle, 0U, storageBufferBytes},
            {output.handle, 0U, storageBufferBytes}}};
        std::array<VkWriteDescriptorSet, 3U> writes{};
        for (std::uint32_t index = 0U; index < writes.size(); ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1U;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &buffers[index];
        }
        vkUpdateDescriptorSets(
            device,
            static_cast<std::uint32_t>(writes.size()),
            writes.data(),
            0U,
            nullptr);

        const VkPushConstantRange pushRange{
            VK_SHADER_STAGE_COMPUTE_BIT,
            0U,
            sizeof(std::uint32_t)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1U;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        CheckVulkan(
            vkCreatePipelineLayout(
                device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
            Ex2VulkanBNativePhase::PipelineConstruction,
            "vkCreatePipelineLayout for EX-2 Vulkan B");

        VkShaderModuleCreateInfo shaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = loadedSpirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = loadedSpirv.data();
        CheckVulkan(
            vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
            Ex2VulkanBNativePhase::PipelineConstruction,
            "vkCreateShaderModule for EX-2 Vulkan B");

        VkComputePipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shader;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;
        CheckVulkan(
            vkCreateComputePipelines(
                device,
                VK_NULL_HANDLE,
                1U,
                &pipelineInfo,
                nullptr,
                &pipeline),
            Ex2VulkanBNativePhase::PipelineConstruction,
            "vkCreateComputePipelines for EX-2 Vulkan B");
    }

    void RecordUpload(
        VkCommandBuffer commands,
        VkBuffer destination,
        const char* name)
    {
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(commands, &begin),
            Ex2VulkanBNativePhase::CommandCreation,
            name);
        BufferDependency(
            commands,
            upload.handle,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            commands,
            destination,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
        {
            const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
            vkCmdCopyBuffer(
                commands, upload.handle, destination, 1U, &copy);
        }
        BufferDependency(
            commands,
            destination,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(commands),
            Ex2VulkanBNativePhase::CommandCreation,
            name);
    }

    void RecordOutputInitialization()
    {
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(outputInitializationCommands, &begin),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkBeginCommandBuffer for EX-2 Vulkan B output initialization");
        BufferDependency(
            outputInitializationCommands,
            output.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
        {
            vkCmdFillBuffer(
                outputInitializationCommands,
                output.handle,
                0U,
                logicalBufferBytes,
                Ex2VulkanBOutputSentinel);
        }
        BufferDependency(
            outputInitializationCommands,
            output.handle,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        CheckVulkan(
            vkEndCommandBuffer(outputInitializationCommands),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkEndCommandBuffer for EX-2 Vulkan B output initialization");
    }

    void RecordReadback(
        VkCommandBuffer commands,
        VkBuffer source,
        VkAccessFlags2 shaderAccess,
        const char* name)
    {
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(commands, &begin),
            Ex2VulkanBNativePhase::CommandCreation,
            name);
        BufferDependency(
            commands,
            source,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT,
            shaderAccess | VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            commands,
            readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT |
                VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT |
                VK_ACCESS_2_HOST_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
        {
            const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
            vkCmdCopyBuffer(commands, source, readback.handle, 1U, &copy);
        }
        BufferDependency(
            commands,
            readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(commands),
            Ex2VulkanBNativePhase::CommandCreation,
            name);
    }

    void CreateCommands()
    {
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkCreateCommandPool for EX-2 Vulkan B");

        std::array<VkCommandBuffer, 7U> commands{};
        VkCommandBufferAllocateInfo allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount =
            static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(device, &allocation, commands.data()),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkAllocateCommandBuffers for EX-2 Vulkan B");
        inputUploadCommands = commands[0];
        indexUploadCommands = commands[1];
        outputInitializationCommands = commands[2];
        computeCommands = commands[3];
        outputReadbackCommands = commands[4];
        inputReadbackCommands = commands[5];
        indexReadbackCommands = commands[6];

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &computeFence),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan B compute");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &transferFence),
            Ex2VulkanBNativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan B transfer");

        RecordUpload(
            inputUploadCommands,
            input.handle,
            "EX-2 Vulkan B input upload commands");
        RecordUpload(
            indexUploadCommands,
            indices.handle,
            "EX-2 Vulkan B index upload commands");
        RecordOutputInitialization();
        RecordReadback(
            outputReadbackCommands,
            output.handle,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            "EX-2 Vulkan B output readback commands");
        RecordReadback(
            inputReadbackCommands,
            input.handle,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            "EX-2 Vulkan B input diagnostic readback commands");
        RecordReadback(
            indexReadbackCommands,
            indices.handle,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            "EX-2 Vulkan B index diagnostic readback commands");
    }

    void RecordCompute()
    {
        CheckVulkan(
            vkResetCommandBuffer(computeCommands, 0U),
            Ex2VulkanBNativePhase::Preparation,
            "vkResetCommandBuffer for EX-2 Vulkan B compute");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(computeCommands, &begin),
            Ex2VulkanBNativePhase::Preparation,
            "vkBeginCommandBuffer for EX-2 Vulkan B compute");
        if (configuration.elementCount != 0U)
        {
            BufferDependency(
                computeCommands,
                input.handle,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            BufferDependency(
                computeCommands,
                indices.handle,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            BufferDependency(
                computeCommands,
                output.handle,
                VK_PIPELINE_STAGE_2_CLEAR_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            vkCmdBindPipeline(
                computeCommands,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline);
            vkCmdBindDescriptorSets(
                computeCommands,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelineLayout,
                0U,
                1U,
                &descriptorSet,
                0U,
                nullptr);
            const std::uint32_t elementCount =
                static_cast<std::uint32_t>(configuration.elementCount);
            vkCmdPushConstants(
                computeCommands,
                pipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0U,
                sizeof(elementCount),
                &elementCount);
            vkCmdDispatch(
                computeCommands,
                dispatchShape.groupCountX,
                1U,
                1U);
        }
        CheckVulkan(
            vkEndCommandBuffer(computeCommands),
            Ex2VulkanBNativePhase::Preparation,
            "vkEndCommandBuffer for EX-2 Vulkan B compute");
    }

    void SubmitCommands(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending,
        Ex2VulkanBNativePhase phase,
        const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(queue, 1U, &submission, fence);
        switch (detail::ClassifyEx2VulkanBSubmissionResult(result))
        {
        case detail::Ex2VulkanBSubmissionDisposition::Submitted:
            pending = true;
            return;
        case detail::Ex2VulkanBSubmissionDisposition::FailedWithoutSubmission:
            state = OperationState::Failed;
            ThrowVulkanError(result, phase, operation);
        case detail::Ex2VulkanBSubmissionDisposition::CompletionUncertain:
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(result, phase, operation);
        }
        std::terminate();
    }

    void Transfer(
        VkCommandBuffer commands,
        Ex2VulkanBNativePhase phase,
        const char* operation)
    {
        const VkResult resetResult =
            vkResetFences(device, 1U, &transferFence);
        if (resetResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                resetResult,
                phase,
                "vkResetFences for EX-2 Vulkan B transfer");
        }
        SubmitCommands(commands, transferFence, transferPending, phase, operation);
        const VkResult waitResult = vkWaitForFences(
            device,
            1U,
            &transferFence,
            VK_TRUE,
            30'000'000'000ULL);
        if (waitResult != VK_SUCCESS)
        {
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(
                waitResult,
                phase,
                "vkWaitForFences for EX-2 Vulkan B transfer");
        }
        transferPending = false;
    }

    std::vector<std::uint32_t> ReadBack(
        VkCommandBuffer commands,
        Ex2VulkanBNativePhase phase,
        const char* operation)
    {
        RequireReusable(operation);
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan B "} + operation +
                " requires explicit successful completion");
        }
        std::vector<std::uint32_t> result(
            static_cast<std::size_t>(configuration.elementCount));
        if (result.empty())
        {
            return result;
        }

        state = OperationState::Failed;
        Transfer(commands, phase, operation);
        MaintainHostCache(readback, logicalBufferBytes, false, phase);
        std::memcpy(
            result.data(),
            readback.mapped,
            static_cast<std::size_t>(logicalBufferBytes));
        state = OperationState::Complete;
        return result;
    }
};

Ex2VulkanBOperation::Ex2VulkanBOperation(
    const ex2::IndexedConfiguration& configuration,
    std::uint64_t seed,
    const std::filesystem::path& b1SpirvPath,
    const std::filesystem::path& b2SpirvPath,
    std::uint32_t physicalDeviceIndex)
    : resources_{std::make_unique<Resources>()}
{
    detail::ValidateEx2VulkanBConfiguration(
        ex2::MakeConfiguration(configuration, seed));
    auto& resources = *resources_;
    resources.configuration = configuration;
    resources.seed = seed;
    resources.logicalBufferBytes =
        detail::CalculateEx2VulkanBBufferByteCount(
            configuration.elementCount,
            std::numeric_limits<VkDeviceSize>::max());
    resources.storageBufferBytes = std::max<VkDeviceSize>(
        resources.logicalBufferBytes,
        sizeof(std::uint32_t));
    if (configuration.elementCount >
        std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 Vulkan B logical buffers exceed the host vector maximum");
    }

    const bool gather = configuration.variant == ex2::IndexedVariant::B1;
    resources.loadedShaderName = gather ? "Ex2B1.comp.spv" : "Ex2B2.comp.spv";
    resources.loadedSpirv = ReadSpirv(
        gather ? b1SpirvPath : b2SpirvPath,
        resources.loadedShaderName);
    resources.SetupDevice(physicalDeviceIndex);

    resources.CreateBuffer(
        resources.input,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan B input buffer");
    resources.CreateBuffer(
        resources.indices,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan B index buffer");
    resources.CreateBuffer(
        resources.output,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan B output buffer");
    resources.CreateBuffer(
        resources.upload,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "EX-2 Vulkan B upload staging buffer");
    resources.CreateBuffer(
        resources.readback,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        "EX-2 Vulkan B readback staging buffer");

    resources.diagnostics.inputMemoryFlags = resources.input.memoryFlags;
    resources.diagnostics.indexMemoryFlags = resources.indices.memoryFlags;
    resources.diagnostics.outputMemoryFlags = resources.output.memoryFlags;
    resources.diagnostics.uploadMemoryFlags = resources.upload.memoryFlags;
    resources.diagnostics.readbackMemoryFlags = resources.readback.memoryFlags;
    resources.SetupPipeline();
    resources.CreateCommands();
}

Ex2VulkanBOperation::~Ex2VulkanBOperation() noexcept = default;

std::uint64_t Ex2VulkanBOperation::ElementCount() const noexcept
{
    return resources_->configuration.elementCount;
}

ex2::IndexedVariant Ex2VulkanBOperation::Variant() const noexcept
{
    return resources_->configuration.variant;
}

ex2::IndexPattern Ex2VulkanBOperation::Pattern() const noexcept
{
    return resources_->configuration.indexPattern;
}

std::uint64_t Ex2VulkanBOperation::Seed() const noexcept
{
    return resources_->seed;
}

const std::array<std::uint8_t, VK_UUID_SIZE>&
Ex2VulkanBOperation::SelectedDeviceUuid() const noexcept
{
    return resources_->diagnostics.deviceUuid;
}

const Ex2VulkanBDiagnostics&
Ex2VulkanBOperation::Diagnostics() const noexcept
{
    return resources_->diagnostics;
}

std::string_view Ex2VulkanBOperation::LoadedShaderName() const noexcept
{
    return resources_->loadedShaderName;
}

std::span<const std::uint32_t>
Ex2VulkanBOperation::LoadedSpirv() const noexcept
{
    return resources_->loadedSpirv;
}

void Ex2VulkanBOperation::Upload(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices)
{
    auto& resources = *resources_;
    resources.RequireReusable("upload");
    if (resources.state != OperationState::Empty &&
        resources.state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan B upload requires initial or completed state");
    }

    // Invalidate any previously uploaded pair before validating replacement
    // data so a rejected replacement can never submit stale device buffers.
    resources.state = OperationState::Empty;
    resources.lastCompletionExecutedShader = false;
    if (input.size() != resources.configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan B input upload size does not match the configured element count");
    }
    if (indices.size() != resources.configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan B index upload size does not match the configured element count");
    }
    ex2::ValidateIndexPermutation(indices, resources.configuration.elementCount);
    {
        const auto declaredIndices =
            resources.configuration.indexPattern ==
                ex2::IndexPattern::StructuredV1
            ? ex2::GenerateStructuredPermutation(
                resources.configuration.elementCount)
            : ex2::GenerateShuffledPermutation(
                resources.seed,
                resources.configuration.elementCount);
        if (!std::equal(
                indices.begin(), indices.end(), declaredIndices.begin()))
        {
            throw std::invalid_argument(
                "EX-2 Vulkan B indices do not match the declared pattern and seed");
        }
    }

    resources.state = OperationState::Failed;
    if (!input.empty())
    {
        std::memcpy(resources.upload.mapped, input.data(), input.size_bytes());
        resources.MaintainHostCache(
            resources.upload,
            resources.logicalBufferBytes,
            true,
            Ex2VulkanBNativePhase::InputUpload);
        resources.Transfer(
            resources.inputUploadCommands,
            Ex2VulkanBNativePhase::InputUpload,
            "vkQueueSubmit2 for EX-2 Vulkan B input upload");

        std::memcpy(
            resources.upload.mapped,
            indices.data(),
            indices.size_bytes());
        resources.MaintainHostCache(
            resources.upload,
            resources.logicalBufferBytes,
            true,
            Ex2VulkanBNativePhase::IndexUpload);
        resources.Transfer(
            resources.indexUploadCommands,
            Ex2VulkanBNativePhase::IndexUpload,
            "vkQueueSubmit2 for EX-2 Vulkan B index upload");

        resources.Transfer(
            resources.outputInitializationCommands,
            Ex2VulkanBNativePhase::OutputInitialization,
            "vkQueueSubmit2 for EX-2 Vulkan B output initialization");
    }
    resources.state = OperationState::Uploaded;
}

void Ex2VulkanBOperation::Prepare()
{
    auto& resources = *resources_;
    resources.RequireReusable("preparation");
    if (resources.state != OperationState::Uploaded)
    {
        throw std::logic_error(
            "EX-2 Vulkan B preparation requires a completed upload");
    }
    resources.state = OperationState::Failed;
    CheckVulkan(
        vkResetFences(resources.device, 1U, &resources.computeFence),
        Ex2VulkanBNativePhase::Preparation,
        "vkResetFences for EX-2 Vulkan B compute");
    resources.RecordCompute();
    resources.state = OperationState::Prepared;
}

void Ex2VulkanBOperation::Submit()
{
    auto& resources = *resources_;
    resources.RequireReusable("submission");
    if (resources.state != OperationState::Prepared)
    {
        throw std::logic_error(
            "EX-2 Vulkan B submission requires prepared commands");
    }
    resources.SubmitCommands(
        resources.computeCommands,
        resources.computeFence,
        resources.computePending,
        Ex2VulkanBNativePhase::Submission,
        "vkQueueSubmit2 for EX-2 Vulkan B compute");
    resources.state = OperationState::Submitted;
}

void Ex2VulkanBOperation::WaitForCompletion(
    std::uint64_t timeoutNanoseconds)
{
    auto& resources = *resources_;
    if (resources.state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 Vulkan B completion cannot be retried after an uncertain result");
    }
    if (resources.state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 Vulkan B completion wait requires submitted work");
    }
    const VkResult result = vkWaitForFences(
        resources.device,
        1U,
        &resources.computeFence,
        VK_TRUE,
        timeoutNanoseconds);
    if (result != VK_SUCCESS)
    {
        resources.state = OperationState::CompletionUncertain;
        ThrowVulkanError(
            result,
            Ex2VulkanBNativePhase::CompletionWait,
            "vkWaitForFences for EX-2 Vulkan B compute");
    }
    resources.computePending = false;
    resources.state = OperationState::Complete;
    resources.lastCompletionExecutedShader =
        resources.configuration.elementCount != 0U;
}

std::vector<std::uint32_t> Ex2VulkanBOperation::RetrieveOutput()
{
    return resources_->ReadBack(
        resources_->outputReadbackCommands,
        Ex2VulkanBNativePhase::OutputReadback,
        "output readback");
}

std::vector<std::uint32_t> Ex2VulkanBOperation::RetrieveDeviceInput()
{
    return resources_->ReadBack(
        resources_->inputReadbackCommands,
        Ex2VulkanBNativePhase::InputDiagnosticReadback,
        "input diagnostic readback");
}

std::vector<std::uint32_t> Ex2VulkanBOperation::RetrieveDeviceIndices()
{
    return resources_->ReadBack(
        resources_->indexReadbackCommands,
        Ex2VulkanBNativePhase::IndexDiagnosticReadback,
        "index diagnostic readback");
}

bool Ex2VulkanBOperation::LastCompletionExecutedShader() const
{
    if (resources_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan B dispatch status requires successful completion");
    }
    return resources_->lastCompletionExecutedShader;
}

} // namespace computelab::vulkan
