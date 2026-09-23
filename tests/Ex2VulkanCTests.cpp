#include "vulkan/Ex2VulkanC.hpp"

#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2CpuOracles.hpp"

#include <gtest/gtest.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

ex2::ContentionConfiguration CConfiguration(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    return {elementCount, activeCounterCount, elementCount};
}

vulkan::Ex2VulkanCOperation MakeOperation(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    return vulkan::Ex2VulkanCOperation{
        CConfiguration(elementCount, activeCounterCount),
        COMPUTELAB_EX2_C_SPIRV_PATH,
        0U};
}

std::vector<std::uint32_t> ExecuteOne(
    vulkan::Ex2VulkanCOperation& operation,
    const std::vector<std::uint32_t>& targets)
{
    operation.UploadTargets(targets);
    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    return operation.RetrieveCounters();
}

void VerifyIndependentOccupancy(
    const std::vector<std::uint32_t>& counters,
    std::uint64_t activeCounterCount,
    std::uint32_t expectedActiveValue)
{
    for (std::size_t index = 0U; index < counters.size(); ++index)
    {
        EXPECT_EQ(
            counters[index],
            index < activeCounterCount ? expectedActiveValue : 0U);
    }
}

void VerifyApprovedCore(
    std::uint64_t activeCounterCount,
    std::uint32_t expectedActiveValue)
{
    constexpr std::uint64_t elementCount = ex2::ContentionElementCount;
    const auto targets = ex2::GenerateContentionTargets(
        elementCount, activeCounterCount);
    const auto reference = ex2::ReferenceContention(
        elementCount, activeCounterCount);
    auto operation = MakeOperation(elementCount, activeCounterCount);

    const auto first = ExecuteOne(operation, targets);
    ASSERT_EQ(first.size(), elementCount);
    EXPECT_EQ(first, reference.counters);
    VerifyIndependentOccupancy(first, activeCounterCount, expectedActiveValue);
    EXPECT_TRUE(std::all_of(
        first.begin() + static_cast<std::ptrdiff_t>(activeCounterCount),
        first.end(),
        [](std::uint32_t value) { return value == 0U; }));
    EXPECT_EQ(std::accumulate(first.begin(), first.end(), 0ULL), elementCount);
    EXPECT_EQ(operation.RetrieveDeviceTargets(), targets);
    EXPECT_TRUE(operation.LastCompletionExecutedShader());

    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveCounters();
    EXPECT_EQ(second, reference.counters);
    EXPECT_EQ(second, first);
    EXPECT_EQ(std::accumulate(second.begin(), second.end(), 0ULL), elementCount);
    EXPECT_EQ(operation.RetrieveDeviceTargets(), targets);
}

TEST(Ex2VulkanCCore, LowContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActiveAll, 1U);
}

TEST(Ex2VulkanCCore, MediumContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActiveOnePer32, 32U);
}

TEST(Ex2VulkanCCore, HighContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActive64, 16'384U);
}

TEST(Ex2VulkanCCorrectnessOnly,
    Padded257DispatchExercisesGuardAndPreservesCompleteState)
{
    constexpr std::uint64_t elementCount = 257U;
    const auto targets = ex2::GenerateContentionTargets(elementCount, elementCount);
    auto operation = MakeOperation(elementCount, elementCount);

    const auto counters = ExecuteOne(operation, targets);
    EXPECT_EQ(counters, std::vector<std::uint32_t>(elementCount, 1U));
    EXPECT_EQ(operation.RetrieveDeviceTargets(), targets);
    EXPECT_EQ(std::accumulate(counters.begin(), counters.end(), 0ULL), elementCount);
    EXPECT_TRUE(operation.LastCompletionExecutedShader());

    VkPhysicalDeviceLimits limits{};
    limits.maxStorageBufferRange = std::numeric_limits<std::uint32_t>::max();
    limits.maxPushConstantsSize = 128U;
    limits.maxPerStageDescriptorStorageBuffers = 2U;
    limits.maxDescriptorSetStorageBuffers = 2U;
    limits.maxComputeWorkGroupInvocations = 256U;
    limits.maxComputeWorkGroupSize[0] = 256U;
    limits.maxComputeWorkGroupSize[1] = 1U;
    limits.maxComputeWorkGroupSize[2] = 1U;
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxComputeWorkGroupCount[1] = 1U;
    limits.maxComputeWorkGroupCount[2] = 1U;
    const auto shape = vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(elementCount, elementCount), limits, 1'028U);
    EXPECT_EQ(shape.groupCountX, 2U);
    EXPECT_EQ(shape.localSizeX, 256U);

    const auto configuration = ex2::MakeConfiguration(
        CConfiguration(elementCount, elementCount));
    EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
        ex2::CellEligibility::CorrectnessOnly);
}

TEST(Ex2VulkanCCorrectnessOnly, ZeroLifecycleUsesNoLogicalPayloadOrShader)
{
    const std::vector<std::uint32_t> targets;
    auto operation = MakeOperation(0U, 0U);

    const auto counters = ExecuteOne(operation, targets);
    EXPECT_TRUE(counters.empty());
    EXPECT_TRUE(operation.RetrieveDeviceTargets().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedShader());

    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    EXPECT_TRUE(operation.RetrieveCounters().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedShader());
}

TEST(Ex2VulkanCValidation,
    RejectsWrongLengthsRangeAndAlteredTargetsAndInvalidatesReadiness)
{
    const auto exact = ex2::GenerateContentionTargets(8U, 2U);
    const std::vector<std::uint32_t> shortTargets(exact.begin(), exact.end() - 1);
    auto longTargets = exact;
    longTargets.push_back(0U);
    auto outOfRange = exact;
    outOfRange[0] = 2U;
    auto alteredInRange = exact;
    alteredInRange[0] = alteredInRange[0] == 0U ? 1U : 0U;

    auto operation = MakeOperation(8U, 2U);
    operation.UploadTargets(exact);
    operation.PrepareReset();
    EXPECT_THROW(operation.UploadTargets(shortTargets), std::invalid_argument);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.PrepareReset(), std::logic_error);
    EXPECT_THROW(operation.UploadTargets(longTargets), std::invalid_argument);
    EXPECT_THROW(operation.UploadTargets(outOfRange), std::invalid_argument);
    EXPECT_THROW(operation.UploadTargets(alteredInRange), std::invalid_argument);

    EXPECT_EQ(ExecuteOne(operation, exact),
        ex2::ReferenceContention(8U, 2U).counters);
}

TEST(Ex2VulkanCValidation, RejectsMalformedConfigurationsBeforeVulkanCreation)
{
    for (const auto configuration : {
        ex2::ContentionConfiguration{1U, 0U, 1U},
        ex2::ContentionConfiguration{1U, 2U, 1U},
        ex2::ContentionConfiguration{8U, 3U, 8U},
        ex2::ContentionConfiguration{8U, 2U, 7U},
        ex2::ContentionConfiguration{8191U, 1U, 8191U},
        ex2::ContentionConfiguration{
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U,
            1U,
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U}})
    {
        EXPECT_THROW(vulkan::detail::ValidateEx2VulkanCConfiguration(
            ex2::MakeConfiguration(configuration)), std::invalid_argument);
    }

    auto wrongRevision = ex2::MakeConfiguration(CConfiguration(1U, 1U));
    wrongRevision.common.generatorRevision = "wrong-revision";
    EXPECT_THROW(vulkan::detail::ValidateEx2VulkanCConfiguration(wrongRevision),
        std::invalid_argument);
    auto wrongBoundary = ex2::MakeConfiguration(CConfiguration(1U, 1U));
    wrongBoundary.common.operationBoundary =
        ex2::OperationBoundary::SingleDispatchCompletion;
    EXPECT_THROW(vulkan::detail::ValidateEx2VulkanCConfiguration(wrongBoundary),
        std::invalid_argument);
}

TEST(Ex2VulkanC, InvalidLifecycleTransitionsFailExplicitly)
{
    const auto targets = ex2::GenerateContentionTargets(1U, 1U);
    auto operation = MakeOperation(1U, 1U);

    EXPECT_THROW(operation.PrepareReset(), std::logic_error);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveCounters()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveDeviceTargets()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionExecutedShader()),
        std::logic_error);

    operation.UploadTargets(targets);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    operation.PrepareReset();
    operation.SubmitAtomic();
    EXPECT_THROW(operation.PrepareReset(), std::logic_error);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.UploadTargets(targets), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveCounters()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);

    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveCounters(), (std::vector<std::uint32_t>{1U}));
}

TEST(Ex2VulkanC, SuccessfulUploadOwnsCallerBytesThroughCompletion)
{
    const auto expected = ex2::GenerateContentionTargets(257U, 257U);
    auto operation = MakeOperation(257U, 257U);
    {
        auto callerTargets = expected;
        operation.UploadTargets(callerTargets);
    }
    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveCounters(),
        std::vector<std::uint32_t>(257U, 1U));
    EXPECT_EQ(operation.RetrieveDeviceTargets(), expected);
}

TEST(Ex2VulkanCGuards, CalculatesDispatchAndRejectsResourceLimitsPurely)
{
    VkPhysicalDeviceLimits limits{};
    limits.maxStorageBufferRange = 4'096U;
    limits.maxPushConstantsSize = 4U;
    limits.maxPerStageDescriptorStorageBuffers = 2U;
    limits.maxDescriptorSetStorageBuffers = 2U;
    limits.maxComputeWorkGroupInvocations = 256U;
    limits.maxComputeWorkGroupSize[0] = 256U;
    limits.maxComputeWorkGroupSize[1] = 1U;
    limits.maxComputeWorkGroupSize[2] = 1U;
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxComputeWorkGroupCount[1] = 1U;
    limits.maxComputeWorkGroupCount[2] = 1U;

    EXPECT_EQ(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(0U, 0U), limits, 4'096U).groupCountX, 0U);
    EXPECT_EQ(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(256U, 256U), limits, 4'096U).groupCountX, 1U);
    EXPECT_EQ(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(257U, 257U), limits, 4'096U).groupCountX, 2U);

    auto bad = limits;
    bad.maxComputeWorkGroupCount[0] = 1U;
    EXPECT_THROW(static_cast<void>(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(257U, 257U), bad, 4'096U)), std::invalid_argument);
    bad = limits;
    bad.maxComputeWorkGroupInvocations = 255U;
    EXPECT_THROW(static_cast<void>(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(1U, 1U), bad, 4'096U)), std::invalid_argument);
    bad = limits;
    bad.maxPushConstantsSize = 3U;
    EXPECT_THROW(static_cast<void>(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(1U, 1U), bad, 4'096U)), std::invalid_argument);
    bad = limits;
    bad.maxDescriptorSetStorageBuffers = 1U;
    EXPECT_THROW(static_cast<void>(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(1U, 1U), bad, 4'096U)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vulkan::detail::ValidateEx2VulkanCDispatchShape(
        CConfiguration(257U, 257U), limits, 1'027U)), std::length_error);
}

TEST(Ex2VulkanCGuards, RejectsByteAllocationHeapQueueAndMemoryTypeLimits)
{
    EXPECT_EQ(vulkan::detail::CalculateEx2VulkanCBufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(static_cast<void>(vulkan::detail::CalculateEx2VulkanCBufferByteCount(
        257U, 1'024U)), std::length_error);
    EXPECT_THROW(static_cast<void>(vulkan::detail::CalculateEx2VulkanCBufferByteCount(
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U,
        std::numeric_limits<VkDeviceSize>::max())), std::invalid_argument);
    EXPECT_NO_THROW(vulkan::detail::ValidateEx2VulkanCAllocationCount(4U));
    EXPECT_THROW(vulkan::detail::ValidateEx2VulkanCAllocationCount(3U),
        std::length_error);
    EXPECT_NO_THROW(vulkan::detail::ValidateEx2VulkanCHeapFeasibility(4U, 4U, 8U));
    EXPECT_THROW(vulkan::detail::ValidateEx2VulkanCHeapFeasibility(5U, 4U, 8U),
        std::length_error);

    const std::array<VkQueueFamilyProperties, 2U> queues{{
        VkQueueFamilyProperties{VK_QUEUE_GRAPHICS_BIT, 0U, 0U, {}},
        VkQueueFamilyProperties{VK_QUEUE_COMPUTE_BIT, 1U, 0U, {}}}};
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanCQueue(queues), 1U);
    const std::array<VkQueueFamilyProperties, 1U> noCompute{{
        VkQueueFamilyProperties{VK_QUEUE_GRAPHICS_BIT, 1U, 0U, {}}}};
    EXPECT_THROW(static_cast<void>(vulkan::detail::SelectEx2VulkanCQueue(noCompute)),
        std::invalid_argument);

    VkPhysicalDeviceMemoryProperties memory{};
    memory.memoryTypeCount = 2U;
    memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memory.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanCMemoryType(
        0x3U, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memory), 1U);
    EXPECT_THROW(static_cast<void>(vulkan::detail::SelectEx2VulkanCMemoryType(
        0x1U, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0U, memory)),
        std::invalid_argument);
}

TEST(Ex2VulkanCGuards, AlignsNoncoherentRangesWithoutPassingAllocationEnd)
{
    const auto middle = vulkan::detail::AlignEx2VulkanCNoncoherentRange(
        65U, 32U, 512U, 64U);
    EXPECT_EQ(middle.offset, 64U);
    EXPECT_EQ(middle.size, 64U);
    const auto ending = vulkan::detail::AlignEx2VulkanCNoncoherentRange(
        64U, 448U, 512U, 64U);
    EXPECT_EQ(ending.offset, 64U);
    EXPECT_EQ(ending.size, VK_WHOLE_SIZE);
    EXPECT_THROW(static_cast<void>(vulkan::detail::AlignEx2VulkanCNoncoherentRange(
        500U, 13U, 512U, 64U)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(vulkan::detail::AlignEx2VulkanCNoncoherentRange(
        0U, 1U, 512U, 0U)), std::invalid_argument);
}

TEST(Ex2VulkanC, DeviceIdentityResourceMetadataAndShaderAreExplicit)
{
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &application;
    VkInstance instance{VK_NULL_HANDLE};
    ASSERT_EQ(vkCreateInstance(&createInfo, nullptr, &instance), VK_SUCCESS);
    std::uint32_t count{};
    ASSERT_EQ(vkEnumeratePhysicalDevices(instance, &count, nullptr), VK_SUCCESS);
    ASSERT_GT(count, 0U);
    std::vector<VkPhysicalDevice> devices(count);
    ASSERT_EQ(vkEnumeratePhysicalDevices(instance, &count, devices.data()), VK_SUCCESS);
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(devices[0], &properties);
    std::array<std::uint8_t, VK_UUID_SIZE> expectedUuid{};
    std::copy_n(ids.deviceUUID, expectedUuid.size(), expectedUuid.begin());
    vkDestroyInstance(instance, nullptr);

    auto operation = MakeOperation(1U, 1U);
    EXPECT_EQ(operation.SelectedDeviceUuid(), expectedUuid);
    EXPECT_EQ(operation.Diagnostics().physicalDeviceIndex, 0U);
    EXPECT_NE(operation.Diagnostics().queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT, 0U);
    EXPECT_TRUE(operation.Diagnostics().synchronization2Enabled);
    EXPECT_EQ(operation.LoadedShaderName(), "Ex2C.comp.spv");
    ASSERT_FALSE(operation.LoadedSpirv().empty());
    EXPECT_EQ(operation.LoadedSpirv().front(), 0x07230203U);
    EXPECT_EQ(operation.ElementCount(), 1U);
    EXPECT_EQ(operation.ActiveCounterCount(), 1U);
    EXPECT_EQ(operation.AllocatedCounterCount(), 1U);
    EXPECT_GE(operation.Diagnostics().targetAllocationBytes, sizeof(std::uint32_t));
    EXPECT_GE(operation.Diagnostics().counterAllocationBytes, sizeof(std::uint32_t));

    EXPECT_THROW(vulkan::Ex2VulkanCOperation(
        CConfiguration(1U, 1U), COMPUTELAB_EX2_C_SPIRV_PATH,
        std::numeric_limits<std::uint32_t>::max()), std::invalid_argument);
}

TEST(Ex2VulkanC,
    SubmissionWaitAndUncertainResourceClassificationsAreConservative)
{
    using Resource = vulkan::detail::Ex2VulkanCResourceDisposition;
    using State = vulkan::detail::Ex2VulkanCOperationState;
    using Submission = vulkan::detail::Ex2VulkanCSubmissionDisposition;

    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCSubmissionResult(VK_SUCCESS),
        Submission::Submitted);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCSubmissionResult(
        VK_ERROR_OUT_OF_DEVICE_MEMORY), Submission::FailedWithoutSubmission);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCSubmissionResult(VK_ERROR_DEVICE_LOST),
        Submission::CompletionUncertain);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCWaitResult(VK_TIMEOUT),
        State::CompletionUncertain);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCWaitResult(VK_ERROR_DEVICE_LOST),
        State::CompletionUncertain);
    EXPECT_FALSE(vulkan::detail::IsEx2VulkanCOperationStateReusable(
        State::CompletionUncertain));
    EXPECT_FALSE(vulkan::detail::IsEx2VulkanCOperationStateReusable(State::Failed));
    EXPECT_TRUE(vulkan::detail::IsEx2VulkanCOperationStateReusable(State::Complete));
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCResourceDisposition(false, false, false),
        Resource::DestroyNormally);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCResourceDisposition(false, true, false),
        Resource::DrainAtomicThenDestroy);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCResourceDisposition(true, false, false),
        Resource::PreserveForProcessTeardown);
    EXPECT_EQ(vulkan::detail::ClassifyEx2VulkanCResourceDisposition(false, false, true),
        Resource::PreserveForProcessTeardown);

    EXPECT_EQ(vulkan::ToString(vulkan::Ex2VulkanCNativePhase::CounterReset),
        "counter reset");
    EXPECT_EQ(vulkan::ToString(vulkan::Ex2VulkanCNativePhase::CounterReadback),
        "counter readback");
}

} // namespace
