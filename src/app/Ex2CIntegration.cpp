#include "app/Ex2CIntegration.hpp"

#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <memory>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::c
{
namespace
{

bool IsCudaInitializationPhase(cuda::Ex2CudaCNativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaCNativePhase;
    return phase == Phase::DeviceSelection
        || phase == Phase::RuntimeInitialization
        || phase == Phase::DeviceProperties;
}

bool IsCudaAllocationPhase(cuda::Ex2CudaCNativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaCNativePhase;
    return phase == Phase::ResourcePreflight
        || phase == Phase::StreamCreation
        || phase == Phase::TargetAllocation
        || phase == Phase::CounterAllocation;
}

bool IsVulkanInitializationPhase(
    vulkan::Ex2VulkanCNativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanCNativePhase;
    return phase == Phase::InstanceCreation
        || phase == Phase::DeviceEnumeration
        || phase == Phase::DeviceSelection
        || phase == Phase::DeviceProperties
        || phase == Phase::QueueSelection
        || phase == Phase::LogicalDeviceCreation;
}

bool IsVulkanAllocationPhase(
    vulkan::Ex2VulkanCNativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanCNativePhase;
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
    bool countersObserved)
{
    BackendObservation result;
    result.nativeOperationCompleted = nativeOperationCompleted;
    result.countersObserved = countersObserved;
    result.status = failure.status;
    result.failurePhase = failure.phase;
    result.errorCode = failure.errorCode;
    result.safeForFurtherGpuCalls = false;
    return result;
}

BackendObservation ExecuteCuda(
    cuda::Ex2CudaCOperation& operation,
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> expectedCounters)
{
    bool completed = false;
    bool countersObserved = false;
    try
    {
        operation.UploadTargets(targets);
        operation.PrepareReset();
        operation.SubmitAtomic();
        operation.WaitForCompletion();
        completed = true;
        auto counters = operation.RetrieveCounters();
        countersObserved = true;
        auto deviceTargets = operation.RetrieveDeviceTargets();
        return CompletedObservation(
            expectedCounters,
            std::move(counters),
            targets,
            std::move(deviceTargets),
            operation.LastCompletionExecutedKernel());
    }
    catch (const cuda::Ex2CudaCNativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            completed,
            countersObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {IntegrationStatus::Incomplete,
                IntegrationFailurePhase::ResourceAllocation,
                IntegrationErrorCode::ResourceAllocationFailed},
            completed,
            countersObserved);
    }
}

BackendObservation ExecuteVulkan(
    vulkan::Ex2VulkanCOperation& operation,
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> expectedCounters)
{
    bool completed = false;
    bool countersObserved = false;
    try
    {
        operation.UploadTargets(targets);
        operation.PrepareReset();
        operation.SubmitAtomic();
        operation.WaitForCompletion();
        completed = true;
        auto counters = operation.RetrieveCounters();
        countersObserved = true;
        auto deviceTargets = operation.RetrieveDeviceTargets();
        return CompletedObservation(
            expectedCounters,
            std::move(counters),
            targets,
            std::move(deviceTargets),
            operation.LastCompletionExecutedShader());
    }
    catch (const vulkan::Ex2VulkanCNativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            completed,
            countersObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {IntegrationStatus::Incomplete,
                IntegrationFailurePhase::ResourceAllocation,
                IntegrationErrorCode::ResourceAllocationFailed},
            completed,
            countersObserved);
    }
}

} // namespace

bool BufferValidation::Passed() const noexcept
{
    return counterLengthMatches && countersMatchExpected
        && targetLengthMatches && targetsPreserved
        && counterTotalMatches && nativeDispatchMatches;
}

bool CrossBackendObservation::Passed() const noexcept
{
    return physicalIdentityVerified
        && vulkanShaderProvenanceVerified
        && cuda.status == IntegrationStatus::Ok
        && vulkan.status == IntegrationStatus::Ok
        && pairComparisonPerformed
        && pairCountersEqual;
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
        throw std::invalid_argument("EX-2 C device UUID must not be all zero");

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

void ValidateDeclaredTargets(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> targets)
{
    if (!std::holds_alternative<ContentionConfiguration>(
            configuration.parameters)
        || !ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 C integration configuration is not correctness eligible");
    }

    const auto& contention = std::get<ContentionConfiguration>(
        configuration.parameters);
    if (contention.allocatedCounterCount != contention.elementCount)
    {
        throw std::invalid_argument(
            "EX-2 C integration requires exactly N allocated counters");
    }
    ValidateContentionTargets(
        targets,
        contention.elementCount,
        contention.activeCounterCount);
}

BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedCounters,
    std::span<const std::uint32_t> observedCounters,
    std::span<const std::uint32_t> originalTargets,
    std::span<const std::uint32_t> observedDeviceTargets,
    bool nativeDispatchExecuted) noexcept
{
    const bool counterLength =
        observedCounters.size() == expectedCounters.size();
    const bool targetLength =
        observedDeviceTargets.size() == originalTargets.size();
    const std::uint64_t observedTotal = std::accumulate(
        observedCounters.begin(), observedCounters.end(), std::uint64_t{0});
    return {
        counterLength,
        counterLength && std::equal(
            observedCounters.begin(),
            observedCounters.end(),
            expectedCounters.begin()),
        targetLength,
        targetLength && std::equal(
            observedDeviceTargets.begin(),
            observedDeviceTargets.end(),
            originalTargets.begin()),
        observedTotal == originalTargets.size(),
        nativeDispatchExecuted == !originalTargets.empty()};
}

BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedCounters,
    std::vector<std::uint32_t> observedCounters,
    std::span<const std::uint32_t> originalTargets,
    std::vector<std::uint32_t> observedDeviceTargets,
    bool nativeDispatchExecuted)
{
    const auto validation = ValidateBuffers(
        expectedCounters,
        observedCounters,
        originalTargets,
        observedDeviceTargets,
        nativeDispatchExecuted);

    BackendObservation result;
    result.nativeOperationCompleted = true;
    result.countersObserved = true;
    result.cpuComparisonPerformed = true;
    result.validationPassed = validation.Passed();
    result.status = validation.Passed()
        ? IntegrationStatus::Ok
        : IntegrationStatus::ValidationFailed;
    if (!validation.Passed())
    {
        result.failurePhase = IntegrationFailurePhase::ValidationMismatch;
        if (!validation.targetLengthMatches || !validation.targetsPreserved)
        {
            result.errorCode =
                IntegrationErrorCode::TargetPreservationFailed;
        }
        else if (!validation.nativeDispatchMatches)
        {
            result.errorCode = IntegrationErrorCode::NativeDispatchMismatch;
        }
        else
        {
            result.errorCode = IntegrationErrorCode::CounterMismatch;
        }
    }
    result.nativeDispatchExecuted = nativeDispatchExecuted;
    result.counters = std::move(observedCounters);
    result.deviceTargets = std::move(observedDeviceTargets);
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

void ReconcileCrossBackendCounters(CrossBackendObservation& observation)
{
    if (!observation.cuda.cpuComparisonPerformed
        || !observation.vulkan.cpuComparisonPerformed)
    {
        return;
    }
    observation.pairComparisonPerformed = true;
    observation.pairCountersEqual =
        observation.cuda.counters.size() == observation.vulkan.counters.size()
        && std::equal(
            observation.cuda.counters.begin(),
            observation.cuda.counters.end(),
            observation.vulkan.counters.begin());
}

FailedSessionResourceDisposition ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept
{
    return failedObservation.safeForFurtherGpuCalls
        ? FailedSessionResourceDisposition::DestroyNormally
        : FailedSessionResourceDisposition::PreserveForProcessTeardown;
}

FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaCNativePhase phase,
    int) noexcept
{
    using Phase = cuda::Ex2CudaCNativePhase;
    if (IsCudaInitializationPhase(phase))
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
    if (IsCudaAllocationPhase(phase))
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
    if (phase == Phase::TargetUpload)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::TargetUpload,
            IntegrationErrorCode::TargetUploadFailed};
    if (phase == Phase::TargetUploadCompletion)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::UploadCompletion,
            IntegrationErrorCode::UploadCompletionFailed};
    if (phase == Phase::CounterReset)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CounterReset,
            IntegrationErrorCode::CounterResetFailed};
    if (phase == Phase::ResetCompletion)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResetCompletion,
            IntegrationErrorCode::ResetCompletionFailed};
    if (phase == Phase::Submission)
        return {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::Submission,
            IntegrationErrorCode::SubmissionFailed};
    if (phase == Phase::CompletionWait)
        return {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
    if (phase == Phase::CounterReadback)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CounterReadback,
            IntegrationErrorCode::CounterReadbackFailed};
    if (phase == Phase::TargetDiagnosticReadback)
        return {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::TargetDiagnosticReadback,
            IntegrationErrorCode::TargetReadbackFailed};
    return {IntegrationStatus::Incomplete,
        IntegrationFailurePhase::NativeFailure,
        IntegrationErrorCode::UnclassifiedNativeFailure};
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanCNativePhase phase,
    VkResult nativeResult) noexcept
{
    using Phase = vulkan::Ex2VulkanCNativePhase;
    FailureClassification classification;
    if (IsVulkanInitializationPhase(phase))
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::BackendInitialization,
            IntegrationErrorCode::BackendInitializationFailed};
    else if (IsVulkanAllocationPhase(phase))
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResourceAllocation,
            IntegrationErrorCode::ResourceAllocationFailed};
    else if (phase == Phase::TargetUpload)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::TargetUpload,
            IntegrationErrorCode::TargetUploadFailed};
    else if (phase == Phase::CounterReset)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CounterReset,
            IntegrationErrorCode::CounterResetFailed};
    else if (phase == Phase::ResetCompletion)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::ResetCompletion,
            IntegrationErrorCode::ResetCompletionFailed};
    else if (phase == Phase::Submission)
        classification = {IntegrationStatus::SubmitFailed,
            IntegrationFailurePhase::Submission,
            IntegrationErrorCode::SubmissionFailed};
    else if (phase == Phase::CompletionWait)
        classification = {IntegrationStatus::WaitFailed,
            IntegrationFailurePhase::CompletionWait,
            IntegrationErrorCode::CompletionFailed};
    else if (phase == Phase::CounterReadback)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::CounterReadback,
            IntegrationErrorCode::CounterReadbackFailed};
    else if (phase == Phase::TargetDiagnosticReadback)
        classification = {IntegrationStatus::Incomplete,
            IntegrationFailurePhase::TargetDiagnosticReadback,
            IntegrationErrorCode::TargetReadbackFailed};
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
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& cSpirvPath)
{
    const ContentionConfiguration contention{
        elementCount, activeCounterCount, elementCount};
    const WorkloadConfiguration configuration = MakeConfiguration(contention);
    if (!ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 C request is not eligible for correctness execution");
    }

    auto targets = GenerateContentionTargets(
        elementCount, activeCounterCount);
    ValidateDeclaredTargets(configuration, targets);
    auto expectedCounters = ReferenceContentionHistogram(
        targets, contention.allocatedCounterCount);

    auto cudaOperation = std::make_unique<cuda::Ex2CudaCOperation>(
        cudaDeviceOrdinal, contention);
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanCOperation>(
        contention, cSpirvPath, vulkanPhysicalDeviceIndex);

    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const std::string uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);

    const std::string loadedSpirvSha256 = Sha256(
        std::as_bytes(vulkanOperation->LoadedSpirv()));
    const std::string shaderFileSha256 = Sha256File(cSpirvPath);
    const std::string loadedShaderName{
        vulkanOperation->LoadedShaderName()};
    if (loadedShaderName != "Ex2C.comp.spv"
        || loadedSpirvSha256 != shaderFileSha256)
    {
        throw std::runtime_error(
            "EX-2 Vulkan C loaded SPIR-V does not match the selected artifact");
    }

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.targetsSha256 = WordInputSha256(targets);
    result.expectedCountersSha256 = WordInputSha256(expectedCounters);
    result.vulkanLoadedShaderName = loadedShaderName;
    result.vulkanLoadedSpirvSha256 = loadedSpirvSha256;
    result.vulkanShaderFileSha256 = shaderFileSha256;
    result.vulkanShaderProvenanceVerified = true;
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    result.targets = std::move(targets);
    result.expectedCounters = std::move(expectedCounters);

    result.cuda = ExecuteCuda(
        *cudaOperation, result.targets, result.expectedCounters);
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
        *vulkanOperation, result.targets, result.expectedCounters);
    ReconcileCrossBackendCounters(result);
    if (ClassifyFailedSessionResourceDisposition(result.vulkan)
        == FailedSessionResourceDisposition::PreserveForProcessTeardown)
    {
        static_cast<void>(vulkanOperation.release());
        static_cast<void>(cudaOperation.release());
    }
    return result;
}

} // namespace computelab::ex2::c
