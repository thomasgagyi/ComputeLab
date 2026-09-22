#include "app/Ex2A1Integration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace computelab::ex2::a1
{
namespace
{

constexpr std::array<std::uint32_t, 4> kLiteralInput{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4> kLiteralExpected{
    0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U};

void VerifyLiteralFixture(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    const std::size_t applicable = std::min(input.size(), kLiteralInput.size());
    if (expected.size() < applicable
        || !std::equal(input.begin(), input.begin() + applicable,
            kLiteralInput.begin())
        || !std::equal(expected.begin(), expected.begin() + applicable,
            kLiteralExpected.begin()))
    {
        throw std::runtime_error(
            "EX-2 A1 generated input or CPU oracle disagrees with the literal fixture");
    }
}

bool IsInitializationPhase(cuda::Ex2CudaA1NativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaA1NativePhase;
    return phase == Phase::DeviceSelection
        || phase == Phase::RuntimeInitialization
        || phase == Phase::DeviceProperties;
}

bool IsAllocationPhase(cuda::Ex2CudaA1NativePhase phase) noexcept
{
    using Phase = cuda::Ex2CudaA1NativePhase;
    return phase == Phase::ResourcePreflight
        || phase == Phase::StreamCreation
        || phase == Phase::InputAllocation
        || phase == Phase::OutputAllocation;
}

bool IsVulkanInitializationPhase(
    vulkan::Ex2VulkanA1NativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanA1NativePhase;
    return phase == Phase::InstanceCreation
        || phase == Phase::DeviceEnumeration
        || phase == Phase::DeviceSelection
        || phase == Phase::DeviceProperties
        || phase == Phase::QueueSelection
        || phase == Phase::LogicalDeviceCreation;
}

bool IsVulkanAllocationPhase(
    vulkan::Ex2VulkanA1NativePhase phase) noexcept
{
    using Phase = vulkan::Ex2VulkanA1NativePhase;
    return phase == Phase::ResourcePreflight
        || phase == Phase::BufferCreation
        || phase == Phase::MemoryAllocation
        || phase == Phase::MemoryBinding
        || phase == Phase::MemoryMapping
        || phase == Phase::DescriptorCreation
        || phase == Phase::PipelineCreation
        || phase == Phase::CommandCreation;
}

BackendObservation FailureObservation(
    const FailureClassification& failure,
    bool operationCompleted,
    bool outputObserved)
{
    BackendObservation result;
    result.operationCompleted = operationCompleted;
    result.outputObserved = outputObserved;
    result.status = failure.status;
    result.failurePhase = failure.phase;
    result.errorCode = failure.errorCode;
    result.safeForFurtherGpuCalls = false;
    return result;
}

BackendObservation InterruptedObservation()
{
    return FailureObservation(
        {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Interrupted,
            std::string(evidence::error_code::Interrupted)},
        false,
        false);
}

BackendObservation ExecuteCuda(
    cuda::Ex2CudaA1Operation& operation,
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    bool completed = false;
    bool outputObserved = false;
    std::vector<std::uint32_t> output;
    try
    {
        operation.Upload(input);
        operation.SubmitA1();
        operation.WaitForCompletion();
        completed = true;
        output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        return CompletedObservation(
            expected,
            std::move(output),
            input,
            std::move(deviceInput),
            operation.LastCompletionExecutedKernel());
    }
    catch (const cuda::Ex2CudaA1NativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {evidence::OperationStatus::Incomplete,
                evidence::FailurePhase::ResourceAllocation,
                std::string(evidence::error_code::ResourceAllocationFailed)},
            completed,
            outputObserved);
    }
}

BackendObservation ExecuteVulkan(
    vulkan::Ex2VulkanA1Operation& operation,
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    bool completed = false;
    bool outputObserved = false;
    std::vector<std::uint32_t> output;
    try
    {
        operation.Upload(input);
        operation.Prepare();
        operation.SubmitA1();
        operation.WaitForCompletion();
        completed = true;
        output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        return CompletedObservation(
            expected,
            std::move(output),
            input,
            std::move(deviceInput),
            operation.LastCompletionExecutedShader());
    }
    catch (const vulkan::Ex2VulkanA1NativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {evidence::OperationStatus::Incomplete,
                evidence::FailurePhase::ResourceAllocation,
                std::string(evidence::error_code::ResourceAllocationFailed)},
            completed,
            outputObserved);
    }
}

evidence::SampleRecord SampleFromObservation(
    const evidence::CorrectnessPlan& plan,
    const BackendObservation& observation)
{
    auto sample = evidence::MakeSampleRecord(plan, 0U);
    sample.correctness.expectedOutputGenerated =
        observation.expectedOutputGenerated;
    sample.correctness.operationCompleted = observation.operationCompleted;
    sample.correctness.outputObserved = observation.outputObserved;
    sample.correctness.comparisonPerformed = observation.comparisonPerformed;
    sample.correctness.validationPassed = observation.validationPassed;
    sample.status = observation.status;
    sample.failurePhase = observation.failurePhase;
    sample.errorCode = observation.errorCode;
    return sample;
}

evidence::InitializationRecord SetupObservation(
    const evidence::CorrectnessPlan& plan)
{
    const auto& linear = std::get<LinearConfiguration>(
        plan.seriesIdentity.condition.workload.parameters);
    const std::string variant{ToString(linear.variant)};
    return {
        plan.runId,
        plan.seriesIdentity.backend,
        plan.seriesIdentity.processIndex,
        0U,
        "backend_setup",
        std::string{"A"},
        variant,
        linear.elementCount,
        "setup_complete",
        std::nullopt,
        std::string{"native_"} +
            (linear.variant == LinearVariant::A1 ? "a1" : "a2") +
            "_resources_created"};
}

evidence::BackendDiagnostics CudaDiagnostics(LinearVariant variant)
{
    evidence::BackendDiagnostics result;
    result.implementation = variant == LinearVariant::A1
        ? "native_cuda_a1"
        : "native_cuda_a2";
    result.streamFlags = "nonblocking";
    return result;
}

evidence::BackendDiagnostics VulkanDiagnostics(
    const vulkan::Ex2VulkanA1Diagnostics& native,
    LinearVariant variant)
{
    evidence::BackendDiagnostics result;
    result.implementation = variant == LinearVariant::A1
        ? "native_vulkan_a1"
        : "native_vulkan_a2";
    result.queueFamilyIndex = native.queueFamilyIndex;
    result.queueFlags = native.queueFamily.queueFlags;
    result.queueCount = native.queueFamily.queueCount;
    result.timestampValidBits = std::nullopt;
    result.timestampPeriodNanoseconds = std::nullopt;
    result.inputMemoryFlags = native.inputMemoryFlags;
    result.outputMemoryFlags = native.outputMemoryFlags;
    result.uploadMemoryFlags = native.uploadMemoryFlags;
    result.readbackMemoryFlags = native.readbackMemoryFlags;
    return result;
}

SerializedSeries BuildSeries(
    evidence::CorrectnessPlan plan,
    results::EnvironmentRecord common,
    const BackendObservation& observation,
    const std::string& inputSha256,
    const std::string& expectedOutputSha256,
    evidence::BackendDiagnostics diagnostics)
{
    common.schemaVersion = evidence::SchemaVersion;
    evidence::EnvironmentRecord environment{
        std::move(common),
        plan,
        inputSha256,
        expectedOutputSha256,
        std::move(diagnostics)};
    std::vector initialization{SetupObservation(plan)};
    std::vector samples{SampleFromObservation(plan, observation)};
    auto summary = evidence::SummarizeSamples(
        plan,
        samples,
        observation.status,
        observation.failurePhase,
        observation.errorCode);
    evidence::ValidateEvidenceBundle(
        environment, initialization, samples, summary);

    SerializedSeries result{
        std::move(environment),
        std::move(initialization),
        std::move(samples),
        std::move(summary)};
    result.environmentJson = evidence::SerializeEnvironmentJson(result.environment);
    result.initializationCsv = evidence::SerializeInitializationCsv(
        result.environment.plan, result.initialization);
    result.samplesCsv = evidence::SerializeSamplesCsv(
        result.environment.plan, result.samples);
    result.summaryJson = evidence::SerializeSummaryJson(
        result.summary, result.samples);
    return result;
}

} // namespace

bool BufferValidation::Passed() const noexcept
{
    return outputLengthMatches && outputMatchesExpected
        && inputLengthMatches && inputPreserved;
}

bool CrossBackendObservation::Passed() const noexcept
{
    return cuda.status == evidence::OperationStatus::Ok
        && vulkan.status == evidence::OperationStatus::Ok
        && crossBackendComparisonPerformed
        && crossBackendOutputsEqual;
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
        throw std::invalid_argument("EX-2 A1 device UUID must not be all zero");
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
        throw std::invalid_argument(
            "selected CUDA and Vulkan devices do not have the same physical-device UUID");
    return formatted;
}

BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedOutput,
    std::span<const std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalInput,
    std::span<const std::uint32_t> observedDeviceInput) noexcept
{
    const bool outputLength = observedOutput.size() == expectedOutput.size();
    const bool inputLength = observedDeviceInput.size() == originalInput.size();
    return {
        outputLength,
        outputLength && std::equal(
            observedOutput.begin(), observedOutput.end(), expectedOutput.begin()),
        inputLength,
        inputLength && std::equal(
            observedDeviceInput.begin(), observedDeviceInput.end(), originalInput.begin())};
}

BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedOutput,
    std::vector<std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalInput,
    std::vector<std::uint32_t> observedDeviceInput,
    bool nativeDispatchExecuted)
{
    const auto validation = ValidateBuffers(
        expectedOutput, observedOutput, originalInput, observedDeviceInput);
    BackendObservation result;
    result.operationCompleted = true;
    result.outputObserved = true;
    result.comparisonPerformed = true;
    result.validationPassed = validation.Passed();
    result.status = validation.Passed()
        ? evidence::OperationStatus::Ok
        : evidence::OperationStatus::ValidationFailed;
    if (!validation.Passed())
    {
        result.failurePhase = evidence::FailurePhase::Validation;
        result.errorCode = validation.inputLengthMatches && validation.inputPreserved
            ? std::string(evidence::error_code::OutputMismatch)
            : "input_preservation_failed";
    }
    result.nativeDispatchExecuted = nativeDispatchExecuted;
    result.output = std::move(observedOutput);
    result.deviceInput = std::move(observedDeviceInput);
    return result;
}

void ReconcileCrossBackendOutputs(CrossBackendObservation& observation)
{
    if (!observation.cuda.comparisonPerformed
        || !observation.vulkan.comparisonPerformed)
    {
        return;
    }
    observation.crossBackendComparisonPerformed = true;
    observation.crossBackendOutputsEqual =
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
    cuda::Ex2CudaA1NativePhase phase,
    int) noexcept
{
    using Phase = cuda::Ex2CudaA1NativePhase;
    if (IsInitializationPhase(phase))
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::BackendInitialization,
            std::string(evidence::error_code::BackendInitializationFailed)};
    if (IsAllocationPhase(phase))
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::ResourceAllocation,
            std::string(evidence::error_code::ResourceAllocationFailed)};
    if (phase == Phase::Submission)
        return {evidence::OperationStatus::SubmitFailed,
            evidence::FailurePhase::Submission,
            std::string(evidence::error_code::SubmissionFailed)};
    if (phase == Phase::CompletionWait)
        return {evidence::OperationStatus::WaitFailed,
            evidence::FailurePhase::CompletionWait,
            std::string(evidence::error_code::CompletionFailed)};
    if (phase == Phase::OutputReadback
        || phase == Phase::InputDiagnosticReadback)
    {
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Readback,
            std::string(evidence::error_code::ReadbackFailed)};
    }
    return {evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::ResourceAllocation,
        "input_upload_failed"};
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanA1NativePhase phase,
    VkResult nativeResult) noexcept
{
    using Phase = vulkan::Ex2VulkanA1NativePhase;
    if (nativeResult == VK_ERROR_DEVICE_LOST)
    {
        const auto failurePhase = phase == Phase::Submission
            ? evidence::FailurePhase::Submission
            : phase == Phase::CompletionWait
                ? evidence::FailurePhase::CompletionWait
                : phase == Phase::OutputReadback
                    || phase == Phase::InputDiagnosticReadback
                    ? evidence::FailurePhase::Readback
                    : evidence::FailurePhase::BackendInitialization;
        return {evidence::OperationStatus::DeviceLost,
            failurePhase,
            std::string(evidence::error_code::DeviceLost)};
    }
    if (nativeResult == VK_TIMEOUT)
        return {evidence::OperationStatus::Timeout,
            evidence::FailurePhase::CompletionWait,
            std::string(evidence::error_code::OperationTimeout)};
    if (IsVulkanInitializationPhase(phase))
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::BackendInitialization,
            std::string(evidence::error_code::BackendInitializationFailed)};
    if (IsVulkanAllocationPhase(phase))
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::ResourceAllocation,
            std::string(evidence::error_code::ResourceAllocationFailed)};
    if (phase == Phase::Submission)
        return {evidence::OperationStatus::SubmitFailed,
            evidence::FailurePhase::Submission,
            std::string(evidence::error_code::SubmissionFailed)};
    if (phase == Phase::CompletionWait)
        return {evidence::OperationStatus::WaitFailed,
            evidence::FailurePhase::CompletionWait,
            std::string(evidence::error_code::CompletionFailed)};
    if (phase == Phase::OutputReadback
        || phase == Phase::InputDiagnosticReadback)
    {
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Readback,
            std::string(evidence::error_code::ReadbackFailed)};
    }
    if (phase == Phase::Preparation)
        return {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::ResourceAllocation,
            "preparation_failed"};
    return {evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::ResourceAllocation,
        "input_upload_failed"};
}

CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& spirvPath)
{
    if (seed != CoreInputSeed)
        throw std::invalid_argument(
            "EX-2 A1 minimal correctness path requires the approved core seed");
    const LinearConfiguration linear{LinearVariant::A1, elementCount};
    const WorkloadConfiguration configuration = MakeConfiguration(linear, seed);
    if (!ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 A1 request is not eligible for correctness execution");
    }

    auto cudaOperation = std::make_unique<cuda::Ex2CudaA1Operation>(
        cudaDeviceOrdinal, linear);
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanA1Operation>(
        linear, spirvPath, vulkanPhysicalDeviceIndex);
    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const std::string uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);

    auto input = GenerateWordInput(seed, elementCount);
    auto expected = ReferenceA1(input);
    VerifyLiteralFixture(input, expected);

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.inputSha256 = WordInputSha256(input);
    result.expectedOutputSha256 = WordInputSha256(expected);
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    result.input = std::move(input);
    result.expectedOutput = std::move(expected);

    result.cuda = ExecuteCuda(
        *cudaOperation, result.input, result.expectedOutput);
    if (result.cuda.status != evidence::OperationStatus::Ok)
    {
        result.vulkan = InterruptedObservation();
        if (ClassifyFailedSessionResourceDisposition(result.cuda)
            == FailedSessionResourceDisposition::PreserveForProcessTeardown)
        {
            // An uncertain native failure can affect the shared physical
            // device. Retain both API objects until OS process teardown.
            static_cast<void>(vulkanOperation.release());
            static_cast<void>(cudaOperation.release());
        }
        return result;
    }

    result.vulkan = ExecuteVulkan(
        *vulkanOperation, result.input, result.expectedOutput);
    ReconcileCrossBackendOutputs(result);
    if (!result.vulkan.safeForFurtherGpuCalls)
    {
        static_cast<void>(vulkanOperation.release());
        static_cast<void>(cudaOperation.release());
    }
    return result;
}

SerializedEvidencePair BuildEvidence(
    const CrossBackendObservation& observation,
    EvidenceBuildContext context)
{
    const auto variant = std::get<LinearConfiguration>(
        observation.configuration.parameters).variant;
    if (!observation.physicalIdentityVerified
        || FormatDeviceUuid(observation.verifiedDeviceUuid)
            != observation.verifiedDeviceUuidText)
    {
        throw std::invalid_argument(
            "EX-2 A1 evidence requires a genuine, consistent physical-device match");
    }
    const ComparisonConditionContext condition{
        "1.0",
        context.machineId,
        {observation.verifiedDeviceUuidText, true},
        observation.configuration,
        InstrumentMode::P};
    const SeriesIdentityContext cudaIdentity{
        condition,
        Backend::Cuda,
        0U,
        0U,
        0U,
        0U,
        1U,
        context.sourceRevision,
        context.executableSha256,
        std::nullopt};
    const SeriesIdentityContext vulkanIdentity{
        condition,
        Backend::Vulkan,
        0U,
        0U,
        1U,
        0U,
        1U,
        context.sourceRevision,
        context.executableSha256,
        context.shaderSha256};
    auto cudaPlan = evidence::MakeCorrectnessPlan(
        context.cudaRunId, cudaIdentity);
    auto vulkanPlan = evidence::MakeCorrectnessPlan(
        context.vulkanRunId, vulkanIdentity);
    if (cudaPlan.comparisonConditionId != vulkanPlan.comparisonConditionId
        || cudaPlan.seriesId == vulkanPlan.seriesId
        || cudaPlan.runId == vulkanPlan.runId)
    {
        throw std::logic_error(
            "EX-2 A1 common condition and backend-specific identity invariants failed");
    }

    return {
        BuildSeries(
            std::move(cudaPlan),
            std::move(context.cudaEnvironment),
            observation.cuda,
            observation.inputSha256,
            observation.expectedOutputSha256,
            CudaDiagnostics(variant)),
        BuildSeries(
            std::move(vulkanPlan),
            std::move(context.vulkanEnvironment),
            observation.vulkan,
            observation.inputSha256,
            observation.expectedOutputSha256,
            VulkanDiagnostics(observation.vulkanDiagnostics, variant))};
}

} // namespace computelab::ex2::a1
