#include "ex2/Ex2Configuration.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Evidence.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace evidence = computelab::ex2::evidence;

constexpr std::string_view kSyntheticUuid =
    "00112233-4455-6677-8899-aabbccddeeff";

bool FullWordComparison(
    std::span<const std::uint32_t> expected,
    std::span<const std::uint32_t> observed)
{
    return expected.size() == observed.size()
        && std::equal(expected.begin(), expected.end(), observed.begin());
}

bool FullByteComparison(
    std::span<const std::uint8_t> expected,
    std::span<const std::uint8_t> observed)
{
    return expected.size() == observed.size()
        && std::equal(expected.begin(), expected.end(), observed.begin());
}

ex2::ComparisonConditionContext SyntheticCondition(
    ex2::WorkloadConfiguration workload)
{
    // This externallyVerified flag is a schema fixture only. It does not claim
    // that this CPU-only test queried or verified physical hardware.
    return {
        "1.0", "i2e-synthetic-machine",
        {std::string(kSyntheticUuid), true}, std::move(workload),
        ex2::InstrumentMode::H};
}

ex2::SeriesIdentityContext SyntheticSeries(
    ex2::ComparisonConditionContext condition,
    ex2::Backend backend = ex2::Backend::Cuda)
{
    return {
        std::move(condition), backend, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'),
        backend == ex2::Backend::Vulkan
            ? std::optional<std::string>{std::string(64U, 'c')}
            : std::nullopt};
}

evidence::CorrectnessPlan SyntheticPlan(
    ex2::WorkloadConfiguration workload,
    ex2::Backend backend = ex2::Backend::Cuda,
    std::uint64_t plannedSampleCount = 1U)
{
    auto series = SyntheticSeries(SyntheticCondition(std::move(workload)), backend);
    series.plannedSampleCount = plannedSampleCount;
    return evidence::MakeCorrectnessPlan(
        "i2e-synthetic-correctness", std::move(series));
}

struct WorkloadLabels
{
    std::string workload;
    std::string variant;
    std::optional<std::uint64_t> elementCount;
};

WorkloadLabels Labels(const ex2::WorkloadConfiguration& configuration)
{
    return std::visit([](const auto& value) -> WorkloadLabels {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ex2::LinearConfiguration>)
            return {"A", std::string(ex2::ToString(value.variant)), value.elementCount};
        else if constexpr (std::is_same_v<T, ex2::IndexedConfiguration>)
            return {"B", std::string(ex2::ToString(value.variant)), value.elementCount};
        else if constexpr (std::is_same_v<T, ex2::ContentionConfiguration>)
            return {"C", "C", value.elementCount};
        else if constexpr (std::is_same_v<T, ex2::IterativeConfiguration>)
            return {"D", std::string(ex2::ToString(value.variant)), value.elementCount};
        else
            return {"E", std::string(ex2::ToString(value.variant)), std::nullopt};
    }, configuration.parameters);
}

computelab::results::EnvironmentRecord SyntheticCommonEnvironment(
    const evidence::CorrectnessPlan& plan)
{
    const auto& series = plan.seriesIdentity;
    return {
        evidence::SchemaVersion, std::string(evidence::ExperimentId),
        plan.runId, "2026-09-21T00:00:00Z", series.sourceRevision, true,
        series.condition.machineId, "Windows", "11", "Synthetic CPU fixture",
        std::numeric_limits<std::uint64_t>::max(), "Synthetic GPU fixture",
        "NVIDIA", "10DE:0000", 8'589'934'592ULL, "fixture-driver",
        "fixture-toolkit", "fixture-runtime", "fixture-capability",
        "fixture-sdk", "fixture-api", "MSVC", "fixture-compiler",
        "fixture-cmake", "fixture-ninja", "x64-debug", "Debug", true,
        false};
}

evidence::EnvironmentRecord SyntheticEnvironment(
    const evidence::CorrectnessPlan& plan,
    std::string inputSha256,
    std::string expectedOutputSha256)
{
    return {
        SyntheticCommonEnvironment(plan), plan, std::move(inputSha256),
        std::move(expectedOutputSha256),
        {.implementation = "synthetic-cpu-only-fixture"}};
}

evidence::InitializationRecord SyntheticInitialization(
    const evidence::CorrectnessPlan& plan)
{
    const auto labels = Labels(plan.seriesIdentity.condition.workload);
    return {
        plan.runId, plan.seriesIdentity.backend,
        plan.seriesIdentity.processIndex, 0U, "fixture_setup",
        labels.workload, labels.variant, labels.elementCount,
        "setup_complete", std::nullopt,
        "cpu-only fixture; no hardware operation"};
}

evidence::SampleRecord PassingSample(
    const evidence::CorrectnessPlan& plan,
    std::uint64_t sampleIndex = 0U)
{
    auto sample = evidence::MakeSampleRecord(plan, sampleIndex);
    sample.correctness = {true, true, true, true, true};
    sample.status = evidence::OperationStatus::Ok;
    return sample;
}

evidence::SampleRecord MismatchSample(
    const evidence::CorrectnessPlan& plan,
    std::uint64_t sampleIndex = 0U)
{
    auto sample = evidence::MakeSampleRecord(plan, sampleIndex);
    sample.correctness = {true, true, true, true, false};
    sample.status = evidence::OperationStatus::ValidationFailed;
    sample.failurePhase = evidence::FailurePhase::Validation;
    sample.errorCode = std::string(evidence::error_code::OutputMismatch);
    return sample;
}

void ExpectEvidenceRoundTrip(
    const ex2::WorkloadConfiguration& configuration,
    const std::string& inputSha256,
    const std::string& expectedOutputSha256,
    bool comparisonPassed)
{
    const auto plan = SyntheticPlan(configuration);
    const auto environment = SyntheticEnvironment(
        plan, inputSha256, expectedOutputSha256);
    const std::vector initialization{SyntheticInitialization(plan)};
    auto sample = evidence::MakeSampleRecord(plan, 0U);
    sample.correctness = {true, true, true, true, comparisonPassed};
    sample.status = comparisonPassed
        ? evidence::OperationStatus::Ok
        : evidence::OperationStatus::ValidationFailed;
    if (!comparisonPassed)
    {
        sample.failurePhase = evidence::FailurePhase::Validation;
        sample.errorCode = std::string(evidence::error_code::OutputMismatch);
    }
    const std::vector samples{sample};
    EXPECT_TRUE(comparisonPassed);
    const auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Ok);

    EXPECT_NO_THROW(evidence::ValidateEvidenceBundle(
        environment, initialization, samples, summary));
    EXPECT_EQ(evidence::SerializeEnvironmentJson(environment),
        evidence::SerializeEnvironmentJson(environment));
    EXPECT_EQ(evidence::SerializeInitializationCsv(plan, initialization),
        evidence::SerializeInitializationCsv(plan, initialization));
    EXPECT_EQ(evidence::SerializeSamplesCsv(plan, samples),
        evidence::SerializeSamplesCsv(plan, samples));
    EXPECT_EQ(evidence::SerializeSummaryJson(summary, samples),
        evidence::SerializeSummaryJson(summary, samples));
    EXPECT_EQ(summary.recordedSampleCount, 1U);
    EXPECT_EQ(summary.validationFailures, 0U);
    EXPECT_EQ(summary.failedSampleCount, 0U);
    EXPECT_FALSE(samples.front().hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(samples.front().hostWaitNanoseconds.has_value());
    EXPECT_FALSE(samples.front().hostCompletionNanoseconds.has_value());
    EXPECT_FALSE(samples.front().nativeDeviceIntervalNanoseconds.has_value());
}

TEST(Ex2FoundationIntegration, A1AndA2FlowFromConfigurationThroughEvidence)
{
    const std::vector<std::uint32_t> literalInput{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> literalA1{
        0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U};
    const std::vector<std::uint32_t> literalA2{
        0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U};

    for (const auto variant : {ex2::LinearVariant::A1, ex2::LinearVariant::A2})
    {
        for (const std::uint64_t elementCount : {0ULL, 1ULL, 4ULL, 257ULL})
        {
            const auto configuration = ex2::MakeConfiguration(
                ex2::LinearConfiguration{variant, elementCount});
            EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
            EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
            EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
                ex2::CellEligibility::CorrectnessOnly);

            const auto input = ex2::GenerateWordInput(
                ex2::CoreInputSeed, elementCount);
            const auto originalInput = input;
            const auto expected = variant == ex2::LinearVariant::A1
                ? ex2::ReferenceA1(input) : ex2::ReferenceA2(input);
            auto observed = expected;
            const bool comparisonPassed = FullWordComparison(expected, observed);

            EXPECT_EQ(input, originalInput);
            EXPECT_TRUE(comparisonPassed);
            if (!expected.empty())
                EXPECT_NE(expected.data(), observed.data());
            EXPECT_EQ(expected, variant == ex2::LinearVariant::A1
                ? ex2::ReferenceA1(input) : ex2::ReferenceA2(input));

            if (elementCount == 4U)
            {
                EXPECT_EQ(input, literalInput);
                EXPECT_EQ(expected,
                    variant == ex2::LinearVariant::A1 ? literalA1 : literalA2);
                observed.back() ^= 1U;
                EXPECT_FALSE(FullWordComparison(expected, observed));
                EXPECT_FALSE(FullWordComparison(expected,
                    std::span<const std::uint32_t>{expected.data(),
                        expected.size() - 1U}));
            }

            ExpectEvidenceRoundTrip(configuration, ex2::WordInputSha256(input),
                ex2::WordInputSha256(expected), comparisonPassed);
        }
    }
}

TEST(Ex2FoundationIntegration, B1AndB2IntegrateBothPermutationPatterns)
{
    const std::vector<std::uint32_t> structuredGather{
        0x5D43B3A3U, 0x3AB8D327U, 0x0F1AB743U, 0xA5DBB31EU};
    const std::vector<std::uint32_t> structuredScatter{
        0x5D43B3A0U, 0x3AB8D324U, 0x0F1AB744U, 0xA5DBB319U};

    for (const auto pattern
        : {ex2::IndexPattern::StructuredV1, ex2::IndexPattern::ShuffledV1})
    {
        for (const auto variant : {ex2::IndexedVariant::B1, ex2::IndexedVariant::B2})
        {
            for (const std::uint64_t elementCount : {4ULL, 257ULL})
            {
                const auto configuration = ex2::MakeConfiguration(
                    ex2::IndexedConfiguration{variant, elementCount, pattern});
                EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
                EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
                EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
                    ex2::CellEligibility::CorrectnessOnly);

                const auto input = ex2::GenerateWordInput(
                    ex2::CoreInputSeed, elementCount);
                const auto indices = pattern == ex2::IndexPattern::StructuredV1
                    ? ex2::GenerateStructuredPermutation(elementCount)
                    : ex2::GenerateShuffledPermutation(
                        ex2::CoreInputSeed, elementCount);
                const auto originalInput = input;
                const auto originalIndices = indices;
                EXPECT_NO_THROW(ex2::ValidateIndexPermutation(
                    indices, elementCount));

                auto sorted = indices;
                std::sort(sorted.begin(), sorted.end());
                for (std::size_t index = 0U; index < sorted.size(); ++index)
                    EXPECT_EQ(sorted[index], index);

                const auto expected = variant == ex2::IndexedVariant::B1
                    ? ex2::ReferenceB1Gather(input, indices)
                    : ex2::ReferenceB2Scatter(input, indices);
                auto observed = expected;
                const bool comparisonPassed = FullWordComparison(expected, observed);
                EXPECT_TRUE(comparisonPassed);
                EXPECT_EQ(input, originalInput);
                EXPECT_EQ(indices, originalIndices);

                if (elementCount == 4U
                    && pattern == ex2::IndexPattern::StructuredV1)
                {
                    EXPECT_EQ(indices,
                        (std::vector<std::uint32_t>{1U, 0U, 3U, 2U}));
                    EXPECT_EQ(expected, variant == ex2::IndexedVariant::B1
                        ? structuredGather : structuredScatter);
                    observed.front() ^= 1U;
                    EXPECT_FALSE(FullWordComparison(expected, observed));
                }

                // Schema v2 has no approved aggregate B encoding. The evidence
                // fixture uses the primary word buffer; the index buffer is
                // independently anchored below rather than silently appended.
                ExpectEvidenceRoundTrip(configuration,
                    ex2::WordInputSha256(input),
                    ex2::WordInputSha256(expected), comparisonPassed);
            }
        }
    }
}

TEST(Ex2FoundationIntegration, CIntegratesAllThreeApprovedContentionLevels)
{
    constexpr std::uint64_t elementCount = 128U;
    for (const std::uint64_t activeCounterCount
        : {elementCount, elementCount / 32U, 64ULL})
    {
        const auto configuration = ex2::MakeConfiguration(
            ex2::ContentionConfiguration{
                elementCount, activeCounterCount, elementCount});
        EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
        EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
        EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
            ex2::CellEligibility::CorrectnessOnly);

        const std::vector<std::uint32_t> resetState(elementCount, 0U);
        const auto originalResetState = resetState;
        const auto reference = ex2::ReferenceContention(
            elementCount, activeCounterCount);
        auto observed = reference.counters;
        const bool comparisonPassed = FullWordComparison(
            reference.counters, observed)
            && ex2::ValidateContentionResult(reference, observed);

        EXPECT_EQ(reference.targets, ex2::GenerateContentionTargets(
            elementCount, activeCounterCount));
        EXPECT_EQ(reference.targets.size(), elementCount);
        EXPECT_EQ(reference.counters.size(), elementCount);
        EXPECT_EQ(reference.activeCounterCount, activeCounterCount);
        EXPECT_EQ(resetState, originalResetState);
        EXPECT_TRUE(comparisonPassed);
        EXPECT_EQ(std::accumulate(observed.begin(), observed.end(), 0ULL),
            elementCount);
        for (std::size_t index = static_cast<std::size_t>(activeCounterCount);
            index < observed.size(); ++index)
        {
            EXPECT_EQ(observed[index], 0U);
        }

        auto altered = observed;
        ++altered.front();
        EXPECT_FALSE(ex2::ValidateContentionResult(reference, altered));
        if (activeCounterCount < elementCount)
        {
            altered = observed;
            altered[static_cast<std::size_t>(activeCounterCount)] = 1U;
            EXPECT_FALSE(ex2::ValidateContentionResult(reference, altered));
        }
        EXPECT_FALSE(ex2::ValidateContentionResult(reference,
            std::span<const std::uint32_t>{observed.data(),
                observed.size() - 1U}));

        // Schema v2 has no approved aggregate C encoding. The evidence fixture
        // uses targets; reset-counter bytes are independently anchored below.
        ExpectEvidenceRoundTrip(configuration,
            ex2::WordInputSha256(reference.targets),
            ex2::WordInputSha256(reference.counters), comparisonPassed);
    }

    const auto emptyConfiguration = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{0U, 0U, 0U});
    const auto emptyReference = ex2::ReferenceContention(0U, 0U);
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(emptyConfiguration).IsValid());
    EXPECT_TRUE(ex2::ValidateContentionResult(emptyReference, {}));
}

TEST(Ex2FoundationIntegration, D1IntegratesIterationBoundariesAndBufferParity)
{
    struct IterationCase
    {
        std::uint64_t elementCount;
        std::uint64_t iterationCount;
    };
    const std::array cases{
        IterationCase{4U, 0U}, IterationCase{4U, 1U},
        IterationCase{4U, 2U}, IterationCase{1U, 16U},
        IterationCase{1U, 64U}};

    for (const auto& item : cases)
    {
        const auto configuration = ex2::MakeConfiguration(
            ex2::IterativeConfiguration{
                ex2::IterativeVariant::D1,
                item.elementCount, item.iterationCount});
        EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
        EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
        EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
            ex2::CellEligibility::CorrectnessOnly);

        const auto initialState = ex2::GenerateWordInput(
            ex2::CoreInputSeed, item.elementCount);
        const auto originalInitialState = initialState;
        const auto reference = ex2::ReferenceD1(
            initialState, item.iterationCount);
        const auto regenerated = ex2::ReferenceD1(
            ex2::CoreInputSeed, item.elementCount, item.iterationCount);
        auto observed = reference.finalState;
        const bool comparisonPassed = FullWordComparison(
            reference.finalState, observed);

        EXPECT_EQ(initialState, originalInitialState);
        EXPECT_EQ(reference.finalState, regenerated.finalState);
        EXPECT_EQ(reference.finalBuffer, item.iterationCount % 2U == 0U
            ? ex2::IterativeFinalBuffer::StateA
            : ex2::IterativeFinalBuffer::StateB);
        EXPECT_TRUE(comparisonPassed);

        if (item.elementCount == 4U && item.iterationCount == 1U)
        {
            EXPECT_EQ(reference.finalState,
                (std::vector<std::uint32_t>{
                    0xD664E09CU, 0x27C0F020U, 0x3AC0DF49U, 0x62A16496U}));
        }
        if (item.elementCount == 4U && item.iterationCount == 2U)
        {
            EXPECT_EQ(reference.finalState,
                (std::vector<std::uint32_t>{
                    0x89BDA0DEU, 0xBE3BAF8CU, 0x1E3F5AC9U, 0x120E2194U}));
        }
        if (item.elementCount == 1U && item.iterationCount == 16U)
            EXPECT_EQ(reference.finalState.front(), 0x8B2A8389U);
        if (item.elementCount == 1U && item.iterationCount == 64U)
            EXPECT_EQ(reference.finalState.front(), 0xB53390FEU);

        if (!observed.empty())
        {
            observed.front() ^= 1U;
            EXPECT_FALSE(FullWordComparison(reference.finalState, observed));
        }
        ExpectEvidenceRoundTrip(configuration,
            ex2::WordInputSha256(initialState),
            ex2::WordInputSha256(reference.finalState), comparisonPassed);
    }

    const auto first = ex2::ReferenceD1(ex2::CoreInputSeed, 4U, 2U);
    static_cast<void>(ex2::ReferenceD1(ex2::CoreInputSeed, 257U, 1U));
    const auto second = ex2::ReferenceD1(ex2::CoreInputSeed, 4U, 2U);
    EXPECT_EQ(first.finalState, second.finalState);
}

TEST(Ex2FoundationIntegration, E1AndE2IntegrateDirectionAndByteExactValidation)
{
    struct TransferCase
    {
        ex2::TransferVariant variant;
        ex2::TransferDirection direction;
    };
    const std::array cases{
        TransferCase{ex2::TransferVariant::E1,
            ex2::TransferDirection::HostToDevice},
        TransferCase{ex2::TransferVariant::E2,
            ex2::TransferDirection::DeviceToHost}};

    for (const auto& item : cases)
    {
        for (const std::uint64_t byteCount : {0ULL, 4ULL, 257ULL})
        {
            const auto configuration = ex2::MakeConfiguration(
                ex2::TransferConfiguration{
                    item.variant, byteCount, item.direction});
            EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
            EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
            EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
                ex2::CellEligibility::CorrectnessOnly);

            const auto reference = ex2::ReferenceTransfer(
                ex2::CoreInputSeed, byteCount, item.direction);
            const auto originalSource = reference.source;
            auto observed = reference.expectedDestination;
            const bool comparisonPassed = FullByteComparison(
                reference.expectedDestination, observed)
                && ex2::ValidateTransferOutput(reference, observed);
            EXPECT_EQ(reference.direction, item.direction);
            EXPECT_TRUE(comparisonPassed);
            EXPECT_EQ(reference.source, originalSource);
            if (!observed.empty())
                EXPECT_NE(reference.expectedDestination.data(), observed.data());

            if (byteCount == 4U)
            {
                EXPECT_EQ(reference.expectedDestination,
                    (std::vector<std::uint8_t>{0x9DU, 0x1AU, 0xA2U, 0xF8U}));
                observed.back() ^= 1U;
                EXPECT_FALSE(ex2::ValidateTransferOutput(reference, observed));
                EXPECT_FALSE(ex2::ValidateTransferOutput(reference,
                    std::span<const std::uint8_t>{
                        reference.expectedDestination.data(),
                        reference.expectedDestination.size() - 1U}));
            }

            ExpectEvidenceRoundTrip(configuration,
                ex2::ByteInputSha256(reference.source),
                ex2::ByteInputSha256(reference.expectedDestination),
                comparisonPassed);
        }
    }
}

TEST(Ex2FoundationIntegration, MultiBufferInputsHaveSeparateAnchoredBytesAndDigests)
{
    const auto primaryInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 4U);
    const auto structured = ex2::GenerateStructuredPermutation(4U);
    const auto shuffled = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 4U);
    const auto contention = ex2::ReferenceContention(8U, 2U);
    const std::vector<std::uint32_t> resetCounters(8U, 0U);

    EXPECT_EQ(ex2::WordInputSha256(primaryInput),
        "e91b6862138c2e366b1976eaa23c1685054eb3b0636936dc4b572f3881585bc9");
    EXPECT_EQ(ex2::WordInputSha256(structured),
        "e21c0819cefe5e44999165dd7dfba0274cd8118d67859032fca6be7ed3b249d5");
    EXPECT_EQ(ex2::WordInputSha256(shuffled),
        "2a8f66c90dfb6bf0c8e7ed10c69f33ba5fddb56f597e7f40dfa61ddca3241797");
    EXPECT_EQ(ex2::WordInputSha256(contention.targets),
        "5e4ffd7d196e150659e625599778aa9c37538bb184fce71b42b1d5b339f2aeec");
    EXPECT_EQ(ex2::WordInputSha256(resetCounters),
        "66687aadf862bd776c8fc18b8e9f8e20089714856ee233b3902a591d0d5f2925");
    EXPECT_EQ(ex2::WordInputSha256(contention.counters),
        "ab99f1d6520dd996faceba083609eb56c02d53615f851b82c1558cf9cf0e7d22");

    EXPECT_EQ(ex2::EncodeWordInputLittleEndian(structured),
        (std::vector<std::uint8_t>{
            1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
            3U, 0U, 0U, 0U, 2U, 0U, 0U, 0U}));
    EXPECT_EQ(ex2::GenerateWordInput(ex2::CoreInputSeed, 4U), primaryInput);
    EXPECT_EQ(ex2::ReferenceD1(ex2::CoreInputSeed, 4U, 0U).finalState,
        primaryInput);

    const auto h2d = ex2::ReferenceTransfer(ex2::CoreInputSeed, 4U,
        ex2::TransferDirection::HostToDevice);
    const auto d2h = ex2::ReferenceTransfer(ex2::CoreInputSeed, 4U,
        ex2::TransferDirection::DeviceToHost);
    EXPECT_EQ(h2d.source, d2h.source);
    EXPECT_EQ(ex2::ByteInputSha256(h2d.source),
        "d09d1a6a9155c673b854b703ab7bed8b1eb5629a8b6309b041c84ec0643e8a21");
}

TEST(Ex2FoundationIntegration, ObservationResultsDriveCorrectnessEvidence)
{
    const auto configuration = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 4U});
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 4U);
    const auto expected = ex2::ReferenceA1(input);
    auto passingObservation = expected;
    auto failingObservation = expected;
    failingObservation[2] ^= 1U;

    const auto plan = SyntheticPlan(configuration, ex2::Backend::Cuda, 2U);
    auto passing = PassingSample(plan, 0U);
    passing.correctness.validationPassed =
        FullWordComparison(expected, passingObservation);
    auto mismatch = MismatchSample(plan, 1U);
    mismatch.correctness.validationPassed =
        FullWordComparison(expected, failingObservation);
    const std::vector samples{passing, mismatch};

    EXPECT_TRUE(passing.correctness.validationPassed.value());
    EXPECT_FALSE(mismatch.correctness.validationPassed.value());
    EXPECT_NO_THROW(evidence::ValidateSamples(plan, samples));
    const auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::ValidationFailed,
        evidence::FailurePhase::Validation,
        std::string(evidence::error_code::OutputMismatch));
    EXPECT_EQ(summary.recordedSampleCount, 2U);
    EXPECT_EQ(summary.validationFailures, 1U);
    EXPECT_EQ(summary.failedSampleCount, 1U);

    auto disguisedMismatch = mismatch;
    disguisedMismatch.status = evidence::OperationStatus::Ok;
    disguisedMismatch.failurePhase.reset();
    disguisedMismatch.errorCode.reset();
    EXPECT_THROW(evidence::ValidateSampleRecord(disguisedMismatch),
        std::invalid_argument);

    auto wrongLength = expected;
    wrongLength.pop_back();
    EXPECT_FALSE(FullWordComparison(expected, wrongLength));
}

TEST(Ex2FoundationIntegration, InvalidConfigurationsStopBeforeThePurePath)
{
    std::vector<ex2::WorkloadConfiguration> invalid;

    auto unsupportedGenerator = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 4U});
    unsupportedGenerator.common.generatorRevision = "unsupported";
    invalid.push_back(unsupportedGenerator);
    invalid.push_back(ex2::MakeConfiguration(ex2::LinearConfiguration{
        static_cast<ex2::LinearVariant>(99), 4U}));
    invalid.push_back(ex2::MakeConfiguration(ex2::IndexedConfiguration{
        ex2::IndexedVariant::B1, 4U, static_cast<ex2::IndexPattern>(99)}));
    invalid.push_back(ex2::MakeConfiguration(ex2::ContentionConfiguration{
        128U, 3U, 128U}));
    invalid.push_back(ex2::MakeConfiguration(ex2::ContentionConfiguration{
        128U, 64U, 127U}));
    invalid.push_back(ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1, 1U,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) + 1U}));
    invalid.push_back(ex2::MakeConfiguration(ex2::TransferConfiguration{
        ex2::TransferVariant::E1, 4U,
        ex2::TransferDirection::DeviceToHost}));

    auto wrongMode = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 4U});
    wrongMode.common.executionMode = ex2::LogicalExecutionMode::Prepared;
    invalid.push_back(wrongMode);
    auto wrongBoundary = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 4U});
    wrongBoundary.common.operationBoundary =
        ex2::OperationBoundary::PreparedSingleCopyCompletion;
    invalid.push_back(wrongBoundary);

    for (const auto& configuration : invalid)
    {
        EXPECT_FALSE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
        EXPECT_FALSE(ex2::IsCorrectnessTestEligible(configuration));
        EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
            ex2::CellEligibility::InvalidSemanticConfiguration);
    }

    const auto d2 = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D2, 4U, 1U});
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(d2).IsValid());
    EXPECT_FALSE(ex2::IsCorrectnessTestEligible(d2));
    EXPECT_EQ(ex2::ClassifyCellEligibility(d2),
        ex2::CellEligibility::ConditionalExtension);
    EXPECT_THROW(static_cast<void>(SyntheticPlan(d2)), std::invalid_argument);

    EXPECT_THROW(static_cast<void>(ex2::GenerateStructuredPermutation(8191U)),
        std::invalid_argument);
    EXPECT_THROW(ex2::ValidateIndexPermutation(
        (std::array<std::uint32_t, 2>{0U, 0U}), 2U), std::invalid_argument);
    EXPECT_THROW(ex2::ValidateIndexPermutation(
        (std::array<std::uint32_t, 2>{0U, 2U}), 2U), std::invalid_argument);
    EXPECT_THROW(ex2::ValidateUint32IndexedOracleElementCount(
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) + 1U),
        std::invalid_argument);
}

TEST(Ex2FoundationIntegration, ConditionAndSeriesIdentityStaySeparated)
{
    const auto configuration = ex2::MakeConfiguration(
        ex2::IndexedConfiguration{
            ex2::IndexedVariant::B2, 257U, ex2::IndexPattern::ShuffledV1});
    const auto condition = SyntheticCondition(configuration);
    const auto repeatedCondition = SyntheticCondition(configuration);
    const auto conditionJson = ex2::ComparisonConditionCanonicalJson(condition);
    const auto conditionId = ex2::ComparisonConditionId(condition);

    EXPECT_EQ(conditionJson,
        ex2::ComparisonConditionCanonicalJson(repeatedCondition));
    EXPECT_EQ(conditionId, ex2::ComparisonConditionId(repeatedCondition));
    EXPECT_EQ(conditionId.size(), 64U);

    auto changedCondition = condition;
    std::get<ex2::IndexedConfiguration>(
        changedCondition.workload.parameters).elementCount = 4U;
    EXPECT_NE(ex2::ComparisonConditionId(changedCondition), conditionId);

    const auto cuda = SyntheticSeries(condition, ex2::Backend::Cuda);
    const auto vulkan = SyntheticSeries(condition, ex2::Backend::Vulkan);
    EXPECT_EQ(ex2::ComparisonConditionId(cuda.condition),
        ex2::ComparisonConditionId(vulkan.condition));
    EXPECT_NE(ex2::SeriesId(cuda), ex2::SeriesId(vulkan));
    EXPECT_EQ(ex2::SeriesId(cuda).size(), 64U);
    EXPECT_EQ(ex2::SeriesId(vulkan).size(), 64U);

    auto stalePlan = evidence::MakeCorrectnessPlan(
        "i2e-synthetic-correctness", cuda);
    std::get<ex2::IndexedConfiguration>(
        stalePlan.seriesIdentity.condition.workload.parameters).elementCount = 4U;
    EXPECT_THROW(evidence::ValidateCorrectnessPlan(stalePlan),
        std::invalid_argument);

    auto invalidHash = cuda;
    invalidHash.executableSha256[0] = 'G';
    EXPECT_THROW(static_cast<void>(ex2::SeriesId(invalidHash)),
        std::invalid_argument);
}

TEST(Ex2FoundationIntegration, FailureAndIncompleteEvidenceCannotBecomeSuccess)
{
    const auto configuration = ex2::MakeConfiguration(
        ex2::TransferConfiguration{
            ex2::TransferVariant::E2, 4U,
            ex2::TransferDirection::DeviceToHost});
    const auto plan = SyntheticPlan(configuration, ex2::Backend::Cuda, 2U);

    auto missingExpected = evidence::MakeSampleRecord(plan, 0U);
    missingExpected.correctness.operationCompleted = true;
    missingExpected.status = evidence::OperationStatus::Incomplete;
    missingExpected.failurePhase = evidence::FailurePhase::Readback;
    missingExpected.errorCode = std::string(evidence::error_code::ReadbackFailed);
    EXPECT_THROW(evidence::ValidateSampleRecord(missingExpected),
        std::invalid_argument);

    auto completionFailure = evidence::MakeSampleRecord(plan, 0U);
    completionFailure.correctness.expectedOutputGenerated = true;
    completionFailure.status = evidence::OperationStatus::WaitFailed;
    completionFailure.failurePhase = evidence::FailurePhase::CompletionWait;
    completionFailure.errorCode =
        std::string(evidence::error_code::CompletionFailed);
    EXPECT_NO_THROW(evidence::ValidateSampleRecord(completionFailure));

    auto readbackFailure = evidence::MakeSampleRecord(plan, 0U);
    readbackFailure.correctness = {true, true, false, false, std::nullopt};
    readbackFailure.status = evidence::OperationStatus::Incomplete;
    readbackFailure.failurePhase = evidence::FailurePhase::Readback;
    readbackFailure.errorCode = std::string(evidence::error_code::ReadbackFailed);
    EXPECT_NO_THROW(evidence::ValidateSampleRecord(readbackFailure));

    auto interrupted = evidence::MakeSampleRecord(plan, 0U);
    interrupted.correctness = {true, true, true, false, std::nullopt};
    interrupted.status = evidence::OperationStatus::Incomplete;
    interrupted.failurePhase = evidence::FailurePhase::Interrupted;
    interrupted.errorCode = std::string(evidence::error_code::Interrupted);
    EXPECT_NO_THROW(evidence::ValidateSampleRecord(interrupted));

    const std::vector onePassingSample{PassingSample(plan, 0U)};
    EXPECT_THROW(static_cast<void>(evidence::SummarizeSamples(
        plan, onePassingSample, evidence::OperationStatus::Ok)),
        std::invalid_argument);
    const auto incompleteSummary = evidence::SummarizeSamples(
        plan, onePassingSample, evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::Interrupted,
        std::string(evidence::error_code::Interrupted));
    EXPECT_EQ(incompleteSummary.recordedSampleCount, 1U);

    auto duplicate = PassingSample(plan, 0U);
    const std::vector duplicateSamples{PassingSample(plan, 0U), duplicate};
    EXPECT_THROW(evidence::ValidateSamples(plan, duplicateSamples),
        std::invalid_argument);

    auto invalidPhase = interrupted;
    invalidPhase.failurePhase = static_cast<evidence::FailurePhase>(999);
    EXPECT_THROW(evidence::ValidateSampleRecord(invalidPhase),
        std::invalid_argument);
}

TEST(Ex2FoundationIntegration, FrozenInventoryAndRunPlanGuardsRemainIntact)
{
    const auto& coreCells = ex2::ApprovedCoreCells();
    EXPECT_EQ(coreCells.size(), 22U);
    EXPECT_TRUE(std::all_of(coreCells.begin(), coreCells.end(),
        [](const ex2::WorkloadConfiguration& cell) {
            return ex2::ClassifyCellEligibility(cell)
                == ex2::CellEligibility::ApprovedCoreCell;
        }));

    const std::filesystem::path root = "results/local";
    const std::vector<ex2::PlannedRun> duplicateRuns{
        {"run-a", root / "run-a"}, {"run-a", root / "run-a"}};
    EXPECT_FALSE(ex2::ValidateRunPlan(duplicateRuns, root).IsValid());

    const std::vector<ex2::PlannedRun> collidingDestinations{
        {"run-a", root / "run-a"}, {"RUN-A", root / "RUN-A"}};
    EXPECT_FALSE(ex2::ValidateRunPlan(
        collidingDestinations, root).IsValid());
}

} // namespace
