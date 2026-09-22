#include "cuda/Ex2CudaB.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cuda_runtime_api.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;

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

std::vector<std::uint32_t> Execute(
    cuda::Ex2CudaBOperation& operation,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& indices)
{
    operation.Upload(input, indices);
    operation.Submit();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(Ex2CudaB, RequiredBoundedSizesMatchCpuOracleAndPreserveBothInputs)
{
    constexpr std::array<std::uint64_t, 7> sizes{
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
                cuda::Ex2CudaBOperation operation{
                    0,
                    BConfiguration(variant, size, pattern),
                    ex2::CoreInputSeed};

                const auto actual = Execute(operation, input, indices);

                EXPECT_EQ(actual.size(), size);
                EXPECT_EQ(actual, expected);
                EXPECT_EQ(operation.RetrieveDeviceInput(), input);
                EXPECT_EQ(operation.RetrieveDeviceIndices(), indices);
                EXPECT_EQ(operation.LastCompletionExecutedKernel(), size != 0U);
                EXPECT_EQ(operation.Variant(), variant);
                EXPECT_EQ(operation.Pattern(), pattern);
            }
        }
    }
}

TEST(Ex2CudaB, StructuredFourWordLiteralsIndependentlyAnchorBothKernels)
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

    cuda::Ex2CudaBOperation gather{
        0,
        BConfiguration(ex2::IndexedVariant::B1, 4U,
            ex2::IndexPattern::StructuredV1),
        ex2::CoreInputSeed};
    cuda::Ex2CudaBOperation scatter{
        0,
        BConfiguration(ex2::IndexedVariant::B2, 4U,
            ex2::IndexPattern::StructuredV1),
        ex2::CoreInputSeed};

    EXPECT_EQ(Execute(gather, input, indices), expectedB1);
    EXPECT_EQ(Execute(scatter, input, indices), expectedB2);
    EXPECT_TRUE(gather.LastCompletionExecutedKernel());
    EXPECT_TRUE(scatter.LastCompletionExecutedKernel());
}

TEST(Ex2CudaB, ZeroAndOneElementPermutationSemanticsAreExplicit)
{
    const std::vector<std::uint32_t> empty;
    const std::vector<std::uint32_t> singleton{0U};
    EXPECT_EQ(ex2::GenerateStructuredPermutation(1U), singleton);
    EXPECT_EQ(
        ex2::GenerateShuffledPermutation(ex2::CoreInputSeed, 1U),
        singleton);

    cuda::Ex2CudaBOperation zero{
        0,
        BConfiguration(ex2::IndexedVariant::B2, 0U,
            ex2::IndexPattern::ShuffledV1),
        ex2::CoreInputSeed};
    EXPECT_TRUE(Execute(zero, empty, empty).empty());
    EXPECT_TRUE(zero.RetrieveDeviceInput().empty());
    EXPECT_TRUE(zero.RetrieveDeviceIndices().empty());
    EXPECT_FALSE(zero.LastCompletionExecutedKernel());
}

TEST(Ex2CudaB, RepeatedUploadsAreDeterministicAndCannotExposeStaleOutput)
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
        cuda::Ex2CudaBOperation operation{
            0,
            BConfiguration(variant, 257U, ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed};

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

TEST(Ex2CudaBHostLifetime,
    SuccessfulUploadOwnsSourcesUntilCompletionAndReadbackTransfersOwnership)
{
    const auto expectedInput = ex2::GenerateWordInput(
        ex2::CoreInputSeed, 257U);
    const auto expectedIndices = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    const auto expectedOutput = ex2::ReferenceB1Gather(
        expectedInput, expectedIndices);
    std::vector<std::uint32_t> output;

    {
        cuda::Ex2CudaBOperation operation{
            0,
            BConfiguration(ex2::IndexedVariant::B1, 257U,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed};
        {
            auto callerInput = expectedInput;
            auto callerIndices = expectedIndices;
            operation.Upload(callerInput, callerIndices);
        }

        operation.Submit();
        operation.WaitForCompletion();
        output = operation.RetrieveOutput();
    }

    EXPECT_EQ(output, expectedOutput);
}

TEST(Ex2CudaBHostLifetime,
    CompletionDispositionCoversSafeAndUncertainTransferPaths)
{
    using Host = cuda::detail::Ex2CudaBHostStorageDisposition;
    using Device = cuda::detail::Ex2CudaBResourceDisposition;

    const auto successfulUpload =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(true, true);
    EXPECT_EQ(successfulUpload.hostStorage, Host::DestroyNormally);
    EXPECT_EQ(successfulUpload.deviceResources, Device::DestroyNormally);
    EXPECT_TRUE(successfulUpload.operationReusable);

    const auto knownSafeReadback =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(true, true);
    EXPECT_EQ(knownSafeReadback.hostStorage, Host::DestroyNormally);
    EXPECT_EQ(knownSafeReadback.deviceResources, Device::DestroyNormally);
    EXPECT_TRUE(knownSafeReadback.operationReusable);

    const auto failureBeforeEnqueue =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(true, false);
    EXPECT_EQ(failureBeforeEnqueue.hostStorage, Host::DestroyNormally);
    EXPECT_EQ(failureBeforeEnqueue.deviceResources, Device::DestroyNormally);
    EXPECT_TRUE(failureBeforeEnqueue.operationReusable);

    const auto failureAfterEarlierUploadCopy =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(false, true);
    EXPECT_EQ(failureAfterEarlierUploadCopy.hostStorage,
        Host::PreserveForProcessTeardown);
    EXPECT_EQ(failureAfterEarlierUploadCopy.deviceResources,
        Device::PreserveForProcessTeardown);
    EXPECT_FALSE(failureAfterEarlierUploadCopy.operationReusable);

    const auto failedUploadSynchronization =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(false, true);
    EXPECT_EQ(failedUploadSynchronization.hostStorage,
        Host::PreserveForProcessTeardown);
    EXPECT_FALSE(failedUploadSynchronization.operationReusable);

    const auto failedReadbackSynchronization =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(false, true);
    EXPECT_EQ(failedReadbackSynchronization.hostStorage,
        Host::PreserveForProcessTeardown);
    EXPECT_FALSE(failedReadbackSynchronization.operationReusable);

    const auto uncertainWithoutHostReference =
        cuda::detail::ClassifyEx2CudaBCompletionDisposition(false, false);
    EXPECT_EQ(uncertainWithoutHostReference.hostStorage,
        Host::DestroyNormally);
    EXPECT_EQ(uncertainWithoutHostReference.deviceResources,
        Device::PreserveForProcessTeardown);
    EXPECT_FALSE(uncertainWithoutHostReference.operationReusable);
}

TEST(Ex2CudaB, IndexOnlyChangeUsesDistinctValidImmutableConfigurations)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto structured = ex2::GenerateStructuredPermutation(257U);
    const auto shuffled = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    ASSERT_NE(structured, shuffled);

    for (const auto variant :
        {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
    {
        SCOPED_TRACE(static_cast<int>(variant));
        cuda::Ex2CudaBOperation structuredOperation{
            0,
            BConfiguration(variant, 257U, ex2::IndexPattern::StructuredV1),
            ex2::CoreInputSeed};
        cuda::Ex2CudaBOperation shuffledOperation{
            0,
            BConfiguration(variant, 257U, ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed};

        const auto structuredOutput =
            Execute(structuredOperation, input, structured);
        const auto shuffledOutput =
            Execute(shuffledOperation, input, shuffled);
        EXPECT_EQ(structuredOutput, Reference(variant, input, structured));
        EXPECT_EQ(shuffledOutput, Reference(variant, input, shuffled));
        EXPECT_NE(structuredOutput, shuffledOutput);
    }
}

TEST(Ex2CudaB, InputAndPermutationCanChangeTogetherOnlyByDeclaredOperations)
{
    constexpr std::uint64_t alternateSeed = 0xFEDCBA9876543210ULL;
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(alternateSeed, 257U);
    const auto firstIndices = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    const auto secondIndices = ex2::GenerateShuffledPermutation(
        alternateSeed, 257U);
    ASSERT_NE(firstInput, secondInput);
    ASSERT_NE(firstIndices, secondIndices);

    for (const auto variant :
        {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
    {
        SCOPED_TRACE(static_cast<int>(variant));
        cuda::Ex2CudaBOperation firstOperation{
            0,
            BConfiguration(variant, 257U, ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed};
        cuda::Ex2CudaBOperation secondOperation{
            0,
            BConfiguration(variant, 257U, ex2::IndexPattern::ShuffledV1),
            alternateSeed};

        const auto first = Execute(firstOperation, firstInput, firstIndices);
        const auto second = Execute(secondOperation, secondInput, secondIndices);
        EXPECT_EQ(first, Reference(variant, firstInput, firstIndices));
        EXPECT_EQ(second, Reference(variant, secondInput, secondIndices));
        EXPECT_NE(first, second);
    }
}

TEST(Ex2CudaBValidation, RejectsMalformedAndWronglyDeclaredUploadsBeforeSubmit)
{
    const std::vector<std::uint32_t> input{1U, 2U, 3U, 4U};
    const std::vector<std::uint32_t> structured{1U, 0U, 3U, 2U};
    const std::vector<std::uint32_t> shuffled{3U, 0U, 2U, 1U};
    const std::vector<std::uint32_t> duplicate{0U, 0U, 2U, 3U};
    const std::vector<std::uint32_t> outOfRange{0U, 1U, 2U, 4U};
    const std::vector<std::uint32_t> shortIndices{0U, 1U, 2U};
    const std::vector<std::uint32_t> shortInput{1U, 2U, 3U};
    cuda::Ex2CudaBOperation operation{
        0,
        BConfiguration(ex2::IndexedVariant::B1, 4U,
            ex2::IndexPattern::StructuredV1),
        ex2::CoreInputSeed};

    EXPECT_THROW(operation.Upload(input, duplicate), std::invalid_argument);
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, outOfRange), std::invalid_argument);
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, shortIndices), std::invalid_argument);
    EXPECT_THROW(operation.Upload(shortInput, structured), std::invalid_argument);
    EXPECT_THROW(operation.Upload(input, shuffled), std::invalid_argument);
    EXPECT_THROW(operation.Submit(), std::logic_error);

    EXPECT_EQ(
        Execute(operation, input, structured),
        ex2::ReferenceB1Gather(input, structured));
}

TEST(Ex2CudaBValidation, RejectsInvalidSemanticConfigurationBeforeCudaWork)
{
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaBOperation{
            0,
            BConfiguration(static_cast<ex2::IndexedVariant>(999), 1U,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaBOperation{
            0,
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                static_cast<ex2::IndexPattern>(999)),
            ex2::CoreInputSeed}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaBOperation{
            0,
            BConfiguration(ex2::IndexedVariant::B1, 8191U,
                ex2::IndexPattern::StructuredV1),
            ex2::CoreInputSeed}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaBOperation{
            0,
            BConfiguration(ex2::IndexedVariant::B1,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed}),
        std::invalid_argument);

    auto wrongRevision = ex2::MakeConfiguration(BConfiguration(
        ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::ShuffledV1));
    wrongRevision.common.generatorRevision = "wrong-revision";
    EXPECT_THROW(
        cuda::detail::ValidateEx2CudaBConfiguration(wrongRevision),
        std::invalid_argument);

    auto wrongBoundary = ex2::MakeConfiguration(BConfiguration(
        ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::ShuffledV1));
    wrongBoundary.common.operationBoundary =
        ex2::OperationBoundary::AtomicDispatchCompletion;
    EXPECT_THROW(
        cuda::detail::ValidateEx2CudaBConfiguration(wrongBoundary),
        std::invalid_argument);

    EXPECT_THROW(
        cuda::detail::ValidateEx2CudaBConfiguration(ex2::MakeConfiguration(
            ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U})),
        std::invalid_argument);
}

TEST(Ex2CudaBGuards, CalculatesLaunchesAndRejectsDeviceLimitViolations)
{
    const auto zero = cuda::detail::ValidateEx2CudaBLaunchShape(
        BConfiguration(ex2::IndexedVariant::B1, 0U,
            ex2::IndexPattern::ShuffledV1),
        1U, 1U, 1U);
    const auto exact = cuda::detail::ValidateEx2CudaBLaunchShape(
        BConfiguration(ex2::IndexedVariant::B1, 256U,
            ex2::IndexPattern::ShuffledV1),
        1U, 256U, 256U);
    const auto padded = cuda::detail::ValidateEx2CudaBLaunchShape(
        BConfiguration(ex2::IndexedVariant::B2, 257U,
            ex2::IndexPattern::ShuffledV1),
        2U, 256U, 256U);
    EXPECT_EQ(zero.blockCount, 0U);
    EXPECT_EQ(exact.blockCount, 1U);
    EXPECT_EQ(padded.blockCount, 2U);
    EXPECT_EQ(padded.threadsPerBlock, 256U);

    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaBLaunchShape(
            BConfiguration(ex2::IndexedVariant::B1, 257U,
                ex2::IndexPattern::ShuffledV1),
            1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaBLaunchShape(
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                ex2::IndexPattern::ShuffledV1),
            1U, 255U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaBLaunchShape(
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                ex2::IndexPattern::ShuffledV1),
            1U, 256U, 255U)),
        std::invalid_argument);
}

TEST(Ex2CudaBGuards, RejectsByteOverflowAndThreeBufferInfeasibilityPurely)
{
    EXPECT_EQ(
        cuda::detail::CalculateEx2CudaBBufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::CalculateEx2CudaBBufferByteCount(
            257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::CalculateEx2CudaBBufferByteCount(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U,
            std::numeric_limits<std::size_t>::max())),
        std::invalid_argument);

    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaBMemoryFeasibility(
        0U, 0U, 0U));
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaBMemoryFeasibility(
        1'024U, 3'072U, 3'072U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaBMemoryFeasibility(
        1'025U, 3'074U, 4'096U), std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaBMemoryFeasibility(
        1'025U, 4'096U, 3'074U), std::length_error);
}

TEST(Ex2CudaB, InvalidLifecycleTransitionsFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    const std::vector<std::uint32_t> indices{0U};
    cuda::Ex2CudaBOperation operation{
        0,
        BConfiguration(ex2::IndexedVariant::B2, 1U,
            ex2::IndexPattern::StructuredV1),
        ex2::CoreInputSeed};

    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceInput()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceIndices()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedKernel()),
        std::logic_error);

    operation.Upload(input, indices);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.Submit();
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.Upload(input, indices), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.Submit(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
}

TEST(Ex2CudaB, DeviceIdentityAndNativeSelectionFailureRemainExplicit)
{
    cudaDeviceProp properties{};
    ASSERT_EQ(cudaGetDeviceProperties(&properties, 0), cudaSuccess);
    cuda::Ex2CudaBOperation operation{
        0,
        BConfiguration(ex2::IndexedVariant::B1, 1U,
            ex2::IndexPattern::StructuredV1),
        ex2::CoreInputSeed};
    EXPECT_EQ(operation.SelectedDeviceOrdinal(), 0);
    EXPECT_EQ(operation.ElementCount(), 1U);
    for (std::size_t index = 0U;
         index < operation.SelectedDeviceUuid().size();
         ++index)
    {
        EXPECT_EQ(operation.SelectedDeviceUuid()[index],
            static_cast<std::uint8_t>(properties.uuid.bytes[index]));
    }

    try
    {
        cuda::Ex2CudaBOperation invalid{
            -1,
            BConfiguration(ex2::IndexedVariant::B1, 1U,
                ex2::IndexPattern::StructuredV1),
            ex2::CoreInputSeed};
        FAIL() << "expected invalid CUDA device selection to fail";
    }
    catch (const cuda::Ex2CudaBNativeError& error)
    {
        EXPECT_EQ(error.Phase(), cuda::Ex2CudaBNativePhase::DeviceSelection);
        EXPECT_NE(error.NativeErrorCode(), 0);
        EXPECT_FALSE(error.NativeErrorName().empty());
        EXPECT_NE(std::string{error.what()}.find("device selection"),
            std::string::npos);
    }
}

TEST(Ex2CudaB, NativePhasesAndUncertainResourceDispositionAreExplicit)
{
    EXPECT_EQ(cuda::ToString(cuda::Ex2CudaBNativePhase::IndexAllocation),
        "index allocation");
    EXPECT_EQ(cuda::ToString(cuda::Ex2CudaBNativePhase::IndexUpload),
        "index upload");
    EXPECT_EQ(cuda::ToString(
        cuda::Ex2CudaBNativePhase::OutputInitialization),
        "output initialization");
    EXPECT_EQ(cuda::ToString(
        cuda::Ex2CudaBNativePhase::IndexDiagnosticReadback),
        "index diagnostic readback");
    EXPECT_EQ(
        cuda::detail::ClassifyEx2CudaBResourceDisposition(true),
        cuda::detail::Ex2CudaBResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        cuda::detail::ClassifyEx2CudaBResourceDisposition(false),
        cuda::detail::Ex2CudaBResourceDisposition::PreserveForProcessTeardown);
}

TEST(Ex2CudaBLarge, ApprovedShuffledCoreSizeRunsBothVariantsSerially)
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
        cuda::Ex2CudaBOperation operation{
            0,
            BConfiguration(variant, elementCount,
                ex2::IndexPattern::ShuffledV1),
            ex2::CoreInputSeed};

        const auto actual = Execute(operation, input, indices);
        EXPECT_EQ(actual.size(), elementCount);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(operation.RetrieveDeviceInput(), input);
        EXPECT_EQ(operation.RetrieveDeviceIndices(), indices);
        EXPECT_TRUE(operation.LastCompletionExecutedKernel());
    }
}

} // namespace
