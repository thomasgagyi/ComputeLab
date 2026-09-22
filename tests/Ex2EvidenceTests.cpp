#include "ex2/Ex2Evidence.hpp"
#include "ex2/Ex2Gate0.hpp"

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
