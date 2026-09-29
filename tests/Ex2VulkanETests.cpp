#include "vulkan/Ex2VulkanE.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace vk = computelab::vulkan;

constexpr std::array<std::uint8_t, 4U> LiteralBytes{
    0x9DU, 0x1AU, 0xA2U, 0xF8U};

ex2::TransferConfiguration Configuration(
    ex2::TransferDirection direction,
    std::uint64_t byteCount)
{
    return {
        direction == ex2::TransferDirection::HostToDevice
            ? ex2::TransferVariant::E1
            : ex2::TransferVariant::E2,
        byteCount,
        direction};
}

void ExpectValidationClean(const vk::Ex2VulkanEDiagnostics& diagnostics)
{
    const char* mode = std::getenv("COMPUTELAB_EX2_E_VALIDATION");
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

void ExpectResourceDiagnostics(
    const vk::Ex2VulkanEDiagnostics& diagnostics,
    std::uint64_t byteCount)
{
    EXPECT_NE(
        diagnostics.uploadMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        0U);
    EXPECT_NE(
        diagnostics.deviceMemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U);
    EXPECT_NE(
        diagnostics.readbackMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        0U);
    EXPECT_GE(diagnostics.uploadAllocationBytes, byteCount);
    EXPECT_GE(diagnostics.deviceAllocationBytes, byteCount);
    EXPECT_GE(diagnostics.readbackAllocationBytes, byteCount);
    EXPECT_NE(diagnostics.queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT, 0U);
    EXPECT_TRUE(diagnostics.synchronization2Enabled);
}

void RunCore(ex2::TransferDirection direction, std::uint64_t byteCount)
{
    auto reference = ex2::ReferenceTransfer(
        ex2::CoreInputSeed, byteCount, direction);
    vk::Ex2VulkanEOperation operation{Configuration(direction, byteCount)};
    EXPECT_EQ(operation.ByteCount(), byteCount);
    EXPECT_EQ(operation.Direction(), direction);
    EXPECT_EQ(operation.SelectedDeviceUuid().size(), VK_UUID_SIZE);
    EXPECT_EQ(operation.ExpectedNativeCopyCount(), 1U);
    EXPECT_FALSE(operation.Diagnostics().timestampQueryPoolCreated);
    EXPECT_EQ(operation.Diagnostics().preparedCommandRecordCount, 1U);
    EXPECT_EQ(operation.Diagnostics().preparedNativeCopyCount, 1U);
    ExpectResourceDiagnostics(operation.Diagnostics(), byteCount);

    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    const auto first = operation.RetrieveDestinationForValidation();
    EXPECT_EQ(first.size(), reference.source.size());
    EXPECT_TRUE(ex2::ValidateTransferOutput(reference, first));
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_TRUE(operation.LastCompletionExecutedCopy());
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(), 1U);

    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveDestinationForValidation();
    EXPECT_TRUE(ex2::ValidateTransferOutput(reference, second));
    EXPECT_EQ(second, first);
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_TRUE(operation.LastCompletionExecutedCopy());
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(), 1U);
    EXPECT_EQ(operation.Diagnostics().preparedCommandRecordCount, 1U);
    ExpectValidationClean(operation.Diagnostics());
}

void RunCorrectnessOnly(
    ex2::TransferDirection direction,
    std::uint64_t byteCount)
{
    auto reference = ex2::ReferenceTransfer(
        ex2::CoreInputSeed, byteCount, direction);
    vk::Ex2VulkanEOperation operation{Configuration(direction, byteCount)};
    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    EXPECT_TRUE(ex2::ValidateTransferOutput(
        reference, operation.RetrieveDestinationForValidation()));
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(),
        byteCount == 0U ? 0U : 1U);
    EXPECT_EQ(operation.LastCompletionExecutedCopy(), byteCount != 0U);
    EXPECT_EQ(operation.Diagnostics().preparedCommandRecordCount, 1U);
    EXPECT_EQ(operation.Diagnostics().preparedNativeCopyCount,
        byteCount == 0U ? 0U : 1U);
    ExpectValidationClean(operation.Diagnostics());
}

} // namespace

TEST(Ex2VulkanECore, E1S1024MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 1'024U);
}

TEST(Ex2VulkanECore, E1S1048576MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 1'048'576U);
}

TEST(Ex2VulkanECore, E1S67108864MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 67'108'864U);
}

TEST(Ex2VulkanECore, E2S1024MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 1'024U);
}

TEST(Ex2VulkanECore, E2S1048576MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 1'048'576U);
}

TEST(Ex2VulkanECore, E2S67108864MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 67'108'864U);
}

TEST(Ex2VulkanECorrectnessOnly, LiteralBytesAreIndependentForBothDirections)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        vk::Ex2VulkanEOperation operation{
            Configuration(direction, LiteralBytes.size())};
        operation.PrepareTransfer(LiteralBytes);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(
            operation.RetrieveDestinationForValidation(),
            std::vector<std::uint8_t>(LiteralBytes.begin(), LiteralBytes.end()));
        ExpectValidationClean(operation.Diagnostics());
    }
}

TEST(Ex2VulkanECorrectnessOnly, ZeroOneAndOdd257AreByteExact)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        for (const std::uint64_t byteCount : {0U, 1U, 257U})
            RunCorrectnessOnly(direction, byteCount);
    }
}

TEST(Ex2VulkanE, SameObjectRepeatUsesReplacementSourceAndPreparedCommand)
{
    constexpr std::uint64_t byteCount = 257U;
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        auto first = ex2::GenerateByteInput(ex2::CoreInputSeed, byteCount);
        auto second = first;
        for (auto& value : second)
            value ^= 0x5AU;
        vk::Ex2VulkanEOperation operation{Configuration(direction, byteCount)};
        operation.PrepareTransfer(first);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(operation.RetrieveDestinationForValidation(), first);
        operation.PrepareTransfer(second);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(operation.RetrieveDestinationForValidation(), second);
        EXPECT_NE(first, second);
        EXPECT_EQ(operation.Diagnostics().preparedCommandRecordCount, 1U);
        ExpectValidationClean(operation.Diagnostics());
    }
}

TEST(Ex2VulkanE, LifecycleRejectsOutOfOrderAndDuplicateSubmission)
{
    const auto input = ex2::GenerateByteInput(ex2::CoreInputSeed, 4U);
    vk::Ex2VulkanEOperation operation{
        Configuration(ex2::TransferDirection::HostToDevice, input.size())};
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDestinationForValidation()),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedCopy()),
        std::logic_error);
    operation.PrepareTransfer(input);
    operation.SubmitTransfer();
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDestinationForValidation()),
        std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanE, FailedReplacementPreparationInvalidatesPriorReadiness)
{
    const auto input = ex2::GenerateByteInput(ex2::CoreInputSeed, 4U);
    vk::Ex2VulkanEOperation operation{
        Configuration(ex2::TransferDirection::DeviceToHost, input.size())};
    operation.PrepareTransfer(input);
    const std::array<std::uint8_t, 3U> wrongLength{};
    EXPECT_THROW(operation.PrepareTransfer(wrongLength), std::invalid_argument);
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(operation.PrepareTransfer(input), std::logic_error);
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanE, TimedE1AndE2MarkOnlyThePreparedNamedCopy)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        auto reference = ex2::ReferenceTransfer(
            ex2::CoreInputSeed, 257U, direction);
        vk::Ex2VulkanEDeviceTimedOperation operation{
            Configuration(direction, 257U)};
        operation.PrepareTransfer(reference.source);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        operation.RetrieveNativeTiming();
        EXPECT_TRUE(ex2::ValidateTransferOutput(
            reference, operation.RetrieveDestinationForValidation()));
        EXPECT_EQ(operation.NativeTimingStatus(),
            vk::Ex2VulkanENativeTimingStatus::Valid);
        ASSERT_TRUE(operation.NativeDeviceIntervalNanoseconds().has_value());
        EXPECT_LE(*operation.NativeDeviceIntervalNanoseconds(),
            vk::Ex2VulkanEDurationEnvelopeNanoseconds);
        EXPECT_TRUE(operation.Diagnostics().timestampQueryPoolCreated);
        EXPECT_EQ(operation.Diagnostics().preparedCommandRecordCount, 1U);
        EXPECT_EQ(operation.Diagnostics().preparedNativeCopyCount, 1U);
        const auto metadata = operation.NativeTimingMetadata();
        EXPECT_EQ(metadata.startStage, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
        EXPECT_EQ(metadata.stopStage, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        ExpectValidationClean(operation.Diagnostics());
    }
}

TEST(Ex2VulkanE, TimedZeroByteTransferIsNotApplicable)
{
    const std::vector<std::uint8_t> empty;
    vk::Ex2VulkanEDeviceTimedOperation operation{
        Configuration(ex2::TransferDirection::HostToDevice, 0U)};
    operation.PrepareTransfer(empty);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    operation.RetrieveNativeTiming();
    EXPECT_EQ(operation.NativeTimingStatus(),
        vk::Ex2VulkanENativeTimingStatus::NotApplicable);
    EXPECT_FALSE(operation.NativeDeviceIntervalNanoseconds().has_value());
    EXPECT_FALSE(operation.LastCompletionExecutedCopy());
    EXPECT_EQ(operation.Diagnostics().preparedNativeCopyCount, 0U);
    ExpectValidationClean(operation.Diagnostics());
}

TEST(Ex2VulkanEPure, ConfigurationResourceAndFailureSeams)
{
    static_assert(!vk::Ex2VulkanEOperation::EnqueuesDeviceTimestamps);
    static_assert(vk::Ex2VulkanEDeviceTimedOperation::EnqueuesDeviceTimestamps);
    auto valid = ex2::MakeConfiguration(Configuration(
        ex2::TransferDirection::HostToDevice, 257U));
    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanEConfiguration(valid));
    valid.common.operationBoundary =
        ex2::OperationBoundary::SingleDispatchCompletion;
    EXPECT_THROW(vk::detail::ValidateEx2VulkanEConfiguration(valid),
        std::invalid_argument);
    EXPECT_THROW(vk::detail::ValidateEx2VulkanEConfiguration(
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E2,
            257U,
            ex2::TransferDirection::HostToDevice})), std::invalid_argument);
    EXPECT_THROW(vk::detail::ValidateEx2VulkanEConfiguration(
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E1,
            257U,
            static_cast<ex2::TransferDirection>(-1)})), std::invalid_argument);

    EXPECT_EQ(vk::detail::CalculateEx2VulkanEByteCount(257U, 257U, 257U),
        257U);
    EXPECT_THROW(static_cast<void>(vk::detail::CalculateEx2VulkanEByteCount(
        258U, 257U, 258U)), std::length_error);
    EXPECT_THROW(static_cast<void>(vk::detail::CalculateEx2VulkanEByteCount(
        258U, 258U, 257U)), std::length_error);
    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanEAllocationCount(3U, 3U));
    EXPECT_THROW(vk::detail::ValidateEx2VulkanEAllocationCount(2U, 3U),
        std::length_error);
    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanEHeapFeasibility(
        4U, 4U, 8U));
    EXPECT_THROW(vk::detail::ValidateEx2VulkanEHeapFeasibility(
        5U, 4U, 8U), std::length_error);

    EXPECT_EQ(vk::detail::ClassifyEx2VulkanESubmissionResult(
        VK_ERROR_OUT_OF_DEVICE_MEMORY),
        vk::detail::Ex2VulkanESubmissionDisposition::FailedWithoutSubmission);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanESubmissionResult(
        VK_ERROR_DEVICE_LOST),
        vk::detail::Ex2VulkanESubmissionDisposition::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanEWaitResult(VK_TIMEOUT),
        vk::detail::Ex2VulkanEOperationState::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanEWaitResult(VK_ERROR_DEVICE_LOST),
        vk::detail::Ex2VulkanEOperationState::CompletionUncertain);
    EXPECT_EQ(vk::detail::ClassifyEx2VulkanEResourceDisposition(true, false),
        vk::detail::Ex2VulkanEResourceDisposition::PreserveForProcessTeardown);
    EXPECT_FALSE(vk::detail::IsEx2VulkanEOperationStateReusable(
        vk::detail::Ex2VulkanEOperationState::CompletionUncertain));
}

TEST(Ex2VulkanEPure, QueueMemoryAlignmentAndTimingSelectionAreExplicit)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_EQ(vk::detail::SelectEx2VulkanEQueue(families), 1U);
    families[1].queueFlags = VK_QUEUE_TRANSFER_BIT;
    EXPECT_THROW(static_cast<void>(vk::detail::SelectEx2VulkanEQueue(families)),
        std::invalid_argument);

    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount = 2U;
    properties.memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    EXPECT_EQ(vk::detail::SelectEx2VulkanEMemoryType(
        0x3U,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        properties), 1U);
    EXPECT_THROW(static_cast<void>(vk::detail::SelectEx2VulkanEMemoryType(
        0x1U,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0U,
        properties)), std::invalid_argument);

    const auto middle = vk::detail::AlignEx2VulkanENoncoherentRange(
        65U, 100U, 1'024U, 64U);
    EXPECT_EQ(middle.offset, 64U);
    EXPECT_EQ(middle.size, 128U);
    const auto ending = vk::detail::AlignEx2VulkanENoncoherentRange(
        64U, 960U, 1'024U, 64U);
    EXPECT_EQ(ending.size, VK_WHOLE_SIZE);
    EXPECT_THROW(static_cast<void>(
        vk::detail::AlignEx2VulkanENoncoherentRange(0U, 1U, 1U, 0U)),
        std::invalid_argument);

    EXPECT_NO_THROW(vk::detail::ValidateEx2VulkanETimestampEnvelope(
        64U, 1.0L, vk::Ex2VulkanEDurationEnvelopeNanoseconds));
    EXPECT_THROW(vk::detail::ValidateEx2VulkanETimestampEnvelope(
        35U, 1.0L, vk::Ex2VulkanEDurationEnvelopeNanoseconds),
        std::invalid_argument);
    const std::array<vk::detail::Ex2VulkanETimestampQuery, 2U> queries{{
        {100U, 1U}, {250U, 1U}}};
    EXPECT_EQ(vk::detail::DecodeEx2VulkanETimestampQueries(
        VK_SUCCESS,
        queries,
        64U,
        2.0L,
        vk::Ex2VulkanEDurationEnvelopeNanoseconds), 300U);
    const std::array<vk::detail::Ex2VulkanETimestampQuery, 2U> unavailable{{
        {100U, 1U}, {250U, 0U}}};
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeEx2VulkanETimestampQueries(
        VK_SUCCESS,
        unavailable,
        64U,
        1.0L,
        vk::Ex2VulkanEDurationEnvelopeNanoseconds)), std::runtime_error);
}

TEST(Ex2VulkanEPure,
    NoncoherentReadbackPoisonRequiresAtomAlignedFlushBeforeGpuWrite)
{
    EXPECT_TRUE(vk::detail::RequiresEx2VulkanEHostCacheMaintenance(
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    EXPECT_FALSE(vk::detail::RequiresEx2VulkanEHostCacheMaintenance(
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

    const auto range = vk::detail::AlignEx2VulkanENoncoherentRange(
        0U, 257U, 384U, 64U);
    EXPECT_EQ(range.offset, 0U);
    EXPECT_EQ(range.size, 320U);
}

TEST(Ex2VulkanE, InvalidPhysicalDeviceIsRejected)
{
    EXPECT_THROW(vk::Ex2VulkanEOperation(
        Configuration(ex2::TransferDirection::HostToDevice, 1U),
        std::numeric_limits<std::uint32_t>::max()),
        std::invalid_argument);
}
