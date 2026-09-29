#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2CorrectnessFoundation.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Evidence.hpp"
#include "ex2/Ex2Gate0.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

namespace
{
namespace ex2 = computelab::ex2;
namespace evidence = computelab::ex2::evidence;
namespace gate0 = computelab::ex2::gate0;

constexpr std::string_view kUuid =
    "00112233-4455-6677-8899-aabbccddeeff";
constexpr std::string_view kConditionId =
    "a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961";
constexpr std::string_view kSeriesId =
    "2cb4644c3087f271a390c2f0c9fbc0b7632276bfe9b6c9b79f1299a3a7a08ac4";

evidence::CorrectnessPlan Plan(
    ex2::WorkloadConfiguration workload = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 257U}),
    std::uint64_t plannedCount = 2U,
    ex2::Backend backend = ex2::Backend::Cuda,
    ex2::InstrumentMode instrumentMode = ex2::InstrumentMode::H)
{
    ex2::ComparisonConditionContext condition{
        "1.0", "anonymous-machine", {std::string(kUuid), true},
        std::move(workload), instrumentMode};
    return evidence::MakeCorrectnessPlan(
        "correctness-run",
        {std::move(condition), backend, 2U, 3U, 1U, 0U, plannedCount,
            std::string(40U, 'a'), std::string(64U, 'b'),
            backend == ex2::Backend::Vulkan
                ? std::optional<std::string>{std::string(64U, 'c')}
                : std::nullopt});
}

computelab::results::EnvironmentRecord CommonEnvironment()
{
    return {
        evidence::SchemaVersion, std::string(evidence::ExperimentId),
        "correctness-run", "2026-09-21T00:00:00Z",
        std::string(40U, 'a'), true, "anonymous-machine",
        "Windows", "11", "Synthetic CPU",
        std::numeric_limits<std::uint64_t>::max(),
        "Synthetic GPU", "NVIDIA", "10DE:1234", 8'589'934'592ULL,
        "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", true, false};
}

evidence::EnvironmentRecord Environment(
    evidence::CorrectnessPlan plan = Plan())
{
    return {
        CommonEnvironment(), std::move(plan), std::string(64U, 'e'),
        std::string(64U, 'f'),
        {.implementation = "synthetic-cuda-fixture",
         .streamFlags = "nonblocking"}};
}

evidence::InitializationRecord Initialization(
    const evidence::CorrectnessPlan& plan)
{
    return {
        plan.runId, plan.seriesIdentity.backend,
        plan.seriesIdentity.processIndex, 0U, "backend_setup",
        "A", "A1", 257U, "setup_complete", std::nullopt,
        "ready,\"quoted\"\r\nline"};
}

evidence::SampleRecord Passing(
    const evidence::CorrectnessPlan& plan,
    std::uint64_t index)
{
    auto result = evidence::MakeSampleRecord(plan, index);
    result.correctness = {true, true, true, true, true};
    result.status = evidence::OperationStatus::Ok;
    return result;
}

evidence::SampleRecord Mismatch(
    const evidence::CorrectnessPlan& plan,
    std::uint64_t index)
{
    auto result = evidence::MakeSampleRecord(plan, index);
    result.correctness = {true, true, true, true, false};
    result.status = evidence::OperationStatus::ValidationFailed;
    result.failurePhase = evidence::FailurePhase::Validation;
    result.errorCode = std::string(evidence::error_code::OutputMismatch);
    return result;
}

evidence::I7CorrectnessFoundation I7AFoundation(
    ex2::Backend backend = ex2::Backend::Cuda,
    std::string runId = "i7-a-cuda")
{
    const auto workload = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 256U});
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256U);
    const auto expected = ex2::ReferenceA1(input);
    auto identity = evidence::MakeI7WordInputIdentity(workload, input);
    ex2::ComparisonConditionContext condition{
        "1.1", "anonymous-machine", {std::string(kUuid), true},
        workload, ex2::InstrumentMode::H};
    ex2::SeriesIdentityContext series{
        std::move(condition), backend, 0U, 0U,
        backend == ex2::Backend::Cuda ? 0U : 1U,
        0U, 1U, std::string(40U, 'a'), std::string(64U, 'b'),
        backend == ex2::Backend::Vulkan
            ? std::optional<std::string>{std::string(64U, 'c')}
            : std::nullopt};
    return evidence::MakeI7WordCorrectnessFoundation(
        std::move(runId), std::move(series), std::move(identity), expected);
}

TEST(Ex2EvidenceSchema, PreservesVersionAndExactExistingHeaders)
{
    EXPECT_EQ(evidence::SchemaVersion, 2U);
    EXPECT_EQ(evidence::EvidenceKind, "correctness");
    EXPECT_EQ(evidence::InitializationCsvHeader(),
        gate0::InitializationCsvHeader());
    EXPECT_EQ(evidence::SamplesCsvHeader(), gate0::SamplesCsvHeader());
    EXPECT_EQ(evidence::SamplesCsvHeader(),
        "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns");
}

TEST(Ex2EvidenceSchema, UsesOnlyApprovedStatusStringsAndStableLocalPhases)
{
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::Ok), "ok");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::ValidationFailed),
        "validation_failed");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::SubmitFailed),
        "submit_failed");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::WaitFailed),
        "wait_failed");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::Timeout), "timeout");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::DeviceLost),
        "device_lost");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::TimestampInvalid),
        "timestamp_invalid");
    EXPECT_EQ(evidence::ToString(evidence::OperationStatus::Incomplete),
        "incomplete");

    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Configuration),
        "configuration");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::InputGeneration),
        "input_generation");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::BackendInitialization),
        "backend_initialization");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::ResourceAllocation),
        "resource_allocation");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Submission),
        "submission");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::CompletionWait),
        "completion_wait");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Readback), "readback");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Validation),
        "validation");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Timing), "timing");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::EvidenceSerialization),
        "evidence_serialization");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::EvidencePublication),
        "evidence_publication");
    EXPECT_EQ(evidence::ToString(evidence::FailurePhase::Interrupted),
        "interrupted");
}

TEST(Ex2I7Foundation, RequiresProtocolOnePointOneAndAnApprovedCoreCell)
{
    const auto core = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 256U});
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256U);
    const auto expected = ex2::ReferenceA1(input);
    auto identity = evidence::MakeI7WordInputIdentity(core, input);
    ex2::SeriesIdentityContext v10{
        {"1.0", "anonymous-machine", {std::string(kUuid), true}, core,
            ex2::InstrumentMode::H},
        ex2::Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    EXPECT_THROW(static_cast<void>(evidence::MakeI7WordCorrectnessFoundation(
        "i7-v10", v10, identity, expected)), std::invalid_argument);

    const auto correctnessOnly = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 257U});
    auto smallIdentity = evidence::MakeI7WordInputIdentity(
        correctnessOnly,
        ex2::GenerateWordInput(ex2::CoreInputSeed, 257U));
    const auto smallExpected = ex2::ReferenceA1(
        ex2::GenerateWordInput(ex2::CoreInputSeed, 257U));
    v10.condition.protocolVersion = "1.1";
    v10.condition.workload = correctnessOnly;
    EXPECT_THROW(static_cast<void>(evidence::MakeI7WordCorrectnessFoundation(
        "i7-not-core", v10, std::move(smallIdentity), smallExpected)),
        std::invalid_argument);

    const auto d2 = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D2, 262'144U, 16U});
    const auto d2Input = ex2::GenerateWordInput(ex2::CoreInputSeed, 262'144U);
    EXPECT_THROW(static_cast<void>(
        evidence::MakeI7WordInputIdentity(d2, d2Input)),
        std::invalid_argument);
}

TEST(Ex2I7Foundation, TypedBAndCIdentityRequiresEveryDeclaredInitialBuffer)
{
    const auto indexed = ex2::MakeConfiguration(ex2::IndexedConfiguration{
        ex2::IndexedVariant::B1, 262'144U,
        ex2::IndexPattern::StructuredV1});
    const auto primary = ex2::GenerateWordInput(
        ex2::CoreInputSeed, 262'144U);
    const auto permutation = ex2::GenerateStructuredPermutation(262'144U);
    const auto indexedIdentity = evidence::MakeI7IndexedInputIdentity(
        indexed, primary, permutation);
    EXPECT_EQ(indexedIdentity.Sha256(),
        ex2::IndexedLogicalInputSha256(primary, permutation));
    const auto indexedExpected = ex2::ReferenceB1Gather(primary, permutation);
    ex2::SeriesIdentityContext indexedSeries{
        {"1.1", "anonymous-machine", {std::string(kUuid), true}, indexed,
            ex2::InstrumentMode::H},
        ex2::Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    const auto indexedFoundation =
        evidence::MakeI7WordCorrectnessFoundation(
            "i7-b-cuda", std::move(indexedSeries), indexedIdentity,
            indexedExpected);
    EXPECT_EQ(indexedFoundation.InputIdentity().Sha256(),
        ex2::IndexedLogicalInputSha256(primary, permutation));

    auto changedPermutation = permutation;
    std::swap(changedPermutation[0], changedPermutation[1]);
    EXPECT_THROW(static_cast<void>(evidence::MakeI7IndexedInputIdentity(
        indexed, primary, changedPermutation)), std::invalid_argument);

    const auto contention = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{
            ex2::ContentionElementCount, ex2::ContentionActive64,
            ex2::ContentionElementCount});
    const auto targets = ex2::GenerateContentionTargets(
        ex2::ContentionElementCount, ex2::ContentionActive64);
    auto initial = ex2::MakeZeroInitialCounterState(
        ex2::ContentionElementCount);
    const auto contentionIdentity = evidence::MakeI7ContentionInputIdentity(
        contention, targets, initial);
    EXPECT_EQ(contentionIdentity.Sha256(),
        ex2::ContentionLogicalInputSha256(targets, initial));
    const auto counters = ex2::ReferenceContentionHistogram(
        targets, ex2::ContentionElementCount);
    ex2::SeriesIdentityContext contentionSeries{
        {"1.1", "anonymous-machine", {std::string(kUuid), true}, contention,
            ex2::InstrumentMode::H},
        ex2::Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    const auto contentionFoundation =
        evidence::MakeI7WordCorrectnessFoundation(
            "i7-c-cuda", std::move(contentionSeries), contentionIdentity,
            counters);
    EXPECT_EQ(contentionFoundation.InputIdentity().Sha256(),
        ex2::ContentionLogicalInputSha256(targets, initial));
    initial.back() = 1U;
    EXPECT_THROW(static_cast<void>(evidence::MakeI7ContentionInputIdentity(
        contention, targets, initial)), std::invalid_argument);
}

TEST(Ex2I7Foundation, BackendIdentityProvenanceAndRunIdsRemainSeparated)
{
    const auto cuda = I7AFoundation(ex2::Backend::Cuda, "i7-a-cuda");
    const auto vulkan = I7AFoundation(ex2::Backend::Vulkan, "i7-a-vulkan");
    EXPECT_EQ(cuda.Plan().comparisonConditionId,
        vulkan.Plan().comparisonConditionId);
    EXPECT_NE(cuda.Plan().seriesId, vulkan.Plan().seriesId);
    EXPECT_NE(cuda.Plan().runId, vulkan.Plan().runId);
    EXPECT_EQ(cuda.InputIdentity().Sha256(),
        vulkan.InputIdentity().Sha256());

    auto changed = cuda.Plan().seriesIdentity;
    changed.sourceRevision[0] = 'd';
    EXPECT_NE(ex2::SeriesId(changed), cuda.Plan().seriesId);
    changed = cuda.Plan().seriesIdentity;
    changed.executableSha256[0] = 'e';
    EXPECT_NE(ex2::SeriesId(changed), cuda.Plan().seriesId);
    changed = vulkan.Plan().seriesIdentity;
    (*changed.shaderSha256)[0] = 'f';
    EXPECT_NE(ex2::SeriesId(changed), vulkan.Plan().seriesId);
}

TEST(Ex2I7Foundation, DAndEKeepTypedSingleBufferInputSemantics)
{
    const auto d = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1, 262'144U, 16U});
    const auto dInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 262'144U);
    const auto dExpected = ex2::ReferenceD1(dInput, 16U).finalState;
    auto dIdentity = evidence::MakeI7WordInputIdentity(d, dInput);
    EXPECT_EQ(dIdentity.Sha256(), ex2::WordInputSha256(dInput));
    ex2::SeriesIdentityContext dSeries{
        {"1.1", "anonymous-machine", {std::string(kUuid), true}, d,
            ex2::InstrumentMode::H},
        ex2::Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    EXPECT_NO_THROW(static_cast<void>(
        evidence::MakeI7WordCorrectnessFoundation(
            "i7-d-cuda", std::move(dSeries), std::move(dIdentity), dExpected)));

    const auto e = ex2::MakeConfiguration(ex2::TransferConfiguration{
        ex2::TransferVariant::E1, 1'024U,
        ex2::TransferDirection::HostToDevice});
    const auto eInput = ex2::GenerateByteInput(ex2::CoreInputSeed, 1'024U);
    auto eIdentity = evidence::MakeI7ByteInputIdentity(e, eInput);
    EXPECT_EQ(eIdentity.Sha256(), ex2::ByteInputSha256(eInput));
    ex2::SeriesIdentityContext eSeries{
        {"1.1", "anonymous-machine", {std::string(kUuid), true}, e,
            ex2::InstrumentMode::H},
        ex2::Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    const auto eFoundation = evidence::MakeI7ByteCorrectnessFoundation(
        "i7-e-cuda", std::move(eSeries), std::move(eIdentity), eInput);
    const auto eSample = evidence::MakeI7ComparedByteSample(
        eFoundation, 0U, eInput, eInput);
    EXPECT_EQ(eSample.status, evidence::OperationStatus::Ok);
}

TEST(Ex2I7Foundation, ComparisonFailureMappingAndUntimedBundleAreDerived)
{
    const auto foundation = I7AFoundation();
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256U);
    const auto expected = ex2::ReferenceA1(input);
    auto observed = expected;
    const auto passing = evidence::MakeI7ComparedWordSample(
        foundation, 0U, expected, observed);
    EXPECT_EQ(passing.status, evidence::OperationStatus::Ok);
    EXPECT_EQ(passing.correctness.validationPassed, true);
    observed.back() ^= 1U;
    const auto mismatch = evidence::MakeI7ComparedWordSample(
        foundation, 0U, expected, observed);
    EXPECT_EQ(mismatch.status, evidence::OperationStatus::ValidationFailed);
    EXPECT_EQ(mismatch.correctness.validationPassed, false);
    auto contradictoryExpected = expected;
    contradictoryExpected.front() ^= 1U;
    EXPECT_THROW(static_cast<void>(evidence::MakeI7ComparedWordSample(
        foundation, 0U, contradictoryExpected, contradictoryExpected)),
        std::invalid_argument);

    const auto readback = evidence::MakeI7FailureObservation(
        foundation, 0U, evidence::I7FailureKind::ReadbackFailed,
        "native-readback-code");
    EXPECT_EQ(readback.sample.status, evidence::OperationStatus::Incomplete);
    EXPECT_TRUE(readback.sample.correctness.operationCompleted);
    EXPECT_FALSE(readback.sample.correctness.outputObserved);
    EXPECT_EQ(readback.nativeDetail, "native-readback-code");
    const auto deviceLost = evidence::MakeI7FailureObservation(
        foundation, 0U, evidence::I7FailureKind::DeviceLostDuringReadback);
    EXPECT_EQ(deviceLost.sample.status, evidence::OperationStatus::DeviceLost);
    EXPECT_TRUE(deviceLost.sample.correctness.operationCompleted);

    auto common = CommonEnvironment();
    common.runId = foundation.Plan().runId;
    common.machineId = foundation.Plan().seriesIdentity.condition.machineId;
    common.gitCommit = foundation.Plan().seriesIdentity.sourceRevision;
    const auto environment = evidence::MakeI7EnvironmentRecord(
        std::move(common), foundation,
        {.implementation = "synthetic-i7-cuda-fixture"});
    EXPECT_EQ(environment.inputSha256, foundation.InputIdentity().Sha256());
    EXPECT_EQ(environment.expectedOutputSha256,
        ex2::WordInputSha256(expected));
    const auto initialization = evidence::MakeI7SetupCompleteInitialization(
        foundation, "actual fixture setup completed without timing");
    EXPECT_FALSE(initialization.durationNanoseconds.has_value());

    const std::vector initializationRows{initialization};
    const std::vector samples{passing};
    const auto summary = evidence::SummarizeSamples(
        foundation.Plan(), samples, evidence::OperationStatus::Ok);
    EXPECT_NO_THROW(evidence::ValidateEvidenceBundle(
        environment, initializationRows, samples, summary));
    const std::string csv = evidence::SerializeSamplesCsv(
        foundation.Plan(), samples);
    EXPECT_TRUE(csv.starts_with(evidence::SamplesCsvHeader() + "\r\n"));
    EXPECT_FALSE(passing.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(passing.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(passing.hostCompletionNanoseconds.has_value());
    EXPECT_FALSE(passing.nativeDeviceIntervalNanoseconds.has_value());
    EXPECT_NE(evidence::SerializeSummaryJson(summary, samples).find(
        "\"native_device_interval_ns\":null"), std::string::npos);
}

TEST(Ex2I7Foundation, PostFoundationFailureMappingsPreservePhaseAndProgress)
{
    struct ExpectedFailure
    {
        evidence::I7FailureKind kind;
        evidence::OperationStatus status;
        evidence::FailurePhase phase;
        std::string_view errorCode;
        bool operationCompleted;
        bool outputObserved;
    };
    const std::vector<ExpectedFailure> cases{
        {evidence::I7FailureKind::BackendInitializationFailed,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::BackendInitialization,
            evidence::error_code::BackendInitializationFailed, false, false},
        {evidence::I7FailureKind::ResourceAllocationFailed,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::ResourceAllocation,
            evidence::error_code::ResourceAllocationFailed, false, false},
        {evidence::I7FailureKind::SubmissionFailed,
            evidence::OperationStatus::SubmitFailed,
            evidence::FailurePhase::Submission,
            evidence::error_code::SubmissionFailed, false, false},
        {evidence::I7FailureKind::CompletionFailed,
            evidence::OperationStatus::WaitFailed,
            evidence::FailurePhase::CompletionWait,
            evidence::error_code::CompletionFailed, false, false},
        {evidence::I7FailureKind::SubmissionTimeout,
            evidence::OperationStatus::Timeout,
            evidence::FailurePhase::Submission,
            evidence::error_code::OperationTimeout, false, false},
        {evidence::I7FailureKind::CompletionTimeout,
            evidence::OperationStatus::Timeout,
            evidence::FailurePhase::CompletionWait,
            evidence::error_code::OperationTimeout, false, false},
        {evidence::I7FailureKind::DeviceLostDuringInitialization,
            evidence::OperationStatus::DeviceLost,
            evidence::FailurePhase::BackendInitialization,
            evidence::error_code::DeviceLost, false, false},
        {evidence::I7FailureKind::DeviceLostDuringSubmission,
            evidence::OperationStatus::DeviceLost,
            evidence::FailurePhase::Submission,
            evidence::error_code::DeviceLost, false, false},
        {evidence::I7FailureKind::DeviceLostDuringCompletion,
            evidence::OperationStatus::DeviceLost,
            evidence::FailurePhase::CompletionWait,
            evidence::error_code::DeviceLost, false, false},
        {evidence::I7FailureKind::DeviceLostDuringReadback,
            evidence::OperationStatus::DeviceLost,
            evidence::FailurePhase::Readback,
            evidence::error_code::DeviceLost, true, false},
        {evidence::I7FailureKind::ReadbackFailed,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Readback,
            evidence::error_code::ReadbackFailed, true, false},
        {evidence::I7FailureKind::InterruptedBeforeCompletion,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Interrupted,
            evidence::error_code::Interrupted, false, false},
        {evidence::I7FailureKind::InterruptedAfterCompletion,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Interrupted,
            evidence::error_code::Interrupted, true, false},
        {evidence::I7FailureKind::InterruptedAfterOutput,
            evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Interrupted,
            evidence::error_code::Interrupted, true, true},
    };

    const auto foundation = I7AFoundation();
    for (std::size_t index = 0U; index < cases.size(); ++index)
    {
        const auto& expected = cases[index];
        const auto failure = evidence::MakeI7FailureObservation(
            foundation, index, expected.kind, "bounded-native-detail");
        const auto& sample = failure.sample;
        EXPECT_EQ(sample.status, expected.status) << index;
        ASSERT_TRUE(sample.failurePhase.has_value()) << index;
        EXPECT_EQ(*sample.failurePhase, expected.phase) << index;
        ASSERT_TRUE(sample.errorCode.has_value()) << index;
        EXPECT_EQ(*sample.errorCode, expected.errorCode) << index;
        EXPECT_TRUE(sample.correctness.expectedOutputGenerated) << index;
        EXPECT_EQ(sample.correctness.operationCompleted,
            expected.operationCompleted) << index;
        EXPECT_EQ(sample.correctness.outputObserved,
            expected.outputObserved) << index;
        EXPECT_FALSE(sample.correctness.comparisonPerformed) << index;
        EXPECT_FALSE(sample.correctness.validationPassed.has_value()) << index;
        EXPECT_EQ(failure.nativeDetail, "bounded-native-detail") << index;
    }

    EXPECT_THROW(static_cast<void>(evidence::MakeI7FailureObservation(
        foundation, 0U, static_cast<evidence::I7FailureKind>(-1))),
        std::invalid_argument);
}

TEST(Ex2I7Foundation, HistoricalV10BAndCPlansRemainValid)
{
    EXPECT_NO_THROW(static_cast<void>(Plan(ex2::MakeConfiguration(
        ex2::IndexedConfiguration{ex2::IndexedVariant::B1, 4U,
            ex2::IndexPattern::StructuredV1}), 1U)));
    EXPECT_NO_THROW(static_cast<void>(Plan(ex2::MakeConfiguration(
        ex2::ContentionConfiguration{257U, 1U, 257U}), 1U)));
}

TEST(Ex2EvidenceSerialization, EnvironmentMatchesIndependentLiteralFixture)
{
    const auto environment = Environment();
    EXPECT_EQ(environment.plan.comparisonConditionId, kConditionId);
    EXPECT_EQ(environment.plan.seriesId, kSeriesId);

    const std::string expected =
        "{\"schema_version\":2,\"experiment_id\":\"EX-2\",\"run_id\":\"correctness-run\",\"timestamp_utc\":\"2026-09-21T00:00:00Z\",\"git_commit\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"git_dirty\":true,\"machine_id\":\"anonymous-machine\",\"os_name\":\"Windows\",\"os_version\":\"11\",\"cpu_name\":\"Synthetic CPU\",\"system_memory_bytes\":18446744073709551615,\"gpu_name\":\"Synthetic GPU\",\"gpu_vendor\":\"NVIDIA\",\"gpu_device_id\":\"10DE:1234\",\"gpu_memory_bytes\":8589934592,\"nvidia_driver_version\":\"driver\",\"cuda_toolkit_version\":\"toolkit\",\"cuda_runtime_version\":\"runtime\",\"cuda_compute_capability\":\"7.5\",\"vulkan_sdk_version\":\"sdk\",\"vulkan_device_api_version\":\"1.4\",\"compiler_name\":\"MSVC\",\"compiler_version\":\"19\",\"cmake_version\":\"4\",\"ninja_version\":\"1\",\"configure_preset\":\"x64-debug\",\"build_type\":\"Debug\",\"validation_enabled\":true,\"diagnostic_instrumentation\":false,\"protocol_version\":\"1.0\",\"evidence_kind\":\"correctness\",\"backend\":\"cuda\",\"instrument_mode\":\"H\",\"warmup_count\":0,\"planned_sample_count\":2,\"comparison_condition_id\":\"a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961\",\"series_id\":\"2cb4644c3087f271a390c2f0c9fbc0b7632276bfe9b6c9b79f1299a3a7a08ac4\",\"block_index\":3,\"process_index\":2,\"order_slot\":1,\"source_revision\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"executable_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"shader_sha256\":null,\"input_sha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\"expected_output_sha256\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"workload\":\"A\",\"variant\":\"A1\",\"generator_revision\":\"ex2-mix64-v1\",\"seed\":81985529216486895,\"element_count\":257,\"byte_count\":null,\"index_pattern\":null,\"counter_count\":null,\"iteration_count\":null,\"transfer_direction\":null,\"execution_mode\":\"ordinary\",\"operation_boundary\":\"single-dispatch-completion\",\"backend_native\":{\"implementation\":\"synthetic-cuda-fixture\",\"stream_flags\":\"nonblocking\",\"queue_family_index\":null,\"queue_flags\":null,\"queue_count\":null,\"timestamp_valid_bits\":null,\"timestamp_period_ns\":null,\"input_memory_flags\":null,\"output_memory_flags\":null,\"upload_memory_flags\":null,\"readback_memory_flags\":null,\"native_markers_enabled\":false,\"native_timing_method\":null,\"native_timing_resolution_ns\":null,\"native_timing_start_stage\":null,\"native_timing_stop_stage\":null,\"native_duration_envelope_ns\":null}}\n";
    EXPECT_EQ(evidence::SerializeEnvironmentJson(environment), expected);
    EXPECT_EQ(expected.find("observed_output"), std::string::npos);
}

TEST(Ex2EvidenceSerialization, InitializationUsesRfc4180AndExactNulls)
{
    const auto plan = Plan();
    const std::vector records{Initialization(plan)};
    const std::string expected =
        "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation\r\n"
        "2,correctness-run,EX-2,cuda,2,0,backend_setup,A,A1,257,setup_complete,,\"ready,\"\"quoted\"\"\r\nline\"\r\n";
    EXPECT_EQ(evidence::SerializeInitializationCsv(plan, records), expected);

    auto maximum = records.front();
    maximum.observation.reset();
    maximum.durationNanoseconds = std::numeric_limits<std::uint64_t>::max();
    EXPECT_NE(evidence::SerializeInitializationCsv(
        plan, std::span<const evidence::InitializationRecord>{&maximum, 1U}).find(
        "18446744073709551615"), std::string::npos);
}

TEST(Ex2EvidenceSerialization, SamplesMatchIndependentLiteralFixture)
{
    const auto plan = Plan();
    const std::vector samples{Passing(plan, 0U), Mismatch(plan, 1U)};
    const std::string expected =
        "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns\r\n"
        "2,correctness-run,EX-2,a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961,2cb4644c3087f271a390c2f0c9fbc0b7632276bfe9b6c9b79f1299a3a7a08ac4,cuda,A,A1,81985529216486895,257,,,,,,ordinary,H,0,2,3,1,2,0,true,ok,,,,,,\r\n"
        "2,correctness-run,EX-2,a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961,2cb4644c3087f271a390c2f0c9fbc0b7632276bfe9b6c9b79f1299a3a7a08ac4,cuda,A,A1,81985529216486895,257,,,,,,ordinary,H,0,2,3,1,2,1,false,validation_failed,validation,output_mismatch,,,,\r\n";
    EXPECT_EQ(evidence::SerializeSamplesCsv(plan, samples), expected);
}

TEST(Ex2EvidenceSerialization, SummaryIsReconstructedAndHasNoTimingStatistics)
{
    const auto plan = Plan();
    const std::vector samples{Passing(plan, 0U), Mismatch(plan, 1U)};
    const auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::ValidationFailed,
        evidence::FailurePhase::Validation,
        std::string(evidence::error_code::OutputMismatch));
    EXPECT_EQ(summary.recordedSampleCount, 2U);
    EXPECT_EQ(summary.validationFailures, 1U);
    EXPECT_EQ(summary.failedSampleCount, 1U);

    const std::string expected =
        "{\"schema_version\":2,\"run_id\":\"correctness-run\",\"experiment_id\":\"EX-2\",\"evidence_kind\":\"correctness\",\"process_status\":\"validation_failed\",\"failure_phase\":\"validation\",\"error_code\":\"output_mismatch\",\"sample_groups\":[{\"group\":{\"comparison_condition_id\":\"a20a30e13cf994a6a1c9da778805a9f32e5c6ca9f3c8a6661bd81c457e316961\",\"series_id\":\"2cb4644c3087f271a390c2f0c9fbc0b7632276bfe9b6c9b79f1299a3a7a08ac4\",\"run_id\":\"correctness-run\",\"backend\":\"cuda\",\"workload\":\"A\",\"variant\":\"A1\",\"seed\":81985529216486895,\"element_count\":257,\"byte_count\":null,\"index_pattern\":null,\"counter_count\":null,\"iteration_count\":null,\"transfer_direction\":null,\"execution_mode\":\"ordinary\",\"instrument_mode\":\"H\",\"warmup_count\":0,\"planned_sample_count\":2,\"block_index\":3,\"order_slot\":1,\"process_index\":2},\"recorded_sample_count\":2,\"validation_failures\":1,\"failed_sample_count\":1,\"metric_admission\":{\"host_submission_ns\":{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":\"correctness_only\",\"summary_sample_inclusion\":\"inapplicable_no_timing_metrics\"},\"host_wait_ns\":{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":\"correctness_only\",\"summary_sample_inclusion\":\"inapplicable_no_timing_metrics\"},\"host_completion_ns\":{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":\"correctness_only\",\"summary_sample_inclusion\":\"inapplicable_no_timing_metrics\"},\"native_device_interval_ns\":{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":\"correctness_only\",\"summary_sample_inclusion\":\"inapplicable_no_timing_metrics\"}},\"metrics\":{\"host_submission_ns\":null,\"host_wait_ns\":null,\"host_completion_ns\":null,\"native_device_interval_ns\":null}}]}\n";
    EXPECT_EQ(evidence::SerializeSummaryJson(summary, samples), expected);
}

TEST(Ex2EvidenceCorrectness, RepresentsEveryWorkloadFamilyWithoutRunningGpuCode)
{
    const std::vector<ex2::WorkloadConfiguration> configurations{
        ex2::MakeConfiguration(ex2::LinearConfiguration{
            ex2::LinearVariant::A1, 4U}),
        ex2::MakeConfiguration(ex2::IndexedConfiguration{
            ex2::IndexedVariant::B2, 4U, ex2::IndexPattern::StructuredV1}),
        ex2::MakeConfiguration(ex2::ContentionConfiguration{257U, 1U, 257U}),
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D1, 4U, 2U}),
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E2, 4U,
            ex2::TransferDirection::DeviceToHost}),
    };

    for (const auto& configuration : configurations)
    {
        const auto plan = Plan(configuration, 1U);
        const std::vector samples{Passing(plan, 0U)};
        EXPECT_NO_THROW(evidence::ValidateSamples(plan, samples));
        const std::string csv = evidence::SerializeSamplesCsv(plan, samples);
        EXPECT_NE(csv.find("," + std::string(ex2::ToString(
            plan.seriesIdentity.condition.workload.common.executionMode)) + ","),
            std::string::npos);
        const auto summary = evidence::SummarizeSamples(
            plan, samples, evidence::OperationStatus::Ok);
        EXPECT_EQ(summary.recordedSampleCount, 1U);
        EXPECT_EQ(summary.validationFailures, 0U);
        EXPECT_EQ(summary.failedSampleCount, 0U);
    }
}

TEST(Ex2EvidenceFailures, RepresentsPreValidationAndPackageFailureCategories)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    struct IncompleteCase
    {
        evidence::FailurePhase phase;
        std::string_view code;
        bool expectedGenerated;
        bool operationCompleted;
        bool outputObserved;
    };
    const std::vector<IncompleteCase> cases{
        {evidence::FailurePhase::InputGeneration,
            evidence::error_code::InputGenerationFailed, false, false, false},
        {evidence::FailurePhase::BackendInitialization,
            evidence::error_code::BackendInitializationFailed, true, false, false},
        {evidence::FailurePhase::ResourceAllocation,
            evidence::error_code::ResourceAllocationFailed, true, false, false},
        {evidence::FailurePhase::Readback,
            evidence::error_code::ReadbackFailed, true, true, false},
        {evidence::FailurePhase::Interrupted,
            evidence::error_code::Interrupted, true, true, true},
    };
    for (const auto& item : cases)
    {
        auto record = evidence::MakeSampleRecord(plan, 0U);
        record.correctness.expectedOutputGenerated = item.expectedGenerated;
        record.correctness.operationCompleted = item.operationCompleted;
        record.correctness.outputObserved = item.outputObserved;
        record.status = evidence::OperationStatus::Incomplete;
        record.failurePhase = item.phase;
        record.errorCode = std::string(item.code);
        EXPECT_NO_THROW(evidence::ValidateSampleRecord(record));
        EXPECT_FALSE(record.correctness.validationPassed.has_value());
    }

    const std::vector samples{Passing(plan, 0U)};
    const auto serializationFailure = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::EvidenceSerialization,
        std::string(evidence::error_code::EvidenceSerializationFailed));
    EXPECT_NO_THROW(static_cast<void>(
        evidence::SerializeSummaryJson(serializationFailure, samples)));
    const auto publicationFailure = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::EvidencePublication,
        std::string(evidence::error_code::EvidencePublicationFailed));
    EXPECT_NO_THROW(static_cast<void>(
        evidence::SerializeSummaryJson(publicationFailure, samples)));
}

TEST(Ex2EvidenceCorrectness, RejectsContradictoryValidationStates)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);

    auto missingValidation = Passing(plan, 0U);
    missingValidation.correctness.validationPassed.reset();
    EXPECT_THROW(evidence::ValidateSampleRecord(missingValidation),
        std::invalid_argument);

    auto hiddenMismatch = Mismatch(plan, 0U);
    hiddenMismatch.status = evidence::OperationStatus::Ok;
    hiddenMismatch.failurePhase.reset();
    hiddenMismatch.errorCode.reset();
    EXPECT_THROW(evidence::ValidateSampleRecord(hiddenMismatch),
        std::invalid_argument);

    auto preValidationFailure = evidence::MakeSampleRecord(plan, 0U);
    preValidationFailure.correctness.expectedOutputGenerated = true;
    preValidationFailure.status = evidence::OperationStatus::SubmitFailed;
    preValidationFailure.failurePhase = evidence::FailurePhase::Submission;
    preValidationFailure.errorCode =
        std::string(evidence::error_code::SubmissionFailed);
    EXPECT_NO_THROW(evidence::ValidateSampleRecord(preValidationFailure));
    EXPECT_FALSE(preValidationFailure.correctness.validationPassed.has_value());

    preValidationFailure.correctness.operationCompleted = true;
    EXPECT_THROW(evidence::ValidateSampleRecord(preValidationFailure),
        std::invalid_argument);
}

TEST(Ex2EvidenceFailures, DiagnosticReadbackFailurePreservesPartialProgress)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    auto record = evidence::MakeSampleRecord(plan, 0U);
    record.correctness.expectedOutputGenerated = true;
    record.correctness.operationCompleted = true;
    record.correctness.outputObserved = true;
    record.status = evidence::OperationStatus::Incomplete;
    record.failurePhase = evidence::FailurePhase::Readback;
    record.errorCode = std::string(evidence::error_code::ReadbackFailed);

    EXPECT_NO_THROW(evidence::ValidateSampleRecord(record));
    EXPECT_FALSE(record.correctness.comparisonPerformed);
    EXPECT_FALSE(record.correctness.validationPassed.has_value());
}

TEST(Ex2EvidenceFailures, EnforcesApprovedStatusesAndStableFailurePhases)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    struct Case
    {
        evidence::OperationStatus status;
        evidence::FailurePhase phase;
        std::string_view code;
    };
    const std::vector<Case> cases{
        {evidence::OperationStatus::SubmitFailed,
            evidence::FailurePhase::Submission,
            evidence::error_code::SubmissionFailed},
        {evidence::OperationStatus::WaitFailed,
            evidence::FailurePhase::CompletionWait,
            evidence::error_code::CompletionFailed},
        {evidence::OperationStatus::Timeout,
            evidence::FailurePhase::CompletionWait,
            evidence::error_code::OperationTimeout},
        {evidence::OperationStatus::DeviceLost,
            evidence::FailurePhase::Readback,
            evidence::error_code::DeviceLost},
        {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::ResourceAllocation,
            evidence::error_code::ResourceAllocationFailed},
    };

    for (const auto& item : cases)
    {
        auto record = evidence::MakeSampleRecord(plan, 0U);
        record.correctness.expectedOutputGenerated = true;
        record.status = item.status;
        record.failurePhase = item.phase;
        record.errorCode = std::string(item.code);
        EXPECT_NO_THROW(evidence::ValidateSampleRecord(record));
    }

    auto wrongPhase = evidence::MakeSampleRecord(plan, 0U);
    wrongPhase.status = evidence::OperationStatus::SubmitFailed;
    wrongPhase.failurePhase = evidence::FailurePhase::Readback;
    wrongPhase.errorCode = std::string(evidence::error_code::ReadbackFailed);
    EXPECT_THROW(evidence::ValidateSampleRecord(wrongPhase),
        std::invalid_argument);

    wrongPhase.failurePhase = evidence::FailurePhase::Submission;
    wrongPhase.errorCode = "private/path/not-allowed";
    EXPECT_THROW(evidence::ValidateSampleRecord(wrongPhase),
        std::invalid_argument);
}

TEST(Ex2EvidenceFailures, RejectsInvalidFailurePhaseBeforeSerialization)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    auto sample = evidence::MakeSampleRecord(plan, 0U);
    sample.status = evidence::OperationStatus::Incomplete;
    sample.failurePhase = static_cast<evidence::FailurePhase>(999);
    sample.errorCode = std::string(evidence::error_code::Interrupted);

    EXPECT_THROW(evidence::ValidateSampleRecord(sample),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(evidence::SerializeSamplesCsv(
        plan, std::span<const evidence::SampleRecord>{&sample, 1U})),
        std::invalid_argument);

    const std::vector samples{Passing(plan, 0U)};
    auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::EvidenceSerialization,
        std::string(evidence::error_code::EvidenceSerializationFailed));
    summary.failurePhase = static_cast<evidence::FailurePhase>(999);
    EXPECT_THROW(static_cast<void>(
        evidence::SerializeSummaryJson(summary, samples)),
        std::invalid_argument);
}

TEST(Ex2EvidenceIdentity, RejectsStaleIdentityAndMissingProvenance)
{
    auto plan = Plan();
    auto& linear = std::get<ex2::LinearConfiguration>(
        plan.seriesIdentity.condition.workload.parameters);
    ++linear.elementCount;
    EXPECT_THROW(evidence::ValidateCorrectnessPlan(plan), std::invalid_argument);

    auto unverified = Plan().seriesIdentity;
    unverified.condition.gpuIdentity.externallyVerified = false;
    EXPECT_THROW(static_cast<void>(
        evidence::MakeCorrectnessPlan("correctness-run", unverified)),
        std::invalid_argument);

    auto environment = Environment();
    environment.inputSha256 = "not-a-digest";
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
    environment = Environment();
    environment.expectedOutputSha256 = std::string(64U, 'A');
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
    environment = Environment();
    environment.common.machineId = "different-machine";
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
    environment = Environment();
    environment.common.gpuName.reset();
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
}

TEST(Ex2EvidenceOrdering, RejectsDuplicatesGapsAndUnverifiedSummaryTotals)
{
    const auto plan = Plan();
    auto first = Passing(plan, 0U);
    auto second = Passing(plan, 1U);
    std::vector samples{first, second};
    EXPECT_NO_THROW(evidence::ValidateSamples(plan, samples));

    samples[1].sampleIndex = 0U;
    EXPECT_THROW(evidence::ValidateSamples(plan, samples),
        std::invalid_argument);
    samples[1].sampleIndex = 2U;
    EXPECT_THROW(evidence::ValidateSamples(plan, samples),
        std::invalid_argument);
    std::swap(samples[0], samples[1]);
    EXPECT_THROW(evidence::ValidateSamples(plan, samples),
        std::invalid_argument);

    samples = {first, second};
    auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Ok);
    ++summary.recordedSampleCount;
    EXPECT_THROW(static_cast<void>(
        evidence::SerializeSummaryJson(summary, samples)),
        std::invalid_argument);
}

TEST(Ex2EvidenceIncomplete, MissingRowsNeverBecomeSuccessfulOperations)
{
    const auto plan = Plan();
    auto failure = evidence::MakeSampleRecord(plan, 0U);
    failure.correctness.expectedOutputGenerated = true;
    failure.status = evidence::OperationStatus::SubmitFailed;
    failure.failurePhase = evidence::FailurePhase::Submission;
    failure.errorCode = std::string(evidence::error_code::SubmissionFailed);
    const std::vector samples{failure};

    EXPECT_THROW(static_cast<void>(evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Ok)), std::invalid_argument);
    const auto summary = evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::Incomplete,
        evidence::FailurePhase::Interrupted,
        std::string(evidence::error_code::Interrupted));
    EXPECT_EQ(summary.recordedSampleCount, 1U);
    EXPECT_EQ(summary.validationFailures, 0U);
    EXPECT_EQ(summary.failedSampleCount, 1U);
}

TEST(Ex2EvidenceMetrics, RejectsFabricatedCorrectnessTimingAndWarmupClaims)
{
    auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    auto sample = Passing(plan, 0U);
    sample.hostCompletionNanoseconds = 0U;
    EXPECT_THROW(evidence::ValidateSampleRecord(sample), std::invalid_argument);

    auto identity = plan.seriesIdentity;
    identity.warmupCount = 1U;
    EXPECT_THROW(static_cast<void>(
        evidence::MakeCorrectnessPlan("correctness-run", identity)),
        std::invalid_argument);

    identity = plan.seriesIdentity;
    identity.condition.instrumentMode = ex2::InstrumentMode::N;
    EXPECT_THROW(static_cast<void>(
        evidence::MakeCorrectnessPlan("correctness-run", identity)),
        std::invalid_argument);

    identity = plan.seriesIdentity;
    identity.condition.workload = ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D2, 1U, 1U});
    EXPECT_THROW(static_cast<void>(
        evidence::MakeCorrectnessPlan("correctness-run", identity)),
        std::invalid_argument);

    auto environment = Environment(plan);
    environment.backendDiagnostics.nativeMarkersEnabled = true;
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
    environment = Environment(plan);
    environment.backendDiagnostics.timestampPeriodNanoseconds =
        std::numeric_limits<double>::infinity();
    EXPECT_THROW(evidence::ValidateEnvironmentRecord(environment),
        std::invalid_argument);
}

TEST(Ex2EvidenceMetrics, RejectsTimestampInvalidForCorrectnessOnlyEvidence)
{
    const auto plan = Plan(Plan().seriesIdentity.condition.workload, 1U);
    auto sample = Passing(plan, 0U);
    sample.status = evidence::OperationStatus::TimestampInvalid;
    sample.failurePhase = evidence::FailurePhase::Timing;
    sample.errorCode = std::string(evidence::error_code::TimestampInvalid);
    EXPECT_THROW(evidence::ValidateSampleRecord(sample),
        std::invalid_argument);

    const std::vector samples{Passing(plan, 0U)};
    EXPECT_THROW(static_cast<void>(evidence::SummarizeSamples(
        plan, samples, evidence::OperationStatus::TimestampInvalid,
        evidence::FailurePhase::Timing,
        std::string(evidence::error_code::TimestampInvalid))),
        std::invalid_argument);
}

TEST(Ex2EvidenceBundle, CrossChecksAllFourArtifactRecordSets)
{
    const auto environment = Environment();
    const std::vector initialization{Initialization(environment.plan)};
    const std::vector samples{
        Passing(environment.plan, 0U), Passing(environment.plan, 1U)};
    const auto summary = evidence::SummarizeSamples(
        environment.plan, samples, evidence::OperationStatus::Ok);
    EXPECT_NO_THROW(evidence::ValidateEvidenceBundle(
        environment, initialization, samples, summary));

    auto drifted = summary;
    drifted.plan.runId = "different-run";
    EXPECT_THROW(evidence::ValidateEvidenceBundle(
        environment, initialization, samples, drifted), std::invalid_argument);
}

} // namespace
