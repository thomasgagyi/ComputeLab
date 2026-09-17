#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"
#include "vulkan/VulkanQualification.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace vk = computelab::vulkan;
constexpr auto SpirvPath = COMPUTELAB_EX1_SPIRV_PATH;

void ExpectValidHostTiming(const computelab::ex2::HostTimingIntervals& timing)
{
    EXPECT_EQ(timing.status, computelab::ex2::HostTimingStatus::Ok);
    ASSERT_TRUE(timing.hostSubmissionNanoseconds.has_value());
    ASSERT_TRUE(timing.hostWaitNanoseconds.has_value());
    ASSERT_TRUE(timing.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(
        *timing.hostSubmissionNanoseconds + *timing.hostWaitNanoseconds,
        *timing.hostCompletionNanoseconds);
}

TEST(VulkanQualification, TinyTransformExactlyMatchesCpuOracleAndG001Timing)
{
    const auto input = computelab::GenerateSeededInput(0xC0FFEEU, 257U);
    vk::VulkanQualificationOperation operation{input.size(), SpirvPath};
    operation.Upload(input);
    operation.PrepareHostOnly();

    const auto execution = operation.ExecuteHostOnly();

    EXPECT_EQ(execution.status, vk::VulkanQualificationStatus::Ok);
    EXPECT_EQ(
        execution.failurePhase,
        vk::VulkanQualificationFailurePhase::None);
    EXPECT_TRUE(execution.validationPassed);
    EXPECT_EQ(execution.output, computelab::TransformSequence(input));
    EXPECT_FALSE(execution.nativeResult.has_value());
    EXPECT_EQ(
        execution.nativeTimingStatus,
        vk::VulkanNativeTimingStatus::NotApplicable);
    EXPECT_FALSE(execution.nativeTimingMetadata.has_value());
    EXPECT_FALSE(operation.Diagnostics().timestampCommandsRecorded);
    EXPECT_FALSE(operation.Diagnostics().hostQueryResetEnabled);
    ExpectValidHostTiming(execution.hostTiming);
}

TEST(VulkanQualification, HostOnlyModeDoesNotRequireOrEnqueueDeviceTimestamps)
{
    EXPECT_EQ(vk::VulkanQualificationOperation::InstrumentMode, "H");
    EXPECT_FALSE(vk::VulkanQualificationOperation::EnqueuesDeviceTimestamps);

    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[0].timestampValidBits = 64U;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    families[1].timestampValidBits = 0U;
    EXPECT_EQ(vk::SelectComputeQualificationQueue(families), 1U);
}

TEST(VulkanQualification, HostAndNativeModesAreDistinctInstrumentConditions)
{
    EXPECT_EQ(vk::VulkanQualificationOperation::InstrumentMode, "H");
    EXPECT_FALSE(vk::VulkanQualificationOperation::EnqueuesDeviceTimestamps);
    EXPECT_EQ(
        vk::VulkanDeviceTimedQualificationOperation::InstrumentMode, "N");
    EXPECT_TRUE(
        vk::VulkanDeviceTimedQualificationOperation::EnqueuesDeviceTimestamps);
    EXPECT_EQ(
        vk::QualificationTimestampStartStage,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    EXPECT_EQ(
        vk::QualificationTimestampStopStage,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

TEST(VulkanQualification, ModeNRequiresTimestampSupportOnTheModeHQueue)
{
    std::array<VkQueueFamilyProperties, 3U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_COMPUTE_BIT;
    families[0].timestampValidBits = 0U;
    families[1].queueCount = 1U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    families[1].timestampValidBits = 64U;

    const auto hostQueue = vk::SelectComputeQualificationQueue(families);
    ASSERT_EQ(hostQueue, 0U);
    EXPECT_THROW(
        static_cast<void>(vk::ValidatePairedTimestampQualificationQueue(
            families, hostQueue)),
        std::runtime_error);

    families[0].timestampValidBits = 36U;
    EXPECT_EQ(
        vk::ValidatePairedTimestampQualificationQueue(families, hostQueue),
        hostQueue);
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    EXPECT_THROW(
        static_cast<void>(vk::ValidatePairedTimestampQualificationQueue(
            families, hostQueue)),
        std::runtime_error);
    EXPECT_THROW(
        static_cast<void>(vk::ValidatePairedTimestampQualificationQueue(
            families, 99U)),
        std::invalid_argument);
}

TEST(VulkanQualificationTiming, MasksTimestampsAndAllowsAtMostOneWrap)
{
    constexpr std::uint64_t Modulus = std::uint64_t{1U} << 36U;
    EXPECT_EQ(
        vk::QualificationTimestampNanoseconds(
            3U * Modulus + 10U,
            2U * Modulus + 20U,
            36U,
            1.0L),
        10U);
    EXPECT_EQ(
        vk::QualificationTimestampNanoseconds(
            Modulus - 6U, 5U, 36U, 1.0L),
        11U);
    EXPECT_EQ(
        vk::QualificationTimestampNanoseconds(10U, 13U, 64U, 2.5L),
        8U);
    EXPECT_EQ(
        vk::QualificationTimestampNanoseconds(77U, 77U, 64U, 1.0L),
        0U);
}

TEST(VulkanQualificationTiming, RejectsInvalidOrAmbiguousDurationEnvelopes)
{
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U, 1U, 0U, 1.0L)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U, 1U, 36U, 0.0L)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U, 1U, 36U, 1.0L, 0U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U,
            1U,
            36U,
            0.25L,
            vk::QualificationNativeDurationEnvelopeNanoseconds)),
        std::range_error);
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U, 101U, 64U, 1.0L, 100U)),
        std::range_error);
    EXPECT_THROW(
        static_cast<void>(vk::QualificationTimestampNanoseconds(
            0U,
            std::numeric_limits<std::uint64_t>::max(),
            64U,
            2.0L,
            std::numeric_limits<std::uint64_t>::max())),
        std::overflow_error);
}

TEST(VulkanQualificationTiming, UnavailableQueriesNeverBecomeZeroDuration)
{
    std::array<vk::VulkanQualificationTimestampQuery, 2U> queries{{
        {10U, 1U},
        {20U, 0U}}};
    auto result = vk::DecodeQualificationTimestampQueries(
        VK_SUCCESS, queries, 64U, 1.0L);
    EXPECT_EQ(result.status, vk::VulkanNativeTimingStatus::QueryUnavailable);
    EXPECT_FALSE(result.intervalNanoseconds.has_value());

    queries[1].available = 1U;
    result = vk::DecodeQualificationTimestampQueries(
        VK_NOT_READY, queries, 64U, 1.0L);
    EXPECT_EQ(
        result.status,
        vk::VulkanNativeTimingStatus::QueryRetrievalFailed);
    EXPECT_FALSE(result.intervalNanoseconds.has_value());
}

TEST(VulkanQualificationTiming, TimingFailureStatusPreservesNoInterval)
{
    vk::VulkanQualificationExecution execution;
    execution.instrumentMode = "N";
    execution.nativeTimingStatus =
        vk::VulkanNativeTimingStatus::ConversionInvalid;
    execution.failurePhase = vk::VulkanQualificationFailurePhase::NativeTiming;
    EXPECT_EQ(execution.instrumentMode, "N");
    EXPECT_FALSE(execution.nativeDeviceIntervalNanoseconds.has_value());
    EXPECT_EQ(
        execution.failurePhase,
        vk::VulkanQualificationFailurePhase::NativeTiming);
}

TEST(VulkanQualification, PendingWorkIsPreservedForProcessTeardownWithoutDestructorWait)
{
    using vk::detail::ClassifyVulkanResourceDisposition;
    using vk::detail::VulkanResourceDisposition;

    EXPECT_EQ(
        ClassifyVulkanResourceDisposition(false, false),
        VulkanResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        ClassifyVulkanResourceDisposition(true, false),
        VulkanResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(
        ClassifyVulkanResourceDisposition(false, true),
        VulkanResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(
        ClassifyVulkanResourceDisposition(true, true),
        VulkanResourceDisposition::PreserveForProcessTeardown);
}

TEST(VulkanQualification, ProvenCompleteOperationCanBePreparedAndReused)
{
    const auto input = computelab::GenerateSeededInput(77U, 1'025U);
    const auto expected = computelab::TransformSequence(input);
    vk::VulkanQualificationOperation operation{input.size(), SpirvPath};
    operation.Upload(input);
    operation.PrepareHostOnly();
    const auto first = operation.ExecuteHostOnly();

    operation.PrepareHostOnly();
    const auto second = operation.ExecuteHostOnly();

    ASSERT_EQ(first.status, vk::VulkanQualificationStatus::Ok);
    ASSERT_EQ(second.status, vk::VulkanQualificationStatus::Ok);
    EXPECT_EQ(first.output, expected);
    EXPECT_EQ(second.output, expected);
    ExpectValidHostTiming(first.hostTiming);
    ExpectValidHostTiming(second.hostTiming);
}

TEST(VulkanQualification, DeviceTimedTransformMatchesOracleAndCanBeSafelyReused)
{
    const auto input = computelab::GenerateSeededInput(0xC0FFEEU, 257U);
    const auto expected = computelab::TransformSequence(input);
    vk::VulkanDeviceTimedQualificationOperation operation{
        input.size(), SpirvPath};
    operation.Upload(input);
    operation.PrepareDeviceTimed();
    const auto first = operation.ExecuteDeviceTimed();

    operation.PrepareDeviceTimed();
    const auto second = operation.ExecuteDeviceTimed();

    ASSERT_EQ(first.status, vk::VulkanQualificationStatus::Ok);
    ASSERT_EQ(second.status, vk::VulkanQualificationStatus::Ok);
    EXPECT_EQ(first.instrumentMode, "N");
    EXPECT_EQ(first.output, expected);
    EXPECT_EQ(second.output, expected);
    EXPECT_TRUE(first.validationPassed);
    EXPECT_EQ(
        first.nativeTimingStatus,
        vk::VulkanNativeTimingStatus::Valid);
    EXPECT_TRUE(first.nativeDeviceIntervalNanoseconds.has_value());
    ASSERT_TRUE(first.nativeTimingMetadata.has_value());
    EXPECT_EQ(
        first.nativeTimingMetadata->startStage,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    EXPECT_EQ(
        first.nativeTimingMetadata->stopStage,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    EXPECT_GT(first.nativeTimingMetadata->timestampValidBits, 0U);
    EXPECT_GT(first.nativeTimingMetadata->timestampPeriodNanoseconds, 0.0L);
    EXPECT_TRUE(operation.Diagnostics().timestampCommandsRecorded);
    EXPECT_TRUE(operation.Diagnostics().hostQueryResetEnabled);
    ExpectValidHostTiming(first.hostTiming);
    ExpectValidHostTiming(second.hostTiming);
}

TEST(VulkanQualification, InvalidLifecycleTransitionsAreRejected)
{
    vk::VulkanQualificationOperation operation{1U, SpirvPath};
    EXPECT_THROW(operation.PrepareHostOnly(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.ExecuteHostOnly()),
        std::logic_error);
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);

    const std::vector<std::uint32_t> input{123U};
    operation.Upload(input);
    EXPECT_THROW(
        static_cast<void>(operation.ExecuteHostOnly()),
        std::logic_error);
    operation.PrepareHostOnly();
    EXPECT_THROW(operation.PrepareHostOnly(), std::logic_error);
    EXPECT_THROW(operation.Upload(input), std::logic_error);
    EXPECT_EQ(
        operation.ExecuteHostOnly().status,
        vk::VulkanQualificationStatus::Ok);

    vk::VulkanDeviceTimedQualificationOperation timedOperation{1U, SpirvPath};
    EXPECT_THROW(timedOperation.PrepareDeviceTimed(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(timedOperation.ExecuteDeviceTimed()),
        std::logic_error);
    timedOperation.Upload(input);
    EXPECT_THROW(
        static_cast<void>(timedOperation.ExecuteDeviceTimed()),
        std::logic_error);
    timedOperation.PrepareDeviceTimed();
    EXPECT_THROW(timedOperation.PrepareDeviceTimed(), std::logic_error);
    EXPECT_EQ(
        timedOperation.ExecuteDeviceTimed().status,
        vk::VulkanQualificationStatus::Ok);
}

TEST(VulkanQualification, MissingShaderAndInvalidDeviceFailuresRetainContext)
{
    try
    {
        vk::VulkanQualificationOperation operation{
            1U, "missing-qualification-shader.spv"};
        FAIL() << "expected missing SPIR-V failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(
            std::string{error.what()}.find("SPIR-V"),
            std::string::npos);
    }

    EXPECT_THROW(
        static_cast<void>(vk::VulkanQualificationOperation{
            1U,
            SpirvPath,
            std::numeric_limits<std::uint32_t>::max()}),
        std::invalid_argument);
}

TEST(VulkanQualification, QueueSelectionRejectsLackOfComputeCapability)
{
    std::array<VkQueueFamilyProperties, 2U> families{};
    families[0].queueCount = 1U;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[1].queueCount = 0U;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    EXPECT_THROW(
        static_cast<void>(vk::SelectComputeQualificationQueue(families)),
        std::runtime_error);
}

} // namespace
