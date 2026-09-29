#include "app/Ex2D1Integration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::d1
{
namespace
{

IntegrationErrorCode ValidationError(
    const CompletedValidation& validation) noexcept
{
    if (!validation.finalStateLengthMatches
        || !validation.finalStateMatchesExpected)
    {
        return IntegrationErrorCode::FinalStateMismatch;
    }
    if (!validation.operationExpectedBufferMatches
        || !validation.completedBufferMatches)
    {
        return IntegrationErrorCode::FinalBufferMismatch;
    }
    if (!validation.dispatchCountMatches)
        return IntegrationErrorCode::DispatchCountMismatch;
    if (!validation.interPassBarrierCountMatches)
        return IntegrationErrorCode::InterPassBarrierCountMismatch;
    if (!validation.nativeSequenceMatches)
        return IntegrationErrorCode::NativeSequenceMismatch;
    return IntegrationErrorCode::InstrumentationMismatch;
}

BackendObservation FailureObservation(
    const FailureClassification& failure,
    bool nativeSequenceCompleted,
    bool finalStateObserved,
    IterativeFinalBuffer expectedFinalBuffer,
    std::uint64_t expectedIterationCount,
    bool hModeUninstrumented)
{
    BackendObservation result;
    result.nativeSequenceCompleted = nativeSequenceCompleted;
    result.finalStateObserved = finalStateObserved;
    result.status = failure.status;
    result.failurePhase = failure.phase;
    result.errorCode = failure.errorCode;
    result.safeForFurtherGpuCalls = false;
    result.hModeUninstrumented = hModeUninstrumented;
    result.expectedFinalBuffer = expectedFinalBuffer;
    result.expectedIterationCount = expectedIterationCount;
    result.attemptedPass = failure.attemptedPass;
    result.acceptedDispatchCount = failure.acceptedDispatchCount;
    return result;
}

BackendObservation AllocationFailureObservation(
    bool nativeSequenceCompleted,
    bool finalStateObserved,
    IterativeFinalBuffer expectedFinalBuffer,
    std::uint64_t expectedIterationCount,
    bool hModeUninstrumented)
{
    return FailureObservation(
        {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed},
        nativeSequenceCompleted,
        finalStateObserved,
        expectedFinalBuffer,
        expectedIterationCount,
        hModeUninstrumented);
}

BackendObservation ExecuteCuda(
    cuda::Ex2CudaD1Operation& operation,
    std::span<const std::uint32_t> initialState,
    std::span<const std::uint32_t> expectedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    std::uint64_t iterationCount)
{
    static_assert(cuda::Ex2CudaD1Operation::InstrumentMode == "H");
    static_assert(!cuda::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
    bool completed = false;
    bool observed = false;
    try
    {
        operation.UploadInitialState(initialState);
        operation.SubmitSequence();
        operation.WaitForCompletion();
        completed = true;
        auto finalState = operation.RetrieveFinalState();
        observed = true;
        return CompletedObservation(
            expectedFinalState,
            std::move(finalState),
            expectedFinalBuffer,
            operation.ExpectedFinalBuffer(),
            operation.CompletedFinalBuffer(),
            iterationCount,
            operation.LastCompletionNativeDispatchCount(),
            std::nullopt,
            std::nullopt,
            operation.LastCompletionExecutedSequence(),
            !cuda::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
    }
    catch (const cuda::Ex2CudaD1NativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(
                error.Phase(),
                error.NativeErrorCode(),
                error.AttemptedPass(),
                error.AcceptedDispatchCount()),
            completed,
            observed,
            expectedFinalBuffer,
            iterationCount,
            !cuda::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
    }
    catch (const std::bad_alloc&)
    {
        return AllocationFailureObservation(
            completed,
            observed,
            expectedFinalBuffer,
            iterationCount,
            !cuda::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
    }
}

BackendObservation ExecuteVulkan(
    vulkan::Ex2VulkanD1Operation& operation,
    std::span<const std::uint32_t> initialState,
    std::span<const std::uint32_t> expectedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    std::uint64_t iterationCount)
{
    static_assert(vulkan::Ex2VulkanD1Operation::InstrumentMode == "H");
    static_assert(!vulkan::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps);
    bool completed = false;
    bool observed = false;
    try
    {
        operation.UploadInitialState(initialState);
        operation.SubmitSequence();
        operation.WaitForCompletion();
        completed = true;
        auto finalState = operation.RetrieveFinalState();
        observed = true;
        const std::uint32_t expectedBarrierCount =
            initialState.empty() || iterationCount == 0U
                ? 0U
                : static_cast<std::uint32_t>(iterationCount - 1U);
        return CompletedObservation(
            expectedFinalState,
            std::move(finalState),
            expectedFinalBuffer,
            operation.ExpectedFinalBuffer(),
            operation.CompletedFinalBuffer(),
            iterationCount,
            operation.LastCompletionNativeDispatchCount(),
            expectedBarrierCount,
            operation.LastCompletionInterPassBarrierCount(),
            operation.LastCompletionExecutedSequence(),
            !vulkan::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps
                && !operation.Diagnostics().timestampQueryPoolCreated);
    }
    catch (const vulkan::Ex2VulkanD1NativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            completed,
            observed,
            expectedFinalBuffer,
            iterationCount,
            !vulkan::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps
                && !operation.Diagnostics().timestampQueryPoolCreated);
    }
    catch (const std::bad_alloc&)
    {
        return AllocationFailureObservation(
            completed,
            observed,
            expectedFinalBuffer,
            iterationCount,
            !vulkan::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps
                && !operation.Diagnostics().timestampQueryPoolCreated);
    }
}

bool BackendPassed(
    const BackendObservation& observation,
    std::span<const std::uint32_t> expectedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    bool expectedExecution,
    std::uint64_t expectedIterationCount,
    std::optional<std::uint32_t> expectedBarrierCount) noexcept
{
    return observation.nativeSequenceCompleted
        && observation.finalStateObserved
        && observation.cpuComparisonPerformed
        && observation.validationPassed == true
        && observation.status == IntegrationStatus::Ok
        && observation.safeForFurtherGpuCalls
        && observation.nativeSequenceExecuted == expectedExecution
        && observation.hModeUninstrumented
        && observation.expectedFinalBuffer == expectedFinalBuffer
        && observation.completedFinalBuffer.has_value()
        && *observation.completedFinalBuffer == expectedFinalBuffer
        && observation.expectedIterationCount == expectedIterationCount
        && observation.completedNativeDispatchCount.has_value()
        && *observation.completedNativeDispatchCount
            == (expectedExecution
                ? static_cast<std::uint32_t>(expectedIterationCount)
                : 0U)
        && (!expectedBarrierCount.has_value()
            || (observation.completedInterPassBarrierCount.has_value()
                && *observation.completedInterPassBarrierCount
                    == *expectedBarrierCount))
        && observation.finalState.size() == expectedFinalState.size()
        && std::equal(
            observation.finalState.begin(),
            observation.finalState.end(),
            expectedFinalState.begin());
}

} // namespace

bool CompletedValidation::Passed() const noexcept
{
    return finalStateLengthMatches && finalStateMatchesExpected
        && operationExpectedBufferMatches && completedBufferMatches
        && dispatchCountMatches && interPassBarrierCountMatches
        && nativeSequenceMatches && hModeUninstrumented;
}

bool CrossBackendObservation::Passed() const noexcept
{
    const auto* iterative = std::get_if<IterativeConfiguration>(
        &configuration.parameters);
    if (iterative == nullptr || iterative->variant != IterativeVariant::D1)
        return false;
    const bool expectedExecution = iterative->elementCount != 0U
        && iterative->iterationCount != 0U;
    const std::uint32_t expectedBarrierCount = expectedExecution
        ? static_cast<std::uint32_t>(iterative->iterationCount - 1U)
        : 0U;
    const IterativeFinalBuffer parity = (iterative->iterationCount & 1U) == 0U
        ? IterativeFinalBuffer::StateA
        : IterativeFinalBuffer::StateB;
    return physicalIdentityVerified
        && !IsZeroDeviceUuid(verifiedDeviceUuid)
        && verifiedDeviceUuid == vulkanDiagnostics.deviceUuid
        && initialState.size() == iterative->elementCount
        && expectedFinalState.size() == iterative->elementCount
        && expectedFinalBuffer == parity
        && vulkanShaderProvenanceVerified
        && vulkanLoadedShaderName == "Ex2D1.comp.spv"
        && !vulkanLoadedSpirvSha256.empty()
        && vulkanLoadedSpirvSha256 == vulkanShaderFileSha256
        && BackendPassed(
            cuda,
            expectedFinalState,
            expectedFinalBuffer,
            expectedExecution,
            iterative->iterationCount,
            std::nullopt)
        && !cuda.completedInterPassBarrierCount.has_value()
        && BackendPassed(
            vulkan,
            expectedFinalState,
            expectedFinalBuffer,
            expectedExecution,
            iterative->iterationCount,
            expectedBarrierCount)
        && pairComparisonPerformed
        && pairFinalStatesEqual;
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
        throw std::invalid_argument("EX-2 D1 device UUID must not be all zero");

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
            "selected CUDA and Vulkan D1 devices do not have the same physical-device UUID");
    }
    return formatted;
}

void ValidateDeclaredInitialState(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> initialState)
{
    const auto* iterative = std::get_if<IterativeConfiguration>(
        &configuration.parameters);
    if (iterative == nullptr
        || iterative->variant != IterativeVariant::D1
        || configuration.common.seed != CoreInputSeed
        || configuration.common.generatorRevision != InputGeneratorRevision
        || configuration.common.executionMode != LogicalExecutionMode::Ordinary
        || configuration.common.operationBoundary
            != OperationBoundary::OrdinaryIterationSequenceCompletion
        || !ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 D1 integration configuration is not correctness eligible");
    }
    if (initialState.size() != iterative->elementCount)
    {
        throw std::invalid_argument(
            "EX-2 D1 declared initial-state length does not match element count");
    }
}

CompletedValidation ValidateCompletedResult(
    std::span<const std::uint32_t> expectedFinalState,
    std::span<const std::uint32_t> observedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    IterativeFinalBuffer operationExpectedFinalBuffer,
    IterativeFinalBuffer completedFinalBuffer,
    std::uint64_t expectedIterationCount,
    std::uint32_t completedNativeDispatchCount,
    std::optional<std::uint32_t> expectedInterPassBarrierCount,
    std::optional<std::uint32_t> completedInterPassBarrierCount,
    bool nativeSequenceExecuted,
    bool hModeUninstrumented) noexcept
{
    const bool lengthMatches =
        observedFinalState.size() == expectedFinalState.size();
    const bool expectedExecution = !expectedFinalState.empty()
        && expectedIterationCount != 0U;
    const std::uint32_t expectedDispatchCount = expectedExecution
        ? static_cast<std::uint32_t>(expectedIterationCount)
        : 0U;
    const bool barrierCountMatches =
        !expectedInterPassBarrierCount.has_value()
            ? !completedInterPassBarrierCount.has_value()
            : completedInterPassBarrierCount.has_value()
                && *completedInterPassBarrierCount
                    == *expectedInterPassBarrierCount;
    return {
        lengthMatches,
        lengthMatches && std::equal(
            observedFinalState.begin(),
            observedFinalState.end(),
            expectedFinalState.begin()),
        operationExpectedFinalBuffer == expectedFinalBuffer,
        completedFinalBuffer == expectedFinalBuffer,
        completedNativeDispatchCount == expectedDispatchCount,
        barrierCountMatches,
        nativeSequenceExecuted == expectedExecution,
        hModeUninstrumented};
}

BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedFinalState,
    std::vector<std::uint32_t> observedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    IterativeFinalBuffer operationExpectedFinalBuffer,
    IterativeFinalBuffer completedFinalBuffer,
    std::uint64_t expectedIterationCount,
    std::uint32_t completedNativeDispatchCount,
    std::optional<std::uint32_t> expectedInterPassBarrierCount,
    std::optional<std::uint32_t> completedInterPassBarrierCount,
    bool nativeSequenceExecuted,
    bool hModeUninstrumented)
{
    const auto validation = ValidateCompletedResult(
        expectedFinalState,
        observedFinalState,
        expectedFinalBuffer,
        operationExpectedFinalBuffer,
        completedFinalBuffer,
        expectedIterationCount,
        completedNativeDispatchCount,
        expectedInterPassBarrierCount,
        completedInterPassBarrierCount,
        nativeSequenceExecuted,
        hModeUninstrumented);

    BackendObservation result;
    result.nativeSequenceCompleted = true;
    result.finalStateObserved = true;
    result.cpuComparisonPerformed = true;
    result.validationPassed = validation.Passed();
    result.status = validation.Passed()
        ? IntegrationStatus::Ok
        : IntegrationStatus::ValidationFailed;
    if (!validation.Passed())
    {
        result.failurePhase = IntegrationFailurePhase::ValidationMismatch;
        result.errorCode = ValidationError(validation);
    }
    result.nativeSequenceExecuted = nativeSequenceExecuted;
    result.hModeUninstrumented = hModeUninstrumented;
    result.expectedFinalBuffer = expectedFinalBuffer;
    result.completedFinalBuffer = completedFinalBuffer;
    result.expectedIterationCount = expectedIterationCount;
    result.completedNativeDispatchCount = completedNativeDispatchCount;
    result.completedInterPassBarrierCount = completedInterPassBarrierCount;
    result.finalState = std::move(observedFinalState);
    return result;
}

BackendObservation MakeInterruptedObservation(
    bool sharedDeviceKnownSafe) noexcept
{
    BackendObservation result;
    result.status = IntegrationStatus::Incomplete;
    result.failurePhase = IntegrationFailurePhase::InterruptedSecondBackend;
    result.errorCode = IntegrationErrorCode::Interrupted;
    result.safeForFurtherGpuCalls = sharedDeviceKnownSafe;
    return result;
}

void ReconcileCrossBackendFinalStates(CrossBackendObservation& observation)
{
    if (!observation.cuda.cpuComparisonPerformed
        || !observation.vulkan.cpuComparisonPerformed)
    {
        return;
    }
    observation.pairComparisonPerformed = true;
    observation.pairFinalStatesEqual =
        observation.cuda.finalState.size() == observation.vulkan.finalState.size()
        && std::equal(
            observation.cuda.finalState.begin(),
            observation.cuda.finalState.end(),
            observation.vulkan.finalState.begin());
}

FailedSessionResourceDisposition ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept
{
    return failedObservation.safeForFurtherGpuCalls
        ? FailedSessionResourceDisposition::DestroyNormally
        : FailedSessionResourceDisposition::PreserveForProcessTeardown;
}

FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaD1NativePhase phase,
    int,
    std::optional<std::uint32_t> attemptedPass,
    std::uint32_t acceptedDispatchCount) noexcept
{
    using Phase = cuda::Ex2CudaD1NativePhase;
    FailureClassification classification;
    switch (phase)
    {
    case Phase::DeviceSelection:
    case Phase::RuntimeInitialization:
    case Phase::DeviceProperties:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
        break;
    case Phase::ResourcePreflight:
    case Phase::StreamCreation:
    case Phase::StateAAllocation:
    case Phase::StateBAllocation:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
        break;
    case Phase::InitialUpload:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InitialUpload,
            IntegrationErrorCode::InitialUploadFailed};
        break;
    case Phase::UploadCompletion:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::UploadCompletion,
            IntegrationErrorCode::UploadCompletionFailed};
        break;
    case Phase::StartMarker:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::StartMarker,
            IntegrationErrorCode::StartMarkerFailed};
        break;
    case Phase::SequenceSubmission:
        classification = {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::SequenceSubmission,
            IntegrationErrorCode::SubmissionFailed};
        break;
    case Phase::StopMarker:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::StopMarker,
            IntegrationErrorCode::StopMarkerFailed};
        break;
    case Phase::CompletionWait:
        classification = {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
        break;
    case Phase::FinalReadback:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::FinalReadback,
            IntegrationErrorCode::FinalReadbackFailed};
        break;
    case Phase::NativeTimingRetrieval:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeTimingRetrieval,
            IntegrationErrorCode::NativeTimingRetrievalFailed};
        break;
    case Phase::NativeTimingConversion:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeTimingConversion,
            IntegrationErrorCode::NativeTimingConversionFailed};
        break;
    default:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeFailure,
            IntegrationErrorCode::UnclassifiedNativeFailure};
        break;
    }
    classification.attemptedPass = attemptedPass;
    if (attemptedPass.has_value() || acceptedDispatchCount != 0U
        || phase == Phase::SequenceSubmission)
    {
        classification.acceptedDispatchCount = acceptedDispatchCount;
    }
    return classification;
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanD1NativePhase phase,
    VkResult nativeResult) noexcept
{
    using Phase = vulkan::Ex2VulkanD1NativePhase;
    FailureClassification classification;
    switch (phase)
    {
    case Phase::InstanceCreation:
    case Phase::DeviceEnumeration:
    case Phase::DeviceSelection:
    case Phase::DeviceProperties:
    case Phase::QueueSelection:
    case Phase::LogicalDeviceCreation:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
        break;
    case Phase::ResourcePreflight:
    case Phase::BufferCreation:
    case Phase::MemoryAllocation:
    case Phase::MemoryBinding:
    case Phase::MemoryMapping:
    case Phase::DescriptorCreation:
    case Phase::PipelineConstruction:
    case Phase::CommandCreation:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
        break;
    case Phase::InitialUpload:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::InitialUpload,
            IntegrationErrorCode::InitialUploadFailed};
        break;
    case Phase::CommandBufferReset:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CommandBufferReset,
            IntegrationErrorCode::CommandBufferResetFailed};
        break;
    case Phase::CommandRecording:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CommandRecording,
            IntegrationErrorCode::CommandRecordingFailed};
        break;
    case Phase::Submission:
        classification = {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::SequenceSubmission,
            IntegrationErrorCode::SubmissionFailed};
        break;
    case Phase::CompletionWait:
        classification = {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
        break;
    case Phase::FinalReadback:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::FinalReadback,
            IntegrationErrorCode::FinalReadbackFailed};
        break;
    case Phase::QueryReset:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::QueryReset,
            IntegrationErrorCode::QueryResetFailed};
        break;
    case Phase::NativeTimingRetrieval:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeTimingRetrieval,
            IntegrationErrorCode::NativeTimingRetrievalFailed};
        break;
    case Phase::NativeTimingConversion:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeTimingConversion,
            IntegrationErrorCode::NativeTimingConversionFailed};
        break;
    default:
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::NativeFailure,
            IntegrationErrorCode::UnclassifiedNativeFailure};
        break;
    }

    if (nativeResult == VK_TIMEOUT)
    {
        classification.status = IntegrationStatus::Timeout;
        classification.errorCode = IntegrationErrorCode::OperationTimeout;
    }
    else if (nativeResult == VK_ERROR_DEVICE_LOST)
    {
        classification.status = IntegrationStatus::DeviceLost;
        classification.errorCode = IntegrationErrorCode::DeviceLost;
    }
    return classification;
}

CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t iterationCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& d1SpirvPath)
{
    const IterativeConfiguration iterative{
        IterativeVariant::D1, elementCount, iterationCount};
    const WorkloadConfiguration configuration = MakeConfiguration(iterative);
    if (!ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 D1 request is not eligible for correctness execution");
    }

    auto initialState = GenerateWordInput(CoreInputSeed, elementCount);
    ValidateDeclaredInitialState(configuration, initialState);
    auto expected = ReferenceD1(initialState, iterationCount);
    const IterativeFinalBuffer parity = (iterationCount & 1U) == 0U
        ? IterativeFinalBuffer::StateA
        : IterativeFinalBuffer::StateB;
    if (expected.finalState.size() != initialState.size()
        || expected.finalBuffer != parity)
    {
        throw std::runtime_error(
            "EX-2 D1 independent CPU expectation violates the frozen parity contract");
    }

    auto cudaOperation = std::make_unique<cuda::Ex2CudaD1Operation>(
        cudaDeviceOrdinal, iterative);
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanD1Operation>(
        iterative, d1SpirvPath, vulkanPhysicalDeviceIndex);

    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const std::string uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);

    const auto loadedSpirv = vulkanOperation->LoadedSpirv();
    const std::string loadedShaderName{
        vulkanOperation->LoadedShaderName()};
    if (loadedShaderName != "Ex2D1.comp.spv"
        || loadedSpirv.empty()
        || loadedSpirv.front() != 0x07230203U)
    {
        throw std::runtime_error(
            "EX-2 Vulkan D1 loaded shader identity or SPIR-V magic is invalid");
    }
    const std::string loadedSpirvSha256 = Sha256(
        std::as_bytes(loadedSpirv));
    const std::string shaderFileSha256 = Sha256File(d1SpirvPath);
    if (loadedSpirvSha256 != shaderFileSha256)
    {
        throw std::runtime_error(
            "EX-2 Vulkan D1 loaded SPIR-V does not match the selected artifact");
    }

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.initialStateSha256 = WordInputSha256(initialState);
    result.expectedFinalStateSha256 = WordInputSha256(expected.finalState);
    result.expectedFinalBuffer = expected.finalBuffer;
    result.vulkanLoadedShaderName = loadedShaderName;
    result.vulkanLoadedSpirvSha256 = loadedSpirvSha256;
    result.vulkanShaderFileSha256 = shaderFileSha256;
    result.vulkanShaderProvenanceVerified = true;
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    result.initialState = std::move(initialState);
    result.expectedFinalState = std::move(expected.finalState);

    result.cuda = ExecuteCuda(
        *cudaOperation,
        result.initialState,
        result.expectedFinalState,
        result.expectedFinalBuffer,
        iterationCount);
    if (result.cuda.status != IntegrationStatus::Ok)
    {
        result.vulkan = MakeInterruptedObservation(
            result.cuda.safeForFurtherGpuCalls);
        result.vulkan.expectedFinalBuffer = result.expectedFinalBuffer;
        result.vulkan.expectedIterationCount = iterationCount;
        result.vulkan.hModeUninstrumented =
            !vulkan::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps
            && !vulkanOperation->Diagnostics().timestampQueryPoolCreated;
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
        result.initialState,
        result.expectedFinalState,
        result.expectedFinalBuffer,
        iterationCount);
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    ReconcileCrossBackendFinalStates(result);
    if (ClassifyFailedSessionResourceDisposition(result.vulkan)
        == FailedSessionResourceDisposition::PreserveForProcessTeardown)
    {
        static_cast<void>(vulkanOperation.release());
        static_cast<void>(cudaOperation.release());
    }
    return result;
}

} // namespace computelab::ex2::d1
