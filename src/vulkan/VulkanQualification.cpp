#include "vulkan/VulkanQualification.hpp"

#include "oracle/DeterministicTransform.hpp"
#include "timing/HostTiming.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace computelab::vulkan
{
namespace
{

constexpr std::uint64_t TransferTimeoutNanoseconds = 30'000'000'000ULL;

std::string VulkanErrorMessage(VkResult result, const char* operation)
{
    return std::string{operation} + " failed with VkResult=" +
        std::to_string(static_cast<std::int32_t>(result));
}

[[noreturn]] void ThrowVulkanError(VkResult result, const char* operation)
{
    throw std::runtime_error(VulkanErrorMessage(result, operation));
}

void CheckVulkan(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        ThrowVulkanError(result, operation);
    }
}

std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        throw std::runtime_error(
            "Vulkan qualification cannot open SPIR-V: " + path.string());
    }
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
    {
        throw std::runtime_error(
            "Vulkan qualification malformed SPIR-V length: " + path.string());
    }
    std::vector<std::uint32_t> words(
        static_cast<std::size_t>(length) / sizeof(std::uint32_t));
    stream.seekg(0);
    if (!stream.read(
            reinterpret_cast<char*>(words.data()),
            static_cast<std::streamsize>(length)) ||
        words[0] != 0x07230203U)
    {
        throw std::runtime_error(
            "Vulkan qualification malformed SPIR-V data: " + path.string());
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
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1U;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

std::uint32_t ValidateQualificationDispatch(
    std::size_t elementCount,
    const VkPhysicalDeviceLimits& limits)
{
    if (limits.maxComputeWorkGroupInvocations < QualificationLocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < QualificationLocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1U ||
        limits.maxComputeWorkGroupSize[2] < 1U)
    {
        throw std::runtime_error(
            "Vulkan qualification requires a supported 256 x 1 x 1 compute workgroup");
    }
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "Vulkan qualification elementCount exceeds uint32 shader addressing");
    }

    const std::uint64_t groupCount = std::max<std::uint64_t>(
        1U,
        (static_cast<std::uint64_t>(elementCount) +
            QualificationLocalSizeX - 1U) /
            QualificationLocalSizeX);
    if (groupCount > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1U ||
        limits.maxComputeWorkGroupCount[2] < 1U)
    {
        throw std::runtime_error(
            "Vulkan qualification dispatch exceeds maxComputeWorkGroupCount");
    }

    const std::uint64_t byteCount =
        std::max<std::uint64_t>(1U, elementCount) * sizeof(std::uint32_t);
    if (byteCount > limits.maxStorageBufferRange)
    {
        throw std::runtime_error(
            "Vulkan qualification buffer exceeds maxStorageBufferRange");
    }
    return static_cast<std::uint32_t>(groupCount);
}

} // namespace

std::uint32_t SelectComputeQualificationQueue(
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
    throw std::runtime_error(
        "Vulkan device has no compute-capable queue for qualification");
}

struct VulkanQualificationOperation::Resources
{
    enum class Phase
    {
        Empty,
        Uploaded,
        Prepared,
        Complete,
        Failed,
        CompletionUncertain,
    };

    struct Buffer
    {
        VkBuffer handle{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
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
    VkCommandBuffer readbackCommands{VK_NULL_HANDLE};
    VkFence computeFence{VK_NULL_HANDLE};
    VkFence transferFence{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkShaderModule shader{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VulkanQualificationDiagnostics diagnostics;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    std::uint32_t elementCount{};
    std::uint32_t groupCount{};
    VkDeviceSize bufferBytes{};
    std::vector<std::uint32_t> expected;
    Phase phase{Phase::Empty};
    bool computePending{};
    bool transferPending{};

    ~Resources() noexcept
    {
        // A timeout or failed fence wait does not prove completion, including
        // VK_ERROR_DEVICE_LOST. Never block indefinitely here and never destroy
        // resources that may still be referenced. The later process supervisor
        // must terminate the failed child; process teardown then reclaims them.
        if (detail::ClassifyVulkanResourceDisposition(
                computePending,
                transferPending) ==
            detail::VulkanResourceDisposition::PreserveForProcessTeardown)
        {
            return;
        }

        if (device != VK_NULL_HANDLE)
        {
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
        {
            vkDestroyInstance(instance, nullptr);
        }
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

    void Require(Phase expected, const char* operation) const
    {
        if (phase != expected)
        {
            throw std::logic_error(
                std::string{operation} +
                ": invalid Vulkan qualification lifecycle phase");
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
        info.size = bufferBytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVulkan(
            vkCreateBuffer(device, &info, nullptr, &buffer.handle),
            (std::string{"vkCreateBuffer "} + name).c_str());

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        std::uint32_t selected = memoryProperties.memoryTypeCount;
        for (unsigned int pass = 0U;
             pass < 2U && selected == memoryProperties.memoryTypeCount;
             ++pass)
        {
            const VkMemoryPropertyFlags flags =
                required | (pass == 0U ? preferred : 0U);
            for (std::uint32_t index = 0U;
                 index < memoryProperties.memoryTypeCount;
                 ++index)
            {
                if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
                    (memoryProperties.memoryTypes[index].propertyFlags & flags) ==
                        flags)
                {
                    selected = index;
                    break;
                }
            }
        }
        if (selected == memoryProperties.memoryTypeCount)
        {
            throw std::runtime_error(
                std::string{"Vulkan qualification has no compatible memory type for "} +
                name);
        }

        buffer.memoryFlags = memoryProperties.memoryTypes[selected].propertyFlags;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = selected;
        CheckVulkan(
            vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
            (std::string{"vkAllocateMemory "} + name).c_str());
        CheckVulkan(
            vkBindBufferMemory(device, buffer.handle, buffer.memory, 0U),
            (std::string{"vkBindBufferMemory "} + name).c_str());

        if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U)
        {
            CheckVulkan(
                vkMapMemory(
                    device,
                    buffer.memory,
                    0U,
                    VK_WHOLE_SIZE,
                    0U,
                    &buffer.mapped),
                (std::string{"vkMapMemory "} + name).c_str());
        }
    }

    void MaintainHostCache(const Buffer& buffer, bool flush)
    {
        if ((buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U)
        {
            return;
        }

        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.offset = 0U;
        range.size = VK_WHOLE_SIZE;
        CheckVulkan(
            flush
                ? vkFlushMappedMemoryRanges(device, 1U, &range)
                : vkInvalidateMappedMemoryRanges(device, 1U, &range),
            flush
                ? "vkFlushMappedMemoryRanges qualification upload"
                : "vkInvalidateMappedMemoryRanges qualification readback");
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-2 G0-02";
        application.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        CheckVulkan(
            vkCreateInstance(&instanceInfo, nullptr, &instance),
            "vkCreateInstance for Vulkan qualification");

        std::uint32_t count{};
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, nullptr),
            "vkEnumeratePhysicalDevices count for Vulkan qualification");
        if (physicalDeviceIndex >= count)
        {
            throw std::invalid_argument(
                "Vulkan qualification physicalDeviceIndex is outside device enumeration");
        }
        std::vector<VkPhysicalDevice> devices(count);
        CheckVulkan(
            vkEnumeratePhysicalDevices(instance, &count, devices.data()),
            "vkEnumeratePhysicalDevices devices for Vulkan qualification");
        if (physicalDeviceIndex >= count)
        {
            throw std::runtime_error(
                "Vulkan device enumeration changed during qualification setup");
        }
        physicalDevice = devices[physicalDeviceIndex];

        VkPhysicalDeviceIDProperties idProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &idProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
        diagnostics.properties = properties.properties;
        std::copy_n(
            idProperties.deviceUUID,
            diagnostics.deviceUuid.size(),
            diagnostics.deviceUuid.begin());
        if (diagnostics.properties.apiVersion < VK_API_VERSION_1_3)
        {
            throw std::runtime_error(
                "Vulkan qualification requires core API 1.3");
        }

        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        std::uint32_t familyCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex = SelectComputeQualificationQueue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];

        VkPhysicalDeviceVulkan13Features supported13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 supported{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported13;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
        if (supported13.synchronization2 != VK_TRUE)
        {
            throw std::runtime_error(
                "Vulkan qualification selected device lacks synchronization2");
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
            "vkCreateDevice for Vulkan qualification");
        diagnostics.synchronization2Enabled = true;
        vkGetDeviceQueue(device, diagnostics.queueFamilyIndex, 0U, &queue);
        if (queue == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "vkGetDeviceQueue returned a null Vulkan qualification queue");
        }
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
            "vkCreateDescriptorSetLayout for Vulkan qualification");

        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2U};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1U;
        poolInfo.poolSizeCount = 1U;
        poolInfo.pPoolSizes = &poolSize;
        CheckVulkan(
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool),
            "vkCreateDescriptorPool for Vulkan qualification");

        VkDescriptorSetAllocateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1U;
        setInfo.pSetLayouts = &descriptorLayout;
        CheckVulkan(
            vkAllocateDescriptorSets(device, &setInfo, &descriptorSet),
            "vkAllocateDescriptorSets for Vulkan qualification");

        const std::array<VkDescriptorBufferInfo, 2U> buffers{{
            {input.handle, 0U, bufferBytes},
            {output.handle, 0U, bufferBytes}}};
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

        VkPushConstantRange pushRange{
            VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(elementCount)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1U;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1U;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        CheckVulkan(
            vkCreatePipelineLayout(
                device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
            "vkCreatePipelineLayout for Vulkan qualification");

        VkShaderModuleCreateInfo shaderInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = spirv.data();
        CheckVulkan(
            vkCreateShaderModule(device, &shaderInfo, nullptr, &shader),
            "vkCreateShaderModule for Vulkan qualification");

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
            "vkCreateComputePipelines for Vulkan qualification");
    }

    void SetupCommands()
    {
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckVulkan(
            vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
            "vkCreateCommandPool for Vulkan qualification");

        std::array<VkCommandBuffer, 3U> commands{};
        VkCommandBufferAllocateInfo allocation{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount =
            static_cast<std::uint32_t>(commands.size());
        CheckVulkan(
            vkAllocateCommandBuffers(device, &allocation, commands.data()),
            "vkAllocateCommandBuffers for Vulkan qualification");
        uploadCommands = commands[0];
        computeCommands = commands[1];
        readbackCommands = commands[2];

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &computeFence),
            "vkCreateFence for Vulkan qualification compute");
        CheckVulkan(
            vkCreateFence(device, &fenceInfo, nullptr, &transferFence),
            "vkCreateFence for Vulkan qualification transfer");

        VkCommandBufferBeginInfo begin{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckVulkan(
            vkBeginCommandBuffer(uploadCommands, &begin),
            "vkBeginCommandBuffer for Vulkan qualification upload");
        BufferDependency(
            uploadCommands,
            input.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (elementCount != 0U)
        {
            const VkBufferCopy copy{0U, 0U, bufferBytes};
            vkCmdCopyBuffer(
                uploadCommands, upload.handle, input.handle, 1U, &copy);
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
            "vkEndCommandBuffer for Vulkan qualification upload");

        CheckVulkan(
            vkBeginCommandBuffer(computeCommands, &begin),
            "vkBeginCommandBuffer for Vulkan qualification compute");
        BufferDependency(
            computeCommands,
            output.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        vkCmdBindPipeline(
            computeCommands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(
            computeCommands,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0U,
            1U,
            &descriptorSet,
            0U,
            nullptr);
        vkCmdPushConstants(
            computeCommands,
            pipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0U,
            sizeof(elementCount),
            &elementCount);
        vkCmdDispatch(computeCommands, groupCount, 1U, 1U);
        CheckVulkan(
            vkEndCommandBuffer(computeCommands),
            "vkEndCommandBuffer for timestamp-free Vulkan qualification compute");

        CheckVulkan(
            vkBeginCommandBuffer(readbackCommands, &begin),
            "vkBeginCommandBuffer for Vulkan qualification readback");
        BufferDependency(
            readbackCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        BufferDependency(
            readbackCommands,
            output.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
        if (elementCount != 0U)
        {
            const VkBufferCopy copy{0U, 0U, bufferBytes};
            vkCmdCopyBuffer(
                readbackCommands, output.handle, readback.handle, 1U, &copy);
        }
        BufferDependency(
            readbackCommands,
            readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_READ_BIT);
        CheckVulkan(
            vkEndCommandBuffer(readbackCommands),
            "vkEndCommandBuffer for Vulkan qualification readback");
    }

    VkResult Submit(
        VkCommandBuffer commands,
        VkFence fence,
        bool& pending) noexcept
    {
        VkCommandBufferSubmitInfo commandInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1U;
        submission.pCommandBufferInfos = &commandInfo;
        const VkResult result = vkQueueSubmit2(queue, 1U, &submission, fence);
        pending = result == VK_SUCCESS;
        return result;
    }

    void Transfer(VkCommandBuffer commands, const char* operation)
    {
        phase = Phase::Failed;
        CheckVulkan(
            vkResetFences(device, 1U, &transferFence),
            "vkResetFences for Vulkan qualification transfer");
        const VkResult submitResult =
            Submit(commands, transferFence, transferPending);
        CheckVulkan(submitResult, operation);
        const VkResult waitResult = vkWaitForFences(
            device,
            1U,
            &transferFence,
            VK_TRUE,
            TransferTimeoutNanoseconds);
        if (waitResult != VK_SUCCESS)
        {
            phase = Phase::CompletionUncertain;
            ThrowVulkanError(
                waitResult,
                "vkWaitForFences for Vulkan qualification untimed transfer");
        }
        transferPending = false;
    }
};

VulkanQualificationOperation::VulkanQualificationOperation(
    std::size_t elementCount,
    const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex)
    : resources_{std::make_unique<Resources>()}
{
    const auto spirv = ReadSpirv(spirvPath);
    auto& resources = *resources_;
    resources.SetupDevice(physicalDeviceIndex);
    resources.groupCount = ValidateQualificationDispatch(
        elementCount, resources.diagnostics.properties.limits);
    resources.elementCount = static_cast<std::uint32_t>(elementCount);
    resources.bufferBytes =
        std::max<VkDeviceSize>(1U, elementCount) * sizeof(std::uint32_t);

    resources.CreateBuffer(
        resources.input,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "qualification device-local input");
    resources.CreateBuffer(
        resources.output,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        "qualification device-local output");
    resources.CreateBuffer(
        resources.upload,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        "qualification upload staging");
    resources.CreateBuffer(
        resources.readback,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        "qualification readback staging");

    resources.diagnostics.inputMemoryFlags = resources.input.memoryFlags;
    resources.diagnostics.outputMemoryFlags = resources.output.memoryFlags;
    resources.diagnostics.uploadMemoryFlags = resources.upload.memoryFlags;
    resources.diagnostics.readbackMemoryFlags = resources.readback.memoryFlags;
    resources.SetupPipeline(spirv);
    resources.SetupCommands();
}

VulkanQualificationOperation::~VulkanQualificationOperation() noexcept = default;

std::size_t VulkanQualificationOperation::ElementCount() const noexcept
{
    return resources_->elementCount;
}

const VulkanQualificationDiagnostics&
VulkanQualificationOperation::Diagnostics() const noexcept
{
    return resources_->diagnostics;
}

void VulkanQualificationOperation::Upload(
    std::span<const std::uint32_t> input)
{
    auto& resources = *resources_;
    if (resources.phase != Resources::Phase::Empty &&
        resources.phase != Resources::Phase::Complete)
    {
        throw std::logic_error(
            "Vulkan qualification upload requires initial setup or proven-complete work");
    }
    if (input.size() != resources.elementCount)
    {
        throw std::invalid_argument(
            "Vulkan qualification upload size differs from fixed elementCount");
    }

    resources.expected = TransformSequence(
        std::vector<std::uint32_t>{input.begin(), input.end()});
    if (!input.empty())
    {
        std::memcpy(
            resources.upload.mapped, input.data(), input.size_bytes());
        resources.MaintainHostCache(resources.upload, true);
    }
    resources.Transfer(
        resources.uploadCommands,
        "vkQueueSubmit2 for Vulkan qualification untimed upload");
    resources.phase = Resources::Phase::Uploaded;
}

void VulkanQualificationOperation::PrepareHostOnly()
{
    auto& resources = *resources_;
    if (resources.phase != Resources::Phase::Uploaded &&
        resources.phase != Resources::Phase::Complete)
    {
        throw std::logic_error(
            "Vulkan qualification preparation requires uploaded input and proven completion");
    }
    resources.phase = Resources::Phase::Failed;
    CheckVulkan(
        vkResetFences(resources.device, 1U, &resources.computeFence),
        "vkResetFences for Vulkan qualification preparation");
    resources.phase = Resources::Phase::Prepared;
}

VulkanQualificationExecution VulkanQualificationOperation::ExecuteHostOnly(
    std::uint64_t timeoutNanoseconds)
{
    auto& resources = *resources_;
    resources.Require(Resources::Phase::Prepared, "ExecuteHostOnly");

    const timing::HostTimePoint t0 = timing::CaptureHostTime();
    const VkResult submitResult = resources.Submit(
        resources.computeCommands,
        resources.computeFence,
        resources.computePending);
    const timing::HostTimePoint t1 = timing::CaptureHostTime();

    if (submitResult != VK_SUCCESS)
    {
        resources.phase = Resources::Phase::Failed;
        return {
            VulkanQualificationStatus::SubmitFailed,
            VulkanQualificationFailurePhase::Submission,
            ex2::CalculateHostTimingIntervals(
                ex2::HostTimingStatus::SubmitFailed, t0, t1, std::nullopt),
            submitResult,
            VulkanErrorMessage(
                submitResult,
                "vkQueueSubmit2 for Vulkan qualification transform"),
            false,
            {}};
    }

    const VkResult waitResult = vkWaitForFences(
        resources.device,
        1U,
        &resources.computeFence,
        VK_TRUE,
        timeoutNanoseconds);
    if (waitResult != VK_SUCCESS)
    {
        resources.phase = Resources::Phase::CompletionUncertain;
        const bool timedOut = waitResult == VK_TIMEOUT;
        return {
            timedOut
                ? VulkanQualificationStatus::Timeout
                : VulkanQualificationStatus::WaitFailed,
            VulkanQualificationFailurePhase::CompletionWait,
            ex2::CalculateHostTimingIntervals(
                timedOut
                    ? ex2::HostTimingStatus::Timeout
                    : ex2::HostTimingStatus::WaitFailed,
                t0,
                t1,
                std::nullopt),
            waitResult,
            VulkanErrorMessage(
                waitResult,
                "vkWaitForFences for Vulkan qualification completion"),
            false,
            {}};
    }
    const timing::HostTimePoint t2 = timing::CaptureHostTime();
    resources.computePending = false;
    resources.phase = Resources::Phase::Complete;
    const auto hostTiming = ex2::CalculateHostTimingIntervals(
        ex2::HostTimingStatus::Ok, t0, t1, t2);

    std::vector<std::uint32_t> output(resources.elementCount);
    const VkResult resetResult =
        vkResetFences(resources.device, 1U, &resources.transferFence);
    if (resetResult != VK_SUCCESS)
    {
        resources.phase = Resources::Phase::Failed;
        return {
            VulkanQualificationStatus::ReadbackFailed,
            VulkanQualificationFailurePhase::Readback,
            hostTiming,
            resetResult,
            VulkanErrorMessage(
                resetResult,
                "vkResetFences for Vulkan qualification readback"),
            false,
            {}};
    }

    const VkResult readbackSubmitResult = resources.Submit(
        resources.readbackCommands,
        resources.transferFence,
        resources.transferPending);
    if (readbackSubmitResult != VK_SUCCESS)
    {
        resources.phase = Resources::Phase::Failed;
        return {
            VulkanQualificationStatus::ReadbackFailed,
            VulkanQualificationFailurePhase::Readback,
            hostTiming,
            readbackSubmitResult,
            VulkanErrorMessage(
                readbackSubmitResult,
                "vkQueueSubmit2 for Vulkan qualification readback"),
            false,
            {}};
    }

    const VkResult readbackWaitResult = vkWaitForFences(
        resources.device,
        1U,
        &resources.transferFence,
        VK_TRUE,
        TransferTimeoutNanoseconds);
    if (readbackWaitResult != VK_SUCCESS)
    {
        resources.phase = Resources::Phase::CompletionUncertain;
        return {
            VulkanQualificationStatus::ReadbackFailed,
            VulkanQualificationFailurePhase::Readback,
            hostTiming,
            readbackWaitResult,
            VulkanErrorMessage(
                readbackWaitResult,
                "vkWaitForFences for Vulkan qualification readback"),
            false,
            {}};
    }
    resources.transferPending = false;

    if (!output.empty())
    {
        resources.MaintainHostCache(resources.readback, false);
        std::memcpy(
            output.data(),
            resources.readback.mapped,
            output.size() * sizeof(std::uint32_t));
    }

    if (output != resources.expected)
    {
        resources.phase = Resources::Phase::Failed;
        return {
            VulkanQualificationStatus::ValidationFailed,
            VulkanQualificationFailurePhase::Validation,
            hostTiming,
            std::nullopt,
            "Vulkan qualification output differs from the exact CPU oracle",
            false,
            std::move(output)};
    }

    if (hostTiming.status != ex2::HostTimingStatus::Ok)
    {
        resources.phase = Resources::Phase::Failed;
        return {
            VulkanQualificationStatus::TimingInvalid,
            VulkanQualificationFailurePhase::HostTiming,
            hostTiming,
            std::nullopt,
            "Vulkan qualification host timestamps failed the G0-01 contract",
            true,
            std::move(output)};
    }

    resources.phase = Resources::Phase::Complete;
    return {
        VulkanQualificationStatus::Ok,
        VulkanQualificationFailurePhase::None,
        hostTiming,
        std::nullopt,
        {},
        true,
        std::move(output)};
}

} // namespace computelab::vulkan
