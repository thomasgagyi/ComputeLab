#include "vulkan/Ex2VulkanC.hpp"

#include "ex2/Ex2ContentionTargets.hpp"

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

using OperationState = detail::Ex2VulkanCOperationState;

[[noreturn]] void ThrowVulkanError(
    VkResult result,
    Ex2VulkanCNativePhase phase,
    std::string operation)
{
    throw Ex2VulkanCNativeError{
        phase,
        result,
        operation,
        operation + " failed during " + std::string{ToString(phase)} +
            " with VkResult " + std::to_string(static_cast<int>(result))};
}

void CheckVulkan(
    VkResult result,
    Ex2VulkanCNativePhase phase,
    const char* operation)
{
    if (result != VK_SUCCESS)
    {
        ThrowVulkanError(result, phase, operation);
    }
}

std::vector<std::uint32_t> ReadSpirv(
    const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        throw std::runtime_error(
            "EX-2 Vulkan C cannot open SPIR-V: " + path.string());
    }
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
    {
        throw std::runtime_error(
            "EX-2 Vulkan C SPIR-V has an invalid byte length: " +
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
            "EX-2 Vulkan C SPIR-V data is malformed: " + path.string());
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

std::string_view ToString(Ex2VulkanCNativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2VulkanCNativePhase::InstanceCreation: return "instance creation";
    case Ex2VulkanCNativePhase::DeviceEnumeration: return "device enumeration";
    case Ex2VulkanCNativePhase::DeviceSelection: return "device selection";
    case Ex2VulkanCNativePhase::DeviceProperties: return "device properties";
    case Ex2VulkanCNativePhase::QueueSelection: return "queue selection";
    case Ex2VulkanCNativePhase::LogicalDeviceCreation:
        return "logical device creation";
    case Ex2VulkanCNativePhase::ResourcePreflight: return "resource preflight";
    case Ex2VulkanCNativePhase::BufferCreation: return "buffer creation";
    case Ex2VulkanCNativePhase::MemoryAllocation: return "memory allocation";
    case Ex2VulkanCNativePhase::MemoryBinding: return "memory binding";
    case Ex2VulkanCNativePhase::MemoryMapping: return "memory mapping";
    case Ex2VulkanCNativePhase::DescriptorCreation: return "descriptor creation";
    case Ex2VulkanCNativePhase::PipelineConstruction:
        return "pipeline construction";
    case Ex2VulkanCNativePhase::CommandCreation: return "command creation";
    case Ex2VulkanCNativePhase::TargetUpload: return "target upload";
    case Ex2VulkanCNativePhase::CounterReset: return "counter reset";
    case Ex2VulkanCNativePhase::ResetCompletion: return "reset completion";
    case Ex2VulkanCNativePhase::Submission: return "submission";
    case Ex2VulkanCNativePhase::CompletionWait: return "completion wait";
    case Ex2VulkanCNativePhase::CounterReadback: return "counter readback";
    case Ex2VulkanCNativePhase::TargetDiagnosticReadback:
        return "target diagnostic readback";
    }
    return "invalid";
}

Ex2VulkanCNativeError::Ex2VulkanCNativeError(
    Ex2VulkanCNativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanCNativePhase Ex2VulkanCNativeError::Phase() const noexcept
{
    return phase_;
}

VkResult Ex2VulkanCNativeError::NativeResult() const noexcept
{
    return nativeResult_;
}

const std::string& Ex2VulkanCNativeError::Operation() const noexcept
{
    return operation_;
}

void detail::ValidateEx2VulkanCConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    if (!ex2::ValidateSemanticConfiguration(configuration).IsValid() ||
        !std::holds_alternative<ex2::ContentionConfiguration>(
            configuration.parameters))
    {
        throw std::invalid_argument(
            "EX-2 Vulkan C configuration violates the I2-C contention semantic contract");
    }
}

detail::Ex2VulkanCDispatchShape detail::ValidateEx2VulkanCDispatchShape(
    const ex2::ContentionConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize)
{
    ValidateEx2VulkanCConfiguration(ex2::MakeConfiguration(configuration));
    const VkDeviceSize bytes = CalculateEx2VulkanCBufferByteCount(
        configuration.elementCount, maximumBufferSize);
    if (bytes > limits.maxStorageBufferRange)
    {
        throw std::length_error(
            "EX-2 Vulkan C logical buffer exceeds maxStorageBufferRange");
    }
    if (limits.maxPushConstantsSize < sizeof(std::uint32_t))
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the C element-count push constant");
    }
    if (limits.maxPerStageDescriptorStorageBuffers < 2U ||
        limits.maxDescriptorSetStorageBuffers < 2U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the two C storage buffers");
    }
    if (configuration.elementCount == 0U)
    {
        return {};
    }
    if (limits.maxComputeWorkGroupInvocations < Ex2VulkanCLocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < Ex2VulkanCLocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1U ||
        limits.maxComputeWorkGroupSize[2] < 1U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot execute the EX-2 C 256 x 1 x 1 workgroup");
    }

    const std::uint64_t groupCount =
        configuration.elementCount / Ex2VulkanCLocalSizeX +
        (configuration.elementCount % Ex2VulkanCLocalSizeX == 0U ? 0U : 1U);
    if (groupCount > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1U ||
        limits.maxComputeWorkGroupCount[2] < 1U ||
        groupCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan C element count exceeds maxComputeWorkGroupCount");
    }
    return {static_cast<std::uint32_t>(groupCount), Ex2VulkanCLocalSizeX};
}

VkDeviceSize detail::CalculateEx2VulkanCBufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan C element count exceeds uint32 logical indexing");
    }
    if (elementCount > maximumAddressableByteCount / sizeof(std::uint32_t))
    {
        throw std::length_error(
            "EX-2 Vulkan C buffer byte count exceeds the supported size");
    }
    return static_cast<VkDeviceSize>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2VulkanCAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount)
{
    if (requiredAllocationCount > maximumAllocationCount)
    {
        throw std::length_error(
            "selected Vulkan device cannot support the required EX-2 C buffer allocations");
    }
}

void detail::ValidateEx2VulkanCHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize)
{
    if (plannedHeapBytes > heapSize || requirementSize > heapSize - plannedHeapBytes)
    {
        throw std::length_error(
            "EX-2 Vulkan C memory requirements exceed the selected Vulkan heap");
    }
}

std::uint32_t detail::SelectEx2VulkanCQueue(
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

std::uint32_t detail::SelectEx2VulkanCMemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties)
{
    for (unsigned pass = 0U; pass < 2U; ++pass)
    {
        const VkMemoryPropertyFlags requested =
            required | (pass == 0U ? preferred : 0U);
        for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index)
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
        "EX-2 Vulkan C found no compatible memory type");
}

detail::Ex2VulkanCMappedRange detail::AlignEx2VulkanCNoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize)
{
    if (nonCoherentAtomSize == 0U)
        throw std::invalid_argument("Vulkan nonCoherentAtomSize must be positive");
    if (offset > allocationSize || size > allocationSize - offset)
        throw std::out_of_range("EX-2 Vulkan C mapped range exceeds its allocation");
    if (size == 0U)
        return {offset, 0U};

    const VkDeviceSize alignedOffset = offset - (offset % nonCoherentAtomSize);
    const VkDeviceSize end = offset + size;
    if (end == allocationSize)
        return {alignedOffset, VK_WHOLE_SIZE};
    const VkDeviceSize remainder = end % nonCoherentAtomSize;
    VkDeviceSize alignedEnd = end;
    if (remainder != 0U)
    {
        const VkDeviceSize increment = nonCoherentAtomSize - remainder;
        if (end > std::numeric_limits<VkDeviceSize>::max() - increment)
            throw std::overflow_error("EX-2 Vulkan C noncoherent range alignment overflow");
        alignedEnd += increment;
    }
    if (alignedEnd >= allocationSize)
        return {alignedOffset, VK_WHOLE_SIZE};
    return {alignedOffset, alignedEnd - alignedOffset};
}

struct Ex2VulkanCOperation::Resources
{
    struct Buffer
    {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize allocationSize{};
        std::uint32_t memoryTypeIndex{};
        VkMemoryPropertyFlags memoryFlags{};
        void* mapped{};
    } targets, counters, upload, readback;

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer uploadCommands{VK_NULL_HANDLE};
    VkCommandBuffer resetCommands{VK_NULL_HANDLE};
    VkCommandBuffer atomicCommands{VK_NULL_HANDLE};
    VkCommandBuffer counterReadbackCommands{VK_NULL_HANDLE};
    VkCommandBuffer targetReadbackCommands{VK_NULL_HANDLE};
    VkFence atomicFence{VK_NULL_HANDLE};
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
    ex2::ContentionConfiguration configuration{};
    detail::Ex2VulkanCDispatchShape dispatchShape{};
    VkDeviceSize logicalBufferBytes{};
    VkDeviceSize storageBufferBytes{};
    Ex2VulkanCDiagnostics diagnostics{};
    std::string loadedShaderName{"Ex2C.comp.spv"};
    std::vector<std::uint32_t> loadedSpirv;
    OperationState state{OperationState::Empty};
    bool atomicPending{};
    bool transferPending{};
    bool lastCompletionExecutedShader{};

    ~Resources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            const auto disposition = detail::ClassifyEx2VulkanCResourceDisposition(
                state == OperationState::CompletionUncertain,
                atomicPending,
                transferPending);
            if (disposition == detail::Ex2VulkanCResourceDisposition::PreserveForProcessTeardown)
                return;
            if (disposition == detail::Ex2VulkanCResourceDisposition::DrainAtomicThenDestroy)
            {
                const VkResult result = vkWaitForFences(
                    device, 1U, &atomicFence, VK_TRUE,
                    std::numeric_limits<std::uint64_t>::max());
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    return;
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
            if (atomicFence != VK_NULL_HANDLE)
                vkDestroyFence(device, atomicFence, nullptr);
            if (transferFence != VK_NULL_HANDLE)
                vkDestroyFence(device, transferFence, nullptr);
            DestroyBuffer(readback);
            DestroyBuffer(upload);
            DestroyBuffer(counters);
            DestroyBuffer(targets);
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
        if (detail::IsEx2VulkanCOperationStateReusable(state))
            return;
        const char* reason = state == OperationState::Submitted
            ? " while atomic work is pending"
            : state == OperationState::CompletionUncertain
                ? " after uncertain completion"
                : " after a native failure";
        throw std::logic_error(
            std::string{"EX-2 Vulkan C "} + operation + " is unavailable" + reason);
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 Vulkan C";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        CheckVulkan(
            vkCreateInstance(&instanceInfo, nullptr, &instance),
            Ex2VulkanCNativePhase::InstanceCreation,
            "vkCreateInstance for EX-2 Vulkan C");

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            Ex2VulkanCNativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices count for EX-2 Vulkan C");
        if (physicalDeviceIndex >= count)
            throw std::invalid_argument("requested Vulkan physical-device index is unavailable");
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            Ex2VulkanCNativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices data for EX-2 Vulkan C");
        if (physicalDeviceIndex >= count)
            throw std::runtime_error("Vulkan physical-device enumeration changed during EX-2 C setup");
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
            throw std::invalid_argument("selected Vulkan device does not support core Vulkan 1.3");
        if (storageBufferBytes > maximumBufferSize ||
            storageBufferBytes > diagnostics.properties.limits.maxStorageBufferRange)
        {
            throw std::length_error("EX-2 Vulkan C physical buffer exceeds selected device limits");
        }

        VkPhysicalDeviceVulkan13Features supported13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
        if (supported13.synchronization2 != VK_TRUE)
            throw std::invalid_argument("selected Vulkan device lacks required synchronization2 support");

        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex = detail::SelectEx2VulkanCQueue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];

        dispatchShape = detail::ValidateEx2VulkanCDispatchShape(
            configuration, diagnostics.properties.limits, maximumBufferSize);
        detail::ValidateEx2VulkanCAllocationCount(
            diagnostics.properties.limits.maxMemoryAllocationCount);

        VkPhysicalDeviceVulkan13Features enabled13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled13.synchronization2 = VK_TRUE;
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        queueInfo.queueCount = 1U;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &enabled13;
        deviceInfo.queueCreateInfoCount = 1U;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        CheckVulkan(
            vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
            Ex2VulkanCNativePhase::LogicalDeviceCreation,
            "vkCreateDevice for EX-2 Vulkan C");
        diagnostics.synchronization2Enabled = true;
        vkGetDeviceQueue(device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
            throw std::runtime_error("vkGetDeviceQueue returned a null EX-2 Vulkan C queue");
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
            Ex2VulkanCNativePhase::BufferCreation,
            name);

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        if (requirements.size == 0U || requirements.size > maximumMemoryAllocationSize)
            throw std::length_error(std::string{name} + " memory requirements exceed maxMemoryAllocationSize");
        buffer.memoryTypeIndex = detail::SelectEx2VulkanCMemoryType(
            requirements.memoryTypeBits, required, preferred, memoryProperties);
        const std::uint32_t heapIndex =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].heapIndex;
        detail::ValidateEx2VulkanCHeapFeasibility(
            requirements.size,
            plannedHeapBytes[heapIndex],
            memoryProperties.memoryHeaps[heapIndex].size);
        plannedHeapBytes[heapIndex] += requirements.size;
        buffer.allocationSize = requirements.size;
        buffer.memoryFlags = memoryProperties.memoryTypes[buffer.memoryTypeIndex].propertyFlags;

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = buffer.memoryTypeIndex;
        CheckVulkan(
            vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
            Ex2VulkanCNativePhase::MemoryAllocation,
            name);
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            Ex2VulkanCNativePhase::MemoryBinding,
            name);
        if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U)
        {
            CheckVulkan(
                vkMapMemory(device, buffer.memory, 0U, buffer.allocationSize, 0U, &buffer.mapped),
                Ex2VulkanCNativePhase::MemoryMapping,
                name);
        }
    }

    void MaintainHostCache(
        const Buffer& buffer,
        VkDeviceSize bytes,
        bool flush,
        Ex2VulkanCNativePhase phase)
    {
        if (bytes == 0U ||
            (buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U)
            return;
        const auto aligned = detail::AlignEx2VulkanCNoncoherentRange(
            0U, bytes, buffer.allocationSize,
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
                ? "vkFlushMappedMemoryRanges for EX-2 Vulkan C upload"
                : "vkInvalidateMappedMemoryRanges for EX-2 Vulkan C readback");
    }

    void SetupPipeline()
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
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout),
            Ex2VulkanCNativePhase::DescriptorCreation,
            "vkCreateDescriptorSetLayout for EX-2 Vulkan C");

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2U};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1U;
        poolInfo.poolSizeCount = 1U;
        poolInfo.pPoolSizes = &poolSize;
        CheckVulkan(
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
            Ex2VulkanCNativePhase::DescriptorCreation,
            "vkCreateDescriptorPool for EX-2 Vulkan C");

        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1U;
        setInfo.pSetLayouts = &descriptorLayout;
        CheckVulkan(
            vkAllocateDescriptorSets(device, &setInfo, &descriptorSet),
            Ex2VulkanCNativePhase::DescriptorCreation,
            "vkAllocateDescriptorSets for EX-2 Vulkan C");

        const std::array<VkDescriptorBufferInfo, 2U> buffers{{
            {targets.handle, 0U, storageBufferBytes},
            {counters.handle, 0U, storageBufferBytes}}};
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
            device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0U, nullptr);

        const VkPushConstantRange pushRange{
            VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(std::uint32_t)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1U;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        CheckVulkan(
            vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
            Ex2VulkanCNativePhase::PipelineConstruction,
            "vkCreatePipelineLayout for EX-2 Vulkan C");

        VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = loadedSpirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = loadedSpirv.data();
        CheckVulkan(
            vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
            Ex2VulkanCNativePhase::PipelineConstruction,
            "vkCreateShaderModule for EX-2 Vulkan C");

        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shader;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;
        CheckVulkan(
            vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1U, &pipelineInfo, nullptr, &pipeline),
            Ex2VulkanCNativePhase::PipelineConstruction,
            "vkCreateComputePipelines for EX-2 Vulkan C");
    }

    void RecordUpload()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(uploadCommands, &begin),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkBeginCommandBuffer for EX-2 Vulkan C target upload");
        BufferDependency(
            uploadCommands, upload.handle,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            uploadCommands, targets.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
        {
            const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
            vkCmdCopyBuffer(uploadCommands, upload.handle, targets.handle, 1U, &copy);
        }
        BufferDependency(
            uploadCommands, targets.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(uploadCommands),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkEndCommandBuffer for EX-2 Vulkan C target upload");
    }

    void RecordReset()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(resetCommands, &begin),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkBeginCommandBuffer for EX-2 Vulkan C reset");
        BufferDependency(
            resetCommands, counters.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
            vkCmdFillBuffer(resetCommands, counters.handle, 0U, logicalBufferBytes, 0U);
        BufferDependency(
            resetCommands, counters.handle,
            VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        CheckVulkan(
            vkEndCommandBuffer(resetCommands),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkEndCommandBuffer for EX-2 Vulkan C reset");
    }

    void RecordAtomic()
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(atomicCommands, &begin),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkBeginCommandBuffer for EX-2 Vulkan C atomic dispatch");
        if (configuration.elementCount != 0U)
        {
            BufferDependency(
                atomicCommands, targets.handle,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            BufferDependency(
                atomicCommands, counters.handle,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            vkCmdBindPipeline(atomicCommands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(
                atomicCommands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                0U, 1U, &descriptorSet, 0U, nullptr);
            const std::uint32_t elementCount =
                static_cast<std::uint32_t>(configuration.elementCount);
            vkCmdPushConstants(
                atomicCommands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                0U, sizeof(elementCount), &elementCount);
            vkCmdDispatch(atomicCommands, dispatchShape.groupCountX, 1U, 1U);
        }
        CheckVulkan(
            vkEndCommandBuffer(atomicCommands),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkEndCommandBuffer for EX-2 Vulkan C atomic dispatch");
    }

    void RecordReadback(
        VkCommandBuffer commands,
        VkBuffer source,
        VkPipelineStageFlags2 sourceStage,
        VkAccessFlags2 sourceAccess,
        const char* name)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(commands, &begin),
            Ex2VulkanCNativePhase::CommandCreation,
            name);
        BufferDependency(
            commands, source, sourceStage, sourceAccess,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            commands, readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (logicalBufferBytes != 0U)
        {
            const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
            vkCmdCopyBuffer(commands, source, readback.handle, 1U, &copy);
        }
        BufferDependency(
            commands, readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(commands),
            Ex2VulkanCNativePhase::CommandCreation,
            name);
    }

    void CreateCommands()
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkCreateCommandPool for EX-2 Vulkan C");

        std::array<VkCommandBuffer, 5U> commands{};
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(device, &allocation, commands.data()),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkAllocateCommandBuffers for EX-2 Vulkan C");
        uploadCommands = commands[0];
        resetCommands = commands[1];
        atomicCommands = commands[2];
        counterReadbackCommands = commands[3];
        targetReadbackCommands = commands[4];

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &atomicFence),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan C atomic dispatch");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &transferFence),
            Ex2VulkanCNativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan C transfers");

        RecordUpload();
        RecordReset();
        RecordAtomic();
        RecordReadback(
            counterReadbackCommands, counters.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            "EX-2 Vulkan C counter readback commands");
        RecordReadback(
            targetReadbackCommands, targets.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            "EX-2 Vulkan C target diagnostic readback commands");
    }

    void SubmitCommands(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending,
        Ex2VulkanCNativePhase phase,
        const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(queue, 1U, &submission, fence);
        switch (detail::ClassifyEx2VulkanCSubmissionResult(result))
        {
        case detail::Ex2VulkanCSubmissionDisposition::Submitted:
            pending = true;
            return;
        case detail::Ex2VulkanCSubmissionDisposition::FailedWithoutSubmission:
            state = OperationState::Failed;
            ThrowVulkanError(result, phase, operation);
        case detail::Ex2VulkanCSubmissionDisposition::CompletionUncertain:
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(result, phase, operation);
        }
        std::terminate();
    }

    void Transfer(
        VkCommandBuffer commands,
        Ex2VulkanCNativePhase submissionPhase,
        Ex2VulkanCNativePhase completionPhase,
        const char* operation)
    {
        const VkResult resetResult = vkResetFences(device, 1U, &transferFence);
        if (resetResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                resetResult, submissionPhase,
                "vkResetFences for EX-2 Vulkan C transfer");
        }
        SubmitCommands(
            commands,
            transferFence,
            transferPending,
            submissionPhase,
            operation);
        const VkResult waitResult = vkWaitForFences(
            device, 1U, &transferFence, VK_TRUE, 30'000'000'000ULL);
        if (waitResult != VK_SUCCESS)
        {
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(
                waitResult, completionPhase,
                "vkWaitForFences for EX-2 Vulkan C transfer");
        }
        transferPending = false;
    }

    std::vector<std::uint32_t> ReadBack(
        VkCommandBuffer commands,
        Ex2VulkanCNativePhase phase,
        const char* operation)
    {
        RequireReusable(operation);
        if (state != OperationState::Complete)
        {
            throw std::logic_error(
                std::string{"EX-2 Vulkan C "} + operation +
                " requires explicit successful completion");
        }
        std::vector<std::uint32_t> result(
            static_cast<std::size_t>(configuration.elementCount));
        if (result.empty())
            return result;

        state = OperationState::Failed;
        Transfer(commands, phase, phase, operation);
        MaintainHostCache(readback, logicalBufferBytes, false, phase);
        std::memcpy(
            result.data(), readback.mapped,
            static_cast<std::size_t>(logicalBufferBytes));
        state = OperationState::Complete;
        return result;
    }
};

Ex2VulkanCOperation::Ex2VulkanCOperation(
    const ex2::ContentionConfiguration& configuration,
    const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex)
    : resources_{std::make_unique<Resources>()}
{
    detail::ValidateEx2VulkanCConfiguration(ex2::MakeConfiguration(configuration));
    auto& resources = *resources_;
    resources.configuration = configuration;
    resources.logicalBufferBytes = detail::CalculateEx2VulkanCBufferByteCount(
        configuration.elementCount,
        std::numeric_limits<VkDeviceSize>::max());
    resources.storageBufferBytes = std::max<VkDeviceSize>(
        resources.logicalBufferBytes, sizeof(std::uint32_t));
    if (configuration.elementCount > std::vector<std::uint32_t>{}.max_size())
        throw std::length_error("EX-2 Vulkan C logical buffers exceed the host vector maximum");

    resources.loadedSpirv = ReadSpirv(spirvPath);
    resources.SetupDevice(physicalDeviceIndex);
    resources.CreateBuffer(
        resources.targets,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan C target buffer");
    resources.CreateBuffer(
        resources.counters,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "EX-2 Vulkan C counter buffer");
    resources.CreateBuffer(
        resources.upload,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "EX-2 Vulkan C upload staging buffer");
    resources.CreateBuffer(
        resources.readback,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        "EX-2 Vulkan C readback staging buffer");

    resources.diagnostics.targetMemoryFlags = resources.targets.memoryFlags;
    resources.diagnostics.counterMemoryFlags = resources.counters.memoryFlags;
    resources.diagnostics.uploadMemoryFlags = resources.upload.memoryFlags;
    resources.diagnostics.readbackMemoryFlags = resources.readback.memoryFlags;
    resources.diagnostics.targetMemoryTypeIndex = resources.targets.memoryTypeIndex;
    resources.diagnostics.counterMemoryTypeIndex = resources.counters.memoryTypeIndex;
    resources.diagnostics.uploadMemoryTypeIndex = resources.upload.memoryTypeIndex;
    resources.diagnostics.readbackMemoryTypeIndex = resources.readback.memoryTypeIndex;
    resources.diagnostics.targetAllocationBytes = resources.targets.allocationSize;
    resources.diagnostics.counterAllocationBytes = resources.counters.allocationSize;
    resources.diagnostics.uploadAllocationBytes = resources.upload.allocationSize;
    resources.diagnostics.readbackAllocationBytes = resources.readback.allocationSize;
    resources.SetupPipeline();
    resources.CreateCommands();
}

Ex2VulkanCOperation::~Ex2VulkanCOperation() noexcept = default;

std::uint64_t Ex2VulkanCOperation::ElementCount() const noexcept
{
    return resources_->configuration.elementCount;
}

std::uint64_t Ex2VulkanCOperation::ActiveCounterCount() const noexcept
{
    return resources_->configuration.activeCounterCount;
}

std::uint64_t Ex2VulkanCOperation::AllocatedCounterCount() const noexcept
{
    return resources_->configuration.allocatedCounterCount;
}

const std::array<std::uint8_t, VK_UUID_SIZE>&
Ex2VulkanCOperation::SelectedDeviceUuid() const noexcept
{
    return resources_->diagnostics.deviceUuid;
}

const Ex2VulkanCDiagnostics& Ex2VulkanCOperation::Diagnostics() const noexcept
{
    return resources_->diagnostics;
}

std::string_view Ex2VulkanCOperation::LoadedShaderName() const noexcept
{
    return resources_->loadedShaderName;
}

std::span<const std::uint32_t> Ex2VulkanCOperation::LoadedSpirv() const noexcept
{
    return resources_->loadedSpirv;
}

void Ex2VulkanCOperation::UploadTargets(
    std::span<const std::uint32_t> targets)
{
    auto& resources = *resources_;
    resources.RequireReusable("target upload");
    if (resources.state != OperationState::Empty &&
        resources.state != OperationState::TargetsReady &&
        resources.state != OperationState::ResetReady &&
        resources.state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan C target upload requires a reusable lifecycle state");
    }

    resources.state = OperationState::Empty;
    resources.lastCompletionExecutedShader = false;
    ex2::ValidateContentionTargets(
        targets,
        resources.configuration.elementCount,
        resources.configuration.activeCounterCount);

    resources.state = OperationState::Failed;
    if (!targets.empty())
    {
        std::memcpy(resources.upload.mapped, targets.data(), targets.size_bytes());
        resources.MaintainHostCache(
            resources.upload,
            resources.logicalBufferBytes,
            true,
            Ex2VulkanCNativePhase::TargetUpload);
        resources.Transfer(
            resources.uploadCommands,
            Ex2VulkanCNativePhase::TargetUpload,
            Ex2VulkanCNativePhase::TargetUpload,
            "vkQueueSubmit2 for EX-2 Vulkan C target upload");
    }
    resources.state = OperationState::TargetsReady;
}

void Ex2VulkanCOperation::PrepareReset()
{
    auto& resources = *resources_;
    resources.RequireReusable("counter reset");
    if (resources.state != OperationState::TargetsReady &&
        resources.state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan C reset requires valid uploaded targets");
    }

    resources.state = OperationState::TargetsReady;
    resources.lastCompletionExecutedShader = false;
    const VkResult fenceResult = vkResetFences(
        resources.device, 1U, &resources.atomicFence);
    if (fenceResult != VK_SUCCESS)
    {
        resources.state = OperationState::Failed;
        ThrowVulkanError(
            fenceResult,
            Ex2VulkanCNativePhase::CounterReset,
            "vkResetFences for EX-2 Vulkan C atomic dispatch");
    }
    if (resources.configuration.allocatedCounterCount != 0U)
    {
        resources.state = OperationState::Failed;
        resources.Transfer(
            resources.resetCommands,
            Ex2VulkanCNativePhase::CounterReset,
            Ex2VulkanCNativePhase::ResetCompletion,
            "vkQueueSubmit2 for EX-2 Vulkan C full counter reset");
    }
    resources.state = OperationState::ResetReady;
}

void Ex2VulkanCOperation::SubmitAtomic()
{
    auto& resources = *resources_;
    resources.RequireReusable("atomic submission");
    if (resources.state != OperationState::ResetReady)
    {
        throw std::logic_error(
            "EX-2 Vulkan C submission requires a newly completed full reset");
    }
    resources.SubmitCommands(
        resources.atomicCommands,
        resources.atomicFence,
        resources.atomicPending,
        Ex2VulkanCNativePhase::Submission,
        "vkQueueSubmit2 for EX-2 Vulkan C atomic dispatch");
    resources.state = OperationState::Submitted;
}

void Ex2VulkanCOperation::WaitForCompletion(
    std::uint64_t timeoutNanoseconds)
{
    auto& resources = *resources_;
    if (resources.state == OperationState::CompletionUncertain)
    {
        throw std::logic_error(
            "EX-2 Vulkan C completion cannot be retried after an uncertain result");
    }
    if (resources.state != OperationState::Submitted)
    {
        throw std::logic_error(
            "EX-2 Vulkan C completion wait requires submitted atomic work");
    }
    const VkResult result = vkWaitForFences(
        resources.device,
        1U,
        &resources.atomicFence,
        VK_TRUE,
        timeoutNanoseconds);
    resources.state = detail::ClassifyEx2VulkanCWaitResult(result);
    if (result != VK_SUCCESS)
    {
        ThrowVulkanError(
            result,
            Ex2VulkanCNativePhase::CompletionWait,
            "vkWaitForFences for EX-2 Vulkan C atomic dispatch");
    }
    resources.atomicPending = false;
    resources.lastCompletionExecutedShader =
        resources.configuration.elementCount != 0U;
}

std::vector<std::uint32_t> Ex2VulkanCOperation::RetrieveCounters()
{
    return resources_->ReadBack(
        resources_->counterReadbackCommands,
        Ex2VulkanCNativePhase::CounterReadback,
        "counter readback");
}

std::vector<std::uint32_t> Ex2VulkanCOperation::RetrieveDeviceTargets()
{
    return resources_->ReadBack(
        resources_->targetReadbackCommands,
        Ex2VulkanCNativePhase::TargetDiagnosticReadback,
        "target diagnostic readback");
}

bool Ex2VulkanCOperation::LastCompletionExecutedShader() const
{
    if (resources_->state != OperationState::Complete)
    {
        throw std::logic_error(
            "EX-2 Vulkan C dispatch status requires successful completion");
    }
    return resources_->lastCompletionExecutedShader;
}

} // namespace computelab::vulkan
