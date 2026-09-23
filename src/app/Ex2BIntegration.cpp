#include "app/Ex2BIntegration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::b
{
namespace
{

bool IsCudaInitializationPhase(cuda::Ex2CudaBNativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaBNativePhase;
    return phase == Phase::DeviceSelection
        || phase == Phase::RuntimeInitialization
        || phase == Phase::DeviceProperties;
}

bool IsCudaAllocationPhase(cuda::Ex2CudaBNativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaBNativePhase;
    return phase == Phase::ResourcePreflight
        || phase == Phase::StreamCreation
        || phase == Phase::InputAllocation
        || phase == Phase::IndexAllocation
        || phase == Phase::OutputAllocation;
}

bool IsVulkanInitializationPhase(
    vulkan::Ex2VulkanBNativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanBNativePhase;
    return phase == Phase::InstanceCreation
        || phase == Phase::DeviceEnumeration
        || phase == Phase::DeviceSelection
        || phase == Phase::DeviceProperties
        || phase == Phase::QueueSelection
        || phase == Phase::LogicalDeviceCreation;
}

bool IsVulkanAllocationPhase(
    vulkan::Ex2VulkanBNativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanBNativePhase;
    return phase == Phase::ResourcePreflight
        || phase == Phase::BufferCreation
        || phase == Phase::MemoryAllocation
        || phase == Phase::MemoryBinding
        || phase == Phase::MemoryMapping
        || phase == Phase::DescriptorCreation
        || phase == Phase::PipelineConstruction
        || phase == Phase::CommandCreation;
}

BackendObservation FailureObservation(
    const FailureClassification& failure,
    bool nativeOperationCompleted,
    bool outputObserved)
{
    BackendObservation result;
    result.nativeOperationCompleted = nativeOperationCompleted;
    result.outputObserved = outputObserved;
    result.status = failure.status;
    result.failurePhase = failure.phase;
    result.errorCode = failure.errorCode;
    result.safeForFurtherGpuCalls = false;
    return result;
}

BackendObservation ExecuteCuda(
    cuda::Ex2CudaBOperation& operation,
    std::span<const std::uint32_t> primaryInput,
    std::span<const std::uint32_t> permutation,
    std::span<const std::uint32_t> expectedOutput)
{
    bool completed = false;
    bool outputObserved = false;
    try
    {
        operation.Upload(primaryInput, permutation);
        operation.Submit();
        operation.WaitForCompletion();
        completed = true;
        auto output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        auto devicePermutation = operation.RetrieveDeviceIndices();
        return CompletedObservation(
            expectedOutput,
            std::move(output),
            primaryInput,
            std::move(deviceInput),
            permutation,
            std::move(devicePermutation),
            operation.LastCompletionExecutedKernel());
    }
    catch (const cuda::Ex2CudaBNativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {IntegrationStatus::Incomplete,
                IntegrationFailurePhase::ResourceAllocation,
                IntegrationErrorCode::ResourceAllocationFailed},
            completed,
            outputObserved);
    }
}

BackendObservation ExecuteVulkan(
    vulkan::Ex2VulkanBOperation& operation,
    std::span<const std::uint32_t> primaryInput,
    std::span<const std::uint32_t> permutation,
    std::span<const std::uint32_t> expectedOutput)
{
    bool completed = false;
    bool outputObserved = false;
    try
    {
        operation.Upload(primaryInput, permutation);
        operation.Prepare();
        operation.Submit();
        operation.WaitForCompletion();
        completed = true;
        auto output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        auto devicePermutation = operation.RetrieveDeviceIndices();
        return CompletedObservation(
            expectedOutput,
            std::move(output),
            primaryInput,
            std::move(deviceInput),
            permutation,
            std::move(devicePermutation),
            operation.LastCompletionExecutedShader());
    }
    catch (const vulkan::Ex2VulkanBNativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {IntegrationStatus::Incomplete,
                IntegrationFailurePhase::ResourceAllocation,
                IntegrationErrorCode::ResourceAllocationFailed},
            completed,
            outputObserved);
    }
}

std::vector<std::uint32_t> GeneratePermutation(
    IndexPattern pattern,
    std::uint64_t seed,
    std::uint64_t elementCount)
{
    return pattern == IndexPattern::StructuredV1
        ? GenerateStructuredPermutation(elementCount)
        : GenerateShuffledPermutation(seed, elementCount);
}

std::vector<std::uint32_t> GenerateExpectedOutput(
    IndexedVariant variant,
    std::span<const std::uint32_t> primaryInput,
    std::span<const std::uint32_t> permutation)
{
    return variant == IndexedVariant::B1
        ? ReferenceB1Gather(primaryInput, permutation)
        : ReferenceB2Scatter(primaryInput, permutation);
}

} // namespace

bool BufferValidation::Passed() const noexcept
{
    return outputLengthMatches && outputMatchesExpected
        && primaryInputLengthMatches && primaryInputPreserved
        && permutationLengthMatches && permutationPreserved;
}

bool CrossBackendObservation::Passed() const noexcept
{
    return physicalIdentityVerified
        && vulkanShaderProvenanceVerified
        && cuda.status == IntegrationStatus::Ok
        && vulkan.status == IntegrationStatus::Ok
        && pairComparisonPerformed
        && pairOutputsEqual;
}

bool IsZeroDeviceUuid(const DeviceUuid& uuid) noexcept
{
    return std::all_of(uuid.begin(), uuid.end(), [](std::uint8_t value) {
        return value == 0U;
    });
}

std::string FormatDeviceUuid(const DeviceUuid& uuid)
{
    if (IsZeroDeviceUuid(uuid))
        throw std::invalid_argument("EX-2 B device UUID must not be all zero");

    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(36U);
    for (std::size_t index = 0U; index < uuid.size(); ++index)
    {
        if (index == 4U || index == 6U || index == 8U || index == 10U)
            output.push_back('-');
        output.push_back(hex[uuid[index] >> 4U]);
        output.push_back(hex[uuid[index] & 0x0FU]);
    }
    return output;
}

std::string VerifySamePhysicalDevice(
    const DeviceUuid& cudaUuid,
    const DeviceUuid& vulkanUuid)
{
    const std::string formatted = FormatDeviceUuid(cudaUuid);
    static_cast<void>(FormatDeviceUuid(vulkanUuid));
    if (cudaUuid != vulkanUuid)
    {
        throw std::invalid_argument(
            "selected CUDA and Vulkan devices do not have the same physical-device UUID");
    }
    return formatted;
}

void ValidateDeclaredInputPair(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> primaryInput,
    std::span<const std::uint32_t> permutation)
{
    if (!std::holds_alternative<IndexedConfiguration>(configuration.parameters)
        || !ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 B integration configuration is not correctness eligible");
    }

    const auto& indexed = std::get<IndexedConfiguration>(
        configuration.parameters);
    if (primaryInput.size() != indexed.elementCount
        || permutation.size() != indexed.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 B logical input lengths do not match the declared element count");
    }

    ValidateIndexPermutation(permutation, indexed.elementCount);
    const auto declared = GeneratePermutation(
        indexed.indexPattern,
        configuration.common.seed,
        indexed.elementCount);
    if (!std::equal(permutation.begin(), permutation.end(), declared.begin()))
    {
        throw std::invalid_argument(
            "EX-2 B permutation does not match the declared pattern and seed");
    }
}

BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedOutput,
    std::span<const std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalPrimaryInput,
    std::span<const std::uint32_t> observedDevicePrimaryInput,
    std::span<const std::uint32_t> originalPermutation,
    std::span<const std::uint32_t> observedDevicePermutation) noexcept
{
    const bool outputLength = observedOutput.size() == expectedOutput.size();
    const bool inputLength =
        observedDevicePrimaryInput.size() == originalPrimaryInput.size();
    const bool permutationLength =
        observedDevicePermutation.size() == originalPermutation.size();
    return {
        outputLength,
        outputLength && std::equal(
            observedOutput.begin(), observedOutput.end(), expectedOutput.begin()),
        inputLength,
        inputLength && std::equal(
            observedDevicePrimaryInput.begin(),
            observedDevicePrimaryInput.end(),
            originalPrimaryInput.begin()),
        permutationLength,
        permutationLength && std::equal(
            observedDevicePermutation.begin(),
            observedDevicePermutation.end(),
            originalPermutation.begin())};
}

BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedOutput,
    std::vector<std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalPrimaryInput,
    std::vector<std::uint32_t> observedDevicePrimaryInput,
    std::span<const std::uint32_t> originalPermutation,
    std::vector<std::uint32_t> observedDevicePermutation,
    bool nativeDispatchExecuted)
{
    const auto validation = ValidateBuffers(
        expectedOutput,
        observedOutput,
        originalPrimaryInput,
        observedDevicePrimaryInput,
        originalPermutation,
        observedDevicePermutation);

    BackendObservation result;
    result.nativeOperationCompleted = true;
    result.outputObserved = true;
    result.cpuComparisonPerformed = true;
    result.validationPassed = validation.Passed();
    result.status = validation.Passed()
        ? IntegrationStatus::Ok
        : IntegrationStatus::ValidationFailed;
    if (!validation.Passed())
    {
        result.failurePhase = IntegrationFailurePhase::ValidationMismatch;
        if (!validation.primaryInputLengthMatches
            || !validation.primaryInputPreserved)
        {
            result.errorCode =
                IntegrationErrorCode::PrimaryInputPreservationFailed;
        }
        else if (!validation.permutationLengthMatches
            || !validation.permutationPreserved)
        {
            result.errorCode =
                IntegrationErrorCode::PermutationPreservationFailed;
        }
        else
        {
            result.errorCode = IntegrationErrorCode::OutputMismatch;
        }
    }
    result.nativeDispatchExecuted = nativeDispatchExecuted;
    result.output = std::move(observedOutput);
    result.devicePrimaryInput = std::move(observedDevicePrimaryInput);
    result.devicePermutation = std::move(observedDevicePermutation);
    return result;
}

BackendObservation MakeInterruptedObservation(
    bool sharedDeviceKnownSafe) noexcept
{
    BackendObservation result;
    result.status = IntegrationStatus::Incomplete;
    result.failurePhase =
        IntegrationFailurePhase::InterruptedSecondBackend;
    result.errorCode = IntegrationErrorCode::Interrupted;
    result.safeForFurtherGpuCalls = sharedDeviceKnownSafe;
    return result;
}

void ReconcileCrossBackendOutputs(CrossBackendObservation& observation)
{
    if (!observation.cuda.cpuComparisonPerformed
        || !observation.vulkan.cpuComparisonPerformed)
    {
        return;
    }
    observation.pairComparisonPerformed = true;
    observation.pairOutputsEqual =
        observation.cuda.output.size() == observation.vulkan.output.size()
        && std::equal(
            observation.cuda.output.begin(),
            observation.cuda.output.end(),
            observation.vulkan.output.begin());
}

FailedSessionResourceDisposition ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept
{
    return failedObservation.safeForFurtherGpuCalls
        ? FailedSessionResourceDisposition::DestroyNormally
        : FailedSessionResourceDisposition::PreserveForProcessTeardown;
}

FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaBNativePhase phase,
    int) noexcept
{
    using Phase = cuda::Ex2CudaBNativePhase;
    if (IsCudaInitializationPhase(phase))
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
    if (IsCudaAllocationPhase(phase))
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
    if (phase == Phase::InputUpload)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InputUpload,
            IntegrationErrorCode::InputUploadFailed};
    if (phase == Phase::IndexUpload)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::IndexUpload,
            IntegrationErrorCode::IndexUploadFailed};
    if (phase == Phase::OutputInitialization)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::OutputInitialization,
            IntegrationErrorCode::OutputInitializationFailed};
    if (phase == Phase::UploadCompletion)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::UploadCompletion,
            IntegrationErrorCode::UploadCompletionFailed};
    if (phase == Phase::Submission)
        return {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::Submission,
            IntegrationErrorCode::SubmissionFailed};
    if (phase == Phase::CompletionWait)
        return {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
    if (phase == Phase::OutputReadback)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::OutputReadback,
            IntegrationErrorCode::OutputReadbackFailed};
    if (phase == Phase::InputDiagnosticReadback)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InputDiagnosticReadback,
            IntegrationErrorCode::InputReadbackFailed};
    if (phase == Phase::IndexDiagnosticReadback)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::IndexDiagnosticReadback,
            IntegrationErrorCode::IndexReadbackFailed};
    return {IntegrationStatus::Incomplete,
        IntegrationFailurePhase::NativeFailure,
        IntegrationErrorCode::UnclassifiedNativeFailure};
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanBNativePhase phase,
    VkResult nativeResult) noexcept
{
    using Phase = vulkan::Ex2VulkanBNativePhase;
    FailureClassification classification;
    if (IsVulkanInitializationPhase(phase))
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
    else if (IsVulkanAllocationPhase(phase))
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
    else if (phase == Phase::InputUpload)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InputUpload,
            IntegrationErrorCode::InputUploadFailed};
    else if (phase == Phase::IndexUpload)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::IndexUpload,
            IntegrationErrorCode::IndexUploadFailed};
    else if (phase == Phase::OutputInitialization)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::OutputInitialization,
            IntegrationErrorCode::OutputInitializationFailed};
    else if (phase == Phase::Preparation)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::Preparation,
            IntegrationErrorCode::PreparationFailed};
    else if (phase == Phase::Submission)
        classification = {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::Submission,
            IntegrationErrorCode::SubmissionFailed};
    else if (phase == Phase::CompletionWait)
        classification = {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
    else if (phase == Phase::OutputReadback)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::OutputReadback,
            IntegrationErrorCode::OutputReadbackFailed};
    else if (phase == Phase::InputDiagnosticReadback)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InputDiagnosticReadback,
            IntegrationErrorCode::InputReadbackFailed};
    else if (phase == Phase::IndexDiagnosticReadback)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::IndexDiagnosticReadback,
            IntegrationErrorCode::IndexReadbackFailed};
    else
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeFailure,
            IntegrationErrorCode::UnclassifiedNativeFailure};

    if (nativeResult == VK_ERROR_DEVICE_LOST)
    {
        classification.status = IntegrationStatus::DeviceLost;
        classification.errorCode = IntegrationErrorCode::DeviceLost;
    }
    else if (nativeResult == VK_TIMEOUT)
    {
        classification.status = IntegrationStatus::Timeout;
        classification.errorCode = IntegrationErrorCode::OperationTimeout;
    }
    return classification;
}

CrossBackendObservation RunCrossBackendCorrectness(
    IndexedVariant variant,
    std::uint64_t elementCount,
    IndexPattern pattern,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& b1SpirvPath,
    const std::filesystem::path& b2SpirvPath)
{
    if (seed != CoreInputSeed)
        throw std::invalid_argument(
            "EX-2 B correctness integration requires the approved core seed");

    const IndexedConfiguration indexed{variant, elementCount, pattern};
    const WorkloadConfiguration configuration = MakeConfiguration(indexed, seed);
    if (!ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 B request is not eligible for correctness execution");
    }

    auto primaryInput = GenerateWordInput(seed, elementCount);
    auto permutation = GeneratePermutation(pattern, seed, elementCount);
    ValidateDeclaredInputPair(configuration, primaryInput, permutation);
    auto expectedOutput = GenerateExpectedOutput(
        variant, primaryInput, permutation);

    auto cudaOperation = std::make_unique<cuda::Ex2CudaBOperation>(
        cudaDeviceOrdinal, indexed, seed);
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanBOperation>(
        indexed,
        seed,
        b1SpirvPath,
        b2SpirvPath,
        vulkanPhysicalDeviceIndex);

    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const std::string uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);

    const std::filesystem::path& selectedSpirvPath =
        variant == IndexedVariant::B1 ? b1SpirvPath : b2SpirvPath;
    const std::string loadedSpirvSha256 = Sha256(
        std::as_bytes(vulkanOperation->LoadedSpirv()));
    const std::string shaderFileSha256 = Sha256File(selectedSpirvPath);
    if (loadedSpirvSha256 != shaderFileSha256)
    {
        throw std::runtime_error(
            "EX-2 Vulkan B loaded SPIR-V does not match the selected artifact");
    }

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.primaryInputSha256 = WordInputSha256(primaryInput);
    result.permutationSha256 = WordInputSha256(permutation);
    result.expectedOutputSha256 = WordInputSha256(expectedOutput);
    result.vulkanLoadedShaderName =
        std::string{vulkanOperation->LoadedShaderName()};
    result.vulkanLoadedSpirvSha256 = loadedSpirvSha256;
    result.vulkanShaderFileSha256 = shaderFileSha256;
    result.vulkanShaderProvenanceVerified = true;
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    result.primaryInput = std::move(primaryInput);
    result.permutation = std::move(permutation);
    result.expectedOutput = std::move(expectedOutput);

    result.cuda = ExecuteCuda(
        *cudaOperation,
        result.primaryInput,
        result.permutation,
        result.expectedOutput);
    if (result.cuda.status != IntegrationStatus::Ok)
    {
        result.vulkan = MakeInterruptedObservation(
            result.cuda.safeForFurtherGpuCalls);
        if (ClassifyFailedSessionResourceDisposition(result.cuda)
            == FailedSessionResourceDisposition::PreserveForProcessTeardown)
        {
            static_cast<void>(vulkanOperation.release());
            static_cast<void>(cudaOperation.release());
        }
        return result;
    }

    result.vulkan = ExecuteVulkan(
        *vulkanOperation,
        result.primaryInput,
        result.permutation,
        result.expectedOutput);
    ReconcileCrossBackendOutputs(result);
    if (!result.vulkan.safeForFurtherGpuCalls)
    {
        static_cast<void>(vulkanOperation.release());
        static_cast<void>(cudaOperation.release());
    }
    return result;
}

} // namespace computelab::ex2::b
