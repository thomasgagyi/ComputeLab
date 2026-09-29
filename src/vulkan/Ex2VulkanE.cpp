#include "vulkan/Ex2VulkanE.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

namespace computelab::vulkan
{
namespace
{

using OperationState = detail::Ex2VulkanEOperationState;

VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U)
    {
        auto* diagnostics = static_cast<Ex2VulkanEDiagnostics*>(userData);
        ++diagnostics->validationErrorCount;
        std::fprintf(
            stderr,
            "EX-2 Vulkan E validation error: %s\n",
            callbackData != nullptr && callbackData->pMessage != nullptr
                ? callbackData->pMessage
                : "unknown validation message");
    }
    return VK_FALSE;
}

[[noreturn]] void ThrowVulkanError(
    VkResult result,
    Ex2VulkanENativePhase phase,
    std::string operation)
{
    throw Ex2VulkanENativeError{
        phase,
        result,
        operation,
        operation + " failed during " + std::string{ToString(phase)} +
            " with VkResult " + std::to_string(static_cast<int>(result))};
}

void CheckVulkan(
    VkResult result,
    Ex2VulkanENativePhase phase,
    const char* operation)
{
    if (result != VK_SUCCESS)
        ThrowVulkanError(result, phase, operation);
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

class EResources
{
public:
    struct Buffer
    {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize allocationSize{};
        std::uint32_t memoryTypeIndex{};
        VkMemoryPropertyFlags memoryFlags{};
        void* mapped{};
    } upload, deviceBuffer, readback;

    EResources(
        const ex2::TransferConfiguration& requestedConfiguration,
        bool enableTiming)
        : configuration{requestedConfiguration},
          timingEnabled{enableTiming}
    {
    }

    void Initialize(std::uint32_t physicalDeviceIndex)
    {
        detail::ValidateEx2VulkanEConfiguration(
            ex2::MakeConfiguration(configuration));
        logicalByteCount = detail::CalculateEx2VulkanEByteCount(
            configuration.byteCount,
            std::numeric_limits<VkDeviceSize>::max(),
            std::vector<std::uint8_t>{}.max_size());
        SetupDevice(physicalDeviceIndex);
        if (logicalByteCount != 0U)
        {
            CreateBuffer(
                upload,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "EX-2 Vulkan E upload staging buffer");
            CreateBuffer(
                deviceBuffer,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0U,
                "EX-2 Vulkan E device-local working buffer");
            CreateBuffer(
                readback,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                "EX-2 Vulkan E readback staging buffer");
            PopulateMemoryDiagnostics();
        }
        CreateCommands();
        if (timingEnabled)
            CreateQueryPool();
        RecordPreparedNamedCommand();
    }

    ~EResources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            const bool submittedOrUncertain =
                state == OperationState::Submitted ||
                state == OperationState::CompletionUncertain;
            if (detail::ClassifyEx2VulkanEResourceDisposition(
                    submittedOrUncertain,
                    auxiliaryTransferPending) ==
                detail::Ex2VulkanEResourceDisposition::PreserveForProcessTeardown)
            {
                return;
            }
            if (commandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, commandPool, nullptr);
            if (queryPool != VK_NULL_HANDLE)
                vkDestroyQueryPool(device, queryPool, nullptr);
            if (namedFence != VK_NULL_HANDLE)
                vkDestroyFence(device, namedFence, nullptr);
            if (auxiliaryFence != VK_NULL_HANDLE)
                vkDestroyFence(device, auxiliaryFence, nullptr);
            DestroyBuffer(readback);
            DestroyBuffer(deviceBuffer);
            DestroyBuffer(upload);
            vkDestroyDevice(device, nullptr);
        }
        if (debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        {
            const auto destroyMessenger =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(
                        instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger != nullptr)
                destroyMessenger(instance, debugMessenger, nullptr);
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
        if (detail::IsEx2VulkanEOperationStateReusable(state))
            return;
        const char* reason = state == OperationState::Submitted
            ? " while transfer work is pending"
            : state == OperationState::CompletionUncertain
                ? " after uncertain completion"
                : " after a native failure";
        throw std::logic_error(
            std::string{"EX-2 Vulkan E "} + operation +
            " is unavailable" + reason);
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        const char* validationMode =
            std::getenv("COMPUTELAB_EX2_E_VALIDATION");
        diagnostics.validationEnabled = validationMode != nullptr;
        diagnostics.synchronizationValidationEnabled =
            validationMode != nullptr &&
            std::string_view{validationMode} == "sync";
        if (validationMode != nullptr &&
            std::string_view{validationMode} != "standard" &&
            std::string_view{validationMode} != "sync")
        {
            throw std::invalid_argument(
                "COMPUTELAB_EX2_E_VALIDATION must be 'standard' or 'sync'");
        }

        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 Vulkan E";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkValidationFeatureEnableEXT synchronizationFeature =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validationFeatures{
            VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        if (diagnostics.synchronizationValidationEnabled)
        {
            validationFeatures.enabledValidationFeatureCount = 1U;
            validationFeatures.pEnabledValidationFeatures =
                &synchronizationFeature;
        }
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = ValidationCallback;
        debugInfo.pUserData = &diagnostics;
        if (diagnostics.validationEnabled)
        {
            debugInfo.pNext = diagnostics.synchronizationValidationEnabled
                ? &validationFeatures
                : nullptr;
            instanceInfo.pNext = &debugInfo;
            instanceInfo.enabledLayerCount = 1U;
            instanceInfo.ppEnabledLayerNames = &validationLayer;
            instanceInfo.enabledExtensionCount = 1U;
            instanceInfo.ppEnabledExtensionNames = &debugExtension;
        }
        CheckVulkan(
            vkCreateInstance(&instanceInfo, nullptr, &instance),
            Ex2VulkanENativePhase::InstanceCreation,
            "vkCreateInstance for EX-2 Vulkan E");
        if (diagnostics.validationEnabled)
        {
            debugInfo.pNext = nullptr;
            const auto createMessenger =
                reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(
                        instance, "vkCreateDebugUtilsMessengerEXT"));
            if (createMessenger == nullptr)
                throw std::runtime_error(
                    "vkCreateDebugUtilsMessengerEXT is unavailable for EX-2 Vulkan E validation");
            CheckVulkan(
                createMessenger(instance, &debugInfo, nullptr, &debugMessenger),
                Ex2VulkanENativePhase::InstanceCreation,
                "vkCreateDebugUtilsMessengerEXT for EX-2 Vulkan E");
        }

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            Ex2VulkanENativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices count for EX-2 Vulkan E");
        if (physicalDeviceIndex >= count)
            throw std::invalid_argument(
                "requested Vulkan physical-device index is unavailable");
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            Ex2VulkanENativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices data for EX-2 Vulkan E");
        if (physicalDeviceIndex >= count)
            throw std::runtime_error(
                "Vulkan physical-device enumeration changed during EX-2 E setup");
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
            throw std::invalid_argument(
                "selected Vulkan device does not support core Vulkan 1.3");
        if (logicalByteCount > maximumBufferSize)
            throw std::length_error(
                "EX-2 Vulkan E byte count exceeds maxBufferSize");

        VkPhysicalDeviceVulkan13Features supported13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features supported12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        supported12.pNext = &supported13;
        VkPhysicalDeviceFeatures2 supported{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
        if (supported13.synchronization2 != VK_TRUE)
            throw std::invalid_argument(
                "selected Vulkan device lacks required synchronization2 support");
        if (timingEnabled && supported12.hostQueryReset != VK_TRUE)
            throw std::invalid_argument(
                "selected Vulkan device lacks hostQueryReset for EX-2 E timing");

        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex = detail::SelectEx2VulkanEQueue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];
        detail::ValidateEx2VulkanEAllocationCount(
            diagnostics.properties.limits.maxMemoryAllocationCount,
            logicalByteCount == 0U ? 0U : 3U);
        if (timingEnabled)
        {
            if (diagnostics.queueFamily.timestampValidBits == 0U)
                throw std::invalid_argument(
                    "selected EX-2 Vulkan E queue cannot write timestamps");
            detail::ValidateEx2VulkanETimestampEnvelope(
                diagnostics.queueFamily.timestampValidBits,
                diagnostics.properties.limits.timestampPeriod,
                Ex2VulkanEDurationEnvelopeNanoseconds);
        }

        VkPhysicalDeviceVulkan13Features enabled13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features enabled12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enabled12.hostQueryReset = timingEnabled ? VK_TRUE : VK_FALSE;
        enabled12.pNext = &enabled13;
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        queueInfo.queueCount = 1U;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &enabled12;
        deviceInfo.queueCreateInfoCount = 1U;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        CheckVulkan(
            vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device),
            Ex2VulkanENativePhase::LogicalDeviceCreation,
            "vkCreateDevice for EX-2 Vulkan E");
        diagnostics.synchronization2Enabled = true;
        diagnostics.hostQueryResetEnabled = timingEnabled;
        vkGetDeviceQueue(device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
            throw std::runtime_error(
                "vkGetDeviceQueue returned a null EX-2 Vulkan E queue");
    }

    void CreateBuffer(
        Buffer& buffer,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        const char* name)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = logicalByteCount;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVulkan(
            vkCreateBuffer(device, &info, nullptr, &buffer.handle),
            Ex2VulkanENativePhase::BufferCreation,
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
        buffer.memoryTypeIndex = detail::SelectEx2VulkanEMemoryType(
            requirements.memoryTypeBits,
            required,
            preferred,
            memoryProperties);
        const std::uint32_t heapIndex =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].heapIndex;
        detail::ValidateEx2VulkanEHeapFeasibility(
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
            Ex2VulkanENativePhase::MemoryAllocation,
            name);
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            Ex2VulkanENativePhase::MemoryBinding,
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
                Ex2VulkanENativePhase::MemoryMapping,
                name);
        }
    }

    void PopulateMemoryDiagnostics() noexcept
    {
        diagnostics.uploadMemoryFlags = upload.memoryFlags;
        diagnostics.deviceMemoryFlags = deviceBuffer.memoryFlags;
        diagnostics.readbackMemoryFlags = readback.memoryFlags;
        diagnostics.uploadMemoryTypeIndex = upload.memoryTypeIndex;
        diagnostics.deviceMemoryTypeIndex = deviceBuffer.memoryTypeIndex;
        diagnostics.readbackMemoryTypeIndex = readback.memoryTypeIndex;
        diagnostics.uploadAllocationBytes = upload.allocationSize;
        diagnostics.deviceAllocationBytes = deviceBuffer.allocationSize;
        diagnostics.readbackAllocationBytes = readback.allocationSize;
    }

    void MaintainHostCache(
        const Buffer& buffer,
        bool flush,
        Ex2VulkanENativePhase phase,
        const char* operation)
    {
        if (!detail::RequiresEx2VulkanEHostCacheMaintenance(
                buffer.memoryFlags))
        {
            return;
        }
        const auto aligned = detail::AlignEx2VulkanENoncoherentRange(
            0U,
            logicalByteCount,
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
            operation);
    }

    void CreateCommands()
    {
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            Ex2VulkanENativePhase::CommandCreation,
            "vkCreateCommandPool for EX-2 Vulkan E");
        std::array<VkCommandBuffer, 3U> commands{};
        VkCommandBufferAllocateInfo allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(device, &allocation, commands.data()),
            Ex2VulkanENativePhase::CommandCreation,
            "vkAllocateCommandBuffers for EX-2 Vulkan E");
        auxiliaryCommands = commands[0];
        namedCommands = commands[1];
        diagnosticCommands = commands[2];
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &namedFence),
            Ex2VulkanENativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan E named transfer");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &auxiliaryFence),
            Ex2VulkanENativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan E auxiliary transfer");
    }

    void CreateQueryPool()
    {
        VkQueryPoolCreateInfo queryInfo{
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2U;
        CheckVulkan(
            vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool),
            Ex2VulkanENativePhase::CommandCreation,
            "vkCreateQueryPool for EX-2 Vulkan E timing");
        diagnostics.timestampQueryPoolCreated = true;
    }

    void RecordPreparedNamedCommand()
    {
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(namedCommands, &begin),
            Ex2VulkanENativePhase::CommandRecording,
            "vkBeginCommandBuffer for EX-2 Vulkan E prepared transfer");
        if (logicalByteCount != 0U)
        {
            if (configuration.direction == ex2::TransferDirection::HostToDevice)
            {
                BufferDependency(
                    namedCommands,
                    upload.handle,
                    VK_PIPELINE_STAGE_2_HOST_BIT,
                    VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                BufferDependency(
                    namedCommands,
                    deviceBuffer.handle,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT |
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }
            else
            {
                BufferDependency(
                    namedCommands,
                    deviceBuffer.handle,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                BufferDependency(
                    namedCommands,
                    readback.handle,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT |
                        VK_PIPELINE_STAGE_2_HOST_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT |
                        VK_ACCESS_2_HOST_READ_BIT |
                        VK_ACCESS_2_HOST_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }
            if (timingEnabled)
            {
                vkCmdWriteTimestamp2(
                    namedCommands,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    queryPool,
                    0U);
            }
            const VkBufferCopy copy{0U, 0U, logicalByteCount};
            vkCmdCopyBuffer(
                namedCommands,
                configuration.direction == ex2::TransferDirection::HostToDevice
                    ? upload.handle
                    : deviceBuffer.handle,
                configuration.direction == ex2::TransferDirection::HostToDevice
                    ? deviceBuffer.handle
                    : readback.handle,
                1U,
                &copy);
            if (timingEnabled)
            {
                vkCmdWriteTimestamp2(
                    namedCommands,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                    queryPool,
                    1U);
            }
            if (configuration.direction == ex2::TransferDirection::DeviceToHost)
            {
                BufferDependency(
                    namedCommands,
                    readback.handle,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_HOST_BIT,
                    VK_ACCESS_2_HOST_READ_BIT);
            }
        }
        CheckVulkan(
            vkEndCommandBuffer(namedCommands),
            Ex2VulkanENativePhase::CommandRecording,
            "vkEndCommandBuffer for EX-2 Vulkan E prepared transfer");
        diagnostics.preparedCommandRecordCount = 1U;
        diagnostics.preparedNativeCopyCount = logicalByteCount == 0U ? 0U : 1U;
    }

    void SubmitCommands(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending,
        Ex2VulkanENativePhase phase,
        const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(queue, 1U, &submission, fence);
        switch (detail::ClassifyEx2VulkanESubmissionResult(result))
        {
        case detail::Ex2VulkanESubmissionDisposition::Submitted:
            pending = true;
            return;
        case detail::Ex2VulkanESubmissionDisposition::FailedWithoutSubmission:
            state = OperationState::Failed;
            ThrowVulkanError(result, phase, operation);
        case detail::Ex2VulkanESubmissionDisposition::CompletionUncertain:
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(result, phase, operation);
        }
        std::terminate();
    }

    void AuxiliaryTransfer(
        VkCommandBuffer commands,
        Ex2VulkanENativePhase phase,
        const char* operation)
    {
        CheckVulkan(
            vkResetFences(device, 1U, &auxiliaryFence),
            phase,
            "vkResetFences for EX-2 Vulkan E auxiliary transfer");
        SubmitCommands(
            commands,
            auxiliaryFence,
            auxiliaryTransferPending,
            phase,
            operation);
        const VkResult result = vkWaitForFences(
            device,
            1U,
            &auxiliaryFence,
            VK_TRUE,
            Ex2VulkanEDurationEnvelopeNanoseconds);
        if (result != VK_SUCCESS)
        {
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(
                result,
                phase,
                "vkWaitForFences for EX-2 Vulkan E auxiliary transfer");
        }
        auxiliaryTransferPending = false;
    }

    void ResetCompletionDiagnostics() noexcept
    {
        lastCompletionExecutedCopy = false;
        lastCompletionCopyCount = 0U;
        timingStatus = Ex2VulkanENativeTimingStatus::NotApplicable;
        timingRetrieved = false;
        nativeIntervalNanoseconds.reset();
    }

    void PrepareTransfer(std::span<const std::uint8_t> canonicalSource)
    {
        RequireReusable("preparation");
        state = OperationState::Failed;
        ResetCompletionDiagnostics();
        detail::ValidateEx2VulkanEConfiguration(
            ex2::MakeConfiguration(configuration));
        if (canonicalSource.size() != logicalByteCount)
            throw std::invalid_argument(
                "EX-2 Vulkan E source length does not match byte count");
        if (logicalByteCount != 0U)
        {
            std::memcpy(
                upload.mapped,
                canonicalSource.data(),
                static_cast<std::size_t>(logicalByteCount));
            std::memset(
                readback.mapped,
                0xA5,
                static_cast<std::size_t>(logicalByteCount));
            MaintainHostCache(
                readback,
                true,
                Ex2VulkanENativePhase::NoncoherentFlush,
                "vkFlushMappedMemoryRanges for EX-2 Vulkan E readback-poison preparation");
            MaintainHostCache(
                upload,
                true,
                Ex2VulkanENativePhase::NoncoherentFlush,
                "vkFlushMappedMemoryRanges for EX-2 Vulkan E upload-source preparation");
            if (configuration.direction == ex2::TransferDirection::DeviceToHost)
                PrepareDeviceSource();
        }

        CheckVulkan(
            vkResetFences(device, 1U, &namedFence),
            Ex2VulkanENativePhase::PreparationCompletion,
            "vkResetFences for EX-2 Vulkan E prepared transfer");
        if (timingEnabled && logicalByteCount != 0U)
        {
            vkResetQueryPool(device, queryPool, 0U, 2U);
            timingStatus = Ex2VulkanENativeTimingStatus::Unavailable;
        }
        state = OperationState::Prepared;
    }

    void PrepareDeviceSource()
    {
        CheckVulkan(
            vkResetCommandBuffer(auxiliaryCommands, 0U),
            Ex2VulkanENativePhase::CommandRecording,
            "vkResetCommandBuffer for EX-2 Vulkan E2 source preparation");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(auxiliaryCommands, &begin),
            Ex2VulkanENativePhase::CommandRecording,
            "vkBeginCommandBuffer for EX-2 Vulkan E2 source preparation");
        BufferDependency(
            auxiliaryCommands,
            upload.handle,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            auxiliaryCommands,
            deviceBuffer.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT |
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferCopy copy{0U, 0U, logicalByteCount};
        vkCmdCopyBuffer(
            auxiliaryCommands,
            upload.handle,
            deviceBuffer.handle,
            1U,
            &copy);
        CheckVulkan(
            vkEndCommandBuffer(auxiliaryCommands),
            Ex2VulkanENativePhase::CommandRecording,
            "vkEndCommandBuffer for EX-2 Vulkan E2 source preparation");
        AuxiliaryTransfer(
            auxiliaryCommands,
            Ex2VulkanENativePhase::DeviceSourcePreparation,
            "vkQueueSubmit2 for EX-2 Vulkan E2 source preparation");
    }

    void SubmitTransfer()
    {
        RequireReusable("submission");
        if (state != OperationState::Prepared)
            throw std::logic_error(
                "EX-2 Vulkan E submission requires a fresh successful preparation");
        SubmitCommands(
            namedCommands,
            namedFence,
            namedTransferPending,
            Ex2VulkanENativePhase::Submission,
            "vkQueueSubmit2 for EX-2 Vulkan E named transfer");
        state = OperationState::Submitted;
    }

    void WaitForCompletion(std::uint64_t timeoutNanoseconds)
    {
        if (state == OperationState::CompletionUncertain)
            throw std::logic_error(
                "EX-2 Vulkan E completion cannot be retried after an uncertain result");
        if (state != OperationState::Submitted)
            throw std::logic_error(
                "EX-2 Vulkan E completion wait requires a submitted transfer");
        const VkResult result = vkWaitForFences(
            device,
            1U,
            &namedFence,
            VK_TRUE,
            timeoutNanoseconds);
        state = detail::ClassifyEx2VulkanEWaitResult(result);
        if (result != VK_SUCCESS)
            ThrowVulkanError(
                result,
                Ex2VulkanENativePhase::CompletionWait,
                "vkWaitForFences for EX-2 Vulkan E named transfer");
        namedTransferPending = false;
        lastCompletionExecutedCopy = logicalByteCount != 0U;
        lastCompletionCopyCount = logicalByteCount == 0U ? 0U : 1U;
        state = OperationState::Complete;
    }

    std::vector<std::uint8_t> RetrieveDestinationForValidation()
    {
        RequireComplete("destination retrieval");
        std::vector<std::uint8_t> result(
            static_cast<std::size_t>(logicalByteCount));
        if (result.empty())
            return result;
        state = OperationState::Failed;
        if (configuration.direction == ex2::TransferDirection::HostToDevice)
            ReadBackE1Destination();
        MaintainHostCache(
            readback,
            false,
            Ex2VulkanENativePhase::NoncoherentInvalidate,
            "vkInvalidateMappedMemoryRanges for EX-2 Vulkan E readback validation");
        std::memcpy(
            result.data(),
            readback.mapped,
            static_cast<std::size_t>(logicalByteCount));
        state = OperationState::Complete;
        return result;
    }

    void ReadBackE1Destination()
    {
        CheckVulkan(
            vkResetCommandBuffer(diagnosticCommands, 0U),
            Ex2VulkanENativePhase::CommandRecording,
            "vkResetCommandBuffer for EX-2 Vulkan E1 validation readback");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(diagnosticCommands, &begin),
            Ex2VulkanENativePhase::CommandRecording,
            "vkBeginCommandBuffer for EX-2 Vulkan E1 validation readback");
        BufferDependency(
            diagnosticCommands,
            deviceBuffer.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            diagnosticCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT |
                VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT |
                VK_ACCESS_2_HOST_READ_BIT |
                VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferCopy copy{0U, 0U, logicalByteCount};
        vkCmdCopyBuffer(
            diagnosticCommands,
            deviceBuffer.handle,
            readback.handle,
            1U,
            &copy);
        BufferDependency(
            diagnosticCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(diagnosticCommands),
            Ex2VulkanENativePhase::CommandRecording,
            "vkEndCommandBuffer for EX-2 Vulkan E1 validation readback");
        AuxiliaryTransfer(
            diagnosticCommands,
            Ex2VulkanENativePhase::ValidationReadback,
            "vkQueueSubmit2 for EX-2 Vulkan E1 validation readback");
    }

    void RetrieveNativeTiming()
    {
        RequireComplete("native timing retrieval");
        if (!timingEnabled)
            throw std::logic_error(
                "EX-2 Vulkan E ordinary operation has no native timing resources");
        if (logicalByteCount == 0U)
        {
            timingStatus = Ex2VulkanENativeTimingStatus::NotApplicable;
            timingRetrieved = true;
            return;
        }
        if (timingRetrieved)
            return;
        std::array<detail::Ex2VulkanETimestampQuery, 2U> queries{};
        constexpr VkQueryResultFlags flags =
            VK_QUERY_RESULT_64_BIT |
            VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
        const VkResult result = vkGetQueryPoolResults(
            device,
            queryPool,
            0U,
            2U,
            sizeof(queries),
            queries.data(),
            sizeof(detail::Ex2VulkanETimestampQuery),
            flags);
        if (result != VK_SUCCESS)
        {
            state = OperationState::Failed;
            timingStatus = Ex2VulkanENativeTimingStatus::RetrievalFailed;
            ThrowVulkanError(
                result,
                Ex2VulkanENativePhase::NativeTimingRetrieval,
                "vkGetQueryPoolResults for EX-2 Vulkan E");
        }
        if (queries[0].available == 0U || queries[1].available == 0U)
        {
            state = OperationState::Failed;
            timingStatus = Ex2VulkanENativeTimingStatus::QueryUnavailable;
            ThrowVulkanError(
                VK_NOT_READY,
                Ex2VulkanENativePhase::NativeTimingRetrieval,
                "EX-2 Vulkan E timestamp availability");
        }
        try
        {
            nativeIntervalNanoseconds =
                detail::DecodeEx2VulkanETimestampQueries(
                    result,
                    queries,
                    diagnostics.queueFamily.timestampValidBits,
                    diagnostics.properties.limits.timestampPeriod,
                    Ex2VulkanEDurationEnvelopeNanoseconds);
            timingStatus = Ex2VulkanENativeTimingStatus::Valid;
            timingRetrieved = true;
        }
        catch (const std::exception& error)
        {
            state = OperationState::Failed;
            timingStatus = Ex2VulkanENativeTimingStatus::ConversionInvalid;
            throw Ex2VulkanENativeError{
                Ex2VulkanENativePhase::NativeTimingConversion,
                VK_SUCCESS,
                "EX-2 Vulkan E timestamp conversion",
                error.what()};
        }
    }

    void RequireComplete(const char* diagnostic) const
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                std::string{"EX-2 Vulkan E "} + diagnostic +
                " requires explicit successful completion");
    }

    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer auxiliaryCommands{VK_NULL_HANDLE};
    VkCommandBuffer namedCommands{VK_NULL_HANDLE};
    VkCommandBuffer diagnosticCommands{VK_NULL_HANDLE};
    VkFence namedFence{VK_NULL_HANDLE};
    VkFence auxiliaryFence{VK_NULL_HANDLE};
    VkQueryPool queryPool{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDeviceSize maximumMemoryAllocationSize{};
    VkDeviceSize maximumBufferSize{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> plannedHeapBytes{};
    const ex2::TransferConfiguration configuration;
    const bool timingEnabled{};
    VkDeviceSize logicalByteCount{};
    Ex2VulkanEDiagnostics diagnostics{};
    OperationState state{OperationState::Empty};
    bool namedTransferPending{};
    bool auxiliaryTransferPending{};
    bool lastCompletionExecutedCopy{};
    std::uint32_t lastCompletionCopyCount{};
    Ex2VulkanENativeTimingStatus timingStatus{
        Ex2VulkanENativeTimingStatus::NotApplicable};
    bool timingRetrieved{};
    std::optional<std::uint64_t> nativeIntervalNanoseconds;
};

} // namespace

std::string_view ToString(Ex2VulkanENativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2VulkanENativePhase::InstanceCreation: return "instance creation";
    case Ex2VulkanENativePhase::DeviceEnumeration: return "device enumeration";
    case Ex2VulkanENativePhase::DeviceSelection: return "device selection";
    case Ex2VulkanENativePhase::DeviceProperties: return "device properties";
    case Ex2VulkanENativePhase::QueueSelection: return "queue selection";
    case Ex2VulkanENativePhase::LogicalDeviceCreation: return "logical device creation";
    case Ex2VulkanENativePhase::ResourcePreflight: return "resource preflight";
    case Ex2VulkanENativePhase::BufferCreation: return "buffer creation";
    case Ex2VulkanENativePhase::MemoryAllocation: return "memory allocation";
    case Ex2VulkanENativePhase::MemoryBinding: return "memory binding";
    case Ex2VulkanENativePhase::MemoryMapping: return "memory mapping";
    case Ex2VulkanENativePhase::HostSourcePreparation: return "host source preparation";
    case Ex2VulkanENativePhase::NoncoherentFlush: return "noncoherent flush";
    case Ex2VulkanENativePhase::DeviceSourcePreparation: return "device source preparation";
    case Ex2VulkanENativePhase::PreparationCompletion: return "preparation completion";
    case Ex2VulkanENativePhase::CommandCreation: return "command creation";
    case Ex2VulkanENativePhase::CommandRecording: return "command recording";
    case Ex2VulkanENativePhase::QueryReset: return "query reset";
    case Ex2VulkanENativePhase::Submission: return "submission";
    case Ex2VulkanENativePhase::CompletionWait: return "completion wait";
    case Ex2VulkanENativePhase::ValidationReadback: return "validation readback";
    case Ex2VulkanENativePhase::HostReadVisibility: return "host read visibility";
    case Ex2VulkanENativePhase::NoncoherentInvalidate: return "noncoherent invalidate";
    case Ex2VulkanENativePhase::NativeTimingRetrieval: return "native timing retrieval";
    case Ex2VulkanENativePhase::NativeTimingConversion: return "native timing conversion";
    }
    return "invalid";
}

Ex2VulkanENativeError::Ex2VulkanENativeError(
    Ex2VulkanENativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanENativePhase Ex2VulkanENativeError::Phase() const noexcept { return phase_; }
VkResult Ex2VulkanENativeError::NativeResult() const noexcept { return nativeResult_; }
const std::string& Ex2VulkanENativeError::Operation() const noexcept { return operation_; }

void detail::ValidateEx2VulkanEConfiguration(
    const ex2::WorkloadConfiguration& configuration)
{
    const auto* transfer = std::get_if<ex2::TransferConfiguration>(
        &configuration.parameters);
    if (transfer == nullptr ||
        configuration.common.executionMode != ex2::LogicalExecutionMode::Prepared ||
        configuration.common.operationBoundary !=
            ex2::OperationBoundary::PreparedSingleCopyCompletion ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan E configuration violates the frozen transfer contract");
    }
}

VkDeviceSize detail::CalculateEx2VulkanEByteCount(
    std::uint64_t requestedByteCount,
    VkDeviceSize maximumAddressableByteCount,
    std::size_t maximumVectorElementCount)
{
    if (requestedByteCount > maximumAddressableByteCount)
        throw std::length_error(
            "EX-2 Vulkan E byte count exceeds VkDeviceSize addressability");
    if (requestedByteCount > maximumVectorElementCount)
        throw std::length_error(
            "EX-2 Vulkan E byte count exceeds the host vector maximum");
    return static_cast<VkDeviceSize>(requestedByteCount);
}

void detail::ValidateEx2VulkanEAllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount)
{
    if (requiredAllocationCount > maximumAllocationCount)
        throw std::length_error(
            "selected Vulkan device cannot support the required EX-2 E allocations");
}

void detail::ValidateEx2VulkanEHeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize)
{
    if (plannedHeapBytes > heapSize ||
        requirementSize > heapSize - plannedHeapBytes)
    {
        throw std::length_error(
            "EX-2 Vulkan E memory requirements exceed the selected Vulkan heap");
    }
}

std::uint32_t detail::SelectEx2VulkanEQueue(
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

std::uint32_t detail::SelectEx2VulkanEMemoryType(
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
                return index;
        }
    }
    throw std::invalid_argument(
        "EX-2 Vulkan E found no compatible memory type");
}

detail::Ex2VulkanEMappedRange detail::AlignEx2VulkanENoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize)
{
    if (nonCoherentAtomSize == 0U)
        throw std::invalid_argument(
            "Vulkan nonCoherentAtomSize must be positive");
    if (offset > allocationSize || size > allocationSize - offset)
        throw std::out_of_range(
            "EX-2 Vulkan E mapped range exceeds its allocation");
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
            throw std::overflow_error(
                "EX-2 Vulkan E noncoherent range alignment overflow");
        alignedEnd += increment;
    }
    if (alignedEnd >= allocationSize)
        return {alignedOffset, VK_WHOLE_SIZE};
    return {alignedOffset, alignedEnd - alignedOffset};
}

void detail::ValidateEx2VulkanETimestampEnvelope(
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds)
{
    if (validBits < 36U || validBits > 64U)
        throw std::invalid_argument(
            "EX-2 Vulkan E timestampValidBits must be in [36, 64]");
    if (!std::isfinite(timestampPeriodNanoseconds) ||
        timestampPeriodNanoseconds <= 0.0L)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan E timestampPeriod must be finite and positive");
    }
    const long double wrapNanoseconds =
        std::ldexp(timestampPeriodNanoseconds, static_cast<int>(validBits));
    if (!std::isfinite(wrapNanoseconds) ||
        static_cast<long double>(durationEnvelopeNanoseconds) >= wrapNanoseconds)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan E duration envelope permits timestamp ambiguity");
    }
}

std::uint64_t detail::DecodeEx2VulkanETimestampQueries(
    VkResult result,
    const std::array<Ex2VulkanETimestampQuery, 2U>& queries,
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(
            "EX-2 Vulkan E timestamp query retrieval did not succeed");
    if (queries[0].available == 0U || queries[1].available == 0U)
        throw std::runtime_error(
            "EX-2 Vulkan E timestamp query was unavailable after completion");
    ValidateEx2VulkanETimestampEnvelope(
        validBits, timestampPeriodNanoseconds, durationEnvelopeNanoseconds);
    std::uint64_t delta{};
    if (validBits == 64U)
        delta = queries[1].ticks - queries[0].ticks;
    else
    {
        const std::uint64_t mask =
            (std::uint64_t{1U} << validBits) - 1U;
        delta = ((queries[1].ticks & mask) -
            (queries[0].ticks & mask)) & mask;
    }
    const long double nanoseconds =
        static_cast<long double>(delta) * timestampPeriodNanoseconds;
    const long double rounded = std::round(nanoseconds);
    if (!std::isfinite(rounded) || rounded < 0.0L ||
        rounded >= std::ldexp(1.0L, 64))
    {
        throw std::overflow_error(
            "EX-2 Vulkan E timestamp duration is outside uint64 nanoseconds");
    }
    const auto stored = static_cast<std::uint64_t>(rounded);
    if (stored > durationEnvelopeNanoseconds)
        throw std::out_of_range(
            "EX-2 Vulkan E timestamp duration exceeds its envelope");
    return stored;
}

struct Ex2VulkanEOperation::Resources final : public EResources
{
    Resources(
        const ex2::TransferConfiguration& configuration,
        std::uint32_t physicalDeviceIndex)
        : EResources{configuration, false}
    {
        Initialize(physicalDeviceIndex);
    }
};

struct Ex2VulkanEDeviceTimedOperation::Resources final : public EResources
{
    Resources(
        const ex2::TransferConfiguration& configuration,
        std::uint32_t physicalDeviceIndex)
        : EResources{configuration, true}
    {
        Initialize(physicalDeviceIndex);
    }
};

#define COMPUTELAB_VULKAN_E_COMMON_METHODS(ClassName) \
ClassName::ClassName(const ex2::TransferConfiguration& config, std::uint32_t index) \
    : resources_{std::make_unique<Resources>(config, index)} {} \
ClassName::~ClassName() noexcept = default; \
const std::array<std::uint8_t, VK_UUID_SIZE>& ClassName::SelectedDeviceUuid() const noexcept { return resources_->diagnostics.deviceUuid; } \
const Ex2VulkanEDiagnostics& ClassName::Diagnostics() const noexcept { return resources_->diagnostics; } \
std::uint64_t ClassName::ByteCount() const noexcept { return resources_->configuration.byteCount; } \
ex2::TransferDirection ClassName::Direction() const noexcept { return resources_->configuration.direction; } \
std::uint32_t ClassName::ExpectedNativeCopyCount() const noexcept { return resources_->logicalByteCount == 0U ? 0U : 1U; } \
void ClassName::PrepareTransfer(std::span<const std::uint8_t> source) { resources_->PrepareTransfer(source); } \
void ClassName::SubmitTransfer() { resources_->SubmitTransfer(); } \
void ClassName::WaitForCompletion(std::uint64_t timeout) { resources_->WaitForCompletion(timeout); } \
std::vector<std::uint8_t> ClassName::RetrieveDestinationForValidation() { return resources_->RetrieveDestinationForValidation(); } \
bool ClassName::LastCompletionExecutedCopy() const { resources_->RequireComplete("execution diagnostic"); return resources_->lastCompletionExecutedCopy; } \
std::uint32_t ClassName::LastCompletionNativeCopyCount() const { resources_->RequireComplete("copy-count diagnostic"); return resources_->lastCompletionCopyCount; }

COMPUTELAB_VULKAN_E_COMMON_METHODS(Ex2VulkanEOperation)
COMPUTELAB_VULKAN_E_COMMON_METHODS(Ex2VulkanEDeviceTimedOperation)

#undef COMPUTELAB_VULKAN_E_COMMON_METHODS

void Ex2VulkanEDeviceTimedOperation::RetrieveNativeTiming()
{
    resources_->RetrieveNativeTiming();
}

Ex2VulkanENativeTimingStatus
Ex2VulkanEDeviceTimedOperation::NativeTimingStatus() const
{
    resources_->RequireComplete("native timing status");
    return resources_->timingStatus;
}

Ex2VulkanENativeTimingMetadata
Ex2VulkanEDeviceTimedOperation::NativeTimingMetadata() const noexcept
{
    return {
        "vkCmdWriteTimestamp2/vkGetQueryPoolResults",
        resources_->diagnostics.properties.limits.timestampPeriod,
        resources_->diagnostics.queueFamily.timestampValidBits,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        Ex2VulkanEDurationEnvelopeNanoseconds};
}

std::optional<std::uint64_t>
Ex2VulkanEDeviceTimedOperation::NativeDeviceIntervalNanoseconds() const
{
    resources_->RequireComplete("native device interval");
    return resources_->nativeIntervalNanoseconds;
}

} // namespace computelab::vulkan
