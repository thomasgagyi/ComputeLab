#include "vulkan/Ex2VulkanD1.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <utility>

namespace computelab::vulkan
{
namespace
{

using OperationState = detail::Ex2VulkanD1OperationState;

VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U)
    {
        auto* diagnostics =
            static_cast<Ex2VulkanD1Diagnostics*>(userData);
        ++diagnostics->validationErrorCount;
        std::fprintf(
            stderr,
            "EX-2 Vulkan D1 validation error: %s\n",
            callbackData != nullptr && callbackData->pMessage != nullptr
                ? callbackData->pMessage
                : "unknown validation message");
    }
    return VK_FALSE;
}

[[noreturn]] void ThrowVulkanError(
    VkResult result,
    Ex2VulkanD1NativePhase phase,
    std::string operation)
{
    throw Ex2VulkanD1NativeError{
        phase,
        result,
        operation,
        operation + " failed during " + std::string{ToString(phase)} +
            " with VkResult " + std::to_string(static_cast<int>(result))};
}

void CheckVulkan(
    VkResult result,
    Ex2VulkanD1NativePhase phase,
    const char* operation)
{
    if (result != VK_SUCCESS)
        ThrowVulkanError(result, phase, operation);
}

std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
        throw std::runtime_error(
            "EX-2 Vulkan D1 cannot open SPIR-V: " + path.string());
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
        throw std::runtime_error(
            "EX-2 Vulkan D1 SPIR-V has an invalid byte length: " +
            path.string());
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(length) / sizeof(std::uint32_t));
    stream.seekg(0);
    if (!stream.read(
            reinterpret_cast<char*>(words.data()),
            static_cast<std::streamsize>(length)) ||
        words.front() != 0x07230203U)
    {
        throw std::runtime_error(
            "EX-2 Vulkan D1 SPIR-V data is malformed: " + path.string());
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

void InterPassDependency(VkCommandBuffer commands)
{
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask =
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1U;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

class D1Resources
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
    } stateA, stateB, upload, readback;

    D1Resources(
        const ex2::IterativeConfiguration& requestedConfiguration,
        bool enableTiming)
        : configuration{requestedConfiguration},
          timingEnabled{enableTiming}
    {
    }

    void Initialize(
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex)
    {
        loadedSpirv = ReadSpirv(spirvPath);
        detail::ValidateEx2VulkanD1Configuration(
            ex2::MakeConfiguration(configuration));
        logicalBufferBytes = detail::CalculateEx2VulkanD1BufferByteCount(
            configuration.elementCount,
            std::numeric_limits<VkDeviceSize>::max());
        if (configuration.elementCount > std::vector<std::uint32_t>{}.max_size())
            throw std::length_error(
                "EX-2 Vulkan D1 logical state exceeds the host vector maximum");

        SetupDevice(physicalDeviceIndex);
        if (configuration.elementCount != 0U)
        {
            CreateBuffer(
                stateA,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0U,
                "EX-2 Vulkan D1 state A buffer");
            CreateBuffer(
                stateB,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0U,
                "EX-2 Vulkan D1 state B buffer");
            CreateBuffer(
                upload,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "EX-2 Vulkan D1 upload staging buffer");
            CreateBuffer(
                readback,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                "EX-2 Vulkan D1 readback staging buffer");
            PopulateMemoryDiagnostics();
            SetupPipeline();
        }
        CreateCommands();
        if (timingEnabled)
            CreateQueryPool();
    }

    ~D1Resources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            const bool submittedOrUncertain =
                state == OperationState::Submitted ||
                state == OperationState::CompletionUncertain;
            if (detail::ClassifyEx2VulkanD1ResourceDisposition(
                    submittedOrUncertain, transferPending) ==
                detail::Ex2VulkanD1ResourceDisposition::PreserveForProcessTeardown)
            {
                return;
            }
            if (commandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, commandPool, nullptr);
            if (queryPool != VK_NULL_HANDLE)
                vkDestroyQueryPool(device, queryPool, nullptr);
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
            if (sequenceFence != VK_NULL_HANDLE)
                vkDestroyFence(device, sequenceFence, nullptr);
            if (transferFence != VK_NULL_HANDLE)
                vkDestroyFence(device, transferFence, nullptr);
            DestroyBuffer(readback);
            DestroyBuffer(upload);
            DestroyBuffer(stateB);
            DestroyBuffer(stateA);
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
        if (detail::IsEx2VulkanD1OperationStateReusable(state))
            return;
        const char* reason = state == OperationState::Submitted
            ? " while sequence work is pending"
            : state == OperationState::CompletionUncertain
                ? " after uncertain completion"
                : " after a native failure";
        throw std::logic_error(
            std::string{"EX-2 Vulkan D1 "} + operation +
            " is unavailable" + reason);
    }

    ex2::IterativeFinalBuffer ExpectedFinalBuffer() const noexcept
    {
        return (configuration.iterationCount & 1U) == 0U
            ? ex2::IterativeFinalBuffer::StateA
            : ex2::IterativeFinalBuffer::StateB;
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        const char* validationMode =
            std::getenv("COMPUTELAB_EX2_D1_VALIDATION");
        diagnostics.validationEnabled = validationMode != nullptr;
        diagnostics.synchronizationValidationEnabled =
            validationMode != nullptr &&
            std::string_view{validationMode} == "sync";
        if (validationMode != nullptr &&
            std::string_view{validationMode} != "standard" &&
            std::string_view{validationMode} != "sync")
        {
            throw std::invalid_argument(
                "COMPUTELAB_EX2_D1_VALIDATION must be 'standard' or 'sync'");
        }

        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 Vulkan D1";
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
            Ex2VulkanD1NativePhase::InstanceCreation,
            "vkCreateInstance for EX-2 Vulkan D1");
        if (diagnostics.validationEnabled)
        {
            debugInfo.pNext = nullptr;
            const auto createMessenger =
                reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(
                        instance, "vkCreateDebugUtilsMessengerEXT"));
            if (createMessenger == nullptr)
                throw std::runtime_error(
                    "vkCreateDebugUtilsMessengerEXT is unavailable for EX-2 Vulkan D1 validation");
            CheckVulkan(
                createMessenger(instance, &debugInfo, nullptr, &debugMessenger),
                Ex2VulkanD1NativePhase::InstanceCreation,
                "vkCreateDebugUtilsMessengerEXT for EX-2 Vulkan D1");
        }

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            Ex2VulkanD1NativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices count for EX-2 Vulkan D1");
        if (physicalDeviceIndex >= count)
            throw std::invalid_argument(
                "requested Vulkan physical-device index is unavailable");
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            Ex2VulkanD1NativePhase::DeviceEnumeration,
            "vkEnumeratePhysicalDevices data for EX-2 Vulkan D1");
        if (physicalDeviceIndex >= count)
            throw std::runtime_error(
                "Vulkan physical-device enumeration changed during EX-2 D1 setup");
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
                "selected Vulkan device lacks hostQueryReset for EX-2 D1 timing");

        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex =
            detail::SelectEx2VulkanD1Queue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];

        dispatchShape = detail::ValidateEx2VulkanD1DispatchShape(
            configuration, diagnostics.properties.limits, maximumBufferSize);
        detail::ValidateEx2VulkanD1AllocationCount(
            diagnostics.properties.limits.maxMemoryAllocationCount,
            configuration.elementCount == 0U ? 0U : 4U);
        if (timingEnabled)
        {
            if (diagnostics.queueFamily.timestampValidBits == 0U)
                throw std::invalid_argument(
                    "selected EX-2 Vulkan D1 queue cannot write timestamps");
            detail::ValidateEx2VulkanD1TimestampEnvelope(
                diagnostics.queueFamily.timestampValidBits,
                diagnostics.properties.limits.timestampPeriod,
                Ex2VulkanD1DurationEnvelopeNanoseconds);
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
            Ex2VulkanD1NativePhase::LogicalDeviceCreation,
            "vkCreateDevice for EX-2 Vulkan D1");
        diagnostics.synchronization2Enabled = true;
        diagnostics.hostQueryResetEnabled = timingEnabled;
        vkGetDeviceQueue(device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
            throw std::runtime_error(
                "vkGetDeviceQueue returned a null EX-2 Vulkan D1 queue");
    }

    void CreateBuffer(
        Buffer& buffer,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        const char* name)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = logicalBufferBytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVulkan(
            vkCreateBuffer(device, &info, nullptr, &buffer.handle),
            Ex2VulkanD1NativePhase::BufferCreation,
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
        buffer.memoryTypeIndex = detail::SelectEx2VulkanD1MemoryType(
            requirements.memoryTypeBits,
            required,
            preferred,
            memoryProperties);
        const std::uint32_t heapIndex =
            memoryProperties.memoryTypes[buffer.memoryTypeIndex].heapIndex;
        detail::ValidateEx2VulkanD1HeapFeasibility(
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
            Ex2VulkanD1NativePhase::MemoryAllocation,
            name);
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            Ex2VulkanD1NativePhase::MemoryBinding,
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
                Ex2VulkanD1NativePhase::MemoryMapping,
                name);
        }
    }

    void PopulateMemoryDiagnostics() noexcept
    {
        diagnostics.stateAMemoryFlags = stateA.memoryFlags;
        diagnostics.stateBMemoryFlags = stateB.memoryFlags;
        diagnostics.uploadMemoryFlags = upload.memoryFlags;
        diagnostics.readbackMemoryFlags = readback.memoryFlags;
        diagnostics.stateAMemoryTypeIndex = stateA.memoryTypeIndex;
        diagnostics.stateBMemoryTypeIndex = stateB.memoryTypeIndex;
        diagnostics.uploadMemoryTypeIndex = upload.memoryTypeIndex;
        diagnostics.readbackMemoryTypeIndex = readback.memoryTypeIndex;
        diagnostics.stateAAllocationBytes = stateA.allocationSize;
        diagnostics.stateBAllocationBytes = stateB.allocationSize;
        diagnostics.uploadAllocationBytes = upload.allocationSize;
        diagnostics.readbackAllocationBytes = readback.allocationSize;
    }

    void MaintainHostCache(
        const Buffer& buffer,
        bool flush,
        Ex2VulkanD1NativePhase phase)
    {
        if ((buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U)
            return;
        const auto aligned = detail::AlignEx2VulkanD1NoncoherentRange(
            0U,
            logicalBufferBytes,
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
                ? "vkFlushMappedMemoryRanges for EX-2 Vulkan D1 upload"
                : "vkInvalidateMappedMemoryRanges for EX-2 Vulkan D1 readback");
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
            vkCreateDescriptorSetLayout(
                device, &layoutInfo, nullptr, &descriptorLayout),
            Ex2VulkanD1NativePhase::DescriptorCreation,
            "vkCreateDescriptorSetLayout for EX-2 Vulkan D1");

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4U};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 2U;
        poolInfo.poolSizeCount = 1U;
        poolInfo.pPoolSizes = &poolSize;
        CheckVulkan(
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
            Ex2VulkanD1NativePhase::DescriptorCreation,
            "vkCreateDescriptorPool for EX-2 Vulkan D1");

        const std::array<VkDescriptorSetLayout, 2U> layouts{
            descriptorLayout, descriptorLayout};
        VkDescriptorSetAllocateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        setInfo.pSetLayouts = layouts.data();
        CheckVulkan(
            vkAllocateDescriptorSets(device, &setInfo, descriptorSets.data()),
            Ex2VulkanD1NativePhase::DescriptorCreation,
            "vkAllocateDescriptorSets for EX-2 Vulkan D1");

        const std::array<VkDescriptorBufferInfo, 4U> infos{{
            {stateA.handle, 0U, logicalBufferBytes},
            {stateB.handle, 0U, logicalBufferBytes},
            {stateB.handle, 0U, logicalBufferBytes},
            {stateA.handle, 0U, logicalBufferBytes}}};
        std::array<VkWriteDescriptorSet, 4U> writes{};
        for (std::uint32_t index = 0U; index < writes.size(); ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSets[index / 2U];
            writes[index].dstBinding = index % 2U;
            writes[index].descriptorCount = 1U;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
        }
        vkUpdateDescriptorSets(
            device,
            static_cast<std::uint32_t>(writes.size()),
            writes.data(),
            0U,
            nullptr);

        struct PushConstants
        {
            std::uint32_t elementCount;
            std::uint32_t pass;
        };
        const VkPushConstantRange pushRange{
            VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1U;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        CheckVulkan(
            vkCreatePipelineLayout(
                device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
            Ex2VulkanD1NativePhase::PipelineConstruction,
            "vkCreatePipelineLayout for EX-2 Vulkan D1");

        VkShaderModuleCreateInfo shaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = loadedSpirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = loadedSpirv.data();
        CheckVulkan(
            vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
            Ex2VulkanD1NativePhase::PipelineConstruction,
            "vkCreateShaderModule for EX-2 Vulkan D1");
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
            Ex2VulkanD1NativePhase::PipelineConstruction,
            "vkCreateComputePipelines for EX-2 Vulkan D1");
    }

    void CreateCommands()
    {
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            Ex2VulkanD1NativePhase::CommandCreation,
            "vkCreateCommandPool for EX-2 Vulkan D1");
        std::array<VkCommandBuffer, 3U> commands{};
        VkCommandBufferAllocateInfo allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(device, &allocation, commands.data()),
            Ex2VulkanD1NativePhase::CommandCreation,
            "vkAllocateCommandBuffers for EX-2 Vulkan D1");
        uploadCommands = commands[0];
        sequenceCommands = commands[1];
        readbackCommands = commands[2];
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &sequenceFence),
            Ex2VulkanD1NativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan D1 sequence");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &transferFence),
            Ex2VulkanD1NativePhase::CommandCreation,
            "vkCreateFence for EX-2 Vulkan D1 transfers");
    }

    void CreateQueryPool()
    {
        VkQueryPoolCreateInfo queryInfo{
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2U;
        CheckVulkan(
            vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool),
            Ex2VulkanD1NativePhase::PipelineConstruction,
            "vkCreateQueryPool for EX-2 Vulkan D1 timing");
        diagnostics.timestampQueryPoolCreated = true;
    }

    void SubmitCommands(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending,
        Ex2VulkanD1NativePhase phase,
        const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(queue, 1U, &submission, fence);
        switch (detail::ClassifyEx2VulkanD1SubmissionResult(result))
        {
        case detail::Ex2VulkanD1SubmissionDisposition::Submitted:
            pending = true;
            return;
        case detail::Ex2VulkanD1SubmissionDisposition::FailedWithoutSubmission:
            state = OperationState::Failed;
            ThrowVulkanError(result, phase, operation);
        case detail::Ex2VulkanD1SubmissionDisposition::CompletionUncertain:
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(result, phase, operation);
        }
        std::terminate();
    }

    void Transfer(
        VkCommandBuffer commands,
        Ex2VulkanD1NativePhase phase,
        const char* operation)
    {
        const VkResult resetResult = vkResetFences(device, 1U, &transferFence);
        if (resetResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                resetResult,
                phase,
                "vkResetFences for EX-2 Vulkan D1 transfer");
        }
        SubmitCommands(commands, transferFence, transferPending, phase, operation);
        const VkResult waitResult = vkWaitForFences(
            device,
            1U,
            &transferFence,
            VK_TRUE,
            Ex2VulkanD1DurationEnvelopeNanoseconds);
        if (waitResult != VK_SUCCESS)
        {
            state = OperationState::CompletionUncertain;
            ThrowVulkanError(
                waitResult,
                phase,
                "vkWaitForFences for EX-2 Vulkan D1 transfer");
        }
        transferPending = false;
    }

    void UploadInitialState(std::span<const std::uint32_t> initialState)
    {
        RequireReusable("initial-state upload");
        state = OperationState::Failed;
        lastCompletionDispatchCount = 0U;
        lastCompletionBarrierCount = 0U;
        lastCompletionExecutedSequence = false;
        timingStatus = Ex2VulkanD1NativeTimingStatus::NotApplicable;
        nativeIntervalNanoseconds.reset();
        detail::ValidateEx2VulkanD1Configuration(
            ex2::MakeConfiguration(configuration));
        if (initialState.size() != configuration.elementCount)
            throw std::invalid_argument(
                "EX-2 Vulkan D1 initial-state length does not match element count");
        if (initialState.empty())
        {
            state = OperationState::InitialStateReady;
            return;
        }

        std::memcpy(upload.mapped, initialState.data(), initialState.size_bytes());
        MaintainHostCache(upload, true, Ex2VulkanD1NativePhase::InitialUpload);
        CheckVulkan(
            vkResetCommandBuffer(uploadCommands, 0U),
            Ex2VulkanD1NativePhase::CommandBufferReset,
            "vkResetCommandBuffer for EX-2 Vulkan D1 upload");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(uploadCommands, &begin),
            Ex2VulkanD1NativePhase::CommandRecording,
            "vkBeginCommandBuffer for EX-2 Vulkan D1 upload");
        BufferDependency(
            uploadCommands,
            upload.handle,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            uploadCommands,
            stateA.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT |
                VK_ACCESS_2_SHADER_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_READ_BIT |
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
        vkCmdCopyBuffer(uploadCommands, upload.handle, stateA.handle, 1U, &copy);
        CheckVulkan(
            vkEndCommandBuffer(uploadCommands),
            Ex2VulkanD1NativePhase::CommandRecording,
            "vkEndCommandBuffer for EX-2 Vulkan D1 upload");
        Transfer(
            uploadCommands,
            Ex2VulkanD1NativePhase::InitialUpload,
            "vkQueueSubmit2 for EX-2 Vulkan D1 initial upload");
        state = OperationState::InitialStateReady;
    }

    void SubmitSequence()
    {
        RequireReusable("sequence submission");
        if (state != OperationState::InitialStateReady)
            throw std::logic_error(
                "EX-2 Vulkan D1 submission requires a fresh completed initial-state upload");

        if (timingEnabled)
        {
            vkResetQueryPool(device, queryPool, 0U, 2U);
            timingStatus = Ex2VulkanD1NativeTimingStatus::Unavailable;
        }
        const VkResult fenceResult = vkResetFences(device, 1U, &sequenceFence);
        if (fenceResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                fenceResult,
                Ex2VulkanD1NativePhase::CommandBufferReset,
                "vkResetFences for EX-2 Vulkan D1 sequence");
        }
        const VkResult resetResult = vkResetCommandBuffer(sequenceCommands, 0U);
        if (resetResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                resetResult,
                Ex2VulkanD1NativePhase::CommandBufferReset,
                "vkResetCommandBuffer for EX-2 Vulkan D1 sequence");
        }
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        const VkResult beginResult = vkBeginCommandBuffer(sequenceCommands, &begin);
        if (beginResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                beginResult,
                Ex2VulkanD1NativePhase::CommandRecording,
                "vkBeginCommandBuffer for EX-2 Vulkan D1 sequence");
        }

        const auto counts =
            detail::Ex2VulkanD1ExpectedCommandCounts(configuration);
        if (counts.dispatchCount != 0U)
        {
            BufferDependency(
                sequenceCommands,
                stateA.handle,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);
            if (timingEnabled)
            {
                vkCmdWriteTimestamp2(
                    sequenceCommands,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    queryPool,
                    0U);
            }
            struct PushConstants
            {
                std::uint32_t elementCount;
                std::uint32_t pass;
            };
            vkCmdBindPipeline(
                sequenceCommands,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline);
            for (std::uint32_t pass = 0U; pass < counts.dispatchCount; ++pass)
            {
                const VkDescriptorSet descriptorSet = descriptorSets[pass & 1U];
                vkCmdBindDescriptorSets(
                    sequenceCommands,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipelineLayout,
                    0U,
                    1U,
                    &descriptorSet,
                    0U,
                    nullptr);
                const PushConstants push{
                    static_cast<std::uint32_t>(configuration.elementCount),
                    pass};
                vkCmdPushConstants(
                    sequenceCommands,
                    pipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0U,
                    sizeof(push),
                    &push);
                vkCmdDispatch(
                    sequenceCommands,
                    dispatchShape.groupCountX,
                    1U,
                    1U);
                if (pass + 1U < counts.dispatchCount)
                    InterPassDependency(sequenceCommands);
            }
            if (timingEnabled)
            {
                vkCmdWriteTimestamp2(
                    sequenceCommands,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                    queryPool,
                    1U);
            }
        }
        else
        {
            timingStatus = Ex2VulkanD1NativeTimingStatus::NotApplicable;
        }

        const VkResult endResult = vkEndCommandBuffer(sequenceCommands);
        if (endResult != VK_SUCCESS)
        {
            state = OperationState::Failed;
            ThrowVulkanError(
                endResult,
                Ex2VulkanD1NativePhase::CommandRecording,
                "vkEndCommandBuffer for EX-2 Vulkan D1 sequence");
        }
        SubmitCommands(
            sequenceCommands,
            sequenceFence,
            sequencePending,
            Ex2VulkanD1NativePhase::Submission,
            "vkQueueSubmit2 for EX-2 Vulkan D1 sequence");
        submittedCounts = counts;
        state = OperationState::Submitted;
    }

    void WaitForCompletion(std::uint64_t timeoutNanoseconds)
    {
        if (state == OperationState::CompletionUncertain)
            throw std::logic_error(
                "EX-2 Vulkan D1 completion cannot be retried after an uncertain result");
        if (state != OperationState::Submitted)
            throw std::logic_error(
                "EX-2 Vulkan D1 completion wait requires a submitted sequence");
        const VkResult result = vkWaitForFences(
            device,
            1U,
            &sequenceFence,
            VK_TRUE,
            timeoutNanoseconds);
        state = detail::ClassifyEx2VulkanD1WaitResult(result);
        if (result != VK_SUCCESS)
            ThrowVulkanError(
                result,
                Ex2VulkanD1NativePhase::CompletionWait,
                "vkWaitForFences for EX-2 Vulkan D1 sequence");
        sequencePending = false;
        completedFinalBuffer = ExpectedFinalBuffer();
        lastCompletionDispatchCount = submittedCounts.dispatchCount;
        lastCompletionBarrierCount = submittedCounts.interPassBarrierCount;
        lastCompletionExecutedSequence = submittedCounts.dispatchCount != 0U;

        if (timingEnabled && submittedCounts.dispatchCount != 0U)
        {
            std::array<detail::Ex2VulkanD1TimestampQuery, 2U> queries{};
            constexpr VkQueryResultFlags flags =
                VK_QUERY_RESULT_64_BIT |
                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
            const VkResult queryResult = vkGetQueryPoolResults(
                device,
                queryPool,
                0U,
                2U,
                sizeof(queries),
                queries.data(),
                sizeof(detail::Ex2VulkanD1TimestampQuery),
                flags);
            if (queryResult != VK_SUCCESS)
            {
                state = OperationState::Failed;
                timingStatus = Ex2VulkanD1NativeTimingStatus::RetrievalFailed;
                ThrowVulkanError(
                    queryResult,
                    Ex2VulkanD1NativePhase::NativeTimingRetrieval,
                    "vkGetQueryPoolResults for EX-2 Vulkan D1");
            }
            if (queries[0].available == 0U || queries[1].available == 0U)
            {
                state = OperationState::Failed;
                timingStatus = Ex2VulkanD1NativeTimingStatus::QueryUnavailable;
                ThrowVulkanError(
                    VK_NOT_READY,
                    Ex2VulkanD1NativePhase::NativeTimingRetrieval,
                    "EX-2 Vulkan D1 timestamp availability");
            }
            try
            {
                nativeIntervalNanoseconds =
                    detail::DecodeEx2VulkanD1TimestampQueries(
                        queryResult,
                        queries,
                        diagnostics.queueFamily.timestampValidBits,
                        diagnostics.properties.limits.timestampPeriod,
                        Ex2VulkanD1DurationEnvelopeNanoseconds);
                timingStatus = Ex2VulkanD1NativeTimingStatus::Valid;
            }
            catch (const std::exception& error)
            {
                state = OperationState::Failed;
                timingStatus = Ex2VulkanD1NativeTimingStatus::ConversionInvalid;
                throw Ex2VulkanD1NativeError{
                    Ex2VulkanD1NativePhase::NativeTimingConversion,
                    VK_SUCCESS,
                    "EX-2 Vulkan D1 timestamp conversion",
                    error.what()};
            }
        }
        state = OperationState::Complete;
    }

    std::vector<std::uint32_t> RetrieveFinalState()
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                "EX-2 Vulkan D1 final readback requires explicit successful completion");
        std::vector<std::uint32_t> result(
            static_cast<std::size_t>(configuration.elementCount));
        if (result.empty())
            return result;

        state = OperationState::Failed;
        CheckVulkan(
            vkResetCommandBuffer(readbackCommands, 0U),
            Ex2VulkanD1NativePhase::CommandBufferReset,
            "vkResetCommandBuffer for EX-2 Vulkan D1 readback");
        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(readbackCommands, &begin),
            Ex2VulkanD1NativePhase::CommandRecording,
            "vkBeginCommandBuffer for EX-2 Vulkan D1 readback");
        const VkBuffer source =
            ExpectedFinalBuffer() == ex2::IterativeFinalBuffer::StateA
                ? stateA.handle
                : stateB.handle;
        const bool computed = configuration.iterationCount != 0U;
        BufferDependency(
            readbackCommands,
            source,
            computed
                ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                : VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            computed
                ? VK_ACCESS_2_SHADER_WRITE_BIT
                : VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(
            readbackCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferCopy copy{0U, 0U, logicalBufferBytes};
        vkCmdCopyBuffer(readbackCommands, source, readback.handle, 1U, &copy);
        BufferDependency(
            readbackCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(readbackCommands),
            Ex2VulkanD1NativePhase::CommandRecording,
            "vkEndCommandBuffer for EX-2 Vulkan D1 readback");
        Transfer(
            readbackCommands,
            Ex2VulkanD1NativePhase::FinalReadback,
            "vkQueueSubmit2 for EX-2 Vulkan D1 final readback");
        MaintainHostCache(
            readback, false, Ex2VulkanD1NativePhase::FinalReadback);
        std::memcpy(
            result.data(),
            readback.mapped,
            static_cast<std::size_t>(logicalBufferBytes));
        state = OperationState::Complete;
        return result;
    }

    void RequireComplete(const char* diagnostic) const
    {
        if (state != OperationState::Complete)
            throw std::logic_error(
                std::string{"EX-2 Vulkan D1 "} + diagnostic +
                " requires explicit successful completion");
    }

    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer uploadCommands{VK_NULL_HANDLE};
    VkCommandBuffer sequenceCommands{VK_NULL_HANDLE};
    VkCommandBuffer readbackCommands{VK_NULL_HANDLE};
    VkFence sequenceFence{VK_NULL_HANDLE};
    VkFence transferFence{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    std::array<VkDescriptorSet, 2U> descriptorSets{};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkShaderModule shader{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkQueryPool queryPool{VK_NULL_HANDLE};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDeviceSize maximumMemoryAllocationSize{};
    VkDeviceSize maximumBufferSize{};
    std::array<VkDeviceSize, VK_MAX_MEMORY_HEAPS> plannedHeapBytes{};
    const ex2::IterativeConfiguration configuration;
    const bool timingEnabled{};
    detail::Ex2VulkanD1DispatchShape dispatchShape{};
    VkDeviceSize logicalBufferBytes{};
    Ex2VulkanD1Diagnostics diagnostics{};
    std::string loadedShaderName{"Ex2D1.comp.spv"};
    std::vector<std::uint32_t> loadedSpirv;
    OperationState state{OperationState::Empty};
    bool sequencePending{};
    bool transferPending{};
    detail::Ex2VulkanD1CommandCounts submittedCounts{};
    ex2::IterativeFinalBuffer completedFinalBuffer{
        ex2::IterativeFinalBuffer::StateA};
    std::uint32_t lastCompletionDispatchCount{};
    std::uint32_t lastCompletionBarrierCount{};
    bool lastCompletionExecutedSequence{};
    Ex2VulkanD1NativeTimingStatus timingStatus{
        Ex2VulkanD1NativeTimingStatus::NotApplicable};
    std::optional<std::uint64_t> nativeIntervalNanoseconds;
};

} // namespace

std::string_view ToString(Ex2VulkanD1NativePhase phase) noexcept
{
    switch (phase)
    {
    case Ex2VulkanD1NativePhase::InstanceCreation: return "instance creation";
    case Ex2VulkanD1NativePhase::DeviceEnumeration: return "device enumeration";
    case Ex2VulkanD1NativePhase::DeviceSelection: return "device selection";
    case Ex2VulkanD1NativePhase::DeviceProperties: return "device properties";
    case Ex2VulkanD1NativePhase::QueueSelection: return "queue selection";
    case Ex2VulkanD1NativePhase::LogicalDeviceCreation: return "logical device creation";
    case Ex2VulkanD1NativePhase::ResourcePreflight: return "resource preflight";
    case Ex2VulkanD1NativePhase::BufferCreation: return "buffer creation";
    case Ex2VulkanD1NativePhase::MemoryAllocation: return "memory allocation";
    case Ex2VulkanD1NativePhase::MemoryBinding: return "memory binding";
    case Ex2VulkanD1NativePhase::MemoryMapping: return "memory mapping";
    case Ex2VulkanD1NativePhase::DescriptorCreation: return "descriptor creation";
    case Ex2VulkanD1NativePhase::PipelineConstruction: return "pipeline construction";
    case Ex2VulkanD1NativePhase::CommandCreation: return "command creation";
    case Ex2VulkanD1NativePhase::InitialUpload: return "initial upload";
    case Ex2VulkanD1NativePhase::CommandBufferReset: return "command-buffer reset";
    case Ex2VulkanD1NativePhase::CommandRecording: return "command recording";
    case Ex2VulkanD1NativePhase::Submission: return "submission";
    case Ex2VulkanD1NativePhase::CompletionWait: return "completion wait";
    case Ex2VulkanD1NativePhase::FinalReadback: return "final readback";
    case Ex2VulkanD1NativePhase::QueryReset: return "query reset";
    case Ex2VulkanD1NativePhase::NativeTimingRetrieval: return "native timing retrieval";
    case Ex2VulkanD1NativePhase::NativeTimingConversion: return "native timing conversion";
    }
    return "invalid";
}

Ex2VulkanD1NativeError::Ex2VulkanD1NativeError(
    Ex2VulkanD1NativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanD1NativePhase Ex2VulkanD1NativeError::Phase() const noexcept { return phase_; }
VkResult Ex2VulkanD1NativeError::NativeResult() const noexcept { return nativeResult_; }
const std::string& Ex2VulkanD1NativeError::Operation() const noexcept { return operation_; }

void detail::ValidateEx2VulkanD1Configuration(
    const ex2::WorkloadConfiguration& configuration)
{
    const auto* iterative = std::get_if<ex2::IterativeConfiguration>(
        &configuration.parameters);
    if (iterative == nullptr ||
        iterative->variant != ex2::IterativeVariant::D1 ||
        configuration.common.executionMode != ex2::LogicalExecutionMode::Ordinary ||
        configuration.common.operationBoundary !=
            ex2::OperationBoundary::OrdinaryIterationSequenceCompletion ||
        !ex2::ValidateSemanticConfiguration(configuration).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan D1 configuration violates the frozen D1 semantic contract");
    }
}

detail::Ex2VulkanD1DispatchShape detail::ValidateEx2VulkanD1DispatchShape(
    const ex2::IterativeConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize)
{
    ValidateEx2VulkanD1Configuration(ex2::MakeConfiguration(configuration));
    const VkDeviceSize bytes = CalculateEx2VulkanD1BufferByteCount(
        configuration.elementCount, maximumBufferSize);
    if (bytes > limits.maxStorageBufferRange)
        throw std::length_error(
            "EX-2 Vulkan D1 logical state exceeds maxStorageBufferRange");
    if (limits.maxPushConstantsSize < 2U * sizeof(std::uint32_t))
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the D1 push constants");
    if (limits.maxPerStageDescriptorStorageBuffers < 2U ||
        limits.maxDescriptorSetStorageBuffers < 2U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot supply the two D1 storage buffers");
    }
    if (configuration.elementCount == 0U)
        return {};
    if (limits.maxComputeWorkGroupInvocations < Ex2VulkanD1LocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < Ex2VulkanD1LocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1U ||
        limits.maxComputeWorkGroupSize[2] < 1U)
    {
        throw std::invalid_argument(
            "selected Vulkan device cannot execute the EX-2 D1 256 x 1 x 1 workgroup");
    }
    const std::uint64_t groupCount =
        configuration.elementCount / Ex2VulkanD1LocalSizeX +
        (configuration.elementCount % Ex2VulkanD1LocalSizeX == 0U ? 0U : 1U);
    if (groupCount > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1U ||
        limits.maxComputeWorkGroupCount[2] < 1U ||
        groupCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan D1 element count exceeds maxComputeWorkGroupCount");
    }
    return {static_cast<std::uint32_t>(groupCount), Ex2VulkanD1LocalSizeX};
}

detail::Ex2VulkanD1CommandCounts detail::Ex2VulkanD1ExpectedCommandCounts(
    const ex2::IterativeConfiguration& configuration)
{
    ValidateEx2VulkanD1Configuration(ex2::MakeConfiguration(configuration));
    if (configuration.elementCount == 0U || configuration.iterationCount == 0U)
        return {};
    return {
        static_cast<std::uint32_t>(configuration.iterationCount),
        static_cast<std::uint32_t>(configuration.iterationCount - 1U)};
}

VkDeviceSize detail::CalculateEx2VulkanD1BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(
            "EX-2 Vulkan D1 element count exceeds uint32 logical indexing");
    if (elementCount > maximumAddressableByteCount / sizeof(std::uint32_t))
        throw std::length_error(
            "EX-2 Vulkan D1 buffer byte count exceeds the supported size");
    return static_cast<VkDeviceSize>(elementCount) * sizeof(std::uint32_t);
}

void detail::ValidateEx2VulkanD1AllocationCount(
    std::uint32_t maximumAllocationCount,
    std::uint32_t requiredAllocationCount)
{
    if (requiredAllocationCount > maximumAllocationCount)
        throw std::length_error(
            "selected Vulkan device cannot support the required EX-2 D1 allocations");
}

void detail::ValidateEx2VulkanD1HeapFeasibility(
    VkDeviceSize requirementSize,
    VkDeviceSize plannedHeapBytes,
    VkDeviceSize heapSize)
{
    if (plannedHeapBytes > heapSize ||
        requirementSize > heapSize - plannedHeapBytes)
    {
        throw std::length_error(
            "EX-2 Vulkan D1 memory requirements exceed the selected Vulkan heap");
    }
}

std::uint32_t detail::SelectEx2VulkanD1Queue(
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

std::uint32_t detail::SelectEx2VulkanD1MemoryType(
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
        "EX-2 Vulkan D1 found no compatible memory type");
}

detail::Ex2VulkanD1MappedRange detail::AlignEx2VulkanD1NoncoherentRange(
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
            "EX-2 Vulkan D1 mapped range exceeds its allocation");
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
                "EX-2 Vulkan D1 noncoherent range alignment overflow");
        alignedEnd += increment;
    }
    if (alignedEnd >= allocationSize)
        return {alignedOffset, VK_WHOLE_SIZE};
    return {alignedOffset, alignedEnd - alignedOffset};
}

void detail::ValidateEx2VulkanD1TimestampEnvelope(
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds)
{
    if (validBits < 36U || validBits > 64U)
        throw std::invalid_argument(
            "EX-2 Vulkan D1 timestampValidBits must be in [36, 64]");
    if (!std::isfinite(timestampPeriodNanoseconds) ||
        timestampPeriodNanoseconds <= 0.0L)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan D1 timestampPeriod must be finite and positive");
    }
    const long double wrapNanoseconds =
        std::ldexp(timestampPeriodNanoseconds, static_cast<int>(validBits));
    if (!std::isfinite(wrapNanoseconds) ||
        static_cast<long double>(durationEnvelopeNanoseconds) >= wrapNanoseconds)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan D1 duration envelope permits timestamp ambiguity");
    }
}

std::uint64_t detail::DecodeEx2VulkanD1TimestampQueries(
    VkResult result,
    const std::array<Ex2VulkanD1TimestampQuery, 2U>& queries,
    std::uint32_t validBits,
    long double timestampPeriodNanoseconds,
    std::uint64_t durationEnvelopeNanoseconds)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(
            "EX-2 Vulkan D1 timestamp query retrieval did not succeed");
    if (queries[0].available == 0U || queries[1].available == 0U)
        throw std::runtime_error(
            "EX-2 Vulkan D1 timestamp query was unavailable after completion");
    ValidateEx2VulkanD1TimestampEnvelope(
        validBits, timestampPeriodNanoseconds, durationEnvelopeNanoseconds);
    std::uint64_t start = queries[0].ticks;
    std::uint64_t stop = queries[1].ticks;
    std::uint64_t delta{};
    if (validBits == 64U)
        delta = stop - start;
    else
    {
        const std::uint64_t mask =
            (std::uint64_t{1U} << validBits) - 1U;
        delta = ((stop & mask) - (start & mask)) & mask;
    }
    const long double nanoseconds =
        static_cast<long double>(delta) * timestampPeriodNanoseconds;
    const long double rounded = std::round(nanoseconds);
    if (!std::isfinite(rounded) || rounded < 0.0L ||
        rounded >= std::ldexp(1.0L, 64))
    {
        throw std::overflow_error(
            "EX-2 Vulkan D1 timestamp duration is outside uint64 nanoseconds");
    }
    const auto stored = static_cast<std::uint64_t>(rounded);
    if (stored > durationEnvelopeNanoseconds)
        throw std::out_of_range(
            "EX-2 Vulkan D1 timestamp duration exceeds its envelope");
    return stored;
}

struct Ex2VulkanD1Operation::Resources final : public D1Resources
{
    Resources(
        const ex2::IterativeConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex)
        : D1Resources{configuration, false}
    {
        Initialize(spirvPath, physicalDeviceIndex);
    }
};

struct Ex2VulkanD1DeviceTimedOperation::Resources final : public D1Resources
{
    Resources(
        const ex2::IterativeConfiguration& configuration,
        const std::filesystem::path& spirvPath,
        std::uint32_t physicalDeviceIndex)
        : D1Resources{configuration, true}
    {
        Initialize(spirvPath, physicalDeviceIndex);
    }
};

#define COMPUTELAB_VULKAN_D1_COMMON_METHODS(ClassName) \
ClassName::ClassName(const ex2::IterativeConfiguration& config, const std::filesystem::path& spirv, std::uint32_t index) \
    : resources_{std::make_unique<Resources>(config, spirv, index)} {} \
ClassName::~ClassName() noexcept = default; \
std::uint64_t ClassName::ElementCount() const noexcept { return resources_->configuration.elementCount; } \
std::uint64_t ClassName::IterationCount() const noexcept { return resources_->configuration.iterationCount; } \
ex2::IterativeFinalBuffer ClassName::ExpectedFinalBuffer() const noexcept { return resources_->ExpectedFinalBuffer(); } \
const std::array<std::uint8_t, VK_UUID_SIZE>& ClassName::SelectedDeviceUuid() const noexcept { return resources_->diagnostics.deviceUuid; } \
const Ex2VulkanD1Diagnostics& ClassName::Diagnostics() const noexcept { return resources_->diagnostics; } \
std::string_view ClassName::LoadedShaderName() const noexcept { return resources_->loadedShaderName; } \
std::span<const std::uint32_t> ClassName::LoadedSpirv() const noexcept { return resources_->loadedSpirv; } \
void ClassName::UploadInitialState(std::span<const std::uint32_t> input) { resources_->UploadInitialState(input); } \
void ClassName::SubmitSequence() { resources_->SubmitSequence(); } \
void ClassName::WaitForCompletion(std::uint64_t timeout) { resources_->WaitForCompletion(timeout); } \
std::vector<std::uint32_t> ClassName::RetrieveFinalState() { return resources_->RetrieveFinalState(); } \
ex2::IterativeFinalBuffer ClassName::CompletedFinalBuffer() const { resources_->RequireComplete("completed final-buffer diagnostic"); return resources_->completedFinalBuffer; } \
std::uint32_t ClassName::LastCompletionNativeDispatchCount() const { resources_->RequireComplete("dispatch-count diagnostic"); return resources_->lastCompletionDispatchCount; } \
std::uint32_t ClassName::LastCompletionInterPassBarrierCount() const { resources_->RequireComplete("barrier-count diagnostic"); return resources_->lastCompletionBarrierCount; } \
bool ClassName::LastCompletionExecutedSequence() const { resources_->RequireComplete("execution diagnostic"); return resources_->lastCompletionExecutedSequence; }

COMPUTELAB_VULKAN_D1_COMMON_METHODS(Ex2VulkanD1Operation)
COMPUTELAB_VULKAN_D1_COMMON_METHODS(Ex2VulkanD1DeviceTimedOperation)

#undef COMPUTELAB_VULKAN_D1_COMMON_METHODS

Ex2VulkanD1NativeTimingStatus
Ex2VulkanD1DeviceTimedOperation::NativeTimingStatus() const
{
    resources_->RequireComplete("native timing status");
    return resources_->timingStatus;
}

Ex2VulkanD1NativeTimingMetadata
Ex2VulkanD1DeviceTimedOperation::NativeTimingMetadata() const noexcept
{
    return {
        "vkCmdWriteTimestamp2/vkGetQueryPoolResults",
        resources_->diagnostics.properties.limits.timestampPeriod,
        resources_->diagnostics.queueFamily.timestampValidBits,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        Ex2VulkanD1DurationEnvelopeNanoseconds};
}

std::optional<std::uint64_t>
Ex2VulkanD1DeviceTimedOperation::NativeDeviceIntervalNanoseconds() const
{
    resources_->RequireComplete("native device interval");
    return resources_->nativeIntervalNanoseconds;
}

} // namespace computelab::vulkan
