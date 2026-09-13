#include "cuda/CudaTransform.hpp"
#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using computelab::cuda::CudaTransformOperation;
using computelab::cuda::DeviceMillisecondsToNanoseconds;

std::vector<std::uint32_t> ExecuteCudaTransform(
    const std::vector<std::uint32_t>& input)
{
    CudaTransformOperation operation{0, input.size()};
    operation.Upload(input);
    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(CudaTransform, ZeroElementBehaviorIsSafeAndDefined)
{
    CudaTransformOperation operation{0, 0U};
    operation.Upload({});
    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();

    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_GE(operation.DeviceElapsedNanoseconds(), 0U);
}

TEST(CudaTransform, OneElementMatchesExistingCpuOracleExactly)
{
    const std::vector<std::uint32_t> input{1U};

    EXPECT_EQ(ExecuteCudaTransform(input), computelab::TransformSequence(input));
}

TEST(CudaTransform, SeededRepresentativeInputMatchesExistingCpuOracleExactly)
{
    const auto input = computelab::GenerateSeededInput(0xDEADBEEFU, 32U);

    EXPECT_EQ(ExecuteCudaTransform(input), computelab::TransformSequence(input));
}

TEST(CudaTransform, SizeNotDivisibleByBlockSizeMatchesExistingCpuOracleExactly)
{
    const auto input = computelab::GenerateSeededInput(257U, 257U);

    EXPECT_EQ(ExecuteCudaTransform(input), computelab::TransformSequence(input));
}

TEST(CudaTransform, LargerRepresentativeInputMatchesExistingCpuOracleExactly)
{
    const auto input = computelab::GenerateSeededInput(0x0123456789ABCDEFULL, 65'537U);

    EXPECT_EQ(ExecuteCudaTransform(input), computelab::TransformSequence(input));
}

TEST(CudaTransform, RepeatedExecutionsWithIdenticalInputAreExactlyDeterministic)
{
    const auto input = computelab::GenerateSeededInput(99U, 4'097U);
    CudaTransformOperation operation{0, input.size()};
    operation.Upload(input);

    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();
    const auto first = operation.RetrieveOutput();

    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveOutput();

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, computelab::TransformSequence(input));
}

TEST(CudaTransform, SubmissionAndExplicitCompletionAreSeparateApiPhases)
{
    const auto input = computelab::GenerateSeededInput(7U, 257U);
    CudaTransformOperation operation{0, input.size()};
    operation.Upload(input);
    operation.RecordDeviceStart();

    EXPECT_NO_THROW(operation.SubmitTransform());
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveOutput()),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.DeviceElapsedNanoseconds()),
        std::logic_error);

    operation.RecordDeviceStop();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveOutput(), computelab::TransformSequence(input));
}

TEST(CudaTransform, TimingEventsProduceAValidNonnegativeDeviceDuration)
{
    const auto input = computelab::GenerateSeededInput(123U, 1'024U);
    CudaTransformOperation operation{0, input.size()};
    operation.Upload(input);
    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();

    EXPECT_GE(operation.DeviceElapsedNanoseconds(), 0U);
}

TEST(CudaTimingConversion, UsesNearestIntegerNanosecondRule)
{
    EXPECT_EQ(DeviceMillisecondsToNanoseconds(0.0F), 0U);
    EXPECT_EQ(DeviceMillisecondsToNanoseconds(1.0F), 1'000'000U);
    EXPECT_EQ(DeviceMillisecondsToNanoseconds(0.0009765625F), 977U);
    EXPECT_EQ(DeviceMillisecondsToNanoseconds(1.5F), 1'500'000U);
}

TEST(CudaTimingConversion, RejectsInvalidElapsedValues)
{
    EXPECT_THROW(
        static_cast<void>(DeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::quiet_NaN())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(DeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::infinity())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(DeviceMillisecondsToNanoseconds(-1.0F)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(DeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::max())),
        std::overflow_error);
}

TEST(CudaTransform, InvalidCudaDeviceErrorRetainsOperationContext)
{
    try
    {
        CudaTransformOperation operation{-1, 1U};
        FAIL() << "expected cudaSetDevice to fail";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string{error.what()}.find("cudaSetDevice"), std::string::npos);
    }
}

} // namespace
