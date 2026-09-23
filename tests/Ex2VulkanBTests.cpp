#include "vulkan/Ex2VulkanB.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <vulkan/vulkan.h>

#include <algorithm>
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

ex2::IndexedConfiguration BConfiguration(
    ex2::IndexedVariant variant,
    std::uint64_t elementCount,
    ex2::IndexPattern pattern)
{
    return {variant, elementCount, pattern};
}

std::vector<std::uint32_t> GenerateIndices(
    ex2::IndexPattern pattern,
    std::uint64_t seed,
    std::uint64_t elementCount)
{
    return pattern == ex2::IndexPattern::StructuredV1
        ? ex2::GenerateStructuredPermutation(elementCount)
        : ex2::GenerateShuffledPermutation(seed, elementCount);
}

std::vector<std::uint32_t> Reference(
    ex2::IndexedVariant variant,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& indices)
{
    return variant == ex2::IndexedVariant::B1
        ? ex2::ReferenceB1Gather(input, indices)
        : ex2::ReferenceB2Scatter(input, indices);
}

vulkan::Ex2VulkanBOperation MakeOperation(
    ex2::IndexedVariant variant,
    std::uint64_t elementCount,
    ex2::IndexPattern pattern,
    std::uint64_t seed = ex2::CoreInputSeed)
{
    return vulkan::Ex2VulkanBOperation{
        BConfiguration(variant, elementCount, pattern),
        seed,
        COMPUTELAB_EX2_B1_SPIRV_PATH,
        COMPUTELAB_EX2_B2_SPIRV_PATH,
        0U};
}

std::vector<std::uint32_t> Execute(
    vulkan::Ex2VulkanBOperation& operation,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& indices)
{
    operation.Upload(input, indices);
    operation.Prepare();
    operation.Submit();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(Ex2VulkanB, RequiredBoundedSizesMatchCpuOracleAndPreserveBothInputs)
{
    static_assert(
        vulkan::Ex2VulkanBOutputSentinel == 0xA5A5A5A5U,
        "the Vulkan B output sentinel must match the frozen CUDA B diagnostic");
    constexpr std::array<std::uint64_t, 7U> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 262'144U};
    constexpr std::array variants{
        ex2::IndexedVariant::B1,
        ex2::IndexedVariant::B2};
    constexpr std::array patterns{
        ex2::IndexPattern::StructuredV1,
        ex2::IndexPattern::ShuffledV1};

    for (const auto variant : variants)
    {
        for (const auto pattern : patterns)
        {
            for (const std::uint64_t size : sizes)
            {
                SCOPED_TRACE(static_cast<int>(variant));
                SCOPED_TRACE(static_cast<int>(pattern));
                SCOPED_TRACE(size);
                const auto input = ex2::GenerateWordInput(
                    ex2::CoreInputSeed, size);
                const auto indices = GenerateIndices(
                    pattern, ex2::CoreInputSeed, size);
                const auto expected = Reference(variant, input, indices);
                auto operation = MakeOperation(variant, size, pattern);

                const auto actual = Execute(operation, input, indices);

                EXPECT_EQ(actual.size(), size);
                EXPECT_EQ(actual, expected);
                EXPECT_EQ(operation.RetrieveDeviceInput(), input);
                EXPECT_EQ(operation.RetrieveDeviceIndices(), indices);
                EXPECT_EQ(operation.RetrieveOutput(), expected);
                EXPECT_EQ(
                    operation.LastCompletionExecutedShader(), size != 0U);
                EXPECT_EQ(operation.Variant(), variant);
                EXPECT_EQ(operation.Pattern(), pattern);
                EXPECT_EQ(operation.Seed(), ex2::CoreInputSeed);
            }
        }
    }
}

TEST(Ex2VulkanB, StructuredFourWordLiteralsIndependentlyAnchorBothShaders)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> indices{1U, 0U, 3U, 2U};
    const std::vector<std::uint32_t> expectedB1{
        0x5D43B3A3U, 0x3AB8D327U, 0x0F1AB743U, 0xA5DBB31EU};
    const std::vector<std::uint32_t> expectedB2{
        0x5D43B3A0U, 0x3AB8D324U, 0x0F1AB744U, 0xA5DBB319U};

    EXPECT_EQ(ex2::ReferenceB1Gather(input, indices), expectedB1);
    EXPECT_EQ(ex2::ReferenceB2Scatter(input, indices), expectedB2);

    auto gather = MakeOperation(
        ex2::IndexedVariant::B1,
        4U,
        ex2::IndexPattern::StructuredV1);
    auto scatter = MakeOperation(
        ex2::IndexedVariant::B2,
        4U,
        ex2::IndexPattern::StructuredV1);

    EXPECT_EQ(Execute(gather, input, indices), expectedB1);
    EXPECT_EQ(Execute(scatter, input, indices), expectedB2);
    EXPECT_TRUE(gather.LastCompletionExecutedShader());
    EXPECT_TRUE(scatter.LastCompletionExecutedShader());
}

TEST(Ex2VulkanB, ZeroElementsPreserveLifecycleWithoutShaderDispatch)
{
    const std::vector<std::uint32_t> empty;
    auto operation = MakeOperation(
        ex2::IndexedVariant::B2,
        0U,
        ex2::IndexPattern::ShuffledV1);

    operation.Upload(empty, empty);
    operation.Prepare();
    operation.Submit();
    operation.WaitForCompletion();

    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceInput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceIndices().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedShader());
}

TEST(Ex2VulkanB, RepeatedUploadsReinitializeOutputAndCannotExposeStaleData)
{
    constexpr std::uint64_t alternateInputSeed = 0xFEDCBA9876543210ULL;
    const auto indices = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(alternateInputSeed, 257U);
    ASSERT_NE(firstInput, secondInput);

    for (const auto variant :
        {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
    {
        SCOPED_TRACE(static_cast<int>(variant));
        auto operation = MakeOperation(
            variant, 257U, ex2::IndexPattern::ShuffledV1);

        const auto first = Execute(operation, firstInput, indices);
        const auto repeated = Execute(operation, firstInput, indices);
        const auto second = Execute(operation, secondInput, indices);

        EXPECT_EQ(first, Reference(variant, firstInput, indices));
        EXPECT_EQ(repeated, first);
        EXPECT_EQ(second, Reference(variant, secondInput, indices));
        EXPECT_NE(second, first);
        EXPECT_EQ(operation.RetrieveDeviceInput(), secondInput);
        EXPECT_EQ(operation.RetrieveDeviceIndices(), indices);
    }
}

TEST(Ex2VulkanB, SuccessfulUploadDoesNotReferenceCallerStorageAfterReturn)
{
    const auto expectedInput = ex2::GenerateWordInput(
        ex2::CoreInputSeed, 257U);
    const auto expectedIndices = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    const auto expectedOutput = ex2::ReferenceB1Gather(
        expectedInput, expectedIndices);
    std::vector<std::uint32_t> output;

    {
        auto operation = MakeOperation(
            ex2::IndexedVariant::B1,
            257U,
            ex2::IndexPattern::ShuffledV1);
        {
            auto callerInput = expectedInput;
            auto callerIndices = expectedIndices;
            operation.Upload(callerInput, callerIndices);
        }
        operation.Prepare();
        operation.Submit();
        operation.WaitForCompletion();
        output = operation.RetrieveOutput();
    }

    EXPECT_EQ(output, expectedOutput);
}

TEST(Ex2VulkanB, DistinctImmutablePatternAndSeedConfigurationsChangeIndices)
{
    constexpr std::uint64_t alternateSeed = 0xFEDCBA9876543210ULL;
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto structured = ex2::GenerateStructuredPermutation(257U);
    const auto shuffled = ex2::GenerateShuffledPermutation(
        alternateSeed, 257U);
    ASSERT_NE(structured, shuffled);

    for (const auto variant :
        {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
    {
        SCOPED_TRACE(static_cast<int>(variant));
        auto structuredOperation = MakeOperation(
            variant, 257U, ex2::IndexPattern::StructuredV1);
        auto shuffledOperation = MakeOperation(
            variant, 257U, ex2::IndexPattern::ShuffledV1, alternateSeed);

        const auto structuredOutput =
            Execute(structuredOperation, input, structured);
        const auto shuffledOutput =
            Execute(shuffledOperation, input, shuffled);
        EXPECT_EQ(structuredOutput, Reference(variant, input, structured));
        EXPECT_EQ(shuffledOutput, Reference(variant, input, shuffled));
        EXPECT_NE(structuredOutput, shuffledOutput);
    }
}

TEST(Ex2VulkanBValidation,
    RejectsMalformedReplacementAndNeverLeavesStalePairSubmitReady)
{
    const std::vector<std::uint32_t> input{1U, 2U, 3U, 4U};
    const std::vector<std::uint32_t> structured{1U, 0U, 3U, 2U};
    const std::vector<std::uint32_t> shuffled{3U, 0U, 2U, 1U};
    const std::vector<std::uint32_t> duplicate{0U, 0U, 2U, 3U};
    const std::vector<std::uint32_t> outOfRange{0U, 1U, 2U, 4U};
    const std::vector<std::uint32_t> shortIndices{0U, 1U, 2U};
    const std::vector<std::uint32_t> shortInput{1U, 2U, 3U};
    auto operation = MakeOperation(
        ex2::IndexedVariant::B1,
        4U,
        ex2::IndexPattern::StructuredV1);

    EXPECT_EQ(
        Execute(operation, input, structured),
        ex2::ReferenceB1Gather(input, structured));

    EXPECT_THROW(operation.Upload(input, duplicate), std::invalid_argument);
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, outOfRange), std::invalid_argument);
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, shortIndices), std::invalid_argument);
    EXPECT_THROW(operation.Upload(shortInput, structured), std::invalid_argument);
    EXPECT_THROW(operation.Upload(input, shuffled), std::invalid_argument);
    EXPECT_THROW(operation.Prepare(), std::logic_error);

    EXPECT_EQ(
        Execute(operation, input, structured),
        ex2::ReferenceB1Gather(input, structured));
}

TEST(Ex2VulkanBValidation, RejectsInvalidSemanticContractBeforeVulkanCreation)
{
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanBOperation{
            BConfiguration(static_cast<ex2::IndexedVariant>(999), 1U,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanBOperation{
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                static_cast<ex2::IndexPattern>(999)),
            ex2::CoreInputSeed,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanBOperation{
            BConfiguration(ex2::IndexedVariant::B1, 8191U,
                ex2::IndexPattern::StructuredV1),
            ex2::CoreInputSeed,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanBOperation{
            BConfiguration(ex2::IndexedVariant::B1,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH}),
        std::invalid_argument);

    auto wrongRevision = ex2::MakeConfiguration(BConfiguration(
        ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::ShuffledV1));
    wrongRevision.common.generatorRevision = "wrong-revision";
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBConfiguration(wrongRevision),
        std::invalid_argument);

    auto wrongMode = ex2::MakeConfiguration(BConfiguration(
        ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::ShuffledV1));
    wrongMode.common.executionMode = ex2::LogicalExecutionMode::Prepared;
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBConfiguration(wrongMode),
        std::invalid_argument);

    auto wrongBoundary = ex2::MakeConfiguration(BConfiguration(
        ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::ShuffledV1));
    wrongBoundary.common.operationBoundary =
        ex2::OperationBoundary::AtomicDispatchCompletion;
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBConfiguration(wrongBoundary),
        std::invalid_argument);

    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBConfiguration(
            ex2::MakeConfiguration(ex2::LinearConfiguration{
                ex2::LinearVariant::A1, 1U})),
        std::invalid_argument);
}

TEST(Ex2VulkanB, InvalidLifecycleTransitionsFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    const std::vector<std::uint32_t> indices{0U};
    auto operation = MakeOperation(
        ex2::IndexedVariant::B2,
        1U,
        ex2::IndexPattern::StructuredV1);

    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceInput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceIndices()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedShader()),
        std::logic_error);

    operation.Upload(input, indices);
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.Prepare();
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    operation.Submit();
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, indices), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.Prepare(), std::logic_error);
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
}

TEST(Ex2VulkanBGuards, CalculatesDispatchAndRejectsDeviceLimits)
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

    const auto zero = vulkan::detail::ValidateEx2VulkanBDispatchShape(
        BConfiguration(ex2::IndexedVariant::B1, 0U,
            ex2::IndexPattern::ShuffledV1),
        limits,
        1'028U);
    const auto exact = vulkan::detail::ValidateEx2VulkanBDispatchShape(
        BConfiguration(ex2::IndexedVariant::B1, 256U,
            ex2::IndexPattern::ShuffledV1),
        limits,
        1'028U);
    const auto padded = vulkan::detail::ValidateEx2VulkanBDispatchShape(
        BConfiguration(ex2::IndexedVariant::B2, 257U,
            ex2::IndexPattern::ShuffledV1),
        limits,
        1'028U);
    EXPECT_EQ(zero.groupCountX, 0U);
    EXPECT_EQ(exact.groupCountX, 1U);
    EXPECT_EQ(padded.groupCountX, 2U);

    limits.maxComputeWorkGroupCount[0] = 1U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanBDispatchShape(
            BConfiguration(ex2::IndexedVariant::B1, 257U,
                ex2::IndexPattern::ShuffledV1),
            limits,
            1'028U)),
        std::invalid_argument);
    limits.maxComputeWorkGroupCount[0] = 2U;
    limits.maxStorageBufferRange = 1'023U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanBDispatchShape(
            BConfiguration(ex2::IndexedVariant::B1, 256U,
                ex2::IndexPattern::ShuffledV1),
            limits,
            1'024U)),
        std::length_error);
    limits.maxStorageBufferRange = 1'028U;
    limits.maxComputeWorkGroupInvocations = 255U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::ValidateEx2VulkanBDispatchShape(
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                ex2::IndexPattern::ShuffledV1),
            limits,
            1'028U)),
        std::invalid_argument);
}

TEST(Ex2VulkanBGuards, RejectsOverflowAllocationCountAndHeapLimitsPurely)
{
    EXPECT_EQ(
        vulkan::detail::CalculateEx2VulkanBBufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::CalculateEx2VulkanBBufferByteCount(
            257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::CalculateEx2VulkanBBufferByteCount(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U,
            std::numeric_limits<VkDeviceSize>::max())),
        std::invalid_argument);

    EXPECT_NO_THROW(vulkan::detail::ValidateEx2VulkanBAllocationCount(5U));
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBAllocationCount(4U),
        std::length_error);
    EXPECT_NO_THROW(vulkan::detail::ValidateEx2VulkanBHeapFeasibility(
        1'024U, 2'048U, 3'072U));
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBHeapFeasibility(
            1'025U, 2'048U, 3'072U),
        std::length_error);
    EXPECT_THROW(
        vulkan::detail::ValidateEx2VulkanBHeapFeasibility(
            1U, 3'073U, 3'072U),
        std::length_error);
}

TEST(Ex2VulkanBGuards, SelectsQueueAndMemoryAndRejectsIncompatibility)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_EQ(vulkan::detail::SelectEx2VulkanBQueue(families), 1U);
    families[1].queueCount = 0U;
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::SelectEx2VulkanBQueue(families)),
        std::invalid_argument);

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
        vulkan::detail::SelectEx2VulkanBMemoryType(
            0b111U,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            properties),
        1U);
    EXPECT_EQ(
        vulkan::detail::SelectEx2VulkanBMemoryType(
            0b101U,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            properties),
        0U);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::SelectEx2VulkanBMemoryType(
            0b011U,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0U,
            properties)),
        std::invalid_argument);
}

TEST(Ex2VulkanBGuards, AlignsNoncoherentRangesWithoutOverflow)
{
    const auto aligned = vulkan::detail::AlignEx2VulkanBNoncoherentRange(
        65U, 63U, 256U, 64U);
    EXPECT_EQ(aligned.offset, 64U);
    EXPECT_EQ(aligned.size, 64U);

    const auto allocationEnd =
        vulkan::detail::AlignEx2VulkanBNoncoherentRange(
            65U, 191U, 256U, 64U);
    EXPECT_EQ(allocationEnd.offset, 64U);
    EXPECT_EQ(allocationEnd.size, VK_WHOLE_SIZE);

    EXPECT_THROW(
        static_cast<void>(vulkan::detail::AlignEx2VulkanBNoncoherentRange(
            257U, 0U, 256U, 64U)),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(vulkan::detail::AlignEx2VulkanBNoncoherentRange(
            0U, 1U, 1U, 0U)),
        std::invalid_argument);
}

TEST(Ex2VulkanB, DeviceIdentityAndVariantSpecificShaderAreExplicit)
{
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "ComputeLab EX-2 Vulkan B test";
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

    auto gather = MakeOperation(
        ex2::IndexedVariant::B1,
        1U,
        ex2::IndexPattern::StructuredV1);
    auto scatter = MakeOperation(
        ex2::IndexedVariant::B2,
        1U,
        ex2::IndexPattern::StructuredV1);
    EXPECT_EQ(gather.Diagnostics().physicalDeviceIndex, 0U);
    EXPECT_EQ(gather.SelectedDeviceUuid(), expected);
    EXPECT_EQ(gather.LoadedShaderName(), "Ex2B1.comp.spv");
    EXPECT_EQ(scatter.LoadedShaderName(), "Ex2B2.comp.spv");
    ASSERT_FALSE(gather.LoadedSpirv().empty());
    ASSERT_FALSE(scatter.LoadedSpirv().empty());
    EXPECT_EQ(gather.LoadedSpirv().front(), 0x07230203U);
    EXPECT_EQ(scatter.LoadedSpirv().front(), 0x07230203U);
    const bool identicalSpirv =
        gather.LoadedSpirv().size() == scatter.LoadedSpirv().size() &&
        std::equal(
            gather.LoadedSpirv().begin(),
            gather.LoadedSpirv().end(),
            scatter.LoadedSpirv().begin());
    EXPECT_FALSE(identicalSpirv);
}

TEST(Ex2VulkanB, InvalidDeviceSelectionDoesNotSubstituteAnotherAdapter)
{
    EXPECT_THROW(
        static_cast<void>(vulkan::Ex2VulkanBOperation{
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                ex2::IndexPattern::StructuredV1),
            ex2::CoreInputSeed,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH,
            std::numeric_limits<std::uint32_t>::max()}),
        std::invalid_argument);
}

TEST(Ex2VulkanB, NativeMetadataAndUncertainCleanupClassificationAreExplicit)
{
    const vulkan::Ex2VulkanBNativeError error{
        vulkan::Ex2VulkanBNativePhase::IndexDiagnosticReadback,
        VK_TIMEOUT,
        "synthetic index readback",
        "synthetic index readback timed out"};
    EXPECT_EQ(
        error.Phase(),
        vulkan::Ex2VulkanBNativePhase::IndexDiagnosticReadback);
    EXPECT_EQ(error.NativeResult(), VK_TIMEOUT);
    EXPECT_EQ(error.Operation(), "synthetic index readback");
    EXPECT_NE(std::string{error.what()}.find("timed out"), std::string::npos);
    EXPECT_EQ(
        vulkan::ToString(vulkan::Ex2VulkanBNativePhase::IndexUpload),
        "index upload");
    EXPECT_EQ(
        vulkan::ToString(vulkan::Ex2VulkanBNativePhase::OutputInitialization),
        "output initialization");

    using Submission = vulkan::detail::Ex2VulkanBSubmissionDisposition;
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBSubmissionResult(VK_SUCCESS),
        Submission::Submitted);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBSubmissionResult(
            VK_ERROR_OUT_OF_DEVICE_MEMORY),
        Submission::FailedWithoutSubmission);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBSubmissionResult(
            VK_ERROR_DEVICE_LOST),
        Submission::CompletionUncertain);

    using Resource = vulkan::detail::Ex2VulkanBResourceDisposition;
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBResourceDisposition(
            false, false, false),
        Resource::DestroyNormally);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBResourceDisposition(
            false, true, false),
        Resource::DrainComputeThenDestroy);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBResourceDisposition(
            false, false, true),
        Resource::PreserveForProcessTeardown);
    EXPECT_EQ(
        vulkan::detail::ClassifyEx2VulkanBResourceDisposition(
            true, false, false),
        Resource::PreserveForProcessTeardown);
}

TEST(Ex2VulkanBLarge, ApprovedShuffledCoreSizeRunsBothVariantsSerially)
{
    constexpr std::uint64_t elementCount = 16'777'216U;
    constexpr std::size_t bytesPerBuffer =
        static_cast<std::size_t>(elementCount) * sizeof(std::uint32_t);
    static_assert(bytesPerBuffer == 64U * 1024U * 1024U);

    for (const auto variant :
        {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
    {
        SCOPED_TRACE(static_cast<int>(variant));
        const auto input = ex2::GenerateWordInput(
            ex2::CoreInputSeed, elementCount);
        const auto indices = ex2::GenerateShuffledPermutation(
            ex2::CoreInputSeed, elementCount);
        const auto expected = Reference(variant, input, indices);

        try
        {
            auto operation = MakeOperation(
                variant,
                elementCount,
                ex2::IndexPattern::ShuffledV1);
            const auto actual = Execute(operation, input, indices);
            EXPECT_EQ(actual.size(), elementCount);
            EXPECT_EQ(actual, expected);
            EXPECT_EQ(operation.RetrieveDeviceInput(), input);
            EXPECT_EQ(operation.RetrieveDeviceIndices(), indices);
            EXPECT_TRUE(operation.LastCompletionExecutedShader());
        }
        catch (const std::length_error& error)
        {
            GTEST_SKIP() << "large Vulkan B cell is infeasible: "
                         << error.what();
        }
        catch (const vulkan::Ex2VulkanBNativeError& error)
        {
            if (error.NativeResult() == VK_ERROR_OUT_OF_HOST_MEMORY ||
                error.NativeResult() == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            {
                GTEST_SKIP() << "large Vulkan B allocation is infeasible: "
                             << error.what();
            }
            throw;
        }
    }
}

} // namespace
