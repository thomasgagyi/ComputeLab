#include "ex2/Ex2Configuration.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
namespace ex2 = computelab::ex2;

constexpr std::string_view kUuid =
    "00112233-4455-6677-8899-aabbccddeeff";

bool Contains(
    const ex2::ConfigurationValidation& validation,
    ex2::ConfigurationError error)
{
    return std::find(validation.errors.begin(), validation.errors.end(), error)
        != validation.errors.end();
}

bool Contains(
    const ex2::RunPlanValidation& validation,
    ex2::RunPlanError error)
{
    return std::find(validation.errors.begin(), validation.errors.end(), error)
        != validation.errors.end();
}

ex2::ComparisonConditionContext Condition(
    ex2::WorkloadConfiguration workload,
    ex2::InstrumentMode instrumentMode = ex2::InstrumentMode::H)
{
    return {
        "1.0",
        "anonymous-machine",
        {std::string(kUuid), true},
        std::move(workload),
        instrumentMode};
}

ex2::SeriesIdentityContext CudaSeries(
    ex2::ComparisonConditionContext condition)
{
    return {
        std::move(condition), ex2::Backend::Cuda,
        2U, 3U, 1U, 2U, 100U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
}

TEST(Ex2Configuration, RepresentsEveryApprovedFamilyWithTypedParameters)
{
    const std::vector<ex2::WorkloadConfiguration> configurations{
        ex2::MakeConfiguration(ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U}),
        ex2::MakeConfiguration(ex2::LinearConfiguration{ex2::LinearVariant::A2, 1U}),
        ex2::MakeConfiguration(ex2::IndexedConfiguration{
            ex2::IndexedVariant::B1, 1U, ex2::IndexPattern::StructuredV1}),
        ex2::MakeConfiguration(ex2::IndexedConfiguration{
            ex2::IndexedVariant::B2, 1U, ex2::IndexPattern::ShuffledV1}),
        ex2::MakeConfiguration(ex2::ContentionConfiguration{1U, 1U, 1U}),
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D1, 1U, 1U}),
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E1, 1U, ex2::TransferDirection::HostToDevice}),
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E2, 1U, ex2::TransferDirection::DeviceToHost}),
    };

    for (const auto& configuration : configurations)
        EXPECT_TRUE(ex2::ValidateSemanticConfiguration(configuration).IsValid());
}

TEST(Ex2Configuration, FrozenCoreInventoryContainsExactlyTwentyTwoCells)
{
    const auto& cells = ex2::ApprovedCoreCells();
    ASSERT_EQ(cells.size(), 22U);

    std::map<std::string, std::size_t> variants;
    for (const auto& cell : cells)
    {
        EXPECT_EQ(ex2::ClassifyCellEligibility(cell),
            ex2::CellEligibility::ApprovedCoreCell);
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ex2::ContentionConfiguration>)
                ++variants["C"];
            else
                ++variants[std::string(ex2::ToString(value.variant))];
        }, cell.parameters);
    }

    EXPECT_EQ(variants["A1"], 3U);
    EXPECT_EQ(variants["A2"], 2U);
    EXPECT_EQ(variants["B1"], 3U);
    EXPECT_EQ(variants["B2"], 3U);
    EXPECT_EQ(variants["C"], 3U);
    EXPECT_EQ(variants["D1"], 2U);
    EXPECT_EQ(variants["E1"], 3U);
    EXPECT_EQ(variants["E2"], 3U);
}

TEST(Ex2Configuration, FrozenCoreCellsUseExactSpecifiedParameters)
{
    const auto& cells = ex2::ApprovedCoreCells();
    EXPECT_NE(std::find(cells.begin(), cells.end(), ex2::MakeConfiguration(
        ex2::IndexedConfiguration{ex2::IndexedVariant::B2, 262'144U,
            ex2::IndexPattern::StructuredV1})), cells.end());
    EXPECT_NE(std::find(cells.begin(), cells.end(), ex2::MakeConfiguration(
        ex2::ContentionConfiguration{1'048'576U, 32'768U, 1'048'576U})),
        cells.end());
    EXPECT_NE(std::find(cells.begin(), cells.end(), ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1,
            1'048'576U, 64U})), cells.end());
    EXPECT_NE(std::find(cells.begin(), cells.end(), ex2::MakeConfiguration(
        ex2::TransferConfiguration{ex2::TransferVariant::E2, 67'108'864U,
            ex2::TransferDirection::DeviceToHost})), cells.end());
}

TEST(Ex2Configuration, CorrectnessEligibilityDoesNotGrantCoreCellEligibility)
{
    const std::vector<ex2::WorkloadConfiguration> correctnessOnly{
        ex2::MakeConfiguration(ex2::LinearConfiguration{ex2::LinearVariant::A1, 0U}),
        ex2::MakeConfiguration(ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U}),
        ex2::MakeConfiguration(ex2::LinearConfiguration{ex2::LinearVariant::A1, 257U}),
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D1, 257U, 0U}),
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E1, 0U, ex2::TransferDirection::HostToDevice}),
    };

    for (const auto& configuration : correctnessOnly)
    {
        EXPECT_TRUE(ex2::IsCorrectnessTestEligible(configuration));
        EXPECT_EQ(ex2::ClassifyCellEligibility(configuration),
            ex2::CellEligibility::CorrectnessOnly);
    }
}

TEST(Ex2Configuration, ConditionalA1ConfirmationIsNotCoreOrAuthorized)
{
    const auto confirmation = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 33'554'432U});
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(confirmation).IsValid());
    EXPECT_EQ(ex2::ClassifyCellEligibility(confirmation),
        ex2::CellEligibility::ConditionalExtension);
}

TEST(Ex2Configuration, D2IsRepresentableOnlyAsAConditionalPreparedExtension)
{
    auto d2 = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D2, 262'144U, 16U});
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(d2).IsValid());
    EXPECT_FALSE(ex2::IsCorrectnessTestEligible(d2));
    EXPECT_EQ(ex2::ClassifyCellEligibility(d2),
        ex2::CellEligibility::ConditionalExtension);
    EXPECT_EQ(d2.common.executionMode, ex2::LogicalExecutionMode::Prepared);
    EXPECT_EQ(d2.common.operationBoundary,
        ex2::OperationBoundary::PreparedIterationReplayCompletion);

    d2.common.executionMode = ex2::LogicalExecutionMode::Ordinary;
    EXPECT_EQ(ex2::ClassifyCellEligibility(d2),
        ex2::CellEligibility::InvalidSemanticConfiguration);
}

TEST(Ex2Configuration, RejectsInvalidVariantsAndUnrepresentableCounts)
{
    auto invalidVariant = ex2::MakeConfiguration(ex2::LinearConfiguration{
        static_cast<ex2::LinearVariant>(99), 1U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(invalidVariant),
        ex2::ConfigurationError::InvalidVariant));

    auto oversized = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U,
        1U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(oversized),
        ex2::ConfigurationError::UnrepresentableElementCount));

    auto tooManyPasses = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1, 1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(tooManyPasses),
        ex2::ConfigurationError::UnrepresentableIterationCount));
}

TEST(Ex2Configuration, EnforcesStructuredPermutationRequirements)
{
    auto invalidPattern = ex2::MakeConfiguration(ex2::IndexedConfiguration{
        ex2::IndexedVariant::B1, 257U, static_cast<ex2::IndexPattern>(99)});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(invalidPattern),
        ex2::ConfigurationError::InvalidIndexPattern));

    auto notCoprime = ex2::MakeConfiguration(ex2::IndexedConfiguration{
        ex2::IndexedVariant::B1, 8191U, ex2::IndexPattern::StructuredV1});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(notCoprime),
        ex2::ConfigurationError::StructuredPatternNotCoprime));

    auto shuffled = ex2::MakeConfiguration(ex2::IndexedConfiguration{
        ex2::IndexedVariant::B1, 8191U, ex2::IndexPattern::ShuffledV1});
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(shuffled).IsValid());
}

TEST(Ex2Configuration, EnforcesContentionActiveAndAllocatedCounterSemantics)
{
    auto wrongAllocation = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 1U, 256U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(wrongAllocation),
        ex2::ConfigurationError::InvalidAllocatedCounterCount));

    auto zeroActive = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 0U, 257U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(zeroActive),
        ex2::ConfigurationError::InvalidActiveCounterCount));

    auto notDivisible = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 2U, 257U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(notDivisible),
        ex2::ConfigurationError::ContentionCountNotDivisible));

    auto badPermutation = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{8191U, 1U, 8191U});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(badPermutation),
        ex2::ConfigurationError::ContentionPermutationNotCoprime));

    const auto empty = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{0U, 0U, 0U});
    EXPECT_TRUE(ex2::ValidateSemanticConfiguration(empty).IsValid());
}

TEST(Ex2Configuration, EnforcesTransferVariantDirectionAndRepresentability)
{
    auto mismatch = ex2::MakeConfiguration(ex2::TransferConfiguration{
        ex2::TransferVariant::E1, 1U, ex2::TransferDirection::DeviceToHost});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(mismatch),
        ex2::ConfigurationError::VariantDirectionMismatch));

    auto invalidDirection = ex2::MakeConfiguration(ex2::TransferConfiguration{
        ex2::TransferVariant::E2, 1U,
        static_cast<ex2::TransferDirection>(99)});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(invalidDirection),
        ex2::ConfigurationError::InvalidTransferDirection));

    auto oversized = ex2::MakeConfiguration(ex2::TransferConfiguration{
        ex2::TransferVariant::E1, std::numeric_limits<std::uint64_t>::max(),
        ex2::TransferDirection::HostToDevice});
    EXPECT_TRUE(Contains(ex2::ValidateSemanticConfiguration(oversized),
        ex2::ConfigurationError::UnrepresentableByteCount));
}

TEST(Ex2Configuration, RejectsMismatchedLogicalModeBoundaryAndGenerator)
{
    auto configuration = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 257U});
    configuration.common.executionMode = ex2::LogicalExecutionMode::Prepared;
    configuration.common.operationBoundary =
        ex2::OperationBoundary::PreparedSingleCopyCompletion;
    configuration.common.generatorRevision = "unknown";
    const auto validation = ex2::ValidateSemanticConfiguration(configuration);
    EXPECT_TRUE(Contains(validation, ex2::ConfigurationError::InvalidExecutionMode));
    EXPECT_TRUE(Contains(validation, ex2::ConfigurationError::InvalidOperationBoundary));
    EXPECT_TRUE(Contains(validation,
        ex2::ConfigurationError::UnsupportedGeneratorRevision));
}

TEST(Ex2Identity, CanonicalJsonSortsKeysEscapesStringsAndRejectsDuplicates)
{
    const std::string canonical = ex2::CanonicalJson({
        {"z", std::uint64_t{7}},
        {"a", std::string{"quote\" slash\\ line\n\t\x01"}},
        {"m", nullptr}});
    EXPECT_EQ(canonical,
        "{\"a\":\"quote\\\" slash\\\\ line\\n\\t\\u0001\",\"m\":null,\"z\":7}");
    EXPECT_EQ(canonical, ex2::CanonicalJson({
        {"m", nullptr}, {"a", std::string{"quote\" slash\\ line\n\t\x01"}},
        {"z", std::uint64_t{7}}}));
    EXPECT_THROW(static_cast<void>(ex2::CanonicalJson(
        {{"a", nullptr}, {"a", nullptr}})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ex2::CanonicalJson({{"Upper", nullptr}})),
        std::invalid_argument);
}

TEST(Ex2Identity, AFixtureMatchesIndependentLiteralCanonicalJsonAndSha256)
{
    // The literal UTF-8 JSON and digests below were calculated independently
    // with System.Security.Cryptography.SHA256, not the production hasher.
    const auto condition = Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 257U}));
    const std::string expectedCondition =
        "{\"byte_count\":null,\"counter_count\":null,\"element_count\":257,\"execution_mode\":\"ordinary\",\"generator_revision\":\"ex2-mix64-v1\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"index_pattern\":null,\"instrument_mode\":\"H\",\"iteration_count\":null,\"machine_id\":\"anonymous-machine\",\"operation_boundary\":\"single-dispatch-completion\",\"protocol_version\":\"1.0\",\"seed\":81985529216486895,\"transfer_direction\":null,\"variant\":\"A1\",\"workload\":\"A\"}";
    EXPECT_EQ(ex2::ComparisonConditionCanonicalJson(condition), expectedCondition);
    EXPECT_EQ(ex2::ComparisonConditionId(condition),
        "a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961");

    const auto series = CudaSeries(condition);
    const std::string expectedSeries =
        "{\"backend\":\"cuda\",\"block_index\":3,\"byte_count\":null,\"counter_count\":null,\"element_count\":257,\"executable_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"execution_mode\":\"ordinary\",\"generator_revision\":\"ex2-mix64-v1\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"index_pattern\":null,\"instrument_mode\":\"H\",\"iteration_count\":null,\"machine_id\":\"anonymous-machine\",\"operation_boundary\":\"single-dispatch-completion\",\"order_slot\":1,\"planned_sample_count\":100,\"process_index\":2,\"protocol_version\":\"1.0\",\"seed\":81985529216486895,\"shader_sha256\":null,\"source_revision\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"transfer_direction\":null,\"variant\":\"A1\",\"warmup_count\":2,\"workload\":\"A\"}";
    EXPECT_EQ(ex2::SeriesCanonicalJson(series), expectedSeries);
    EXPECT_EQ(ex2::SeriesId(series),
        "4d7f59d82a16d18a7738e43eaa236ce233f1384e749da092c813793265b22ffa");
}

TEST(Ex2Identity, IndexedFixtureAnchorsApplicableOptionalSemanticParameter)
{
    // Independently anchored with System.Security.Cryptography.SHA256.
    const auto condition = Condition(ex2::MakeConfiguration(
        ex2::IndexedConfiguration{ex2::IndexedVariant::B1, 262'144U,
            ex2::IndexPattern::ShuffledV1}), ex2::InstrumentMode::N);
    const std::string expected =
        "{\"byte_count\":null,\"counter_count\":null,\"element_count\":262144,\"execution_mode\":\"ordinary\",\"generator_revision\":\"ex2-mix64-v1\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"index_pattern\":\"shuffled-v1\",\"instrument_mode\":\"N\",\"iteration_count\":null,\"machine_id\":\"anonymous-machine\",\"operation_boundary\":\"single-dispatch-completion\",\"protocol_version\":\"1.0\",\"seed\":81985529216486895,\"transfer_direction\":null,\"variant\":\"B1\",\"workload\":\"B\"}";
    EXPECT_EQ(ex2::ComparisonConditionCanonicalJson(condition), expected);
    EXPECT_EQ(ex2::ComparisonConditionId(condition),
        "0f9781fcb04927e384322ce9f505a7f05feb4513e3bd640a9709bec2e1509d1a");

    ex2::SeriesIdentityContext series{
        condition, ex2::Backend::Vulkan, 0U, 0U, 0U, 16U, 200U,
        std::string(40U, 'c'), std::string(64U, 'd'), std::string(64U, 'e')};
    const std::string expectedSeries =
        "{\"backend\":\"vulkan\",\"block_index\":0,\"byte_count\":null,\"counter_count\":null,\"element_count\":262144,\"executable_sha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",\"execution_mode\":\"ordinary\",\"generator_revision\":\"ex2-mix64-v1\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"index_pattern\":\"shuffled-v1\",\"instrument_mode\":\"N\",\"iteration_count\":null,\"machine_id\":\"anonymous-machine\",\"operation_boundary\":\"single-dispatch-completion\",\"order_slot\":0,\"planned_sample_count\":200,\"process_index\":0,\"protocol_version\":\"1.0\",\"seed\":81985529216486895,\"shader_sha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\"source_revision\":\"cccccccccccccccccccccccccccccccccccccccc\",\"transfer_direction\":null,\"variant\":\"B1\",\"warmup_count\":16,\"workload\":\"B\"}";
    EXPECT_EQ(ex2::SeriesCanonicalJson(series), expectedSeries);
    EXPECT_EQ(ex2::SeriesId(series),
        "eb34c51de5d016f2686862f3392b8bd4e4f1b12681d9f679011c99ba01674a39");
}

TEST(Ex2Identity, CDEConditionsPopulateOnlyTheirApplicableSemanticFields)
{
    const auto contention = Condition(ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 1U, 257U}));
    const std::string contentionJson =
        ex2::ComparisonConditionCanonicalJson(contention);
    EXPECT_NE(contentionJson.find("\"counter_count\":1"), std::string::npos);
    EXPECT_NE(contentionJson.find("\"element_count\":257"), std::string::npos);
    EXPECT_NE(contentionJson.find("\"iteration_count\":null"), std::string::npos);

    const auto iterative = Condition(ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1, 257U, 0U}));
    const std::string iterativeJson =
        ex2::ComparisonConditionCanonicalJson(iterative);
    EXPECT_NE(iterativeJson.find("\"iteration_count\":0"), std::string::npos);
    EXPECT_NE(iterativeJson.find("\"counter_count\":null"), std::string::npos);

    const auto transfer = Condition(ex2::MakeConfiguration(
        ex2::TransferConfiguration{ex2::TransferVariant::E2, 0U,
            ex2::TransferDirection::DeviceToHost}));
    const std::string transferJson =
        ex2::ComparisonConditionCanonicalJson(transfer);
    EXPECT_NE(transferJson.find("\"byte_count\":0"), std::string::npos);
    EXPECT_NE(transferJson.find("\"element_count\":null"), std::string::npos);
    EXPECT_NE(transferJson.find("\"transfer_direction\":\"D2H\""),
        std::string::npos);
}

TEST(Ex2Identity, RepeatedIdentityConstructionIsDeterministic)
{
    const auto condition = Condition(ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 1U, 257U}));
    EXPECT_EQ(ex2::ComparisonConditionCanonicalJson(condition),
        ex2::ComparisonConditionCanonicalJson(condition));
    EXPECT_EQ(ex2::ComparisonConditionId(condition),
        ex2::ComparisonConditionId(condition));
    const auto series = CudaSeries(condition);
    EXPECT_EQ(ex2::SeriesCanonicalJson(series), ex2::SeriesCanonicalJson(series));
    EXPECT_EQ(ex2::SeriesId(series), ex2::SeriesId(series));
}

TEST(Ex2Identity, EverySemanticParameterChangesTheConditionId)
{
    const auto baseline = Condition(ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1, 257U, 16U}));
    const std::string expected = ex2::ComparisonConditionId(baseline);
    auto changed = baseline;
    changed.workload.common.seed++;
    EXPECT_NE(ex2::ComparisonConditionId(changed), expected);
    changed = baseline;
    std::get<ex2::IterativeConfiguration>(changed.workload.parameters).elementCount++;
    EXPECT_NE(ex2::ComparisonConditionId(changed), expected);
    changed = baseline;
    std::get<ex2::IterativeConfiguration>(changed.workload.parameters).iterationCount++;
    EXPECT_NE(ex2::ComparisonConditionId(changed), expected);
    changed = baseline; changed.machineId = "other-machine";
    EXPECT_NE(ex2::ComparisonConditionId(changed), expected);
    changed = baseline; changed.instrumentMode = ex2::InstrumentMode::N;
    EXPECT_NE(ex2::ComparisonConditionId(changed), expected);
}

TEST(Ex2Identity, ConditionIsBackendNeutralButSeriesTracksEverySeriesField)
{
    const auto condition = Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A2, 262'144U}));
    auto cuda = CudaSeries(condition);
    auto vulkan = cuda;
    vulkan.backend = ex2::Backend::Vulkan;
    vulkan.shaderSha256 = std::string(64U, 'c');
    EXPECT_EQ(ex2::ComparisonConditionId(cuda.condition),
        ex2::ComparisonConditionId(vulkan.condition));
    EXPECT_NE(ex2::SeriesId(cuda), ex2::SeriesId(vulkan));

    const std::string baseline = ex2::SeriesId(cuda);
    auto changed = cuda; changed.processIndex++;
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.blockIndex++;
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.orderSlot = 0U;
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.warmupCount++;
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.plannedSampleCount++;
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.sourceRevision[0] = 'c';
    EXPECT_NE(ex2::SeriesId(changed), baseline);
    changed = cuda; changed.executableSha256[0] = 'd';
    EXPECT_NE(ex2::SeriesId(changed), baseline);
}

TEST(Ex2Identity, GeneralSeriesIndicesAreIndependentOfCampaignLength)
{
    const auto condition = Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A2, 262'144U}));
    const std::string conditionId = ex2::ComparisonConditionId(condition);
    auto baseline = CudaSeries(condition);
    baseline.processIndex = 0U;
    baseline.blockIndex = 0U;
    const std::string baselineSeriesId = ex2::SeriesId(baseline);

    std::set<std::string> initialSeriesIds;
    for (std::uint64_t index = 0U; index <= 4U; ++index)
    {
        auto existingRange = baseline;
        existingRange.processIndex = index;
        existingRange.blockIndex = index;
        initialSeriesIds.insert(ex2::SeriesId(existingRange));
        EXPECT_EQ(ex2::ComparisonConditionId(existingRange.condition), conditionId);
    }
    EXPECT_EQ(initialSeriesIds.size(), 5U);

    for (const std::uint64_t index : {5U, 9U})
    {
        auto changedBlock = baseline;
        changedBlock.blockIndex = index;
        EXPECT_NE(ex2::SeriesId(changedBlock), baselineSeriesId);
        EXPECT_EQ(ex2::ComparisonConditionId(changedBlock.condition), conditionId);

        auto changedProcess = baseline;
        changedProcess.processIndex = index;
        EXPECT_NE(ex2::SeriesId(changedProcess), baselineSeriesId);
        EXPECT_EQ(ex2::ComparisonConditionId(changedProcess.condition), conditionId);
    }

    auto extended = baseline;
    extended.blockIndex = 9U;
    extended.processIndex = 9U;
    const std::string canonical = ex2::SeriesCanonicalJson(extended);
    EXPECT_NE(canonical.find("\"block_index\":9"), std::string::npos);
    EXPECT_NE(canonical.find("\"process_index\":9"), std::string::npos);
}

TEST(Ex2Identity, RequiresVerifiedHardwareAndExactProvenanceFormats)
{
    auto condition = Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U}));
    condition.gpuIdentity.externallyVerified = false;
    EXPECT_THROW(static_cast<void>(ex2::ComparisonConditionId(condition)),
        std::invalid_argument);
    condition.gpuIdentity.externallyVerified = true;
    condition.gpuIdentity.uuid = "syntactically-wrong";
    EXPECT_THROW(static_cast<void>(ex2::ComparisonConditionId(condition)),
        std::invalid_argument);
    condition = Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U}));
    condition.machineId = "private/path";
    EXPECT_THROW(static_cast<void>(ex2::ComparisonConditionId(condition)),
        std::invalid_argument);

    auto series = CudaSeries(Condition(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 1U})));
    series.sourceRevision = "short";
    EXPECT_THROW(static_cast<void>(ex2::SeriesId(series)),
        std::invalid_argument);
    series = CudaSeries(series.condition);
    series.executableSha256[0] = 'G';
    EXPECT_THROW(static_cast<void>(ex2::SeriesId(series)),
        std::invalid_argument);
    series = CudaSeries(series.condition);
    series.shaderSha256 = std::string(64U, 'c');
    EXPECT_THROW(static_cast<void>(ex2::SeriesId(series)),
        std::invalid_argument);
    series = CudaSeries(series.condition);
    series.backend = ex2::Backend::Vulkan;
    EXPECT_THROW(static_cast<void>(ex2::SeriesId(series)),
        std::invalid_argument);
}

TEST(Ex2RunIdentity, AcceptsOnlyAnonymousPathSafeIdentifiers)
{
    EXPECT_TRUE(ex2::IsValidAnonymousIdentifier("ex2-a1-run_001.test"));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier(""));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier("."));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier("../escape"));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier("machine user"));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier(std::string(129U, 'a')));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier(std::string(64U, 'a')));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier(std::string(64U, 'A')));
    EXPECT_FALSE(ex2::IsValidAnonymousIdentifier(
        "aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01aF01"));
    EXPECT_TRUE(ex2::IsValidAnonymousIdentifier(std::string(64U, 'g')));
}

TEST(Ex2RunIdentity, DetectsDuplicateIdsAndDestinationCollisionsWithoutIo)
{
    const std::filesystem::path root = "results/local";
    const std::vector<ex2::PlannedRun> valid{
        {"run-a", root / "run-a"}, {"run-b", root / "run-b"}};
    EXPECT_TRUE(ex2::ValidateRunPlan(valid, root).IsValid());

    const std::vector<ex2::PlannedRun> duplicateRuns{
        {"run-a", root / "run-a"}, {"run-a", root / "run-a"}};
    const auto duplicate = ex2::ValidateRunPlan(duplicateRuns, root);
    EXPECT_TRUE(Contains(duplicate, ex2::RunPlanError::DuplicateRunId));
    EXPECT_TRUE(Contains(duplicate, ex2::RunPlanError::DestinationCollision));

    const std::vector<ex2::PlannedRun> caseCollidingRuns{
        {"run-a", root / "run-a"}, {"RUN-A", root / "RUN-A"}};
    const auto caseCollision = ex2::ValidateRunPlan(caseCollidingRuns, root);
    EXPECT_TRUE(Contains(caseCollision, ex2::RunPlanError::DestinationCollision));

    const std::vector<ex2::PlannedRun> wrongDestinations{
        {"run-a", std::filesystem::path("elsewhere") / "run-a"},
        {"run-b", root / "wrong-leaf"}};
    const auto outside = ex2::ValidateRunPlan(wrongDestinations, root);
    EXPECT_TRUE(Contains(outside,
        ex2::RunPlanError::DestinationOutsideLocalResults));
    EXPECT_TRUE(Contains(outside, ex2::RunPlanError::DestinationLeafMismatch));
}

} // namespace
