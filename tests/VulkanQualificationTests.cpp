#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"
#include "vulkan/VulkanQualification.hpp"

#include <gtest/gtest.h>

#include <array>
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
