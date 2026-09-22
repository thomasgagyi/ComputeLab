#include "vulkan/Ex2VulkanA1.hpp"

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
    Ex2VulkanA1NativePhase phase,
    std::string operation)
{
    throw Ex2VulkanA1NativeError{
        phase,
        result,
        operation,
        operation + " failed during " + std::string{ToString(phase)} +
            " with VkResult " + std::to_string(static_cast<int>(result))};
}

void CheckVulkan(
    VkResult result,
    Ex2VulkanA1NativePhase phase,
    const char* operation)
{
    if (result != VK_SUCCESS)
    {
        ThrowVulkanError(result, phase, operation);
    }
}

std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        throw std::runtime_error(
            "EX-2 Vulkan A1 cannot open SPIR-V: " + path.string());
    }
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
    {
        throw std::runtime_error(
            "EX-2 Vulkan A1 SPIR-V has an invalid byte length: " +
            path.string());
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
            "EX-2 Vulkan A1 SPIR-V data is malformed: " + path.string());
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

std::string_view ToString(Ex2VulkanA1NativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2VulkanA1NativePhase::InstanceCreation:
        return "instance creation";
    case Ex2VulkanA1NativePhase::DeviceEnumeration:
        return "device enumeration";
    case Ex2VulkanA1NativePhase::DeviceSelection:
        return "device selection";
    case Ex2VulkanA1NativePhase::DeviceProperties:
        return "device properties";
    case Ex2VulkanA1NativePhase::QueueSelection:
        return "queue selection";
    case Ex2VulkanA1NativePhase::LogicalDeviceCreation:
        return "logical device creation";
    case Ex2VulkanA1NativePhase::ResourcePreflight:
        return "resource preflight";
    case Ex2VulkanA1NativePhase::BufferCreation:
        return "buffer creation";
    case Ex2VulkanA1NativePhase::MemoryAllocation:
        return "memory allocation";
    case Ex2VulkanA1NativePhase::MemoryBinding:
        return "memory binding";
    case Ex2VulkanA1NativePhase::MemoryMapping:
        return "memory mapping";
    case Ex2VulkanA1NativePhase::DescriptorCreation:
        return "descriptor creation";
    case Ex2VulkanA1NativePhase::PipelineCreation:
        return "pipeline creation";
    case Ex2VulkanA1NativePhase::CommandCreation:
        return "command creation";
    case Ex2VulkanA1NativePhase::Upload:
        return "upload";
    case Ex2VulkanA1NativePhase::Preparation:
        return "preparation";
    case Ex2VulkanA1NativePhase::Submission:
        return "submission";
    case Ex2VulkanA1NativePhase::CompletionWait:
        return "completion wait";
    case Ex2VulkanA1NativePhase::OutputReadback:
        return "output readback";
    case Ex2VulkanA1NativePhase::InputDiagnosticReadback:
        return "input diagnostic readback";
    }
    return "invalid";
}

Ex2VulkanA1NativeError::Ex2VulkanA1NativeError(
    Ex2VulkanA1NativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanA1NativePhase Ex2VulkanA1NativeError::Phase() const noexcept
{
    return phase_;
}

VkResult Ex2VulkanA1NativeError::NativeResult() const noexcept
{
    return nativeResult_;
}

const std::string& Ex2VulkanA1NativeError::Operation() const noexcept
{
    return operation_;
}

detail::Ex2VulkanA1DispatchShape detail::ValidateEx2VulkanA1DispatchShape(
    const ex2::LinearConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize)
{
    const auto semanticConfiguration = ex2::MakeConfiguration(configuration);
    if (!ex2::ValidateSemanticConfiguration(semanticConfiguration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != ex2::LinearVariant::A1)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 operation requires the A1 linear variant");
    }

    const VkDeviceSize bytes = CalculateEx2VulkanA1BufferByteCount(
        configuration.elementCount, maximumBufferSize);
    if (bytes > limits.maxStorageBufferRange)
    {
        throw std::length_error(
            "EX-2 Vulkan A1 buffer exceeds maxStorageBufferRange");
    }
    if (limits.maxPushConstantsSize < sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the A1 element-count push constant");
    }
    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (limits.maxComputeWorkGroupInvocations < Ex2VulkanA1LocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < Ex2VulkanA1LocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1U ||
        limits.maxComputeWorkGroupSize[2] < 1U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot execute the EX-2 A1 256 x 1 x 1 workgroup");
    }

    const std::uint64_t groupCount =
        configuration.elementCount / Ex2VulkanA1LocalSizeX +
        (configuration.elementCount % Ex2VulkanA1LocalSizeX == 0U
            ? 0U
            : 1U);
    if (groupCount > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1U ||
        limits.maxComputeWorkGroupCount[2] < 1U ||
        groupCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 element count exceeds maxComputeWorkGroupCount");
    }
    return {
        static_cast<std::uint32_t>(groupCount),
        Ex2VulkanA1LocalSizeX};
}

VkDeviceSize detail::CalculateEx2VulkanA1BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 element count exceeds uint32 logical indexing");
    }
    if (elementCount >
        maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 Vulkan A1 buffer byte count exceeds the supported size");
    }
    return static_cast<VkDeviceSize>(elementCount) * sizeof(std::uint32_t);
}

std::uint32_t detail::SelectEx2VulkanA1Queue(
    std::span<const VkQueueFamilyProperties> families)
{
    for (std::size_t index = 0U; index < families.size(); ++index)
    {
        if (families[index].queueCount != 0U &&
            (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U)
        {
            // Vulkan requires graphics- and compute-capable queues to support
            // transfer operations even if VK_QUEUE_TRANSFER_BIT is omitted.
            return static_cast<std::uint32_t>(index);
        }
    }
    throw std::invalid_argument(
        "selected Vulkan device has no compute-capable queue");
}

std::uint32_t detail::SelectEx2VulkanA1MemoryType(
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
        "EX-2 Vulkan A1 found no compatible memory type");
}

detail::Ex2VulkanA1MappedRange detail::AlignEx2VulkanA1NoncoherentRange(
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
            "EX-2 Vulkan A1 mapped range exceeds its allocation");
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
                "EX-2 Vulkan A1 noncoherent range alignment overflow");
        }
        alignedEnd += increment;
    }
    if (alignedEnd >= allocationSize)
    {
        return {alignedOffset, VK_WHOLE_SIZE};
    }
    return {alignedOffset, alignedEnd - alignedOffset};
}

struct Ex2VulkanA1Operation::Resources
{
    struct Buffer
    {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize allocationSize{};
        std::uint32_t memoryTypeIndex{};
        VkMemoryPropertyFlags memoryFlags{};
        void* mapped{};
    } input, output, upload, readback;

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer uploadCommands{VK_NULL_HANDLE};
    VkCommandBuffer computeCommands{VK_NULL_HANDLE};
    VkCommandBuffer outputReadbackCommands{VK_NULL_HANDLE};
    VkCommandBuffer inputReadbackCommands{VK_NULL_HANDLE};
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
    ex2::LinearConfiguration configuration{};
    detail::Ex2VulkanA1DispatchShape dispatchShape{};
    VkDeviceSize logicalBufferBytes{};
    VkDeviceSize storageBufferBytes{};
    Ex2VulkanA1Diagnostics diagnostics{};
    OperationState state{OperationState::Empty};
    bool computePending{};
    bool transferPending{};
    bool lastCompletionExecutedShader{};

    ~Resources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            const auto disposition =
                detail::ClassifyEx2VulkanA1ResourceDisposition(
                    state == OperationState::CompletionUncertain,
                    computePending,
                    transferPending);
            if (disposition ==
                detail::Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown)
            {
                // A timeout or other uncertain completion does not cancel work.
                // Retain all native objects until process teardown rather than
                // freeing resources the device may still reference.
                return;
            }
            if (disposition ==
                detail::Ex2VulkanA1ResourceDisposition::DrainComputeThenDestroy)
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
                std::string{"EX-2 Vulkan A1 "} + operation +
                " is invalid while work is pending");
        }
        if (state == OperationState::CompletionUncertain)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan A1 "} + operation +
                " is unavailable after uncertain completion");
        }
        if (state == OperationState::Failed)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan A1 "} + operation +
                " is unavailable after a native failure");
        }
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 Vulkan A1";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        CheckVulkan(
            vkCreateInstance(&instanceInfo, nullptr, &instance),
            Ex2VulkanA1NativePhase::InstanceCreation,
            "vkCreateInstance for EX-2 Vulkan A1");

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            Ex2VulkanA1NativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices count for EX-2 Vulkan A1");
        if (physicalDeviceIndex >= count)
        {
            throw std::invalid_argument(
                "requested Vulkan physical-device index is unavailable");
        }
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            Ex2VulkanA1NativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices data for EX-2 Vulkan A1");
        if (physicalDeviceIndex >= count)
        {
            throw std::runtime_error(
                "Vulkan physical-device enumeration changed during EX-2 A1 setup");
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
            detail::SelectEx2VulkanA1Queue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];

        auto dispatchConfiguration = configuration;
        dispatchConfiguration.variant = ex2::LinearVariant::A1;
        dispatchShape = detail::ValidateEx2VulkanA1DispatchShape(
            dispatchConfiguration,
            diagnostics.properties.limits,
            maximumBufferSize);
        if (diagnostics.properties.limits.maxMemoryAllocationCount < 4U)
        {
            throw std::invalid_argument(
                "selected Vulkan device cannot support four A1 buffer allocations");
        }

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
            Ex2VulkanA1NativePhase::LogicalDeviceCreation,
            "vkCreateDevice for EX-2 Vulkan A1");
        diagnostics.synchronization2Enabled = true;
        vkGetDeviceQueue(
            device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "vkGetDeviceQueue returned a null EX-2 Vulkan A1 queue");
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
            Ex2VulkanA1NativePhase::BufferCreation,
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
        buffer.memoryTypeIndex = detail::SelectEx2VulkanA1MemoryType(
            requirements.memoryTypeBits,
            required,
            preferred,
            memoryProperties);
        const std::uint32_t heapIndex =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].heapIndex;
        if (requirements.size >
            memoryProperties.memoryHeaps[heapIndex].size -
                plannedHeapBytes[heapIndex])
        {
            throw std::length_error(
                std::string{name} +
                " memory requirements exceed the selected Vulkan heap");
        }
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
            Ex2VulkanA1NativePhase::MemoryAllocation,
            name);
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            Ex2VulkanA1NativePhase::MemoryBinding,
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
                Ex2VulkanA1NativePhase::MemoryMapping,
                name);
        }
    }

    void MaintainHostCache(
        const Buffer& buffer,
        VkDeviceSize bytes,
        bool flush,
        Ex2VulkanA1NativePhase phase)
    {
        if (bytes == 0U ||
            (buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U)
        {
            return;
        }
        const auto aligned = detail::AlignEx2VulkanA1NoncoherentRange(
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
                ? "vkFlushMappedMemoryRanges for EX-2 Vulkan A1 upload"
                : "vkInvalidateMappedMemoryRanges for EX-2 Vulkan A1 readback");
    }

    void SetupPipeline(const std::vector<std::uint32_t>& spirv)
    {
        std::array<VkDescriptorSetLayoutBinding, 2U> bindings{};
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
            Ex2VulkanA1NativePhase::DescriptorCreation,
            "vkCreateDescriptorSetLayout for EX-2 Vulkan A1");

        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2U};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1U;
        poolInfo.poolSizeCount = 1U;
        poolInfo.pPoolSizes = &poolSize;
        CheckVulkan(
            vkCreateDescriptorPool(
                device, &poolInfo, nullptr, &descriptorPool),
            Ex2VulkanA1NativePhase::DescriptorCreation,
            "vkCreateDescriptorPool for EX-2 Vulkan A1");

        VkDescriptorSetAllocateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1U;
        setInfo.pSetLayouts = &descriptorLayout;
        CheckVulkan(
            vkAllocateDescriptorSets(device, &setInfo, &descriptorSet),
            Ex2VulkanA1NativePhase::DescriptorCreation,
            "vkAllocateDescriptorSets for EX-2 Vulkan A1");

        const std::array<VkDescriptorBufferInfo, 2U> buffers{{
            {input.handle, 0U, storageBufferBytes},
            {output.handle, 0U, storageBufferBytes}}};
        std::array<VkWriteDescriptorSet, 2U> writes{};
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
            Ex2VulkanA1NativePhase::PipelineCreation,
            "vkCreatePipelineLayout for EX-2 Vulkan A1");

        VkShaderModuleCreateInfo shaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = spirv.data();
        CheckVulkan(
            vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
            Ex2VulkanA1NativePhase::PipelineCreation,
            "vkCreateShaderModule for EX-2 Vulkan A1");

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
            Ex2VulkanA1NativePhase::PipelineCreation,
            "vkCreateComputePipelines for EX-2 Vulkan A1");
    }

    void RecordTransferCommands()
    {
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkCreateCommandPool for EX-2 Vulkan A1");

        std::array<VkCommandBuffer, 4U> commands{};
        VkCommandBufferAllocateInfo allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount =
            static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(
                device, &allocation, commands.data()),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkAllocateCommandBuffers for EX-2 Vulkan A1");
        uploadCommands = commands[0];
        computeCommands = commands[1];
        outputReadbackCommands = commands[2];
        inputReadbackCommands = commands[3];

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &computeFence),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan A1 compute");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &transferFence),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan A1 transfer");

        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(uploadCommands, &begin),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkBeginCommandBuffer for EX-2 Vulkan A1 upload");
        BufferDependency(
            uploadCommands,
            upload.handle,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            uploadCommands,
            input.handle,
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
                uploadCommands,
                upload.handle,
                input.handle,
                1U,
                &copy);
        }
        BufferDependency(
            uploadCommands,
            input.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(uploadCommands),
            Ex2VulkanA1NativePhase::CommandCreation,
            "vkEndCommandBuffer for EX-2 Vulkan A1 upload");

        RecordReadback(
            outputReadbackCommands,
            output.handle,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            "output");
        RecordReadback(
            inputReadbackCommands,
            input.handle,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            "input diagnostic");
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
            Ex2VulkanA1NativePhase::CommandCreation,
            name);
        BufferDependency(
            commands,
            source,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            shaderAccess,
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
            vkCmdCopyBuffer(
                commands, source, readback.handle, 1U, &copy);
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
            Ex2VulkanA1NativePhase::CommandCreation,
            name);
    }

    void RecordCompute()
    {
        CheckVulkan(
            vkResetCommandBuffer(computeCommands, 0U),
            Ex2VulkanA1NativePhase::Preparation,
            "vkResetCommandBuffer for EX-2 Vulkan A1");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(computeCommands, &begin),
            Ex2VulkanA1NativePhase::Preparation,
            "vkBeginCommandBuffer for EX-2 Vulkan A1 compute");
        if (configuration.elementCount != 0U)
        {
            BufferDependency(
                computeCommands,
                output.handle,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
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
            Ex2VulkanA1NativePhase::Preparation,
            "vkEndCommandBuffer for EX-2 Vulkan A1 compute");
    }

    void Submit(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending,
        Ex2VulkanA1NativePhase phase,
        const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(
            queue, 1U, &submission, fence);
        switch (detail::ClassifyEx2VulkanA1SubmissionResult(result))
        {
        case detail::Ex2VulkanA1SubmissionDisposition::Submitted:
            pending = true;
            return;
        case detail::Ex2VulkanA1SubmissionDisposition::FailedWithoutSubmission:
            // Vulkan guarantees that the command buffers, fence and semaphore
            // payloads are unaffected for these allocation failures. Preserve
            // any independently pending flag and fail this operation terminally.
            state = OperationState::Failed;
            ThrowVulkanError(result, phase, operation);
        case detail::Ex2VulkanA1SubmissionDisposition::CompletionUncertain:
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(result, phase, operation);
        }
        std::terminate();
    }

    void Transfer(
        VkCommandBuffer commands,
        Ex2VulkanA1NativePhase phase,
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
                "vkResetFences for EX-2 Vulkan A1 transfer");
        }
        Submit(commands, transferFence, transferPending, phase, operation);
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
                "vkWaitForFences for EX-2 Vulkan A1 transfer");
        }
        transferPending = false;
    }

    std::vector<std::uint32_t> ReadBack(
        VkCommandBuffer commands,
        Ex2VulkanA1NativePhase phase,
        const char* operation)
    {
        RequireReusable(operation);
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan A1 "} + operation +
                " requires explicit successful completion");
        }
        state = OperationState::Failed;
        Transfer(commands, phase, operation);
        std::vector<std::uint32_t> result(
            static_cast<std::size_t>(configuration.elementCount));
        if (!result.empty())
        {
            MaintainHostCache(readback, logicalBufferBytes, false, phase);
            std::memcpy(
                result.data(),
                readback.mapped,
                static_cast<std::size_t>(logicalBufferBytes));
        }
        state = OperationState::Complete;
        return result;
    }
};

Ex2VulkanA1Operation::Ex2VulkanA1Operation(
    const ex2::LinearConfiguration& configuration,
    const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex)
    : Ex2VulkanA1Operation{
          configuration,
          spirvPath,
          physicalDeviceIndex,
          ex2::LinearVariant::A1}
{
}

Ex2VulkanA1Operation::Ex2VulkanA1Operation(
    const ex2::LinearConfiguration& configuration,
    const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex,
    ex2::LinearVariant requiredVariant)
    : resources_{std::make_unique<Resources>()}
{
    const auto semanticConfiguration = ex2::MakeConfiguration(configuration);
    if (!ex2::ValidateSemanticConfiguration(semanticConfiguration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != requiredVariant)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 operation requires the A1 linear variant");
    }
    auto& resources = *resources_;
    resources.configuration = configuration;
    resources.logicalBufferBytes =
        detail::CalculateEx2VulkanA1BufferByteCount(
            configuration.elementCount,
            std::numeric_limits<VkDeviceSize>::max());
    if (configuration.elementCount >
        std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 Vulkan A1 output exceeds the host vector maximum");
    }
    const auto spirv = ReadSpirv(spirvPath);
    resources.SetupDevice(physicalDeviceIndex);
    resources.storageBufferBytes = std::max<VkDeviceSize>(
        resources.logicalBufferBytes,
        sizeof(std::uint32_t));

    resources.CreateBuffer(
        resources.input,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan A1 input buffer");
    resources.CreateBuffer(
        resources.output,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan A1 output buffer");
    resources.CreateBuffer(
        resources.upload,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "EX-2 Vulkan A1 upload buffer");
    resources.CreateBuffer(
        resources.readback,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        "EX-2 Vulkan A1 readback buffer");

    resources.diagnostics.inputMemoryFlags =
        resources.input.memoryFlags;
    resources.diagnostics.outputMemoryFlags =
        resources.output.memoryFlags;
    resources.diagnostics.uploadMemoryFlags =
        resources.upload.memoryFlags;
    resources.diagnostics.readbackMemoryFlags =
        resources.readback.memoryFlags;
    resources.SetupPipeline(spirv);
    resources.RecordTransferCommands();
}

Ex2VulkanA1Operation::~Ex2VulkanA1Operation() noexcept = default;

std::uint64_t Ex2VulkanA1Operation::ElementCount() const noexcept
{
    return resources_->configuration.elementCount;
}

const std::array<std::uint8_t, VK_UUID_SIZE>&
Ex2VulkanA1Operation::SelectedDeviceUuid() const noexcept
{
    return resources_->diagnostics.deviceUuid;
}

const Ex2VulkanA1Diagnostics&
Ex2VulkanA1Operation::Diagnostics() const noexcept
{
    return resources_->diagnostics;
}

void Ex2VulkanA1Operation::Upload(
    std::span<const std::uint32_t> input)
{
    auto& resources = *resources_;
    resources.RequireReusable("upload");
    if (resources.state != OperationState::Empty &&
        resources.state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 upload requires initial or completed state");
    }
    if (input.size() != resources.configuration.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A1 upload length differs from the configured element count");
    }
    resources.state = OperationState::Failed;
    if (!input.empty())
    {
        std::memcpy(
            resources.upload.mapped,
            input.data(),
            input.size_bytes());
        resources.MaintainHostCache(
            resources.upload,
            resources.logicalBufferBytes,
            true,
            Ex2VulkanA1NativePhase::Upload);
    }
    resources.Transfer(
        resources.uploadCommands,
        Ex2VulkanA1NativePhase::Upload,
        "vkQueueSubmit2 for EX-2 Vulkan A1 upload");
    resources.state = OperationState::Uploaded;
    resources.lastCompletionExecutedShader = false;
}

void Ex2VulkanA1Operation::Prepare()
{
    auto& resources = *resources_;
    resources.RequireReusable("preparation");
    if (resources.state != OperationState::Uploaded)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 preparation requires a completed upload");
    }
    resources.state = OperationState::Failed;
    CheckVulkan(
        vkResetFences(
            resources.device, 1U, &resources.computeFence),
        Ex2VulkanA1NativePhase::Preparation,
        "vkResetFences for EX-2 Vulkan A1 compute");
    resources.RecordCompute();
    resources.state = OperationState::Prepared;
}

void Ex2VulkanA1Operation::SubmitA1()
{
    auto& resources = *resources_;
    resources.RequireReusable("submission");
    if (resources.state != OperationState::Prepared)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 submission requires prepared commands");
    }
    resources.Submit(
        resources.computeCommands,
        resources.computeFence,
        resources.computePending,
        Ex2VulkanA1NativePhase::Submission,
        "vkQueueSubmit2 for EX-2 Vulkan A1 compute");
    resources.state = OperationState::Submitted;
}

void Ex2VulkanA1Operation::WaitForCompletion(
    std::uint64_t timeoutNanoseconds)
{
    auto& resources = *resources_;
    if (resources.state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 completion cannot be retried after an uncertain result");
    }
    if (resources.state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 completion wait requires submitted work");
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
            Ex2VulkanA1NativePhase::CompletionWait,
            "vkWaitForFences for EX-2 Vulkan A1 compute");
    }
    resources.computePending = false;
    resources.state = OperationState::Complete;
    resources.lastCompletionExecutedShader =
        resources.configuration.elementCount != 0U;
}

std::vector<std::uint32_t> Ex2VulkanA1Operation::RetrieveOutput()
{
    return resources_->ReadBack(
        resources_->outputReadbackCommands,
        Ex2VulkanA1NativePhase::OutputReadback,
        "output readback");
}

std::vector<std::uint32_t> Ex2VulkanA1Operation::RetrieveDeviceInput()
{
    return resources_->ReadBack(
        resources_->inputReadbackCommands,
        Ex2VulkanA1NativePhase::InputDiagnosticReadback,
        "input diagnostic readback");
}

bool Ex2VulkanA1Operation::LastCompletionExecutedShader() const
{
    if (resources_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan A1 dispatch status requires successful completion");
    }
    return resources_->lastCompletionExecutedShader;
}

} // namespace computelab::vulkan
