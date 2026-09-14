#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"
#include "vulkan/VulkanTiming.hpp"
#include "vulkan/VulkanTransform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

class VulkanCorrectness : public testing::TestWithParam<std::size_t> {};

TEST_P(VulkanCorrectness, DeviceLocalOutputExactlyMatchesA3BeforeTimingIsAccepted)
{
    const auto input = computelab::GenerateSeededInput(0xDEADBEEF12345678ULL, GetParam());
    const auto expected = computelab::TransformSequence(input);
    vk::TransformDispatch dispatch(input.size(), SpirvPath);
    const auto& diagnostics = dispatch.Diagnostics();
    EXPECT_NE(diagnostics.inputMemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0U);
    EXPECT_NE(diagnostics.outputMemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0U);
    EXPECT_NE(diagnostics.uploadMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0U);
    EXPECT_NE(diagnostics.readbackMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0U);
    dispatch.Upload(input);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    // Explicit completion is required even if this tiny dispatch already finished
    // physically; the getter must not poll or secretly wait on behalf of callers.
    EXPECT_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.RetrieveOutput()), std::logic_error);
    dispatch.WaitForCompletion();
    ASSERT_EQ(dispatch.RetrieveOutput(), expected);
    // No minimum-duration threshold: timestamp granularity can yield zero.
    EXPECT_NO_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()));
}

INSTANTIATE_TEST_SUITE_P(Ex1, VulkanCorrectness,
    testing::Values(std::size_t{0}, std::size_t{1}, std::size_t{8},
        std::size_t{256}, std::size_t{257}, std::size_t{1'048'579}));

TEST(VulkanTransform, IdenticalInputAndChangedInputReuseRemainExact)
{
    const auto input = computelab::GenerateSeededInput(0U, 257);
    const auto expected = computelab::TransformSequence(input);
    vk::TransformDispatch dispatch(input.size(), SpirvPath);
    dispatch.Upload(input);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    dispatch.WaitForCompletion();
    const auto first = dispatch.RetrieveOutput();
    ASSERT_EQ(first, expected);
    const auto firstTime = dispatch.DeviceElapsedNanoseconds();
    EXPECT_EQ(dispatch.DeviceElapsedNanoseconds(), firstTime);

    dispatch.Upload(input);
    EXPECT_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()), std::logic_error);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    dispatch.WaitForCompletion();
    ASSERT_EQ(dispatch.RetrieveOutput(), first);
    EXPECT_NO_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()));

    const auto changed = computelab::GenerateSeededInput(42U, 257);
    dispatch.Upload(changed);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    dispatch.WaitForCompletion();
    ASSERT_EQ(dispatch.RetrieveOutput(), computelab::TransformSequence(changed));
    EXPECT_NO_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()));
    // Repeated untimed readback is also safe and cannot change query contents.
    EXPECT_EQ(dispatch.RetrieveOutput(), computelab::TransformSequence(changed));
}

TEST(VulkanTransform, RejectsInvalidLifecycleWithoutHiddenCompletion)
{
    vk::TransformDispatch dispatch(1, SpirvPath);
    const std::vector<std::uint32_t> input{1U};
    EXPECT_THROW(static_cast<void>(dispatch.PrepareMeasurement()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.SubmitTransform()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.WaitForCompletion()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.RetrieveOutput()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.Upload({})), std::invalid_argument);
    dispatch.Upload(input);
    EXPECT_THROW(static_cast<void>(dispatch.Upload(input)), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.SubmitTransform()), std::logic_error);
    dispatch.PrepareMeasurement();
    EXPECT_THROW(static_cast<void>(dispatch.PrepareMeasurement()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.Upload(input)), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.WaitForCompletion()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()), std::logic_error);
    dispatch.SubmitTransform();
    EXPECT_THROW(static_cast<void>(dispatch.SubmitTransform()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.PrepareMeasurement()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.Upload(input)), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()), std::logic_error);
    dispatch.WaitForCompletion();
    EXPECT_THROW(static_cast<void>(dispatch.WaitForCompletion()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.SubmitTransform()), std::logic_error);
    EXPECT_THROW(static_cast<void>(dispatch.PrepareMeasurement()), std::logic_error);
    ASSERT_EQ(dispatch.RetrieveOutput(), computelab::TransformSequence(input));
    EXPECT_NO_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()));
}

TEST(VulkanTransform, SelectedQueueAndEnabledFeaturesMeetA7Requirements)
{
    vk::TransformDispatch dispatch(0, SpirvPath);
    const auto& info = dispatch.Diagnostics();
    EXPECT_GE(info.properties.apiVersion, VK_API_VERSION_1_3);
    EXPECT_NE(info.queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT, 0U);
    EXPECT_GT(info.queueFamily.queueCount, 0U);
    EXPECT_GE(info.queueFamily.timestampValidBits, 36U);
    EXPECT_LE(info.queueFamily.timestampValidBits, 64U);
    EXPECT_TRUE(info.synchronization2Enabled);
    EXPECT_TRUE(info.hostQueryResetEnabled);
    EXPECT_GT(info.properties.limits.timestampPeriod, 0.0F);
    EXPECT_TRUE(std::isfinite(info.properties.limits.timestampPeriod));
}

TEST(VulkanTransform, SelectedDeviceUuidIsExposedFromOwnedPhysicalDevice)
{
    vk::TransformDispatch dispatch(1, SpirvPath);
    EXPECT_EQ(dispatch.SelectedDeviceUuid(), dispatch.Diagnostics().deviceUuid);
    EXPECT_TRUE(std::any_of(
        dispatch.SelectedDeviceUuid().begin(), dispatch.SelectedDeviceUuid().end(),
        [](std::uint8_t value) { return value != 0U; }));
}

TEST(VulkanTransform, DestructionSafelyDrainsAnAbandonedSubmission)
{
    vk::TransformDispatch dispatch(257, SpirvPath);
    dispatch.Upload(computelab::GenerateSeededInput(7, 257));
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    // Resource teardown, outside measurement, must retain the pending fence and
    // resources until completion. Validation layers can diagnose lifetime errors.
}

TEST(VulkanTransform, ReuseWithoutReadbackStillEstablishesOutputWriteDependency)
{
    vk::TransformDispatch dispatch(257, SpirvPath);
    const auto input = computelab::GenerateSeededInput(1, 257);
    dispatch.Upload(input);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    dispatch.WaitForCompletion();
    // Discard the unvalidated duration; exercise the next output write directly.
    dispatch.Upload(input);
    dispatch.PrepareMeasurement();
    dispatch.SubmitTransform();
    dispatch.WaitForCompletion();
    ASSERT_EQ(dispatch.RetrieveOutput(), computelab::TransformSequence(input));
    EXPECT_NO_THROW(static_cast<void>(dispatch.DeviceElapsedNanoseconds()));
}

TEST(VulkanTransform, SetupFailureHasUsefulContextAndReleasesPartialResources)
{
    try
    {
        vk::TransformDispatch dispatch(1, "missing-ex1-shader.spv");
        FAIL() << "Expected missing SPIR-V failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("SPIR-V"), std::string::npos);
    }
    EXPECT_THROW(static_cast<void>(vk::TransformDispatch(1, SpirvPath,
        std::numeric_limits<std::uint32_t>::max())), std::invalid_argument);
    // Failure after instance/device creation, before allocating an oversized buffer.
    EXPECT_THROW(static_cast<void>(vk::TransformDispatch(std::numeric_limits<std::size_t>::max(), SpirvPath)), std::invalid_argument);
}

TEST(VulkanTiming, ValidBitsMaskBothValuesAndHandleOrdinaryRollover)
{
    constexpr auto modulus = std::uint64_t{1} << 36;
    EXPECT_EQ(vk::TimestampNanoseconds(15 * modulus + 10, 14 * modulus + 20, 36, 1.0L), 10U);
    EXPECT_EQ(vk::TimestampNanoseconds(modulus - 6, 5, 36, 1.0L), 11U);
    EXPECT_EQ(vk::TimestampNanoseconds(2 * modulus - 6, 3 * modulus + 5, 36, 1.0L), 11U);
    EXPECT_EQ(vk::TimestampNanoseconds(modulus - 1, 0, 36, 1.0L), 1U);
    EXPECT_EQ(vk::TimestampNanoseconds(77, 77, 36, 1.0L), 0U);
}

TEST(VulkanTiming, FullWidthSubtractionDoesNotShiftBy64)
{
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(vk::TimestampNanoseconds(maximum - 4, 3, 64, 1.0L), 8U);
    EXPECT_EQ(vk::TimestampNanoseconds(0, maximum, 64, 1.0L), maximum);
    EXPECT_EQ(vk::TimestampNanoseconds(0, 0, 64, 1.0L), 0U);
    EXPECT_EQ(vk::TimestampNanoseconds(std::uint64_t{1} << 63,
        (std::uint64_t{1} << 63) + 123, 64, 1.0L), 123U);
}

TEST(VulkanTiming, PeriodConvertsTicksToNearestIntegerNanosecond)
{
    EXPECT_EQ(vk::TimestampNanoseconds(10, 13, 64, 2.5L), 8U);
    EXPECT_EQ(vk::TimestampNanoseconds(0, 1, 36, 0.49L), 0U);
    EXPECT_EQ(vk::TimestampNanoseconds(0, 1, 36, 0.5L), 1U);
    EXPECT_EQ(vk::TimestampNanoseconds(0, 3, 36, 0.5L), 2U);
    EXPECT_EQ(vk::TimestampNanoseconds(1, 5, 36, 2.25L), 9U);
}

TEST(VulkanTiming, InvalidMetadataAndNonFiniteOrOutOfRangeDurationsFail)
{
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 0, 1)), std::invalid_argument);
    for (std::uint32_t invalidWidth = 1; invalidWidth < 36; ++invalidWidth)
    {
        EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, invalidWidth, 1)),
            std::invalid_argument) << "timestampValidBits=" << invalidWidth;
    }
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 65, 1)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64, 0)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64, -1)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64,
        std::numeric_limits<long double>::infinity())), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64,
        -std::numeric_limits<long double>::infinity())), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64,
        std::numeric_limits<long double>::quiet_NaN())), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 2, 64,
        std::numeric_limits<long double>::max())), std::overflow_error);
    EXPECT_THROW(static_cast<void>(vk::TimestampNanoseconds(0, 1, 64, std::ldexp(1.0L, 64))), std::overflow_error);
    const auto belowLimit = std::nextafter(std::ldexp(1.0L, 64), 0.0L);
    EXPECT_EQ(vk::TimestampNanoseconds(0, 1, 64, belowLimit),
        static_cast<std::uint64_t>(belowLimit));
}

TEST(VulkanTiming, QueryDecodingRequiresSuccessAndBothAvailabilityValues)
{
    std::array<vk::detail::TimestampQuery, 2> queries{{{10, 1}, {20, 1}}};
    EXPECT_EQ(vk::detail::TimestampResultFlags,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    EXPECT_EQ(vk::detail::TimestampResultFlags & VK_QUERY_RESULT_WAIT_BIT, 0U);
    EXPECT_EQ(vk::detail::DecodeTimestampQueries(VK_SUCCESS, queries, 64, 1), 10U);
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeTimestampQueries(VK_NOT_READY, queries, 64, 1)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeTimestampQueries(VK_ERROR_DEVICE_LOST, queries, 64, 1)), std::runtime_error);
    queries[0].available = 0;
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeTimestampQueries(VK_SUCCESS, queries, 64, 1)), std::runtime_error);
    queries[0].available = 1;
    queries[1].available = 0;
    EXPECT_THROW(static_cast<void>(vk::detail::DecodeTimestampQueries(VK_SUCCESS, queries, 64, 1)), std::runtime_error);
    // Availability is any nonzero value per Vulkan, not necessarily exactly 1.
    queries[1].available = 9;
    EXPECT_EQ(vk::detail::DecodeTimestampQueries(VK_SUCCESS, queries, 64, 1), 10U);
}

TEST(VulkanTiming, VulkanFailuresRetainOperationAndResultCode)
{
    try
    {
        vk::detail::CheckResult(VK_ERROR_DEVICE_LOST, "vkQueueSubmit2 EX-1 transform");
        FAIL() << "Expected Vulkan error";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message(error.what());
        EXPECT_NE(message.find("vkQueueSubmit2 EX-1 transform"), std::string::npos);
        EXPECT_NE(message.find("VkResult=-4"), std::string::npos);
    }
    EXPECT_THROW(static_cast<void>(vk::detail::CheckResult(VK_TIMEOUT, "vkWaitForFences")), std::runtime_error);
}

TEST(VulkanSetup, QueueSelectionIsFirstQualifyingFamilyWithoutFallbackFromMalformedWidth)
{
    std::array<VkQueueFamilyProperties, 4> families{};
    families[0].queueCount = 1;
    families[0].queueFlags = VK_QUEUE_TRANSFER_BIT;
    families[0].timestampValidBits = 64;
    families[1].queueCount = 1;
    families[1].queueFlags = VK_QUEUE_COMPUTE_BIT;
    families[2] = families[1];
    families[2].timestampValidBits = 36;
    families[3] = families[2];
    EXPECT_EQ(vk::SelectComputeTimestampQueue(families), 2U);
    families[2].queueCount = 0;
    EXPECT_EQ(vk::SelectComputeTimestampQueue(families), 3U);
    families[2].queueCount = 1;
    for (std::uint32_t invalidWidth = 1; invalidWidth < 36; ++invalidWidth)
    {
        families[2].timestampValidBits = invalidWidth;
        EXPECT_THROW(static_cast<void>(vk::SelectComputeTimestampQueue(families)),
            std::runtime_error) << "timestampValidBits=" << invalidWidth;
    }
    families[2].timestampValidBits = 65;
    EXPECT_THROW(static_cast<void>(vk::SelectComputeTimestampQueue(families)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(vk::SelectComputeTimestampQueue({})), std::runtime_error);
    EXPECT_THROW(static_cast<void>(vk::SelectComputeTimestampQueue(std::span(families).first(2))), std::runtime_error);
}

VkPhysicalDeviceLimits SupportedLimits()
{
    VkPhysicalDeviceLimits limits{};
    limits.maxComputeWorkGroupInvocations = 256;
    limits.maxComputeWorkGroupSize[0] = 256;
    limits.maxComputeWorkGroupSize[1] = limits.maxComputeWorkGroupSize[2] = 1;
    limits.maxComputeWorkGroupCount[0] = 65535;
    limits.maxComputeWorkGroupCount[1] = limits.maxComputeWorkGroupCount[2] = 1;
    limits.maxStorageBufferRange = std::numeric_limits<std::uint32_t>::max();
    return limits;
}

TEST(VulkanSetup, FixedWorkgroupAndDispatchLimitsAreValidatedBeforePipelineUse)
{
    const auto valid = SupportedLimits();
    EXPECT_EQ(vk::ValidateDispatch(0, valid), 1U);
    EXPECT_EQ(vk::ValidateDispatch(1, valid), 1U);
    EXPECT_EQ(vk::ValidateDispatch(256, valid), 1U);
    EXPECT_EQ(vk::ValidateDispatch(257, valid), 2U);
    EXPECT_EQ(vk::ValidateDispatch(65535ULL * 256, valid), 65535U);
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(65535ULL * 256 + 1, valid)), std::runtime_error);
    auto limits = valid;
    limits.maxComputeWorkGroupInvocations = 255;
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(1, limits)), std::runtime_error);
    limits = valid;
    limits.maxComputeWorkGroupSize[0] = 255;
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(1, limits)), std::runtime_error);
    limits = valid;
    limits.maxComputeWorkGroupCount[0] = 1;
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(257, limits)), std::runtime_error);
    limits = valid;
    limits.maxStorageBufferRange = 1024;
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(257, limits)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(vk::ValidateDispatch(std::numeric_limits<std::size_t>::max(), valid)), std::invalid_argument);
}
} // namespace
