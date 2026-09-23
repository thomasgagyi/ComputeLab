#include "app/Ex2BIntegration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

namespace b = computelab::ex2::b;
namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

constexpr std::array<std::uint32_t, 4> kLiteralInput{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4> kLiteralPermutation{1U, 0U, 3U, 2U};
constexpr std::array<std::uint32_t, 4> kLiteralB1{
    0x5D43B3A3U, 0x3AB8D327U, 0x0F1AB743U, 0xA5DBB31EU};
constexpr std::array<std::uint32_t, 4> kLiteralB2{
    0x5D43B3A0U, 0x3AB8D324U, 0x0F1AB744U, 0xA5DBB319U};

b::CrossBackendObservation RunB(
    ex2::IndexedVariant variant,
    std::uint64_t elementCount,
    ex2::IndexPattern pattern,
    int cudaDeviceOrdinal = 0,
    std::uint32_t vulkanPhysicalDeviceIndex = 0U)
{
    return b::RunCrossBackendCorrectness(
        variant,
        elementCount,
        pattern,
        ex2::CoreInputSeed,
        cudaDeviceOrdinal,
        vulkanPhysicalDeviceIndex,
        COMPUTELAB_EX2_B1_SPIRV_PATH,
        COMPUTELAB_EX2_B2_SPIRV_PATH);
}

void ExpectPassingObservation(
    const b::CrossBackendObservation& observation,
    bool expectedDispatch)
{
    ASSERT_TRUE(observation.Passed());
    EXPECT_TRUE(observation.physicalIdentityVerified);
    EXPECT_FALSE(b::IsZeroDeviceUuid(observation.verifiedDeviceUuid));
    EXPECT_EQ(
        observation.verifiedDeviceUuid,
        observation.vulkanDiagnostics.deviceUuid);
    EXPECT_TRUE(observation.vulkanShaderProvenanceVerified);
    EXPECT_EQ(
        observation.vulkanLoadedSpirvSha256,
        observation.vulkanShaderFileSha256);
    EXPECT_EQ(observation.primaryInputSha256.size(), 64U);
    EXPECT_EQ(observation.permutationSha256.size(), 64U);
    EXPECT_EQ(observation.expectedOutputSha256.size(), 64U);

    for (const auto* backend : {&observation.cuda, &observation.vulkan})
    {
        EXPECT_TRUE(backend->nativeOperationCompleted);
        EXPECT_TRUE(backend->outputObserved);
        EXPECT_TRUE(backend->cpuComparisonPerformed);
        EXPECT_EQ(backend->validationPassed, true);
        EXPECT_EQ(backend->status, b::IntegrationStatus::Ok);
        EXPECT_TRUE(backend->safeForFurtherGpuCalls);
        EXPECT_EQ(backend->nativeDispatchExecuted, expectedDispatch);
        EXPECT_EQ(backend->output, observation.expectedOutput);
        EXPECT_EQ(backend->devicePrimaryInput, observation.primaryInput);
        EXPECT_EQ(backend->devicePermutation, observation.permutation);
    }
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_TRUE(observation.pairOutputsEqual);
    EXPECT_EQ(observation.cuda.output, observation.vulkan.output);
}

TEST(Ex2BIntegrationFixture,
    IndependentFourWordLiteralsAnchorCpuCudaVulkanAndPairEquality)
{
    const std::vector<std::uint32_t> input{
        kLiteralInput.begin(), kLiteralInput.end()};
    const std::vector<std::uint32_t> permutation{
        kLiteralPermutation.begin(), kLiteralPermutation.end()};
    const std::vector<std::uint32_t> expectedB1{
        kLiteralB1.begin(), kLiteralB1.end()};
    const std::vector<std::uint32_t> expectedB2{
        kLiteralB2.begin(), kLiteralB2.end()};

    EXPECT_EQ(ex2::ReferenceB1Gather(input, permutation), expectedB1);
    EXPECT_EQ(ex2::ReferenceB2Scatter(input, permutation), expectedB2);

    const auto gather = RunB(
        ex2::IndexedVariant::B1,
        4U,
        ex2::IndexPattern::StructuredV1);
    ExpectPassingObservation(gather, true);
    EXPECT_EQ(gather.primaryInput, input);
    EXPECT_EQ(gather.permutation, permutation);
    EXPECT_EQ(gather.expectedOutput, expectedB1);
    EXPECT_EQ(gather.cuda.output, expectedB1);
    EXPECT_EQ(gather.vulkan.output, expectedB1);
    EXPECT_EQ(gather.vulkanLoadedShaderName, "Ex2B1.comp.spv");

    const auto scatter = RunB(
        ex2::IndexedVariant::B2,
        4U,
        ex2::IndexPattern::StructuredV1);
    ExpectPassingObservation(scatter, true);
    EXPECT_EQ(scatter.primaryInput, input);
    EXPECT_EQ(scatter.permutation, permutation);
    EXPECT_EQ(scatter.expectedOutput, expectedB2);
    EXPECT_EQ(scatter.cuda.output, expectedB2);
    EXPECT_EQ(scatter.vulkan.output, expectedB2);
    EXPECT_EQ(scatter.vulkanLoadedShaderName, "Ex2B2.comp.spv");
}

TEST(Ex2BIntegration,
    RepresentativeBoundariesRunSequentiallyAcrossVariantsAndPatterns)
{
    struct Case
    {
        ex2::IndexedVariant variant;
        std::uint64_t elementCount;
        ex2::IndexPattern pattern;
    };
    constexpr std::array cases{
        Case{ex2::IndexedVariant::B1, 0U,
            ex2::IndexPattern::StructuredV1},
        Case{ex2::IndexedVariant::B2, 0U,
            ex2::IndexPattern::ShuffledV1},
        Case{ex2::IndexedVariant::B1, 1U,
            ex2::IndexPattern::ShuffledV1},
        Case{ex2::IndexedVariant::B2, 1U,
            ex2::IndexPattern::StructuredV1},
        Case{ex2::IndexedVariant::B1, 255U,
            ex2::IndexPattern::StructuredV1},
        Case{ex2::IndexedVariant::B2, 255U,
            ex2::IndexPattern::ShuffledV1},
        Case{ex2::IndexedVariant::B1, 256U,
            ex2::IndexPattern::ShuffledV1},
        Case{ex2::IndexedVariant::B2, 256U,
            ex2::IndexPattern::StructuredV1},
        Case{ex2::IndexedVariant::B1, 257U,
            ex2::IndexPattern::StructuredV1},
        Case{ex2::IndexedVariant::B2, 257U,
            ex2::IndexPattern::ShuffledV1}};

    for (const auto& testCase : cases)
    {
        SCOPED_TRACE(static_cast<int>(testCase.variant));
        SCOPED_TRACE(testCase.elementCount);
        SCOPED_TRACE(static_cast<int>(testCase.pattern));
        const auto observation = RunB(
            testCase.variant,
            testCase.elementCount,
            testCase.pattern);
        ExpectPassingObservation(
            observation, testCase.elementCount != 0U);
    }
}

TEST(Ex2BIntegrationCore,
    AllSixApprovedCoreConditionsPassWithoutSkips)
{
    struct CoreCase
    {
        ex2::IndexedVariant variant;
        std::uint64_t elementCount;
        ex2::IndexPattern pattern;
    };
    constexpr std::array cases{
        CoreCase{ex2::IndexedVariant::B1, 262'144U,
            ex2::IndexPattern::StructuredV1},
        CoreCase{ex2::IndexedVariant::B1, 262'144U,
            ex2::IndexPattern::ShuffledV1},
        CoreCase{ex2::IndexedVariant::B1, 16'777'216U,
            ex2::IndexPattern::ShuffledV1},
        CoreCase{ex2::IndexedVariant::B2, 262'144U,
            ex2::IndexPattern::StructuredV1},
        CoreCase{ex2::IndexedVariant::B2, 262'144U,
            ex2::IndexPattern::ShuffledV1},
        CoreCase{ex2::IndexedVariant::B2, 16'777'216U,
            ex2::IndexPattern::ShuffledV1}};

    for (const auto& testCase : cases)
    {
        SCOPED_TRACE(static_cast<int>(testCase.variant));
        SCOPED_TRACE(testCase.elementCount);
        SCOPED_TRACE(static_cast<int>(testCase.pattern));
        const auto observation = RunB(
            testCase.variant,
            testCase.elementCount,
            testCase.pattern);
        ExpectPassingObservation(observation, true);
        EXPECT_EQ(
            ex2::ClassifyCellEligibility(observation.configuration),
            ex2::CellEligibility::ApprovedCoreCell);
    }
}

TEST(Ex2BIntegrationIdentity,
    ZeroAndDifferentUuidsAreRejectedWithoutTextOrOrdinalMatching)
{
    const b::DeviceUuid zero{};
    const b::DeviceUuid first{
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU};
    auto different = first;
    different.back() ^= 1U;

    EXPECT_TRUE(b::IsZeroDeviceUuid(zero));
    EXPECT_THROW(static_cast<void>(
        b::VerifySamePhysicalDevice(zero, first)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        b::VerifySamePhysicalDevice(first, zero)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        b::VerifySamePhysicalDevice(first, different)),
        std::invalid_argument);
    EXPECT_EQ(
        b::VerifySamePhysicalDevice(first, first),
        "00112233-4455-6677-8899-aabbccddeeff");
}

TEST(Ex2BIntegrationIdentity,
    InvalidNativeSelectorsAreRejectedWithoutSubstitution)
{
    EXPECT_THROW(
        RunB(ex2::IndexedVariant::B1, 0U,
            ex2::IndexPattern::ShuffledV1, -1, 0U),
        cuda::Ex2CudaBNativeError);
    EXPECT_THROW(
        RunB(ex2::IndexedVariant::B1, 0U,
            ex2::IndexPattern::ShuffledV1, 0,
            std::numeric_limits<std::uint32_t>::max()),
        std::invalid_argument);
}

TEST(Ex2BIntegrationValidation,
    WrongLengthAlteredOutputsAndAlteredLogicalInputsRemainDistinct)
{
    const std::vector<std::uint32_t> input{
        kLiteralInput.begin(), kLiteralInput.end()};
    const std::vector<std::uint32_t> permutation{
        kLiteralPermutation.begin(), kLiteralPermutation.end()};
    const std::vector<std::uint32_t> expected{
        kLiteralB1.begin(), kLiteralB1.end()};

    auto shortOutput = expected;
    shortOutput.pop_back();
    const auto wrongLength = b::CompletedObservation(
        expected, shortOutput, input, input, permutation, permutation, true);
    EXPECT_EQ(wrongLength.status, b::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(wrongLength.errorCode, b::IntegrationErrorCode::OutputMismatch);

    auto alteredOutput = expected;
    alteredOutput[2] ^= 1U;
    const auto wrongOutput = b::CompletedObservation(
        expected, alteredOutput, input, input, permutation, permutation, true);
    EXPECT_EQ(wrongOutput.status, b::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(wrongOutput.errorCode, b::IntegrationErrorCode::OutputMismatch);

    auto alteredInput = input;
    alteredInput[1] ^= 1U;
    const auto wrongInput = b::CompletedObservation(
        expected, expected, input, alteredInput,
        permutation, permutation, true);
    EXPECT_EQ(wrongInput.errorCode,
        b::IntegrationErrorCode::PrimaryInputPreservationFailed);

    auto alteredPermutation = permutation;
    std::swap(alteredPermutation[0], alteredPermutation[1]);
    const auto wrongPermutation = b::CompletedObservation(
        expected, expected, input, input,
        permutation, alteredPermutation, true);
    EXPECT_EQ(wrongPermutation.errorCode,
        b::IntegrationErrorCode::PermutationPreservationFailed);
}

TEST(Ex2BIntegrationReconciliation,
    PairDisagreementDoesNotRewriteIndependentBackendOutcomes)
{
    const std::vector<std::uint32_t> input{
        kLiteralInput.begin(), kLiteralInput.end()};
    const std::vector<std::uint32_t> permutation{
        kLiteralPermutation.begin(), kLiteralPermutation.end()};
    const std::vector<std::uint32_t> expected{
        kLiteralB1.begin(), kLiteralB1.end()};
    auto altered = expected;
    altered[0] ^= 1U;

    b::CrossBackendObservation observation;
    observation.cuda = b::CompletedObservation(
        expected, expected, input, input, permutation, permutation, true);
    observation.vulkan = b::CompletedObservation(
        expected, altered, input, input, permutation, permutation, true);
    b::ReconcileCrossBackendOutputs(observation);

    EXPECT_EQ(observation.cuda.status, b::IntegrationStatus::Ok);
    EXPECT_EQ(observation.cuda.validationPassed, true);
    EXPECT_EQ(
        observation.vulkan.status,
        b::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(observation.vulkan.validationPassed, false);
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_FALSE(observation.pairOutputsEqual);
    EXPECT_FALSE(observation.Passed());

    b::CrossBackendObservation opposite;
    opposite.cuda = b::CompletedObservation(
        expected, altered, input, input, permutation, permutation, true);
    opposite.vulkan = b::CompletedObservation(
        expected, expected, input, input, permutation, permutation, true);
    b::ReconcileCrossBackendOutputs(opposite);

    EXPECT_EQ(
        opposite.cuda.status,
        b::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(opposite.cuda.validationPassed, false);
    EXPECT_EQ(opposite.vulkan.status, b::IntegrationStatus::Ok);
    EXPECT_EQ(opposite.vulkan.validationPassed, true);
    EXPECT_TRUE(opposite.pairComparisonPerformed);
    EXPECT_FALSE(opposite.pairOutputsEqual);
}

TEST(Ex2BIntegrationFailure,
    NativeFailureMappingsAndInterruptedSecondBackendAreBounded)
{
    const auto cudaInputUpload = b::ClassifyCudaFailure(
        cuda::Ex2CudaBNativePhase::InputUpload, 1);
    EXPECT_EQ(cudaInputUpload.phase, b::IntegrationFailurePhase::InputUpload);
    EXPECT_EQ(cudaInputUpload.errorCode,
        b::IntegrationErrorCode::InputUploadFailed);

    const auto cudaIndexUpload = b::ClassifyCudaFailure(
        cuda::Ex2CudaBNativePhase::IndexUpload, 1);
    EXPECT_EQ(cudaIndexUpload.phase, b::IntegrationFailurePhase::IndexUpload);
    EXPECT_EQ(cudaIndexUpload.errorCode,
        b::IntegrationErrorCode::IndexUploadFailed);

    const auto cudaOutputInitialization = b::ClassifyCudaFailure(
        cuda::Ex2CudaBNativePhase::OutputInitialization, 1);
    EXPECT_EQ(cudaOutputInitialization.phase,
        b::IntegrationFailurePhase::OutputInitialization);
    EXPECT_EQ(cudaOutputInitialization.errorCode,
        b::IntegrationErrorCode::OutputInitializationFailed);

    const auto cudaUploadCompletion = b::ClassifyCudaFailure(
        cuda::Ex2CudaBNativePhase::UploadCompletion, 1);
    EXPECT_EQ(cudaUploadCompletion.phase,
        b::IntegrationFailurePhase::UploadCompletion);
    EXPECT_EQ(cudaUploadCompletion.errorCode,
        b::IntegrationErrorCode::UploadCompletionFailed);

    const auto cudaWait = b::ClassifyCudaFailure(
        cuda::Ex2CudaBNativePhase::CompletionWait, 1);
    EXPECT_EQ(cudaWait.status, b::IntegrationStatus::WaitFailed);
    EXPECT_EQ(cudaWait.phase,
        b::IntegrationFailurePhase::CompletionWait);

    const auto vulkanReadback = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::IndexDiagnosticReadback,
        VK_ERROR_UNKNOWN);
    EXPECT_EQ(vulkanReadback.phase,
        b::IntegrationFailurePhase::IndexDiagnosticReadback);
    EXPECT_EQ(vulkanReadback.errorCode,
        b::IntegrationErrorCode::IndexReadbackFailed);

    const auto deviceLost = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::Submission,
        VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(deviceLost.status, b::IntegrationStatus::DeviceLost);
    EXPECT_EQ(deviceLost.phase, b::IntegrationFailurePhase::Submission);

    const auto timeout = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::CompletionWait,
        VK_TIMEOUT);
    EXPECT_EQ(timeout.status, b::IntegrationStatus::Timeout);
    EXPECT_EQ(timeout.errorCode, b::IntegrationErrorCode::OperationTimeout);

    const auto timeoutDuringInputUpload = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::InputUpload,
        VK_TIMEOUT);
    EXPECT_EQ(timeoutDuringInputUpload.status, b::IntegrationStatus::Timeout);
    EXPECT_EQ(timeoutDuringInputUpload.phase,
        b::IntegrationFailurePhase::InputUpload);
    EXPECT_EQ(timeoutDuringInputUpload.errorCode,
        b::IntegrationErrorCode::OperationTimeout);

    const auto timeoutDuringOutputReadback = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::OutputReadback,
        VK_TIMEOUT);
    EXPECT_EQ(timeoutDuringOutputReadback.status, b::IntegrationStatus::Timeout);
    EXPECT_EQ(timeoutDuringOutputReadback.phase,
        b::IntegrationFailurePhase::OutputReadback);
    EXPECT_EQ(timeoutDuringOutputReadback.errorCode,
        b::IntegrationErrorCode::OperationTimeout);

    EXPECT_EQ(timeout.phase, b::IntegrationFailurePhase::CompletionWait);

    const auto deviceLostDuringIndexUpload = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::IndexUpload,
        VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(deviceLostDuringIndexUpload.status,
        b::IntegrationStatus::DeviceLost);
    EXPECT_EQ(deviceLostDuringIndexUpload.phase,
        b::IntegrationFailurePhase::IndexUpload);
    EXPECT_EQ(deviceLostDuringIndexUpload.errorCode,
        b::IntegrationErrorCode::DeviceLost);

    const auto deviceLostDuringPreparation = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::Preparation,
        VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(deviceLostDuringPreparation.status,
        b::IntegrationStatus::DeviceLost);
    EXPECT_EQ(deviceLostDuringPreparation.phase,
        b::IntegrationFailurePhase::Preparation);
    EXPECT_EQ(deviceLostDuringPreparation.errorCode,
        b::IntegrationErrorCode::DeviceLost);

    const auto deviceLostDuringCompletion = b::ClassifyVulkanFailure(
        vulkan::Ex2VulkanBNativePhase::CompletionWait,
        VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(deviceLostDuringCompletion.status,
        b::IntegrationStatus::DeviceLost);
    EXPECT_EQ(deviceLostDuringCompletion.phase,
        b::IntegrationFailurePhase::CompletionWait);
    EXPECT_EQ(deviceLostDuringCompletion.errorCode,
        b::IntegrationErrorCode::DeviceLost);

    const auto unknownCuda = b::ClassifyCudaFailure(
        static_cast<cuda::Ex2CudaBNativePhase>(999), 1);
    EXPECT_EQ(unknownCuda.phase, b::IntegrationFailurePhase::NativeFailure);
    EXPECT_EQ(unknownCuda.errorCode,
        b::IntegrationErrorCode::UnclassifiedNativeFailure);
    EXPECT_NE(unknownCuda.phase,
        b::IntegrationFailurePhase::IndexDiagnosticReadback);

    const auto unknownVulkan = b::ClassifyVulkanFailure(
        static_cast<vulkan::Ex2VulkanBNativePhase>(999),
        VK_ERROR_UNKNOWN);
    EXPECT_EQ(unknownVulkan.phase, b::IntegrationFailurePhase::NativeFailure);
    EXPECT_EQ(unknownVulkan.errorCode,
        b::IntegrationErrorCode::UnclassifiedNativeFailure);
    EXPECT_NE(unknownVulkan.phase,
        b::IntegrationFailurePhase::IndexDiagnosticReadback);

    const auto interruptedUnsafe = b::MakeInterruptedObservation(false);
    EXPECT_EQ(interruptedUnsafe.failurePhase,
        b::IntegrationFailurePhase::InterruptedSecondBackend);
    EXPECT_FALSE(interruptedUnsafe.safeForFurtherGpuCalls);
    EXPECT_EQ(
        b::ClassifyFailedSessionResourceDisposition(interruptedUnsafe),
        b::FailedSessionResourceDisposition::PreserveForProcessTeardown);

    const auto interruptedSafe = b::MakeInterruptedObservation(true);
    EXPECT_TRUE(interruptedSafe.safeForFurtherGpuCalls);
    EXPECT_EQ(
        b::ClassifyFailedSessionResourceDisposition(interruptedSafe),
        b::FailedSessionResourceDisposition::DestroyNormally);
}

TEST(Ex2BIntegrationConfiguration,
    InvalidConfigurationAndValidPermutationForWrongPatternAreRejectedPurely)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 4U);
    const auto structured = ex2::GenerateStructuredPermutation(4U);
    const auto shuffled = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 4U);
    ASSERT_NE(structured, shuffled);

    const auto structuredConfiguration = ex2::MakeConfiguration(
        ex2::IndexedConfiguration{
            ex2::IndexedVariant::B1,
            4U,
            ex2::IndexPattern::StructuredV1});
    EXPECT_NO_THROW(b::ValidateDeclaredInputPair(
        structuredConfiguration, input, structured));
    EXPECT_THROW(b::ValidateDeclaredInputPair(
        structuredConfiguration, input, shuffled), std::invalid_argument);

    auto invalidConfiguration = structuredConfiguration;
    std::get<ex2::IndexedConfiguration>(invalidConfiguration.parameters).variant =
        static_cast<ex2::IndexedVariant>(99);
    EXPECT_THROW(b::ValidateDeclaredInputPair(
        invalidConfiguration, input, structured), std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(b::RunCrossBackendCorrectness(
            static_cast<ex2::IndexedVariant>(99),
            4U,
            ex2::IndexPattern::StructuredV1,
            ex2::CoreInputSeed,
            0,
            0U,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH)),
        std::invalid_argument);
}

} // namespace
