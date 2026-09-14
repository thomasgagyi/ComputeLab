#include "vulkan/VulkanTransform.hpp"
#include "vulkan/VulkanTiming.hpp"

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
using detail::CheckResult;

std::uint32_t ValidateDispatch(std::size_t elementCount, const VkPhysicalDeviceLimits& limits)
{
    if (limits.maxComputeWorkGroupInvocations < Ex1LocalSizeX ||
        limits.maxComputeWorkGroupSize[0] < Ex1LocalSizeX ||
        limits.maxComputeWorkGroupSize[1] < 1 || limits.maxComputeWorkGroupSize[2] < 1)
        throw std::runtime_error("Vulkan EX-1 requires a supported 256 x 1 x 1 compute workgroup");
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Vulkan EX-1 elementCount exceeds uint32 shader addressing");
    const auto groups = std::max<std::uint64_t>(1,
        (static_cast<std::uint64_t>(elementCount) + Ex1LocalSizeX - 1) / Ex1LocalSizeX);
    if (groups > limits.maxComputeWorkGroupCount[0] ||
        limits.maxComputeWorkGroupCount[1] < 1 || limits.maxComputeWorkGroupCount[2] < 1)
        throw std::runtime_error("Vulkan EX-1 dispatch exceeds maxComputeWorkGroupCount");
    const auto bytes = std::max<std::uint64_t>(1, elementCount) * sizeof(std::uint32_t);
    if (bytes > limits.maxStorageBufferRange)
        throw std::runtime_error("Vulkan EX-1 buffer exceeds maxStorageBufferRange");
    return static_cast<std::uint32_t>(groups);
}

std::uint32_t SelectComputeTimestampQueue(std::span<const VkQueueFamilyProperties> families)
{
    for (std::size_t index = 0; index < families.size(); ++index)
    {
        const auto& family = families[index];
        if (family.queueCount != 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 &&
            family.timestampValidBits > 0)
        {
            if (family.timestampValidBits < 36 || family.timestampValidBits > 64)
                throw std::runtime_error("Vulkan queue timestampValidBits must be in [36, 64]");
            return static_cast<std::uint32_t>(index);
        }
    }
    throw std::runtime_error("Vulkan device has no compute queue with timestampValidBits in [36, 64]");
}

namespace
{
constexpr std::uint64_t TransferTimeoutNanoseconds = 30'000'000'000ULL;

std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Vulkan EX-1 cannot open SPIR-V: " + path.string());
    const auto length = stream.tellg();
    if (length < 20 || length % 4 != 0)
        throw std::runtime_error("Vulkan EX-1 malformed SPIR-V length: " + path.string());
    std::vector<std::uint32_t> words(static_cast<std::size_t>(length) / sizeof(std::uint32_t));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(length)) ||
        words[0] != 0x07230203U)
        throw std::runtime_error("Vulkan EX-1 malformed SPIR-V data: " + path.string());
    return words;
}

void BufferDependency(VkCommandBuffer commands, VkBuffer buffer,
    VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
    VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess)
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
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}
} // namespace

struct TransformDispatch::Resources
{
    enum class Phase { Empty, Uploaded, Prepared, Submitted, Complete, Failed };
    struct Buffer
    {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkMemoryPropertyFlags memoryFlags = 0;
        void* mapped = nullptr;
    } input, output, upload, readback;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer uploadCommands = VK_NULL_HANDLE;
    VkCommandBuffer computeCommands = VK_NULL_HANDLE;
    VkCommandBuffer readbackCommands = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    VkFence computeFence = VK_NULL_HANDLE;
    VkFence transferFence = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    DeviceDiagnostics diagnostics;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    std::uint32_t elementCount = 0;
    std::uint32_t groupCount = 0;
    VkDeviceSize bufferBytes = 0;
    Phase phase = Phase::Empty;
    bool computePending = false;
    bool transferPending = false;

    ~Resources() noexcept
    {
        if (device != VK_NULL_HANDLE)
        {
            // Abandoned asynchronous work still owns these resources. Only
            // teardown may wait indefinitely on its specific fence, never an
            // idle wait. Device loss permits cleanup; other unexpected failures
            // terminate rather than freeing resources whose completion is unknown.
            DrainForDestruction(computeFence, computePending);
            DrainForDestruction(transferFence, transferPending);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (shader) vkDestroyShaderModule(device, shader, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (descriptorLayout) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            if (queryPool) vkDestroyQueryPool(device, queryPool, nullptr);
            if (computeFence) vkDestroyFence(device, computeFence, nullptr);
            if (transferFence) vkDestroyFence(device, transferFence, nullptr);
            DestroyBuffer(readback);
            DestroyBuffer(upload);
            DestroyBuffer(output);
            DestroyBuffer(input);
            vkDestroyDevice(device, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    void DrainForDestruction(VkFence fence, bool pending) noexcept
    {
        if (!pending) return;
        const auto result = vkWaitForFences(device, 1, &fence, VK_TRUE,
            std::numeric_limits<std::uint64_t>::max());
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
            std::terminate();
    }

    void DestroyBuffer(Buffer& buffer) noexcept
    {
        if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
        if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    }

    void Require(Phase expected, const char* operation) const
    {
        if (phase != expected)
            throw std::logic_error(std::string(operation) + ": invalid Vulkan EX-1 lifecycle phase");
    }

    void CreateBuffer(Buffer& buffer, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, const char* name)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bufferBytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckResult(vkCreateBuffer(device, &info, nullptr, &buffer.handle),
            (std::string("vkCreateBuffer ") + name).c_str());
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        std::uint32_t selected = memoryProperties.memoryTypeCount;
        for (unsigned pass = 0; pass < 2 && selected == memoryProperties.memoryTypeCount; ++pass)
        {
            const auto flags = required | (pass == 0 ? preferred : 0);
            for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
            {
                if ((requirements.memoryTypeBits & (1U << index)) != 0 &&
                    (memoryProperties.memoryTypes[index].propertyFlags & flags) == flags)
                {
                    selected = index;
                    break;
                }
            }
        }
        if (selected == memoryProperties.memoryTypeCount)
            throw std::runtime_error(std::string("Vulkan no compatible memory type for ") + name);
        buffer.memoryFlags = memoryProperties.memoryTypes[selected].propertyFlags;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = selected;
        CheckResult(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory),
            (std::string("vkAllocateMemory ") + name).c_str());
        CheckResult(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0),
            (std::string("vkBindBufferMemory ") + name).c_str());
        if ((required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
            CheckResult(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped),
                (std::string("vkMapMemory ") + name).c_str());
    }

    void MaintainHostCache(const Buffer& buffer, bool flush)
    {
        if ((buffer.memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0) return;
        // Dedicated per-buffer allocations mapped in full: offset zero is atom
        // aligned and VK_WHOLE_SIZE extends to the allocation end, satisfying
        // nonCoherentAtomSize rules even when the buffer size is not aligned.
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = buffer.memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        if (flush)
            CheckResult(vkFlushMappedMemoryRanges(device, 1, &range), "vkFlushMappedMemoryRanges upload");
        else
            CheckResult(vkInvalidateMappedMemoryRanges(device, 1, &range), "vkInvalidateMappedMemoryRanges readback");
    }

    void SetupDevice(std::uint32_t physicalDeviceIndex)
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "ComputeLab EX-1 A7";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        CheckResult(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance EX-1 Vulkan 1.3");
        std::uint32_t count = 0;
        CheckResult(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices count");
        if (physicalDeviceIndex >= count)
            throw std::invalid_argument("Vulkan physicalDeviceIndex is outside Vulkan device enumeration");
        std::vector<VkPhysicalDevice> devices(count);
        CheckResult(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices devices");
        if (physicalDeviceIndex >= count)
            throw std::runtime_error("Vulkan device enumeration changed during setup");
        physicalDevice = devices[physicalDeviceIndex];
        VkPhysicalDeviceIDProperties idProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &idProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
        diagnostics.properties = properties.properties;
        std::copy_n(idProperties.deviceUUID,
            diagnostics.deviceUuid.size(), diagnostics.deviceUuid.begin());
        if (diagnostics.properties.apiVersion < VK_API_VERSION_1_3)
            throw std::runtime_error("Vulkan selected device requires core API 1.3 for EX-1 A7");
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        families.resize(familyCount);
        diagnostics.queueFamilyIndex = SelectComputeTimestampQueue(families);
        diagnostics.queueFamily = families[diagnostics.queueFamilyIndex];
        // Also validates period/valid-bit metadata before pipeline use.
        static_cast<void>(TimestampNanoseconds(0, 0, diagnostics.queueFamily.timestampValidBits,
            diagnostics.properties.limits.timestampPeriod));

        VkPhysicalDeviceVulkan13Features supported13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        supported12.pNext = &supported13;
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
        if (!supported13.synchronization2)
            throw std::runtime_error("Vulkan selected device lacks required synchronization2 feature");
        if (!supported12.hostQueryReset)
            throw std::runtime_error("Vulkan selected device lacks required hostQueryReset feature");

        // Enable only the queried required features, not every supported feature.
        VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        enabled13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features enabled12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enabled12.hostQueryReset = VK_TRUE;
        enabled12.pNext = &enabled13;
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pNext = &enabled12;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        CheckResult(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice EX-1 required features");
        diagnostics.synchronization2Enabled = enabled13.synchronization2 == VK_TRUE;
        diagnostics.hostQueryResetEnabled = enabled12.hostQueryReset == VK_TRUE;
        vkGetDeviceQueue(device, diagnostics.queueFamilyIndex, 0, &queue);
        if (!queue) throw std::runtime_error("vkGetDeviceQueue returned a null EX-1 queue");
    }

    void SetupPipeline(const std::vector<std::uint32_t>& spirv)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        for (std::uint32_t index = 0; index < bindings.size(); ++index)
        {
            bindings[index].binding = index;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        CheckResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout EX-1");
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        CheckResult(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool EX-1");
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout;
        CheckResult(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet), "vkAllocateDescriptorSets EX-1");
        const std::array<VkDescriptorBufferInfo, 2> buffers{{
            {input.handle, 0, bufferBytes}, {output.handle, 0, bufferBytes}}};
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (std::uint32_t index = 0; index < writes.size(); ++index)
        {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &buffers[index];
        }
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(elementCount)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        CheckResult(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout EX-1");
        VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shaderInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = spirv.data();
        CheckResult(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader), "vkCreateShaderModule EX-1");
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shader;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipelineLayout;
        CheckResult(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines EX-1");
    }

    void SetupCommands()
    {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = diagnostics.queueFamilyIndex;
        CheckResult(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool EX-1");
        std::array<VkCommandBuffer, 3> commands{};
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = static_cast<std::uint32_t>(commands.size());
        CheckResult(vkAllocateCommandBuffers(device, &allocation, commands.data()), "vkAllocateCommandBuffers EX-1");
        uploadCommands = commands[0];
        computeCommands = commands[1];
        readbackCommands = commands[2];
        VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2;
        CheckResult(vkCreateQueryPool(device, &queryInfo, nullptr, &queryPool), "vkCreateQueryPool EX-1 timestamps");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CheckResult(vkCreateFence(device, &fenceInfo, nullptr, &computeFence), "vkCreateFence EX-1 compute");
        CheckResult(vkCreateFence(device, &fenceInfo, nullptr, &transferFence), "vkCreateFence EX-1 transfer");

        // No ONE_TIME_SUBMIT or SIMULTANEOUS_USE: reuse only after fence completion.
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CheckResult(vkBeginCommandBuffer(uploadCommands, &begin), "vkBeginCommandBuffer upload");
        // Previous compute input read -> next upload write, and previous output
        // write/readback read -> next compute write. Untimed reuse dependencies.
        BufferDependency(uploadCommands, input.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        BufferDependency(uploadCommands, output.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        if (elementCount != 0)
        {
            VkBufferCopy copy{0, 0, bufferBytes};
            vkCmdCopyBuffer(uploadCommands, upload.handle, input.handle, 1, &copy);
        }
        BufferDependency(uploadCommands, input.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        CheckResult(vkEndCommandBuffer(uploadCommands), "vkEndCommandBuffer upload");

        CheckResult(vkBeginCommandBuffer(computeCommands, &begin), "vkBeginCommandBuffer compute");
        vkCmdBindPipeline(computeCommands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(computeCommands, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(computeCommands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(elementCount), &elementCount);
        // Intentional broad device-timeline dispatch bracket, not exact shader-
        // core execution time. TOP/BOTTOM are timestamp boundaries only.
        vkCmdWriteTimestamp2(computeCommands, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool, 0);
        vkCmdDispatch(computeCommands, groupCount, 1, 1);
        vkCmdWriteTimestamp2(computeCommands, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, queryPool, 1);
        CheckResult(vkEndCommandBuffer(computeCommands), "vkEndCommandBuffer compute");

        CheckResult(vkBeginCommandBuffer(readbackCommands, &begin), "vkBeginCommandBuffer readback");
        BufferDependency(readbackCommands, output.handle,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        BufferDependency(readbackCommands, readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        if (elementCount != 0)
        {
            VkBufferCopy copy{0, 0, bufferBytes};
            vkCmdCopyBuffer(readbackCommands, output.handle, readback.handle, 1, &copy);
        }
        BufferDependency(readbackCommands, readback.handle,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        CheckResult(vkEndCommandBuffer(readbackCommands), "vkEndCommandBuffer readback");
    }

    void Submit(VkCommandBuffer commands, VkFence fence, bool& pending, const char* operation)
    {
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submission{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submission.commandBufferInfoCount = 1;
        submission.pCommandBufferInfos = &commandInfo;
        const auto result = vkQueueSubmit2(queue, 1, &submission, fence);
        if (result != VK_SUCCESS) phase = Phase::Failed;
        CheckResult(result, operation);
        pending = true;
    }

    void Transfer(VkCommandBuffer commands, const char* operation)
    {
        // Any failure leaves the operation unusable (and pending resources owned
        // until teardown). No continuing with incomplete upload/readback data.
        phase = Phase::Failed;
        CheckResult(vkResetFences(device, 1, &transferFence), "vkResetFences transfer");
        Submit(commands, transferFence, transferPending, operation);
        CheckResult(vkWaitForFences(device, 1, &transferFence, VK_TRUE, TransferTimeoutNanoseconds),
            "vkWaitForFences untimed transfer (30 second timeout)");
        transferPending = false;
    }
};

TransformDispatch::TransformDispatch(std::size_t elementCount, const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex) : resources_(std::make_unique<Resources>())
{
    // Member-owned resources clean up even if any later setup step throws.
    const auto spirv = ReadSpirv(spirvPath);
    auto& r = *resources_;
    r.SetupDevice(physicalDeviceIndex);
    r.groupCount = ValidateDispatch(elementCount, r.diagnostics.properties.limits);
    r.elementCount = static_cast<std::uint32_t>(elementCount);
    r.bufferBytes = std::max<VkDeviceSize>(1, elementCount) * sizeof(std::uint32_t);
    r.CreateBuffer(r.input, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, "device-local input");
    r.CreateBuffer(r.output, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, "device-local output");
    r.CreateBuffer(r.upload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "upload staging");
    r.CreateBuffer(r.readback, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "readback staging");
    r.diagnostics.inputMemoryFlags = r.input.memoryFlags;
    r.diagnostics.outputMemoryFlags = r.output.memoryFlags;
    r.diagnostics.uploadMemoryFlags = r.upload.memoryFlags;
    r.diagnostics.readbackMemoryFlags = r.readback.memoryFlags;
    r.SetupPipeline(spirv);
    r.SetupCommands();
}

TransformDispatch::~TransformDispatch() noexcept = default;

void TransformDispatch::Upload(std::span<const std::uint32_t> input)
{
    auto& r = *resources_;
    if (r.phase != Resources::Phase::Empty && r.phase != Resources::Phase::Complete)
        throw std::logic_error("Upload requires initial setup or explicitly completed Vulkan work");
    if (input.size() != r.elementCount)
        throw std::invalid_argument("Upload input size differs from fixed Vulkan EX-1 elementCount");
    r.phase = Resources::Phase::Failed;
    if (!input.empty())
    {
        std::memcpy(r.upload.mapped, input.data(), input.size_bytes());
        r.MaintainHostCache(r.upload, true);
    }
    r.Transfer(r.uploadCommands, "vkQueueSubmit2 untimed upload");
    r.phase = Resources::Phase::Uploaded;
}

void TransformDispatch::PrepareMeasurement()
{
    auto& r = *resources_;
    r.Require(Resources::Phase::Uploaded, "PrepareMeasurement");
    r.phase = Resources::Phase::Failed;
    vkResetQueryPool(r.device, r.queryPool, 0, 2);
    CheckResult(vkResetFences(r.device, 1, &r.computeFence), "vkResetFences compute preparation");
    r.phase = Resources::Phase::Prepared;
}

void TransformDispatch::SubmitTransform()
{
    auto& r = *resources_;
    r.Require(Resources::Phase::Prepared, "SubmitTransform");
    r.Submit(r.computeCommands, r.computeFence, r.computePending, "vkQueueSubmit2 EX-1 transform");
    r.phase = Resources::Phase::Submitted;
}

void TransformDispatch::WaitForCompletion(std::uint64_t timeoutNanoseconds)
{
    auto& r = *resources_;
    r.Require(Resources::Phase::Submitted, "WaitForCompletion");
    const auto result = vkWaitForFences(r.device, 1, &r.computeFence, VK_TRUE, timeoutNanoseconds);
    if (result != VK_SUCCESS && result != VK_TIMEOUT) r.phase = Resources::Phase::Failed;
    CheckResult(result, "vkWaitForFences EX-1 explicit completion (VK_TIMEOUT is incomplete)");
    r.computePending = false;
    r.phase = Resources::Phase::Complete;
}

std::uint64_t TransformDispatch::DeviceElapsedNanoseconds() const
{
    const auto& r = *resources_;
    r.Require(Resources::Phase::Complete, "DeviceElapsedNanoseconds");
    std::array<detail::TimestampQuery, 2> queries{};
    static_assert(sizeof(detail::TimestampQuery) == 2 * sizeof(std::uint64_t));
    const auto result = vkGetQueryPoolResults(r.device, r.queryPool, 0, 2,
        sizeof(queries), queries.data(), sizeof(detail::TimestampQuery), detail::TimestampResultFlags);
    return detail::DecodeTimestampQueries(result, queries,
        r.diagnostics.queueFamily.timestampValidBits, r.diagnostics.properties.limits.timestampPeriod);
}

std::vector<std::uint32_t> TransformDispatch::RetrieveOutput()
{
    auto& r = *resources_;
    r.Require(Resources::Phase::Complete, "RetrieveOutput");
    std::vector<std::uint32_t> output(r.elementCount);
    r.Transfer(r.readbackCommands, "vkQueueSubmit2 untimed readback");
    if (!output.empty())
    {
        r.MaintainHostCache(r.readback, false);
        std::memcpy(output.data(), r.readback.mapped, output.size() * sizeof(std::uint32_t));
    }
    r.phase = Resources::Phase::Complete;
    return output;
}

const std::array<std::uint8_t, 16>&
TransformDispatch::SelectedDeviceUuid() const noexcept
{
    return resources_->diagnostics.deviceUuid;
}

const DeviceDiagnostics& TransformDispatch::Diagnostics() const noexcept
{
    return resources_->diagnostics;
}
} // namespace computelab::vulkan
