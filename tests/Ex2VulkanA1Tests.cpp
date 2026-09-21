#include "vulkan/Ex2VulkanA1.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

ex2::LinearConfiguration A1Configuration(std::uint64_t elementCount)
{
    return {ex2::LinearVariant::A1, elementCount};
}

std::vector<std::uint32_t> Execute(
    vulkan::Ex2VulkanA1Operation& operation,
    const std::vector<std::uint32_t>& input)
{
    operation.Upload(input);
    operation.Prepare();
    operation.SubmitA1();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(Ex2VulkanA1, ExactBoundedSizesMatchIndependentCpuOracleAndPreserveDeviceInput)
{
    constexpr std::array<std::uint64_t, 7U> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 262'144U};

    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, size);
        const auto expected = ex2::ReferenceA1(input);
        vulkan::Ex2VulkanA1Operation operation{
            A1Configuration(size), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};

        const auto actual = Execute(operation, input);

        EXPECT_EQ(actual.size(), size);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(operation.RetrieveDeviceInput(), input);
        EXPECT_EQ(operation.LastCompletionExecutedShader(), size != 0U);
    }
}

TEST(Ex2VulkanA1, LiteralFourWordFixtureIndependentlyAnchorsOracleAndGpu)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> expected{
        0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U};
    EXPECT_EQ(ex2::ReferenceA1(input), expected);

    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(input.size()), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};
    EXPECT_EQ(Execute(operation, input), expected);
    EXPECT_TRUE(operation.LastCompletionExecutedShader());
}

TEST(Ex2VulkanA1, ZeroElementsUseNoDispatchButPreserveTheLifecycle)
{
    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(0U), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};
    operation.Upload({});
    operation.Prepare();
    operation.SubmitA1();
    operation.WaitForCompletion();

    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceInput().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedShader());
}

TEST(Ex2VulkanA1, RepeatedIdenticalOperationsAreDeterministic)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 4'097U);
    const auto expected = ex2::ReferenceA1(input);
    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(input.size()), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};

    const auto first = Execute(operation, input);
    const auto second = Execute(operation, input);

    EXPECT_EQ(first, expected);
    EXPECT_EQ(second, expected);
    EXPECT_EQ(first, second);
}

TEST(Ex2VulkanA1, RepeatedDifferentUploadsCannotReturnStaleOutput)
{
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(
        0xFEDCBA9876543210ULL, 257U);
    ASSERT_NE(firstInput, secondInput);
    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(firstInput.size()), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};

    const auto first = Execute(operation, firstInput);
    const auto second = Execute(operation, secondInput);

    EXPECT_EQ(first, ex2::ReferenceA1(firstInput));
    EXPECT_EQ(second, ex2::ReferenceA1(secondInput));
    EXPECT_NE(first, second);
    EXPECT_EQ(operation.RetrieveDeviceInput(), secondInput);
}

TEST(Ex2VulkanA1, InvalidLifecycleTransitionsFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(input.size()), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};

    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceInput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedShader()),
        std::logic_error);

    operation.Upload(input);
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.Prepare();
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    operation.SubmitA1();
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.Upload(input), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveOutput(), ex2::ReferenceA1(input));
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
}

TEST(Ex2VulkanA1, UploadRequiresExactConfiguredLength)
{
    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(1U), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);
    const std::vector<std::uint32_t> tooLarge{1U, 2U};
    EXPECT_THROW(operation.Upload(tooLarge), std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, RejectsNonA1AndOversizedSemanticConfigurations)
{
    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupInvocations = 256U;
    limits.maxComputeWorkGroupSize[0] = 256U;
    limits.maxComputeWorkGroupSize[1] = 1U;
    limits.maxComputeWorkGroupSize[2] = 1U;
    limits.maxComputeWorkGroupCount[0] =
        std::numeric_limits<std::uint32_t>::max();
    limits.maxComputeWorkGroupCount[1] = 1U;
    limits.maxComputeWorkGroupCount[2] = 1U;
    limits.maxStorageBufferRange =
        std::numeric_limits<std::uint32_t>::max();
    limits.maxPushConstantsSize = 4U;

    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA1DispatchShape(
            {ex2::LinearVariant::A2, 1U},
            limits,
            std::numeric_limits<VkDeviceSize>::max())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA1DispatchShape(
            A1Configuration(
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U),
            limits,
            std::numeric_limits<VkDeviceSize>::max())),
        std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, CalculatesBoundaryDispatchAndRejectsDeviceLimits)
{
    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupInvocations = 256U;
    limits.maxComputeWorkGroupSize[0] = 256U;
    limits.maxComputeWorkGroupSize[1] = 1U;
    limits.maxComputeWorkGroupSize[2] = 1U;
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxComputeWorkGroupCount[1] = 1U;
    limits.maxComputeWorkGroupCount[2] = 1U;
    limits.maxStorageBufferRange = 1'024U;
    limits.maxPushConstantsSize = 4U;

    const auto zero = vulkan::detail::ValidateEx2VulkanA1DispatchShape(
        A1Configuration(0U), limits, 1'024U);
    EXPECT_EQ(zero.groupCountX, 0U);
    const auto exact = vulkan::detail::ValidateEx2VulkanA1DispatchShape(
        A1Configuration(256U), limits, 1'024U);
    EXPECT_EQ(exact.groupCountX, 1U);
    limits.maxStorageBufferRange = 1'028U;
    const auto padded = vulkan::detail::ValidateEx2VulkanA1DispatchShape(
        A1Configuration(257U), limits, 1'028U);
    EXPECT_EQ(padded.groupCountX, 2U);

    limits.maxComputeWorkGroupCount[0] = 1U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA1DispatchShape(
            A1Configuration(257U), limits, 1'028U)),
        std::invalid_argument);
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxStorageBufferRange = 1'023U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA1DispatchShape(
            A1Configuration(256U), limits, 1'024U)),
        std::length_error);
    limits.maxStorageBufferRange = 1'024U;
    limits.maxComputeWorkGroupInvocations = 255U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA1DispatchShape(
            A1Configuration(1U), limits, 1'024U)),
        std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, RejectsBufferByteOverflowWithoutAllocation)
{
    EXPECT_EQ(
        vulkan::detail::CalculateEx2VulkanA1BufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::CalculateEx2VulkanA1BufferByteCount(
            257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::CalculateEx2VulkanA1BufferByteCount(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U,
            std::numeric_limits<VkDeviceSize>::max())),
        std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, SelectsCompatibleMemoryAndRejectsIncompatibility)
{
    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 3U;
    properties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    properties.memoryTypes[2].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(
        vulkan::detail::SelectEx2VulkanA1MemoryType(
            0b111U,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            properties),
        1U);
    EXPECT_EQ(
        vulkan::detail::SelectEx2VulkanA1MemoryType(
            0b101U,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            properties),
        0U);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::SelectEx2VulkanA1MemoryType(
            0b011U,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0U,
            properties)),
        std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, AlignsNoncoherentRangesWithoutOverflow)
{
    const auto aligned = vulkan::detail::AlignEx2VulkanA1NoncoherentRange(
        65U, 63U, 256U, 64U);
    EXPECT_EQ(aligned.offset, 64U);
    EXPECT_EQ(aligned.size, 64U);

    const auto allocationEnd =
        vulkan::detail::AlignEx2VulkanA1NoncoherentRange(
            65U, 191U, 256U, 64U);
    EXPECT_EQ(allocationEnd.offset, 64U);
    EXPECT_EQ(allocationEnd.size, VK_WHOLE_SIZE);

    EXPECT_THROW(
        static_cast<void>(vulkan::detail::AlignEx2VulkanA1NoncoherentRange(
            257U, 0U, 256U, 64U)),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::AlignEx2VulkanA1NoncoherentRange(
            0U, 1U, 1U, 0U)),
        std::invalid_argument);
}

TEST(Ex2VulkanA1Guards, QueueSelectionRequiresComputeCapability)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanA1Queue(families), 1U);
    families[1].queueCount = 0U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::SelectEx2VulkanA1Queue(families)),
        std::invalid_argument);
}

TEST(Ex2VulkanA1, SelectedIndexAndUuidComeFromIndependentVulkanQuery)
{
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "ComputeLab EX-2 Vulkan A1 test";
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &application;
    VkInstance instance{VK_NULL_HANDLE};
    ASSERT_EQ(vkCreateInstance(&createInfo, nullptr, &instance), VK_SUCCESS);

    std::uint32_t count{};
    ASSERT_EQ(vkEnumeratePhysicalDevices(instance, &count, nullptr), VK_SUCCESS);
    ASSERT_GT(count, 0U);
    std::vector<VkPhysicalDevice> devices(count);
    ASSERT_EQ(
        vkEnumeratePhysicalDevices(instance, &count, devices.data()),
        VK_SUCCESS);
    VkPhysicalDeviceIDProperties id{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &id;
    vkGetPhysicalDeviceProperties2(devices[0], &properties);
    std::array<std::uint8_t, VK_UUID_SIZE> expected{};
    std::copy_n(id.deviceUUID, expected.size(), expected.begin());
    vkDestroyInstance(instance, nullptr);

    vulkan::Ex2VulkanA1Operation operation{
        A1Configuration(1U), COMPUTELAB_EX2_A1_SPIRV_PATH, 0U};
    EXPECT_EQ(operation.Diagnostics().physicalDeviceIndex, 0U);
    EXPECT_EQ(operation.SelectedDeviceUuid(), expected);
}

TEST(Ex2VulkanA1, InvalidDeviceSelectionDoesNotSubstituteAnotherDevice)
{
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanA1Operation{
            A1Configuration(1U),
            COMPUTELAB_EX2_A1_SPIRV_PATH,
            std::numeric_limits<std::uint32_t>::max()}),
        std::invalid_argument);
}

TEST(Ex2VulkanA1, NativeErrorRetainsPhaseResultAndOperation)
{
    const vulkan::Ex2VulkanA1NativeError error{
        vulkan::Ex2VulkanA1NativePhase::CompletionWait,
        VK_TIMEOUT,
        "synthetic wait",
        "synthetic wait timed out"};
    EXPECT_EQ(
        error.Phase(),
        vulkan::Ex2VulkanA1NativePhase::CompletionWait);
    EXPECT_EQ(error.NativeResult(), VK_TIMEOUT);
    EXPECT_EQ(error.Operation(), "synthetic wait");
    EXPECT_NE(std::string{error.what()}.find("timed out"), std::string::npos);
}

TEST(Ex2VulkanA1Guards, SubmissionResultsSeparateGuaranteedFailureFromUncertainty)
{
    using vulkan::detail::ClassifyEx2VulkanA1SubmissionResult;
    using vulkan::detail::Ex2VulkanA1SubmissionDisposition;

    EXPECT_EQ(
        ClassifyEx2VulkanA1SubmissionResult(VK_SUCCESS),
        Ex2VulkanA1SubmissionDisposition::Submitted);
    EXPECT_EQ(
        ClassifyEx2VulkanA1SubmissionResult(VK_ERROR_OUT_OF_HOST_MEMORY),
        Ex2VulkanA1SubmissionDisposition::FailedWithoutSubmission);
    EXPECT_EQ(
        ClassifyEx2VulkanA1SubmissionResult(VK_ERROR_OUT_OF_DEVICE_MEMORY),
        Ex2VulkanA1SubmissionDisposition::FailedWithoutSubmission);
    EXPECT_EQ(
        ClassifyEx2VulkanA1SubmissionResult(VK_ERROR_DEVICE_LOST),
        Ex2VulkanA1SubmissionDisposition::CompletionUncertain);
    EXPECT_EQ(
        ClassifyEx2VulkanA1SubmissionResult(VK_TIMEOUT),
        Ex2VulkanA1SubmissionDisposition::CompletionUncertain);
}

TEST(Ex2VulkanA1Guards, CleanupPreservesEveryIndependentlyPendingResource)
{
    using vulkan::detail::ClassifyEx2VulkanA1ResourceDisposition;
    using vulkan::detail::Ex2VulkanA1ResourceDisposition;

    // A guaranteed compute- or transfer-submission failure with no earlier
    // work may clean up immediately.
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(false, false, false),
        Ex2VulkanA1ResourceDisposition::DestroyNormally);

    // An earlier compute submission can be drained by its own fence before
    // cleanup, even if a later independent submission failed without enqueue.
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(false, true, false),
        Ex2VulkanA1ResourceDisposition::DrainComputeThenDestroy);

    // Synchronous transfer helpers have no safe destructor drain after their
    // wait becomes uncertain, so any retained transfer-pending state wins.
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(false, false, true),
        Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(false, true, true),
        Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(true, false, false),
        Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(
        ClassifyEx2VulkanA1ResourceDisposition(true, true, false),
        Ex2VulkanA1ResourceDisposition::PreserveForProcessTeardown);
}

} // namespace
