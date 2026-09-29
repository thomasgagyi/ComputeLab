#include "vulkan/Ex2VulkanD1.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#ifndef COMPUTELAB_EX2_D1_SPIRV_PATH
#error COMPUTELAB_EX2_D1_SPIRV_PATH must name the generated D1 shader
#endif

namespace
{

namespace ex2 = computelab::ex2;
namespace vk = computelab::vulkan;

const std::filesystem::path SpirvPath{COMPUTELAB_EX2_D1_SPIRV_PATH};

constexpr std::array<std::uint32_t, 4U> InitialFixture{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4U> KOneFixture{
    0xD664E09CU, 0x27C0F020U, 0x3AC0DF49U, 0x62A16496U};
constexpr std::array<std::uint32_t, 4U> KTwoFixture{
    0x89BDA0DEU, 0xBE3BAF8CU, 0x1E3F5AC9U, 0x120E2194U};

std::vector<std::uint32_t> AsVector(
    const std::array<std::uint32_t, 4U>& value)
{
    return {value.begin(), value.end()};
}

void ExpectValidationClean(const vk::Ex2VulkanD1Diagnostics& diagnostics)
{
    const char* mode = std::getenv("COMPUTELAB_EX2_D1_VALIDATION");
    if (mode == nullptr)
    {
        EXPECT_FALSE(diagnostics.validationEnabled);
        EXPECT_FALSE(diagnostics.synchronizationValidationEnabled);
    }
    else
    {
        EXPECT_TRUE(diagnostics.validationEnabled);
        EXPECT_EQ(
            diagnostics.synchronizationValidationEnabled,
            std::string_view{mode} == "sync");
    }
    EXPECT_EQ(diagnostics.validationErrorCount, 0U);
}

void RunLiteral(std::uint64_t iterations, std::span<const std::uint32_t> expected)
{
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, InitialFixture.size(), iterations},
        SpirvPath};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(),
        std::vector<std::uint32_t>(expected.begin(), expected.end()));
    EXPECT_EQ(operation.CompletedFinalBuffer(),
        (iterations & 1U) == 0U
            ? ex2::IterativeFinalBuffer::StateA
            : ex2::IterativeFinalBuffer::StateB);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterations);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), iterations - 1U);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
    EXPECT_EQ(operation.LoadedShaderName(), "Ex2D1.comp.spv");
    ASSERT_FALSE(operation.LoadedSpirv().empty());
    EXPECT_EQ(operation.LoadedSpirv().front(), 0x07230203U);
    EXPECT_FALSE(operation.Diagnostics().timestampQueryPoolCreated);
    ExpectValidationClean(operation.Diagnostics());
}

void RunCore(std::uint64_t elementCount, std::uint64_t iterationCount)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, elementCount);
    const auto reference = ex2::ReferenceD1(input, iterationCount);
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, elementCount, iterationCount},
        SpirvPath};
    EXPECT_EQ(operation.ElementCount(), elementCount);
    EXPECT_EQ(operation.IterationCount(), iterationCount);
    EXPECT_EQ(operation.ExpectedFinalBuffer(), reference.finalBuffer);
    EXPECT_EQ(operation.SelectedDeviceUuid().size(), VK_UUID_SIZE);

    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    const auto first = operation.RetrieveFinalState();
    EXPECT_EQ(first.size(), input.size());
    EXPECT_EQ(first, reference.finalState);
    EXPECT_EQ(operation.CompletedFinalBuffer(), reference.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterationCount);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), iterationCount - 1U);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());

    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveFinalState();
    EXPECT_EQ(second, reference.finalState);
    EXPECT_EQ(second, first);
    EXPECT_EQ(operation.CompletedFinalBuffer(), reference.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterationCount);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), iterationCount - 1U);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
    ASSERT_FALSE(operation.LoadedSpirv().empty());
    EXPECT_FALSE(operation.Diagnostics().timestampQueryPoolCreated);
    ExpectValidationClean(operation.Diagnostics());
}

VkPhysicalDeviceLimits CapableLimits()
{
    VkPhysicalDeviceLimits limits{};
    limits.maxStorageBufferRange = std::numeric_limits<std::uint32_t>::max();
    limits.maxPushConstantsSize = 128U;
    limits.maxPerStageDescriptorStorageBuffers = 8U;
    limits.maxDescriptorSetStorageBuffers = 8U;
    limits.maxComputeWorkGroupInvocations = 1024U;
    limits.maxComputeWorkGroupSize[0] = 1024U;
    limits.maxComputeWorkGroupSize[1] = 1024U;
    limits.maxComputeWorkGroupSize[2] = 64U;
    limits.maxComputeWorkGroupCount[0] = 65'535U;
    limits.maxComputeWorkGroupCount[1] = 65'535U;
    limits.maxComputeWorkGroupCount[2] = 65'535U;
    return limits;
}

} // namespace

TEST(Ex2VulkanD1, LiteralKOneUsesStateB)
{
    RunLiteral(1U, KOneFixture);
}

TEST(Ex2VulkanD1, LiteralKTwoUsesStateA)
{
    RunLiteral(2U, KTwoFixture);
}

TEST(Ex2VulkanD1, PaddedN257MatchesIndependentCpuReference)
{
    constexpr std::uint64_t count = 257U;
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, count);
    const auto expected = ex2::ReferenceD1(input, 1U);
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, count, 1U}, SpirvPath};
    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), expected.finalState);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 1U);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), 0U);
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanD1, KZeroSubmitsEmptySequenceAndReturnsStateA)
{
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, InitialFixture.size(), 0U}, SpirvPath};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), AsVector(InitialFixture));
    EXPECT_EQ(operation.CompletedFinalBuffer(), ex2::IterativeFinalBuffer::StateA);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 0U);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), 0U);
    EXPECT_FALSE(operation.LastCompletionExecutedSequence());
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanD1, NZeroSubmitsEmptySequenceAndPreservesParity)
{
    const std::vector<std::uint32_t> empty;
    for (const std::uint64_t iterations : {0U, 1U})
    {
        vk::Ex2VulkanD1Operation operation{
            {ex2::IterativeVariant::D1, 0U, iterations}, SpirvPath};
        operation.UploadInitialState(empty);
        operation.SubmitSequence();
        operation.WaitForCompletion();
        EXPECT_TRUE(operation.RetrieveFinalState().empty());
        EXPECT_EQ(operation.CompletedFinalBuffer(),
            iterations == 0U
                ? ex2::IterativeFinalBuffer::StateA
                : ex2::IterativeFinalBuffer::StateB);
        EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 0U);
        EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), 0U);
        EXPECT_FALSE(operation.LastCompletionExecutedSequence());
        ExpectValidationClean(operation.Diagnostics());
    }
}

TEST(Ex2VulkanD1, LifecycleRejectsOutOfOrderDuplicateAndTimedOutReuse)
{
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, InitialFixture.size(), 1U}, SpirvPath};
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveFinalState()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.CompletedFinalBuffer()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionNativeDispatchCount()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionInterPassBarrierCount()), std::logic_error);
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveFinalState()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
}

TEST(Ex2VulkanD1, FailedReplacementUploadInvalidatesPriorReadiness)
{
    vk::Ex2VulkanD1Operation operation{
        {ex2::IterativeVariant::D1, InitialFixture.size(), 1U}, SpirvPath};
    operation.UploadInitialState(InitialFixture);
    const std::array<std::uint32_t, 3U> wrongLength{};
    EXPECT_THROW(operation.UploadInitialState(wrongLength), std::invalid_argument);
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(operation.UploadInitialState(InitialFixture), std::logic_error);
}

TEST(Ex2VulkanD1, CoreN262144K16MatchesCpuAndRepeats)
{
    RunCore(262'144U, 16U);
}

TEST(Ex2VulkanD1, CoreN1048576K64MatchesCpuAndRepeats)
{
    RunCore(1'048'576U, 64U);
}

TEST(Ex2VulkanD1, TimedOperationProducesCorrectEnvelopeAndMetadata)
{
    constexpr std::uint64_t count = 257U;
    constexpr std::uint64_t iterations = 3U;
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, count);
    const auto expected = ex2::ReferenceD1(input, iterations);
    vk::Ex2VulkanD1DeviceTimedOperation operation{
        {ex2::IterativeVariant::D1, count, iterations}, SpirvPath};
    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), expected.finalState);
    EXPECT_EQ(operation.CompletedFinalBuffer(), expected.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterations);
    EXPECT_EQ(operation.LastCompletionInterPassBarrierCount(), iterations - 1U);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
    EXPECT_EQ(operation.NativeTimingStatus(),
        vk::Ex2VulkanD1NativeTimingStatus::Valid);
    ASSERT_TRUE(operation.NativeDeviceIntervalNanoseconds().has_value());
    EXPECT_LE(*operation.NativeDeviceIntervalNanoseconds(),
        vk::Ex2VulkanD1DurationEnvelopeNanoseconds);
    const auto metadata = operation.NativeTimingMetadata();
    EXPECT_EQ(metadata.startStage, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    EXPECT_EQ(metadata.stopStage, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    EXPECT_EQ(metadata.durationEnvelopeNanoseconds,
        vk::Ex2VulkanD1DurationEnvelopeNanoseconds);
    EXPECT_TRUE(operation.Diagnostics().timestampQueryPoolCreated);
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanD1, TimedNoWorkIsNotApplicable)
{
    vk::Ex2VulkanD1DeviceTimedOperation operation{
        {ex2::IterativeVariant::D1, InitialFixture.size(), 0U}, SpirvPath};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.NativeTimingStatus(),
        vk::Ex2VulkanD1NativeTimingStatus::NotApplicable);
    EXPECT_FALSE(operation.NativeDeviceIntervalNanoseconds().has_value());
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanD1Pure, ConfigurationDispatchAndCommandCountSeams)
{
    static_assert(!vk::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps);
    static_assert(vk::Ex2VulkanD1DeviceTimedOperation::EnqueuesDeviceTimestamps);
    ex2::WorkloadConfiguration valid = ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1, 257U, 2U});
    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanD1Configuration(valid));
    valid.common.operationBoundary =
        ex2::OperationBoundary::PreparedIterationReplayCompletion;
    EXPECT_THROW(vk::detail::ValidateEx2VulkanD1Configuration(valid),
        std::invalid_argument);
    EXPECT_THROW(vk::detail::ValidateEx2VulkanD1Configuration(
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D2, 257U, 2U})), std::invalid_argument);

    auto limits = CapableLimits();
    const auto shape = vk::detail::ValidateEx2VulkanD1DispatchShape(
        {ex2::IterativeVariant::D1, 257U, 2U},
        limits,
        std::numeric_limits<VkDeviceSize>::max());
    EXPECT_EQ(shape.groupCountX, 2U);
    limits.maxComputeWorkGroupCount[0] = 1U;
    EXPECT_THROW(static_cast<void>(vk::detail::ValidateEx2VulkanD1DispatchShape(
        {ex2::IterativeVariant::D1, 257U, 2U},
        limits,
        std::numeric_limits<VkDeviceSize>::max())), std::invalid_argument);
    limits = CapableLimits();
    limits.maxStorageBufferRange = 3U;
    EXPECT_THROW(static_cast<void>(vk::detail::ValidateEx2VulkanD1DispatchShape(
        {ex2::IterativeVariant::D1, 1U, 1U}, limits, 4U)),
        std::length_error);
    limits = CapableLimits();
    limits.maxPushConstantsSize = sizeof(std::uint32_t);
    EXPECT_THROW(static_cast<void>(vk::detail::ValidateEx2VulkanD1DispatchShape(
        {ex2::IterativeVariant::D1, 1U, 1U}, limits, 4U)),
        std::invalid_argument);
    limits = CapableLimits();
    limits.maxPerStageDescriptorStorageBuffers = 1U;
    EXPECT_THROW(static_cast<void>(vk::detail::ValidateEx2VulkanD1DispatchShape(
        {ex2::IterativeVariant::D1, 1U, 1U}, limits, 4U)),
        std::invalid_argument);

    const auto positive = vk::detail::Ex2VulkanD1ExpectedCommandCounts(
        {ex2::IterativeVariant::D1, 257U, 2U});
    EXPECT_EQ(positive.dispatchCount, 2U);
    EXPECT_EQ(positive.interPassBarrierCount, 1U);
    EXPECT_EQ(vk::detail::Ex2VulkanD1ExpectedCommandCounts(
        {ex2::IterativeVariant::D1, 257U, 0U}).dispatchCount, 0U);
    EXPECT_EQ(vk::detail::Ex2VulkanD1ExpectedCommandCounts(
        {ex2::IterativeVariant::D1, 0U, 2U}).interPassBarrierCount, 0U);
}

TEST(Ex2VulkanD1Pure, ResourceAlignmentTimingAndFailureSeams)
{
    EXPECT_THROW(static_cast<void>(vk::detail::CalculateEx2VulkanD1BufferByteCount(
        std::numeric_limits<std::uint32_t>::max() + std::uint64_t{1U},
        std::numeric_limits<VkDeviceSize>::max())), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        vk::detail::CalculateEx2VulkanD1BufferByteCount(2U, 7U)),
        std::length_error);
    EXPECT_THROW(vk::detail::ValidateEx2VulkanD1AllocationCount(3U, 4U),
        std::length_error);
    EXPECT_THROW(vk::detail::ValidateEx2VulkanD1HeapFeasibility(5U, 4U, 8U),
        std::length_error);
    const auto aligned = vk::detail::AlignEx2VulkanD1NoncoherentRange(
        65U, 100U, 1024U, 64U);
    EXPECT_EQ(aligned.offset, 64U);
    EXPECT_EQ(aligned.size, 128U);
    const auto toEnd = vk::detail::AlignEx2VulkanD1NoncoherentRange(
        64U, 960U, 1024U, 64U);
    EXPECT_EQ(toEnd.size, VK_WHOLE_SIZE);
    EXPECT_THROW(static_cast<void>(vk::detail::AlignEx2VulkanD1NoncoherentRange(
        0U, 1U, 1U, 0U)), std::invalid_argument);

    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanD1TimestampEnvelope(
        64U, 1.0L, vk::Ex2VulkanD1DurationEnvelopeNanoseconds));
    EXPECT_THROW(vk::detail::ValidateEx2VulkanD1TimestampEnvelope(
        35U, 1.0L, vk::Ex2VulkanD1DurationEnvelopeNanoseconds),
        std::invalid_argument);
    const std::array<vk::detail::Ex2VulkanD1TimestampQuery, 2U> queries{{
        {100U, 1U}, {250U, 1U}}};
    EXPECT_EQ(vk::detail::DecodeEx2VulkanD1TimestampQueries(
        VK_SUCCESS, queries, 64U, 2.0L,
        vk::Ex2VulkanD1DurationEnvelopeNanoseconds), 300U);

    EXPECT_EQ(vk::detail::ClassifyEx2VulkanD1SubmissionResult(
        VK_ERROR_OUT_OF_DEVICE_MEMORY),
        vk::detail::Ex2VulkanD1SubmissionDisposition::FailedWithoutSubmission);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanD1SubmissionResult(VK_ERROR_DEVICE_LOST),
        vk::detail::Ex2VulkanD1SubmissionDisposition::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanD1WaitResult(VK_TIMEOUT),
        vk::detail::Ex2VulkanD1OperationState::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanD1WaitResult(VK_ERROR_DEVICE_LOST),
        vk::detail::Ex2VulkanD1OperationState::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanD1ResourceDisposition(true, false),
        vk::detail::Ex2VulkanD1ResourceDisposition::PreserveForProcessTeardown);
    EXPECT_FALSE(vk::detail::IsEx2VulkanD1OperationStateReusable(
        vk::detail::Ex2VulkanD1OperationState::CompletionUncertain));
    const std::array<vk::detail::Ex2VulkanD1TimestampQuery, 2U> unavailable{{
        {100U, 1U}, {250U, 0U}}};
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeEx2VulkanD1TimestampQueries(
        VK_SUCCESS, unavailable, 64U, 1.0L,
        vk::Ex2VulkanD1DurationEnvelopeNanoseconds)), std::runtime_error);
}

TEST(Ex2VulkanD1Pure, QueueAndMemorySelectionAreExplicit)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_EQ(vk::detail::SelectEx2VulkanD1Queue(families), 1U);
    families[1].queueFlags = VK_QUEUE_TRANSFER_BIT;
    EXPECT_THROW(static_cast<void>(vk::detail::SelectEx2VulkanD1Queue(families)),
        std::invalid_argument);

    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 2U;
    properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    EXPECT_EQ(vk::detail::SelectEx2VulkanD1MemoryType(
        0x3U,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        properties), 1U);
    EXPECT_THROW(static_cast<void>(vk::detail::SelectEx2VulkanD1MemoryType(
        0x1U,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        properties)), std::invalid_argument);
}

TEST(Ex2VulkanD1, InvalidPhysicalDeviceAndD2AreRejected)
{
    EXPECT_THROW(vk::Ex2VulkanD1Operation(
        {ex2::IterativeVariant::D1, 1U, 1U},
        SpirvPath,
        std::numeric_limits<std::uint32_t>::max()),
        std::invalid_argument);
    EXPECT_THROW(vk::Ex2VulkanD1Operation(
        {ex2::IterativeVariant::D2, 1U, 1U}, SpirvPath),
        std::invalid_argument);
}
