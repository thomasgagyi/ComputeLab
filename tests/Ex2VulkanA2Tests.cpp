#include "vulkan/Ex2VulkanA2.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

ex2::LinearConfiguration A2Configuration(std::uint64_t elementCount)
{
    return {ex2::LinearVariant::A2, elementCount};
}

std::vector<std::uint32_t> Execute(
    vulkan::Ex2VulkanA2Operation& operation,
    const std::vector<std::uint32_t>& input)
{
    operation.Upload(input);
    operation.Prepare();
    operation.SubmitA2();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

VkPhysicalDeviceLimits TestLimits()
{
    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupInvocations = 256U;
    limits.maxComputeWorkGroupSize[0] = 256U;
    limits.maxComputeWorkGroupSize[1] = 1U;
    limits.maxComputeWorkGroupSize[2] = 1U;
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxComputeWorkGroupCount[1] = 1U;
    limits.maxComputeWorkGroupCount[2] = 1U;
    limits.maxStorageBufferRange = 1'028U;
    limits.maxPushConstantsSize = 4U;
    return limits;
}

TEST(Ex2VulkanA2, ExactRequiredSizesMatchCpuOracleAndPreserveDeviceInput)
{
    constexpr std::array<std::uint64_t, 8> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 4'097U, 262'144U};
    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, size);
        const auto expected = ex2::ReferenceA2(input);
        vulkan::Ex2VulkanA2Operation operation{
            A2Configuration(size), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
        const auto actual = Execute(operation, input);
        EXPECT_EQ(actual.size(), size);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(operation.RetrieveDeviceInput(), input);
        EXPECT_EQ(operation.LastCompletionExecutedShader(), size != 0U);
    }
}

TEST(Ex2VulkanA2, LiteralFixtureIndependentlyAnchorsOracleAndGpu)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> expected{
        0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U};
    EXPECT_EQ(ex2::ReferenceA2(input), expected);
    vulkan::Ex2VulkanA2Operation operation{
        A2Configuration(input.size()), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
    EXPECT_EQ(Execute(operation, input), expected);
}

TEST(Ex2VulkanA2, ZeroElementsUseNoDispatchButPreserveLifecycle)
{
    vulkan::Ex2VulkanA2Operation operation{
        A2Configuration(0U), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
    operation.Upload({});
    operation.Prepare();
    operation.SubmitA2();
    operation.WaitForCompletion();
    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceInput().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedShader());
}

TEST(Ex2VulkanA2, RepeatedOperationsAreDeterministicAndDoNotReturnStaleOutput)
{
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(
        0xFEDCBA9876543210ULL, 257U);
    vulkan::Ex2VulkanA2Operation operation{
        A2Configuration(firstInput.size()), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
    const auto first = Execute(operation, firstInput);
    const auto repeated = Execute(operation, firstInput);
    const auto second = Execute(operation, secondInput);
    EXPECT_EQ(first, ex2::ReferenceA2(firstInput));
    EXPECT_EQ(repeated, first);
    EXPECT_EQ(second, ex2::ReferenceA2(secondInput));
    EXPECT_NE(second, first);
    EXPECT_EQ(operation.RetrieveDeviceInput(), secondInput);
}

TEST(Ex2VulkanA2, InvalidLifecycleAndUploadLengthFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    vulkan::Ex2VulkanA2Operation operation{
        A2Configuration(1U), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.SubmitA2(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);
    operation.Upload(input);
    EXPECT_THROW(operation.SubmitA2(), std::logic_error);
    operation.Prepare();
    operation.SubmitA2();
    EXPECT_THROW(operation.Upload(input), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveOutput(), ex2::ReferenceA2(input));
}

TEST(Ex2VulkanA2Guards, RejectsA1OversizedAndInfeasibleDispatches)
{
    auto limits = TestLimits();
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanA2Operation{
            {ex2::LinearVariant::A1, 1U},
            COMPUTELAB_EX2_A2_SPIRV_PATH,
            0U}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA2DispatchShape(
            {ex2::LinearVariant::A1, 1U}, limits, 1'028U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA2DispatchShape(
            A2Configuration(
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U),
            limits,
            std::numeric_limits<VkDeviceSize>::max())),
        std::invalid_argument);
    limits.maxComputeWorkGroupCount[0] = 1U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanA2DispatchShape(
            A2Configuration(257U), limits, 1'028U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::CalculateEx2VulkanA2BufferByteCount(
            257U, 1'024U)),
        std::length_error);
}

TEST(Ex2VulkanA2Guards, CalculatesExactAndPaddedDispatchesWithoutAllocation)
{
    const auto limits = TestLimits();
    const auto zero = vulkan::detail::ValidateEx2VulkanA2DispatchShape(
        A2Configuration(0U), limits, 1'028U);
    const auto exact = vulkan::detail::ValidateEx2VulkanA2DispatchShape(
        A2Configuration(256U), limits, 1'028U);
    const auto padded = vulkan::detail::ValidateEx2VulkanA2DispatchShape(
        A2Configuration(257U), limits, 1'028U);
    EXPECT_EQ(zero.groupCountX, 0U);
    EXPECT_EQ(exact.groupCountX, 1U);
    EXPECT_EQ(padded.groupCountX, 2U);
    EXPECT_EQ(padded.localSizeX, 256U);
}

TEST(Ex2VulkanA2Guards, ReusesVerifiedQueueMemoryAndNoncoherentRules)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanA2Queue(families), 1U);

    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 2U;
    properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanA2MemoryType(
        0b11U,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        properties), 1U);

    const auto aligned = vulkan::detail::AlignEx2VulkanA2NoncoherentRange(
        65U, 63U, 256U, 64U);
    EXPECT_EQ(aligned.offset, 64U);
    EXPECT_EQ(aligned.size, 64U);
}

TEST(Ex2VulkanA2, DeviceIdentityAndSelectorFailureRemainExplicit)
{
    vulkan::Ex2VulkanA2Operation operation{
        A2Configuration(1U), COMPUTELAB_EX2_A2_SPIRV_PATH, 0U};
    EXPECT_EQ(operation.Diagnostics().physicalDeviceIndex, 0U);
    EXPECT_FALSE(std::all_of(
        operation.SelectedDeviceUuid().begin(),
        operation.SelectedDeviceUuid().end(),
        [](std::uint8_t value) { return value == 0U; }));
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanA2Operation{
            A2Configuration(1U),
            COMPUTELAB_EX2_A2_SPIRV_PATH,
            std::numeric_limits<std::uint32_t>::max()}),
        std::invalid_argument);
}

TEST(Ex2VulkanA2, NativeErrorAndResourceUncertaintyRemainClassified)
{
    const vulkan::Ex2VulkanA2NativeError error{
        vulkan::Ex2VulkanA2NativePhase::CompletionWait,
        VK_TIMEOUT,
        "synthetic wait",
        "synthetic wait timed out"};
    EXPECT_EQ(error.NativeResult(), VK_TIMEOUT);
    EXPECT_EQ(error.Operation(), "synthetic wait");
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanA2SubmissionResult(
            VK_ERROR_DEVICE_LOST),
        vulkan::detail::Ex2VulkanA2SubmissionDisposition::CompletionUncertain);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanA2ResourceDisposition(
            true, true, false),
        vulkan::detail::Ex2VulkanA2ResourceDisposition::PreserveForProcessTeardown);
}

TEST(Ex2VulkanA2, PhaseConversionAndDiagnosticRelabelingAreExplicit)
{
    using A1 = vulkan::Ex2VulkanA1NativePhase;
    using A2 = vulkan::Ex2VulkanA2NativePhase;
    EXPECT_EQ(vulkan::detail::ConvertEx2VulkanA1PhaseForA2(A1::Preparation),
        A2::Preparation);
    EXPECT_EQ(vulkan::detail::ConvertEx2VulkanA2PhaseForA1(A2::CompletionWait),
        A1::CompletionWait);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ConvertEx2VulkanA1PhaseForA2(
            static_cast<A1>(-1))),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ConvertEx2VulkanA2PhaseForA1(
            static_cast<A2>(-1))),
        std::invalid_argument);
    EXPECT_EQ(vulkan::detail::RelabelEx2VulkanA1DiagnosticForA2(
        "unrelated A1 token; EX-2 Vulkan A1 submission"),
        "unrelated A1 token; EX-2 Vulkan A2 submission");
}

} // namespace
