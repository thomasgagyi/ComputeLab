#include "cuda/Ex2CudaC.hpp"

#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2CpuOracles.hpp"

#include <gtest/gtest.h>

#include <cuda_runtime_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;

ex2::ContentionConfiguration CConfiguration(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    return {elementCount, activeCounterCount, elementCount};
}

std::vector<std::uint32_t> ExecuteOne(
    cuda::Ex2CudaCOperation& operation,
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
        EXPECT_EQ(counters[index],
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
    cuda::Ex2CudaCOperation operation{
        0, CConfiguration(elementCount, activeCounterCount)};

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
    EXPECT_TRUE(operation.LastCompletionExecutedKernel());

    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveCounters();
    EXPECT_EQ(second, reference.counters);
    EXPECT_EQ(second, first);
    EXPECT_EQ(std::accumulate(second.begin(), second.end(), 0ULL), elementCount);
}

TEST(Ex2CudaCCore, LowContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActiveAll, 1U);
}

TEST(Ex2CudaCCore, MediumContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActiveOnePer32, 32U);
}

TEST(Ex2CudaCCore, HighContentionMatchesCompleteIndependentContractTwice)
{
    VerifyApprovedCore(ex2::ContentionActive64, 16'384U);
}

TEST(Ex2CudaCCorrectnessOnly,
    Padded257DispatchExercisesGuardAndPreservesCompleteState)
{
    constexpr std::uint64_t elementCount = 257U;
    const auto targets = ex2::GenerateContentionTargets(
        elementCount, elementCount);
    cuda::Ex2CudaCOperation operation{
        0, CConfiguration(elementCount, elementCount)};

    const auto counters = ExecuteOne(operation, targets);
    EXPECT_EQ(counters, std::vector<std::uint32_t>(elementCount, 1U));
    EXPECT_EQ(operation.RetrieveDeviceTargets(), targets);
    EXPECT_EQ(std::accumulate(counters.begin(), counters.end(), 0ULL),
        elementCount);
    EXPECT_TRUE(operation.LastCompletionExecutedKernel());

    const auto shape = cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(elementCount, elementCount), 2U, 256U, 256U);
    EXPECT_EQ(shape.blockCount, 2U);
    EXPECT_EQ(shape.threadsPerBlock, 256U);
}

TEST(Ex2CudaCCorrectnessOnly, ZeroLifecycleAllocatesNoLogicalPayloadOrKernel)
{
    const std::vector<std::uint32_t> targets;
    cuda::Ex2CudaCOperation operation{0, CConfiguration(0U, 0U)};

    const auto counters = ExecuteOne(operation, targets);
    EXPECT_TRUE(counters.empty());
    EXPECT_TRUE(operation.RetrieveDeviceTargets().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedKernel());

    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    EXPECT_TRUE(operation.RetrieveCounters().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedKernel());
}

TEST(Ex2CudaCValidation,
    RejectsWrongLengthsRangeAndAlteredInRangeTargetsAndInvalidatesReadiness)
{
    const auto exact = ex2::GenerateContentionTargets(8U, 2U);
    const std::vector<std::uint32_t> shortTargets(
        exact.begin(), exact.end() - 1);
    auto longTargets = exact;
    longTargets.push_back(0U);
    auto outOfRange = exact;
    outOfRange[0] = 2U;
    auto alteredInRange = exact;
    alteredInRange[0] = alteredInRange[0] == 0U ? 1U : 0U;

    cuda::Ex2CudaCOperation operation{0, CConfiguration(8U, 2U)};
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

TEST(Ex2CudaCValidation, RejectsMalformedConfigurationsBeforeCudaWork)
{
    for (const auto configuration : {
        ex2::ContentionConfiguration{1U, 0U, 1U},
        ex2::ContentionConfiguration{1U, 2U, 1U},
        ex2::ContentionConfiguration{8U, 3U, 8U},
        ex2::ContentionConfiguration{8U, 2U, 7U},
        ex2::ContentionConfiguration{8191U, 1U, 8191U},
        ex2::ContentionConfiguration{
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U,
            1U,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U}})
    {
        EXPECT_THROW(cuda::detail::ValidateEx2CudaCConfiguration(
            ex2::MakeConfiguration(configuration)), std::invalid_argument);
    }

    auto wrongRevision = ex2::MakeConfiguration(CConfiguration(1U, 1U));
    wrongRevision.common.generatorRevision = "wrong-revision";
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCConfiguration(wrongRevision),
        std::invalid_argument);

    auto wrongBoundary = ex2::MakeConfiguration(CConfiguration(1U, 1U));
    wrongBoundary.common.operationBoundary =
        ex2::OperationBoundary::SingleDispatchCompletion;
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCConfiguration(wrongBoundary),
        std::invalid_argument);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCConfiguration(
        ex2::MakeConfiguration(
            ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U})),
        std::invalid_argument);
}

TEST(Ex2CudaCGuards, CalculatesCheckedLaunchShapeAndRejectsDeviceLimits)
{
    const auto zero = cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(0U, 0U), 0U, 0U, 0U);
    const auto exact = cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(256U, 256U), 1U, 256U, 256U);
    const auto padded = cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(257U, 257U), 2U, 256U, 256U);
    EXPECT_EQ(zero.blockCount, 0U);
    EXPECT_EQ(exact.blockCount, 1U);
    EXPECT_EQ(padded.blockCount, 2U);
    EXPECT_EQ(padded.threadsPerBlock, 256U);

    EXPECT_THROW(static_cast<void>(cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(257U, 257U), 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(1U, 1U), 0U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(1U, 1U), 1U, 255U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cuda::detail::ValidateEx2CudaCLaunchShape(
        CConfiguration(1U, 1U), 1U, 256U, 255U)),
        std::invalid_argument);
}

TEST(Ex2CudaCGuards,
    RejectsByteVectorAndTwoBufferMemoryLimitsWithoutAllocating)
{
    EXPECT_EQ(cuda::detail::CalculateEx2CudaCBufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(static_cast<void>(
        cuda::detail::CalculateEx2CudaCBufferByteCount(257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(static_cast<void>(cuda::detail::CalculateEx2CudaCBufferByteCount(
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) + 1U,
        std::numeric_limits<std::size_t>::max())), std::invalid_argument);
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaCHostVectorCapacity(8U, 8U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCHostVectorCapacity(9U, 8U),
        std::length_error);

    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaCMemoryFeasibility(
        0U, 0U, 0U));
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaCMemoryFeasibility(
        1'024U, 2'048U, 2'048U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCMemoryFeasibility(
        1'025U, 2'049U, 4'096U), std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaCMemoryFeasibility(
        1'025U, 4'096U, 2'049U), std::length_error);
}

TEST(Ex2CudaC, InvalidLifecycleTransitionsFailExplicitly)
{
    const auto targets = ex2::GenerateContentionTargets(1U, 1U);
    cuda::Ex2CudaCOperation operation{0, CConfiguration(1U, 1U)};

    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.PrepareReset(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveCounters()),
        std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveDeviceTargets()),
        std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionExecutedKernel()),
        std::logic_error);

    operation.UploadTargets(targets);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.PrepareReset();
    operation.SubmitAtomic();
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.PrepareReset(), std::logic_error);
    EXPECT_THROW(operation.UploadTargets(targets), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveCounters()),
        std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);

    operation.UploadTargets(targets);
    EXPECT_THROW(operation.SubmitAtomic(), std::logic_error);
    operation.PrepareReset();
    operation.SubmitAtomic();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveCounters(),
        (std::vector<std::uint32_t>{1U}));
}

TEST(Ex2CudaCHostLifetime, UploadOwnsCallerTargetsUntilNativeCompletion)
{
    const auto expectedTargets = ex2::GenerateContentionTargets(257U, 257U);
    std::vector<std::uint32_t> counters;
    std::vector<std::uint32_t> deviceTargets;
    {
        cuda::Ex2CudaCOperation operation{0, CConfiguration(257U, 257U)};
        {
            auto callerTargets = expectedTargets;
            operation.UploadTargets(callerTargets);
        }
        operation.PrepareReset();
        operation.SubmitAtomic();
        operation.WaitForCompletion();
        counters = operation.RetrieveCounters();
        deviceTargets = operation.RetrieveDeviceTargets();
    }
    EXPECT_EQ(counters, std::vector<std::uint32_t>(257U, 1U));
    EXPECT_EQ(deviceTargets, expectedTargets);
}

TEST(Ex2CudaC, DeviceIdentityAndInvalidSelectorRemainExplicit)
{
    cudaDeviceProp properties{};
    ASSERT_EQ(cudaGetDeviceProperties(&properties, 0), cudaSuccess);
    cuda::Ex2CudaCOperation operation{0, CConfiguration(1U, 1U)};
    EXPECT_EQ(operation.SelectedDeviceOrdinal(), 0);
    EXPECT_EQ(operation.ElementCount(), 1U);
    EXPECT_EQ(operation.ActiveCounterCount(), 1U);
    EXPECT_EQ(operation.AllocatedCounterCount(), 1U);
    for (std::size_t index = 0U;
         index < operation.SelectedDeviceUuid().size();
         ++index)
    {
        EXPECT_EQ(operation.SelectedDeviceUuid()[index],
            static_cast<std::uint8_t>(properties.uuid.bytes[index]));
    }

    try
    {
        cuda::Ex2CudaCOperation invalid{-1, CConfiguration(1U, 1U)};
        FAIL() << "expected invalid CUDA device selection to fail";
    }
    catch (const cuda::Ex2CudaCNativeError& error)
    {
        EXPECT_EQ(error.Phase(), cuda::Ex2CudaCNativePhase::DeviceSelection);
        EXPECT_NE(error.NativeErrorCode(), 0);
        EXPECT_FALSE(error.NativeErrorName().empty());
        EXPECT_NE(std::string{error.what()}.find("device selection"),
            std::string::npos);
    }
}

TEST(Ex2CudaC,
    NativePhasesResourceLifetimeAndUncertainCompletionAreExplicit)
{
    using Device = cuda::detail::Ex2CudaCResourceDisposition;
    using Host = cuda::detail::Ex2CudaCHostStorageDisposition;
    using State = cuda::detail::Ex2CudaCOperationState;

    EXPECT_EQ(cuda::ToString(cuda::Ex2CudaCNativePhase::TargetUpload),
        "target upload");
    EXPECT_EQ(cuda::ToString(cuda::Ex2CudaCNativePhase::CounterReset),
        "counter reset");
    EXPECT_EQ(cuda::ToString(
        cuda::Ex2CudaCNativePhase::TargetDiagnosticReadback),
        "target diagnostic readback");

    const auto safe = cuda::detail::ClassifyEx2CudaCCompletionDisposition(
        true, true);
    EXPECT_EQ(safe.deviceResources, Device::DestroyNormally);
    EXPECT_EQ(safe.hostStorage, Host::DestroyNormally);
    EXPECT_TRUE(safe.operationReusable);

    const auto uncertainWithHost =
        cuda::detail::ClassifyEx2CudaCCompletionDisposition(false, true);
    EXPECT_EQ(uncertainWithHost.deviceResources,
        Device::PreserveForProcessTeardown);
    EXPECT_EQ(uncertainWithHost.hostStorage,
        Host::PreserveForProcessTeardown);
    EXPECT_FALSE(uncertainWithHost.operationReusable);

    const auto uncertainWithoutHost =
        cuda::detail::ClassifyEx2CudaCCompletionDisposition(false, false);
    EXPECT_EQ(uncertainWithoutHost.deviceResources,
        Device::PreserveForProcessTeardown);
    EXPECT_EQ(uncertainWithoutHost.hostStorage, Host::DestroyNormally);
    EXPECT_FALSE(uncertainWithoutHost.operationReusable);

    EXPECT_EQ(cuda::detail::ClassifyEx2CudaCAsyncFailure(true), State::Failed);
    EXPECT_EQ(cuda::detail::ClassifyEx2CudaCAsyncFailure(false),
        State::CompletionUncertain);
    EXPECT_FALSE(cuda::detail::IsEx2CudaCOperationStateReusable(State::Failed));
    EXPECT_FALSE(cuda::detail::IsEx2CudaCOperationStateReusable(
        State::CompletionUncertain));
    EXPECT_FALSE(cuda::detail::IsEx2CudaCOperationStateReusable(
        State::Submitted));
    EXPECT_TRUE(cuda::detail::IsEx2CudaCOperationStateReusable(
        State::TargetsReady));
    EXPECT_TRUE(cuda::detail::IsEx2CudaCOperationStateReusable(
        State::Complete));
}

} // namespace
