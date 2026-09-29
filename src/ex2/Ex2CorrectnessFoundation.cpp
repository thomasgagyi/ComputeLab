#include "ex2/Ex2CorrectnessFoundation.hpp"

#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace computelab::ex2::evidence
{
namespace
{

void RequireValidWorkload(const WorkloadConfiguration& workload)
{
    if (!ValidateSemanticConfiguration(workload).IsValid())
        throw std::invalid_argument(
            "I7 correctness input requires a semantically valid workload");
}

void RequireGeneratedWords(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> input,
    std::uint64_t expectedCount)
{
    if (input.size() != expectedCount)
        throw std::invalid_argument(
            "I7 word input length disagrees with the workload");
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        if (input[index] != Value(workload.common.seed, index))
            throw std::invalid_argument(
                "I7 word input disagrees with the declared seed and generator");
    }
}

void RequireGeneratedBytes(
    const WorkloadConfiguration& workload,
    std::span<const std::uint8_t> input,
    std::uint64_t expectedCount)
{
    if (input.size() != expectedCount)
        throw std::invalid_argument(
            "I7 byte input length disagrees with the workload");
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        if (input[index] != Byte(workload.common.seed, index))
            throw std::invalid_argument(
                "I7 byte input disagrees with the declared seed and generator");
    }
}

std::vector<std::uint32_t> DeclaredPermutation(
    const WorkloadConfiguration& workload,
    const IndexedConfiguration& indexed)
{
    return indexed.indexPattern == IndexPattern::StructuredV1
        ? GenerateStructuredPermutation(indexed.elementCount)
        : GenerateShuffledPermutation(
            workload.common.seed, indexed.elementCount);
}

struct Labels
{
    std::string workload;
    std::string variant;
    std::optional<std::uint64_t> elementCount;
};

Labels WorkloadLabels(const WorkloadConfiguration& workload)
{
    return std::visit([](const auto& value) -> Labels {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
            return {"A", std::string(ToString(value.variant)), value.elementCount};
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            return {"B", std::string(ToString(value.variant)), value.elementCount};
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
            return {"C", "C", value.elementCount};
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
            return {"D", std::string(ToString(value.variant)), value.elementCount};
        else
            return {"E", std::string(ToString(value.variant)), std::nullopt};
    }, workload.parameters);
}

template <typename T>
SampleRecord ComparedSample(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    std::span<const T> expected,
    std::span<const T> observed)
{
    auto sample = MakeSampleRecord(foundation.Plan(), sampleIndex);
    const bool passed = expected.size() == observed.size()
        && std::equal(expected.begin(), expected.end(), observed.begin());
    sample.correctness = {true, true, true, true, passed};
    sample.status = passed
        ? OperationStatus::Ok : OperationStatus::ValidationFailed;
    if (!passed)
    {
        sample.failurePhase = FailurePhase::Validation;
        sample.errorCode = std::string(error_code::OutputMismatch);
    }
    ValidateSampleRecord(sample);
    return sample;
}

struct FailureMapping
{
    OperationStatus status;
    FailurePhase phase;
    std::string_view errorCode;
    CorrectnessProgress progress;
};

FailureMapping MapFailure(I7FailureKind failure)
{
    const CorrectnessProgress expectedReady{true, false, false, false, std::nullopt};
    const CorrectnessProgress completed{true, true, false, false, std::nullopt};
    const CorrectnessProgress observed{true, true, true, false, std::nullopt};
    switch (failure)
    {
    case I7FailureKind::BackendInitializationFailed:
        return {OperationStatus::Incomplete, FailurePhase::BackendInitialization,
            error_code::BackendInitializationFailed, expectedReady};
    case I7FailureKind::ResourceAllocationFailed:
        return {OperationStatus::Incomplete, FailurePhase::ResourceAllocation,
            error_code::ResourceAllocationFailed, expectedReady};
    case I7FailureKind::SubmissionFailed:
        return {OperationStatus::SubmitFailed, FailurePhase::Submission,
            error_code::SubmissionFailed, expectedReady};
    case I7FailureKind::CompletionFailed:
        return {OperationStatus::WaitFailed, FailurePhase::CompletionWait,
            error_code::CompletionFailed, expectedReady};
    case I7FailureKind::SubmissionTimeout:
        return {OperationStatus::Timeout, FailurePhase::Submission,
            error_code::OperationTimeout, expectedReady};
    case I7FailureKind::CompletionTimeout:
        return {OperationStatus::Timeout, FailurePhase::CompletionWait,
            error_code::OperationTimeout, expectedReady};
    case I7FailureKind::DeviceLostDuringInitialization:
        return {OperationStatus::DeviceLost, FailurePhase::BackendInitialization,
            error_code::DeviceLost, expectedReady};
    case I7FailureKind::DeviceLostDuringSubmission:
        return {OperationStatus::DeviceLost, FailurePhase::Submission,
            error_code::DeviceLost, expectedReady};
    case I7FailureKind::DeviceLostDuringCompletion:
        return {OperationStatus::DeviceLost, FailurePhase::CompletionWait,
            error_code::DeviceLost, expectedReady};
    case I7FailureKind::DeviceLostDuringReadback:
        return {OperationStatus::DeviceLost, FailurePhase::Readback,
            error_code::DeviceLost, completed};
    case I7FailureKind::ReadbackFailed:
        return {OperationStatus::Incomplete, FailurePhase::Readback,
            error_code::ReadbackFailed, completed};
    case I7FailureKind::InterruptedBeforeCompletion:
        return {OperationStatus::Incomplete, FailurePhase::Interrupted,
            error_code::Interrupted, expectedReady};
    case I7FailureKind::InterruptedAfterCompletion:
        return {OperationStatus::Incomplete, FailurePhase::Interrupted,
            error_code::Interrupted, completed};
    case I7FailureKind::InterruptedAfterOutput:
        return {OperationStatus::Incomplete, FailurePhase::Interrupted,
            error_code::Interrupted, observed};
    }
    throw std::invalid_argument("I7 correctness failure kind is invalid");
}

EnvironmentRecord MakeEnvironment(
    results::EnvironmentRecord common,
    const I7CorrectnessFoundation& foundation,
    std::string expectedOutputSha256,
    BackendDiagnostics diagnostics)
{
    EnvironmentRecord result{
        std::move(common), foundation.Plan(),
        foundation.InputIdentity().Sha256(),
        std::move(expectedOutputSha256), std::move(diagnostics)};
    ValidateEnvironmentRecord(result);
    return result;
}

void ValidateI7FoundationInputs(
    const SeriesIdentityContext& seriesIdentity,
    const I7CorrectnessInputIdentity& inputIdentity)
{
    if (seriesIdentity.condition.protocolVersion != I7CorrectnessProtocolVersion)
        throw std::invalid_argument(
            "new I7 correctness packages require protocol version 1.1");
    if (ClassifyCellEligibility(seriesIdentity.condition.workload)
        != CellEligibility::ApprovedCoreCell)
    {
        throw std::invalid_argument(
            "new I7 correctness packages require one of the 22 approved core cells");
    }
    if (seriesIdentity.condition.workload != inputIdentity.Workload())
        throw std::invalid_argument(
            "I7 input identity and series workload configuration disagree");
}

std::uint64_t ExpectedWordCount(const WorkloadConfiguration& workload)
{
    return std::visit([](const auto& value) -> std::uint64_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>
            || std::is_same_v<T, IndexedConfiguration>
            || std::is_same_v<T, IterativeConfiguration>)
            return value.elementCount;
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
            return value.allocatedCounterCount;
        else
            throw std::invalid_argument(
                "I7 word expected output is not applicable to E");
    }, workload.parameters);
}

} // namespace

I7CorrectnessInputIdentity::I7CorrectnessInputIdentity(
    WorkloadConfiguration workload,
    std::string sha256)
    : workload_(std::move(workload)), sha256_(std::move(sha256))
{
}

const WorkloadConfiguration& I7CorrectnessInputIdentity::Workload() const noexcept
{
    return workload_;
}

const std::string& I7CorrectnessInputIdentity::Sha256() const noexcept
{
    return sha256_;
}

I7CorrectnessInputIdentity MakeI7WordInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> input)
{
    RequireValidWorkload(workload);
    std::uint64_t elementCount{};
    if (const auto* linear = std::get_if<LinearConfiguration>(&workload.parameters))
        elementCount = linear->elementCount;
    else if (const auto* iterative =
        std::get_if<IterativeConfiguration>(&workload.parameters);
        iterative != nullptr && iterative->variant == IterativeVariant::D1)
        elementCount = iterative->elementCount;
    else
        throw std::invalid_argument(
            "I7 single word input is applicable only to A or D1");
    RequireGeneratedWords(workload, input, elementCount);
    return {workload, WordInputSha256(input)};
}

I7CorrectnessInputIdentity MakeI7ByteInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint8_t> input)
{
    RequireValidWorkload(workload);
    const auto* transfer = std::get_if<TransferConfiguration>(&workload.parameters);
    if (transfer == nullptr)
        throw std::invalid_argument(
            "I7 single byte input is applicable only to E");
    RequireGeneratedBytes(workload, input, transfer->byteCount);
    return {workload, ByteInputSha256(input)};
}

I7CorrectnessInputIdentity MakeI7IndexedInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> primary,
    std::span<const std::uint32_t> permutation)
{
    RequireValidWorkload(workload);
    const auto* indexed = std::get_if<IndexedConfiguration>(&workload.parameters);
    if (indexed == nullptr)
        throw std::invalid_argument("I7 indexed input requires B configuration");
    RequireGeneratedWords(workload, primary, indexed->elementCount);
    ValidateIndexPermutation(permutation, indexed->elementCount);
    const auto declared = DeclaredPermutation(workload, *indexed);
    if (permutation.size() != declared.size()
        || !std::equal(
            permutation.begin(), permutation.end(), declared.begin()))
    {
        throw std::invalid_argument(
            "I7 permutation disagrees with the declared pattern and seed");
    }
    return {workload, IndexedLogicalInputSha256(primary, permutation)};
}

I7CorrectnessInputIdentity MakeI7ContentionInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> initialCounters)
{
    RequireValidWorkload(workload);
    const auto* contention =
        std::get_if<ContentionConfiguration>(&workload.parameters);
    if (contention == nullptr)
        throw std::invalid_argument("I7 contention input requires C configuration");
    ValidateContentionTargets(
        targets, contention->elementCount, contention->activeCounterCount);
    if (initialCounters.size() != contention->allocatedCounterCount)
        throw std::invalid_argument(
            "I7 initial counter length disagrees with the allocated C state");
    if (!std::all_of(initialCounters.begin(), initialCounters.end(),
            [](std::uint32_t value) { return value == 0U; }))
    {
        throw std::invalid_argument(
            "I7 C initial counter state must contain all N zero words");
    }
    return {workload, ContentionLogicalInputSha256(targets, initialCounters)};
}

I7CorrectnessFoundation::I7CorrectnessFoundation(
    CorrectnessPlan plan,
    I7CorrectnessInputIdentity inputIdentity,
    std::string expectedOutputSha256,
    bool expectedOutputUsesWords)
    : plan_(std::move(plan)), inputIdentity_(std::move(inputIdentity)),
      expectedOutputSha256_(std::move(expectedOutputSha256)),
      expectedOutputUsesWords_(expectedOutputUsesWords)
{
}

const CorrectnessPlan& I7CorrectnessFoundation::Plan() const noexcept
{
    return plan_;
}

const I7CorrectnessInputIdentity&
I7CorrectnessFoundation::InputIdentity() const noexcept
{
    return inputIdentity_;
}

const std::string&
I7CorrectnessFoundation::ExpectedOutputSha256() const noexcept
{
    return expectedOutputSha256_;
}

bool I7CorrectnessFoundation::ExpectedOutputUsesWords() const noexcept
{
    return expectedOutputUsesWords_;
}

I7CorrectnessFoundation MakeI7WordCorrectnessFoundation(
    std::string runId,
    SeriesIdentityContext seriesIdentity,
    I7CorrectnessInputIdentity inputIdentity,
    std::span<const std::uint32_t> expectedOutput)
{
    ValidateI7FoundationInputs(seriesIdentity, inputIdentity);
    if (expectedOutput.size() != ExpectedWordCount(
            seriesIdentity.condition.workload))
        throw std::invalid_argument(
            "I7 expected word output length disagrees with the workload");
    auto plan = MakeCorrectnessPlan(std::move(runId), std::move(seriesIdentity));
    return {std::move(plan), std::move(inputIdentity),
        WordInputSha256(expectedOutput), true};
}

I7CorrectnessFoundation MakeI7ByteCorrectnessFoundation(
    std::string runId,
    SeriesIdentityContext seriesIdentity,
    I7CorrectnessInputIdentity inputIdentity,
    std::span<const std::uint8_t> expectedOutput)
{
    ValidateI7FoundationInputs(seriesIdentity, inputIdentity);
    const auto* transfer = std::get_if<TransferConfiguration>(
        &seriesIdentity.condition.workload.parameters);
    if (transfer == nullptr)
        throw std::invalid_argument(
            "I7 byte expected output is applicable only to E");
    if (expectedOutput.size() != transfer->byteCount)
        throw std::invalid_argument(
            "I7 expected byte output length disagrees with the workload");
    auto plan = MakeCorrectnessPlan(std::move(runId), std::move(seriesIdentity));
    return {std::move(plan), std::move(inputIdentity),
        ByteInputSha256(expectedOutput), false};
}

EnvironmentRecord MakeI7EnvironmentRecord(
    results::EnvironmentRecord common,
    const I7CorrectnessFoundation& foundation,
    BackendDiagnostics diagnostics)
{
    return MakeEnvironment(std::move(common), foundation,
        foundation.ExpectedOutputSha256(), std::move(diagnostics));
}

InitializationRecord MakeI7SetupCompleteInitialization(
    const I7CorrectnessFoundation& foundation,
    std::string observation)
{
    if (observation.empty())
        throw std::invalid_argument(
            "I7 setup_complete observation must describe actual setup");
    const auto labels = WorkloadLabels(
        foundation.Plan().seriesIdentity.condition.workload);
    InitializationRecord result{
        foundation.Plan().runId,
        foundation.Plan().seriesIdentity.backend,
        foundation.Plan().seriesIdentity.processIndex,
        0U,
        "backend_setup",
        std::move(labels.workload),
        std::move(labels.variant),
        labels.elementCount,
        "setup_complete",
        std::nullopt,
        std::move(observation)};
    ValidateInitializationRecords(
        foundation.Plan(), std::span<const InitializationRecord>{&result, 1U});
    return result;
}

SampleRecord MakeI7ComparedWordSample(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    std::span<const std::uint32_t> expected,
    std::span<const std::uint32_t> observed)
{
    if (!foundation.ExpectedOutputUsesWords()
        || WordInputSha256(expected) != foundation.ExpectedOutputSha256())
    {
        throw std::invalid_argument(
            "I7 compared word output disagrees with the declared expected output");
    }
    return ComparedSample(foundation, sampleIndex, expected, observed);
}

SampleRecord MakeI7ComparedByteSample(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    std::span<const std::uint8_t> expected,
    std::span<const std::uint8_t> observed)
{
    if (foundation.ExpectedOutputUsesWords()
        || ByteInputSha256(expected) != foundation.ExpectedOutputSha256())
    {
        throw std::invalid_argument(
            "I7 compared byte output disagrees with the declared expected output");
    }
    return ComparedSample(foundation, sampleIndex, expected, observed);
}

I7FailureObservation MakeI7FailureObservation(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    I7FailureKind failure,
    std::optional<std::string> nativeDetail)
{
    if (nativeDetail.has_value()
        && (nativeDetail->empty() || nativeDetail->size() > 512U))
    {
        throw std::invalid_argument(
            "I7 native failure detail must be nonempty and bounded");
    }
    const auto mapping = MapFailure(failure);
    auto sample = MakeSampleRecord(foundation.Plan(), sampleIndex);
    sample.correctness = mapping.progress;
    sample.status = mapping.status;
    sample.failurePhase = mapping.phase;
    sample.errorCode = std::string(mapping.errorCode);
    ValidateSampleRecord(sample);
    return {std::move(sample), std::move(nativeDetail)};
}

} // namespace computelab::ex2::evidence
