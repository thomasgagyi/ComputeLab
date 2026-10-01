#include "ex2/Ex2Stage5Evidence.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Gate0.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <type_traits>

namespace
{
namespace ex2 = computelab::ex2;
namespace s5 = ex2::stage5;
namespace ev = s5::evidence;
namespace old = ex2::evidence;
using Phase = s5::Stage5Phase;
using Status = ev::Status;
using Failure = ev::FailurePhase;

// Literal canonical JSON was hashed independently using .NET SHA256.
constexpr std::string_view ACondition = "6cab3bba434243b62aa8ffef42f6594d3b78219474cc4b02d1831d113a9e5876";
constexpr std::string_view ASeries = "026b680f683e26b61aa36d788b953e74cefefb27130e68b920239ab3db4c5372";
constexpr std::string_view DCondition = "d148ece5f4ce64cc74c17a8b3082ee3b1000e0122a6c816a82a1eb6e03a15203";
constexpr std::string_view DSeries = "8df87812f758835389fd7b4d0d817d338bee860969e377569bd7d6c83e7a02b6";

ev::FoundationRequest Request(Phase phase = Phase::A1Sentinel, std::size_t planIndex = 0U)
{
    const bool a1 = phase == Phase::A1Sentinel, sample = phase == Phase::D1Sample;
    return {{phase, a1 ? s5::Stage5Workload::A1 : s5::Stage5Workload::D1,
        a1 ? 256U : 1048576U, a1 ? std::nullopt : std::optional<std::uint64_t>{64U}, "H",
        sample ? 0U : 48U, sample ? std::optional<std::uint64_t>{2U} : std::nullopt, sample ? 200U : 0U},
        s5::FrozenProcessPlan()[planIndex], "stage5-run", "anonymous-machine",
        {"00112233-4455-6677-8899-aabbccddeeff", true}, std::string(40, 'a'), std::string(64, 'b'),
        s5::FrozenProcessPlan()[planIndex].backend == s5::Stage5Backend::Vulkan
            ? std::optional<std::string>{std::string(64, 'c')} : std::nullopt};
}

ev::Foundation Foundation(Phase phase = Phase::A1Sentinel, std::size_t planIndex = 0U)
{
    const auto request = Request(phase, planIndex);
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, request.condition.elementCount);
    const auto expected = phase == Phase::A1Sentinel ? ex2::ReferenceA1(input) : ex2::ReferenceD1(input, 64).finalState;
    return ev::MakeWordFoundation(request, input, expected);
}

computelab::results::EnvironmentRecord Common(const ev::Foundation& f)
{
    const auto& p = f.Identity();
    return {2, "EX-2", p.runId, "2026-10-01T00:00:00Z", std::string(40, 'a'), false,
        "anonymous-machine", "Windows", "11", "Synthetic CPU", 16,
        "Synthetic GPU", "NVIDIA", "10DE:1234", 8, "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", false, false};
}
ev::BackendDiagnostics Diagnostics(const ev::Foundation& f)
{
    if (f.Identity().process.backend == s5::Stage5Backend::Cuda)
        return {.implementation = "synthetic-cuda", .streamFlags = "nonblocking"};
    return {.implementation = "synthetic-vulkan", .queueFamilyIndex = 0, .queueFlags = 2, .queueCount = 1};
}
template <typename T> void Pass(T& row, std::uint64_t submission = 10, std::uint64_t wait = 20)
{
    row.correctness = {true, true, true, true, true}; row.status = Status::Ok;
    row.failurePhase.reset(); row.errorCode.reset();
    row.hostSubmissionNanoseconds = submission; row.hostWaitNanoseconds = wait;
    row.hostCompletionNanoseconds = submission + wait;
}
void Mismatch(ev::OperationRecord& row)
{
    row.correctness.validationPassed = false; row.status = Status::ValidationFailed;
    row.failurePhase = Failure::Validation; row.errorCode = std::string(old::error_code::OutputMismatch);
}
std::vector<ev::HostClockRecord> Clock(const ev::Foundation& f, std::optional<std::uint64_t> delta = 1)
{
    std::vector<ev::HostClockRecord> rows;
    for (unsigned i = 0; i < 4095; ++i) rows.push_back(ev::MakeHostClockRecord(f, i, delta));
    return rows;
}
std::vector<ev::WarmupRecord> Warmup(const ev::Foundation& f)
{
    std::vector<ev::WarmupRecord> rows;
    if (f.Identity().condition.phase != Phase::D1Sample)
        for (unsigned i = 0; i < 48; ++i) { auto r = ev::MakeWarmupRecord(f, i); Pass(r); rows.push_back(r); }
    return rows;
}
std::vector<ev::SampleRecord> Samples(const ev::Foundation& f)
{
    std::vector<ev::SampleRecord> rows;
    if (f.Identity().condition.phase == Phase::D1Sample)
        for (unsigned i = 0; i < 200; ++i) { auto r = ev::MakeSampleRecord(f, i); Pass(r); rows.push_back(r); }
    return rows;
}

TEST(Ex2Stage5EvidenceIdentity, LiteralA1CanonicalConditionAndHardCodedDigest)
{
    const auto f = Foundation();
    EXPECT_EQ(ex2::ComparisonConditionCanonicalJson(f.Identity().seriesIdentity.condition),
        "{\"byte_count\":null,\"counter_count\":null,\"element_count\":256,\"execution_mode\":\"ordinary\","
        "\"generator_revision\":\"ex2-mix64-v1\",\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\","
        "\"index_pattern\":null,\"instrument_mode\":\"H\",\"iteration_count\":null,\"machine_id\":\"anonymous-machine\","
        "\"operation_boundary\":\"single-dispatch-completion\",\"protocol_version\":\"1.2\",\"seed\":81985529216486895,"
        "\"transfer_direction\":null,\"variant\":\"A1\",\"workload\":\"A\"}");
    EXPECT_EQ(f.Identity().comparisonConditionId, ACondition); EXPECT_EQ(f.Identity().seriesId, ASeries);
    EXPECT_EQ(Foundation().Identity().seriesId, ASeries);
}

TEST(Ex2Stage5EvidenceIdentity, BackendNeutralConditionAndEverySeriesField)
{
    const auto cuda = Foundation(), vulkan = Foundation(Phase::A1Sentinel, 1);
    EXPECT_EQ(cuda.Identity().comparisonConditionId, vulkan.Identity().comparisonConditionId);
    EXPECT_NE(cuda.Identity().seriesId, vulkan.Identity().seriesId);
    const auto baseline = cuda.Identity().seriesIdentity;
    for (int field = 0; field < 6; ++field)
    {
        auto changed = baseline;
        if (field == 0) ++changed.processIndex;
        if (field == 1) ++changed.blockIndex;
        if (field == 2) ++changed.orderSlot;
        if (field == 3) changed.sourceRevision[0] = 'd';
        if (field == 4) changed.executableSha256[0] = 'd';
        if (field == 5) ++changed.warmupCount;
        EXPECT_NE(ex2::SeriesId(changed), ex2::SeriesId(baseline));
    }
    auto changed = vulkan.Identity().seriesIdentity; changed.shaderSha256->front() = 'd';
    EXPECT_NE(ex2::SeriesId(changed), vulkan.Identity().seriesId);
    auto condition = baseline.condition; condition.protocolVersion = "1.1";
    EXPECT_NE(ex2::ComparisonConditionId(condition), ACondition);
}

TEST(Ex2Stage5EvidenceFoundation, ExactTypedA1AndD1AndPhaseSeriesMapping)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto f = Foundation(phase); const auto& p = f.Identity();
        EXPECT_EQ(p.seriesIdentity.condition.workload, ev::WorkloadFor(Request(phase).condition));
        EXPECT_EQ(p.seriesIdentity.condition.protocolVersion, "1.2");
        EXPECT_EQ(p.seriesIdentity.warmupCount, phase == Phase::D1Sample ? 2U : 0U);
        EXPECT_EQ(p.seriesIdentity.plannedSampleCount, phase == Phase::D1Sample ? 200U : 0U);
        EXPECT_EQ(p.seriesIdentity.condition.workload.common.seed, ex2::CoreInputSeed);
        if (phase == Phase::D1Sample)
        {
            EXPECT_EQ(p.comparisonConditionId, DCondition); EXPECT_EQ(p.seriesId, DSeries);
            auto changed = p.seriesIdentity; changed.warmupCount = 4;
            EXPECT_NE(ex2::SeriesId(changed), p.seriesId);
        }
    }
}

TEST(Ex2Stage5EvidenceFoundation, RejectsWrongConditionsModesCountsAndD2)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256); const auto expected = ex2::ReferenceA1(input);
    for (int defect = 0; defect < 12; ++defect)
    {
        auto r = Request();
        if (defect == 0) r.condition.elementCount = 257;
        if (defect == 1) r.condition.workload = s5::Stage5Workload::D1;
        if (defect == 2) r.condition.iterationCount = 64;
        if (defect == 3) r.condition.instrumentMode = "N";
        if (defect == 4) r.condition.instrumentMode = "P";
        if (defect == 5) r.condition.diagnosticCount = 47;
        if (defect == 6) r.condition.measuredSampleCount = 1;
        if (defect == 7) r.condition.selectedW = 0;
        if (defect == 8) { r = Request(Phase::D1Warmup); r.condition.elementCount = 256; }
        if (defect == 9) { r = Request(Phase::D1Warmup); r.condition.iterationCount = 63; }
        if (defect == 10) { r = Request(Phase::D1Sample); r.condition.selectedW = 32; }
        if (defect == 11) r.condition.phase = static_cast<Phase>(99);
        EXPECT_THROW((void)ev::MakeWordFoundation(r, input, expected), std::invalid_argument);
    }
    auto plan = Foundation(Phase::D1Warmup).Identity();
    plan.seriesIdentity.condition.workload = ex2::MakeConfiguration(ex2::IterativeConfiguration{ex2::IterativeVariant::D2, 1048576, 64});
    EXPECT_THROW(ev::ValidatePlan(plan), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceFoundation, RejectsMalformedProvenanceHardwareShaderRunAndProcess)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256); const auto expected = ex2::ReferenceA1(input);
    for (int defect = 0; defect < 15; ++defect)
    {
        auto r = Request();
        if (defect == 0) r.gpuIdentity.externallyVerified = false;
        if (defect == 1) r.gpuIdentity.uuid[10] = 'A';
        if (defect == 2) r.gpuIdentity.uuid = "00000000-0000-0000-0000-000000000000";
        if (defect == 3) r.sourceRevision = "short";
        if (defect == 4) r.executableSha256[0] = 'B';
        if (defect == 5) r.shaderSha256 = std::string(64, 'c');
        if (defect == 6) { r = Request(Phase::A1Sentinel, 1); r.shaderSha256.reset(); }
        if (defect == 7) { r = Request(Phase::A1Sentinel, 1); r.shaderSha256 = "bad"; }
        if (defect == 8) r.process.blockIndex = 1;
        if (defect == 9) r.process.orderSlot = 1;
        if (defect == 10) r.process.processIndex = 5;
        if (defect == 11) r.runId = "../private";
        if (defect == 12) r.runId.clear();
        if (defect == 13) r.machineId = "private/path";
        if (defect == 14) r.process.backend = static_cast<s5::Stage5Backend>(99);
        EXPECT_THROW((void)ev::MakeWordFoundation(r, input, expected), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceFoundation, DerivesWordDigestsAndRejectsChangedInputOrExpected)
{
    auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256); auto expected = ex2::ReferenceA1(input);
    const auto f = ev::MakeWordFoundation(Request(), input, expected);
    // Independently encoded LE words and hashed with .NET SHA256.
    EXPECT_EQ(f.InputSha256(), "f9d00b3f9c3cb0a45e8574729e9f5d12a7a9ca579e0bd0c546c644227f9fce3f");
    EXPECT_EQ(f.ExpectedOutputSha256(), "456fce584ca45a00fb9528cc00d3487a0be5885118dc8a9d968569588f164532");
    input[0] ^= 1; EXPECT_THROW((void)ev::MakeWordFoundation(Request(), input, expected), std::invalid_argument);
    input[0] ^= 1; expected[255] ^= 1;
    EXPECT_THROW((void)ev::MakeWordFoundation(Request(), input, expected), std::invalid_argument);
    input.pop_back(); EXPECT_THROW((void)ev::MakeWordFoundation(Request(), input, expected), std::invalid_argument);
    input.push_back(0); expected.pop_back();
    EXPECT_THROW((void)ev::MakeWordFoundation(Request(), input, expected), std::invalid_argument);
    static_assert(!std::is_default_constructible_v<ev::Foundation>);
}

TEST(Ex2Stage5EvidenceFoundation, WrongD1ExpectedAndVulkanD1ShaderRejected)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 1048576);
    auto expected = ex2::ReferenceD1(input, 64).finalState; expected.back() ^= 1;
    EXPECT_THROW((void)ev::MakeWordFoundation(Request(Phase::D1Warmup), input, expected), std::invalid_argument);
    auto r = Request(Phase::D1Warmup, 1); r.shaderSha256.reset();
    EXPECT_THROW((void)ev::MakeWordFoundation(r, input, expected), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceFoundation, RejectsDriftingRawPlanProtocolAndWorkload)
{
    const auto f = Foundation();
    for (int defect = 0; defect < 9; ++defect)
    {
        auto p = f.Identity();
        if (defect == 0) p.seriesIdentity.condition.protocolVersion = "1.1";
        if (defect == 1) p.seriesIdentity.condition.instrumentMode = ex2::InstrumentMode::N;
        if (defect == 2) p.seriesIdentity.condition.workload.common.seed++;
        if (defect == 3) p.seriesIdentity.condition.workload.common.generatorRevision = "other";
        if (defect == 4) p.seriesIdentity.plannedSampleCount = 1;
        if (defect == 5) p.seriesIdentity.warmupCount = 1;
        if (defect == 6) p.comparisonConditionId[0] = 'x';
        if (defect == 7) p.seriesId[0] = 'x';
        if (defect == 8) p.seriesIdentity.orderSlot = 1;
        EXPECT_THROW(ev::ValidatePlan(p), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceSchema, PinsEveryLiteralHeaderAndHistoricalEquality)
{
    EXPECT_EQ(ev::InitializationCsvHeader(), "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation");
    EXPECT_EQ(ev::SamplesCsvHeader(), "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns");
    EXPECT_EQ(ev::WarmupCsvHeader(), "schema_version,run_id,series_id,process_index,sequence_index,host_submission_ns,host_wait_ns,host_completion_ns,status");
    EXPECT_EQ(ev::HostClockHeader(), "schema_version,run_id,series_id,process_index,sequence_index,delta_ns");
    EXPECT_EQ(ev::SamplesCsvHeader(), ex2::gate0::SamplesCsvHeader());
    EXPECT_EQ(ev::InitializationCsvHeader(), old::InitializationCsvHeader());
    EXPECT_EQ(old::EvidenceKind, "correctness");
}

TEST(Ex2Stage5EvidenceSchema, LiteralInitializationAndClockPositiveZeroNullRows)
{
    const auto f = Foundation();
    const auto init = ev::MakeSetupCompleteInitialization(f, "ready,\"quoted\"\r\nline");
    EXPECT_EQ(ev::SerializeInitializationCsv(f, {&init, 1}), ev::InitializationCsvHeader() + "\r\n"
        "2,stage5-run,EX-2,cuda,0,0,backend_setup,A,A1,256,setup_complete,,\"ready,\"\"quoted\"\"\r\nline\"\r\n");
    EXPECT_EQ(ev::SerializeHostClockRow(f, ev::MakeHostClockRecord(f, 0, 7)),
        "2,stage5-run," + std::string(ASeries) + ",0,0,7\r\n");
    EXPECT_EQ(ev::SerializeHostClockRow(f, ev::MakeHostClockRecord(f, 1, 0)),
        "2,stage5-run," + std::string(ASeries) + ",0,1,0\r\n");
    EXPECT_EQ(ev::SerializeHostClockRow(f, ev::MakeHostClockRecord(f, 2, std::nullopt)),
        "2,stage5-run," + std::string(ASeries) + ",0,2,\r\n");
}

TEST(Ex2Stage5EvidenceSchema, LiteralWarmupSuccessAndRetainedFailure)
{
    const auto f = Foundation(); auto row = ev::MakeWarmupRecord(f, 47); Pass(row, 1, 2);
    EXPECT_EQ(ev::SerializeWarmupRow(f, row), "2,stage5-run," + std::string(ASeries) + ",0,47,1,2,3,ok\r\n");
    Mismatch(row);
    EXPECT_EQ(ev::SerializeWarmupRow(f, row), "2,stage5-run," + std::string(ASeries) + ",0,47,1,2,3,validation_failed\r\n");
}

TEST(Ex2Stage5EvidenceSchema, LiteralD1SuccessAndFailureSampleRows)
{
    const auto f = Foundation(Phase::D1Sample); auto row = ev::MakeSampleRecord(f, 0); Pass(row);
    const std::string prefix = "2,stage5-run,EX-2," + std::string(DCondition) + ',' + std::string(DSeries)
        + ",cuda,D,D1,81985529216486895,1048576,,,,64,,ordinary,H,2,200,0,0,0,0,";
    EXPECT_EQ(ev::SerializeSampleRow(f, row), prefix + "true,ok,,,10,20,30,\r\n");
    Mismatch(row);
    EXPECT_EQ(ev::SerializeSampleRow(f, row), prefix + "false,validation_failed,validation,output_mismatch,10,20,30,\r\n");
}

TEST(Ex2Stage5EvidenceEnvironment, ExactLiteralJson)
{
    const auto f = Foundation(); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    EXPECT_EQ(ev::SerializeEnvironmentJson(f, env),
        "{\"schema_version\":2,\"experiment_id\":\"EX-2\",\"run_id\":\"stage5-run\",\"timestamp_utc\":\"2026-10-01T00:00:00Z\","
        "\"git_commit\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"git_dirty\":false,\"machine_id\":\"anonymous-machine\","
        "\"os_name\":\"Windows\",\"os_version\":\"11\",\"cpu_name\":\"Synthetic CPU\",\"system_memory_bytes\":16,"
        "\"gpu_name\":\"Synthetic GPU\",\"gpu_vendor\":\"NVIDIA\",\"gpu_device_id\":\"10DE:1234\",\"gpu_memory_bytes\":8,"
        "\"nvidia_driver_version\":\"driver\",\"cuda_toolkit_version\":\"toolkit\",\"cuda_runtime_version\":\"runtime\","
        "\"cuda_compute_capability\":\"7.5\",\"vulkan_sdk_version\":\"sdk\",\"vulkan_device_api_version\":\"1.4\","
        "\"compiler_name\":\"MSVC\",\"compiler_version\":\"19\",\"cmake_version\":\"4\",\"ninja_version\":\"1\","
        "\"configure_preset\":\"x64-debug\",\"build_type\":\"Debug\",\"validation_enabled\":false,\"diagnostic_instrumentation\":false,"
        "\"protocol_version\":\"1.2\",\"evidence_kind\":\"qualification\",\"backend\":\"cuda\",\"instrument_mode\":\"H\","
        "\"warmup_count\":0,\"planned_sample_count\":0,\"comparison_condition_id\":\"" + std::string(ACondition)
        + "\",\"series_id\":\"" + std::string(ASeries) + "\",\"block_index\":0,\"process_index\":0,\"order_slot\":0,"
        "\"source_revision\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"executable_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"shader_sha256\":null,\"input_sha256\":\"f9d00b3f9c3cb0a45e8574729e9f5d12a7a9ca579e0bd0c546c644227f9fce3f\","
        "\"expected_output_sha256\":\"456fce584ca45a00fb9528cc00d3487a0be5885118dc8a9d968569588f164532\","
        "\"gpu_uuid_identity\":\"00112233-4455-6677-8899-aabbccddeeff\",\"workload\":\"A\",\"variant\":\"A1\","
        "\"generator_revision\":\"ex2-mix64-v1\",\"seed\":81985529216486895,\"element_count\":256,\"byte_count\":null,"
        "\"index_pattern\":null,\"counter_count\":null,\"iteration_count\":null,\"transfer_direction\":null,\"execution_mode\":\"ordinary\","
        "\"operation_boundary\":\"single-dispatch-completion\",\"backend_native\":{\"implementation\":\"synthetic-cuda\",\"stream_flags\":\"nonblocking\","
        "\"queue_family_index\":null,\"queue_flags\":null,\"queue_count\":null,\"timestamp_valid_bits\":null,\"timestamp_period_ns\":null,"
        "\"input_memory_flags\":null,\"output_memory_flags\":null,\"upload_memory_flags\":null,\"readback_memory_flags\":null,"
        "\"native_markers_enabled\":false,\"native_timing_method\":null,\"native_timing_resolution_ns\":null,\"native_timing_start_stage\":null,"
        "\"native_timing_stop_stage\":null,\"native_duration_envelope_ns\":null}}\n");
}

TEST(Ex2Stage5EvidenceEnvironment, RejectsDirtyMissingPrivateDriftingAndInstrumentedProvenance)
{
    const auto f = Foundation(); const auto good = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    for (int defect = 0; defect < 17; ++defect)
    {
        auto r = good;
        if (defect == 0) r.common.gitDirty = true;
        if (defect == 1) r.common.validationEnabled = true;
        if (defect == 2) r.common.diagnosticInstrumentation = true;
        if (defect == 3) r.common.gitCommit[0] = 'b';
        if (defect == 4) r.common.runId = "other";
        if (defect == 5) r.common.machineId = "other";
        if (defect == 6) r.common.osName.clear();
        if (defect == 7) r.common.gpuName.reset();
        if (defect == 8) r.common.cudaRuntimeVersion.reset();
        if (defect == 9) r.common.compilerName = "C:\\private\\compiler";
        if (defect == 10) r.inputSha256[0] = 'e';
        if (defect == 11) r.expectedOutputSha256[0] = 'e';
        if (defect == 12) r.backendDiagnostics.nativeMarkersEnabled = true;
        if (defect == 13) r.backendDiagnostics.timestampPeriodNanoseconds = std::numeric_limits<double>::quiet_NaN();
        if (defect == 14) r.backendDiagnostics.nativeTimingResolutionNanoseconds = 1;
        if (defect == 15) r.evidenceKind = "correctness";
        if (defect == 16) r.common.schemaVersion = 1;
        EXPECT_THROW((void)ev::SerializeEnvironmentJson(f, r), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceEnvironment, VulkanDiagnosticsAndNullBackendFields)
{
    const auto f = Foundation(Phase::A1Sentinel, 1); auto common = Common(f);
    common.cudaToolkitVersion.reset(); common.cudaRuntimeVersion.reset(); common.cudaComputeCapability.reset();
    const auto env = ev::MakeEnvironmentRecord(common, f, Diagnostics(f));
    EXPECT_NE(ev::SerializeEnvironmentJson(f, env).find("\"cuda_runtime_version\":null"), std::string::npos);
    common.vulkanSdkVersion.reset();
    EXPECT_THROW((void)ev::MakeEnvironmentRecord(common, f, Diagnostics(f)), std::invalid_argument);
    auto d = Diagnostics(f); d.streamFlags = "default";
    EXPECT_THROW((void)ev::MakeEnvironmentRecord(Common(f), f, d), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceInitialization, RequiresTruthfulUntimedSetupAndAttribution)
{
    const auto f = Foundation(); const auto good = ev::MakeSetupCompleteInitialization(f, "ready");
    EXPECT_THROW(ev::ValidateInitializationRecords(f, {}), std::invalid_argument);
    for (int defect = 0; defect < 8; ++defect)
    {
        auto r = good;
        if (defect == 0) r.durationNanoseconds = 0;
        if (defect == 1) r.observation.reset();
        if (defect == 2) r.runId = "other";
        if (defect == 3) r.processIndex = 1;
        if (defect == 4) r.sequenceIndex = 1;
        if (defect == 5) r.variant = "A2";
        if (defect == 6) r.backend = ex2::Backend::Vulkan;
        if (defect == 7) r.metric = "other";
        EXPECT_THROW(ev::ValidateInitializationRecords(f, {&r, 1}), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceClock, ExactCountSequenceAndIdentityWithRetainedNulls)
{
    const auto f = Foundation(); const auto good = Clock(f);
    for (int defect = 0; defect < 7; ++defect)
    {
        auto rows = good;
        if (defect == 0) rows.pop_back();
        if (defect == 1) rows.push_back(rows.back());
        if (defect == 2) rows[100].sequenceIndex = 99;
        if (defect == 3) rows[100].sequenceIndex = 101;
        if (defect == 4) rows[100].runId = "other";
        if (defect == 5) rows[100].seriesId = "other";
        if (defect == 6) rows[100].processIndex = 1;
        EXPECT_THROW(ev::ValidateHostClockRecords(f, rows), std::invalid_argument);
    }
    auto nulls = Clock(f, std::nullopt);
    EXPECT_NO_THROW(ev::ValidateHostClockRecords(f, nulls));
    EXPECT_TRUE(ev::SerializeHostClockCsv(f, nulls).ends_with(",4094,\r\n"));
}

TEST(Ex2Stage5EvidenceBundle, ExactThreePhaseBundlesAndEmptyArtifactHeaders)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto f = Foundation(phase); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
        const std::vector init{ev::MakeSetupCompleteInitialization(f, "ready")};
        const auto clock = Clock(f); const auto warmup = Warmup(f); const auto samples = Samples(f);
        const auto summary = ev::SummarizeSamples(f, samples, Status::Ok);
        EXPECT_NO_THROW(ev::ValidateBundle(f, env, init, clock, warmup, samples, summary));
        EXPECT_EQ(clock.size(), 4095U); EXPECT_EQ(warmup.size(), phase == Phase::D1Sample ? 0U : 48U);
        EXPECT_EQ(samples.size(), phase == Phase::D1Sample ? 200U : 0U);
        if (phase == Phase::D1Sample) EXPECT_EQ(ev::SerializeWarmupCsv(f, warmup), ev::WarmupCsvHeader() + "\r\n");
        else
        {
            EXPECT_EQ(ev::SerializeSamplesCsv(f, samples), ev::SamplesCsvHeader() + "\r\n");
            EXPECT_FALSE(summary.hostSubmissionNanoseconds); EXPECT_FALSE(summary.hostWaitNanoseconds);
            EXPECT_FALSE(summary.hostCompletionNanoseconds);
        }
    }
}

TEST(Ex2Stage5EvidenceBundle, RawCoarseClockAndOrderedSwitchDoNotInvalidateEvidence)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto f = Foundation(phase); auto warmup = Warmup(f); auto samples = Samples(f);
        for (std::size_t i = 0; i < warmup.size(); ++i) Pass(warmup[i], 10, i < 12 ? 2000 : 20);
        for (std::size_t i = 0; i < samples.size(); ++i) Pass(samples[i], 10, i >= 50 && i < 100 ? 2000 : 20);
        const auto summary = ev::SummarizeSamples(f, samples, Status::Ok);
        const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
        const std::vector init{ev::MakeSetupCompleteInitialization(f, "ready")};
        EXPECT_NO_THROW(ev::ValidateBundle(f, env, init, Clock(f, std::numeric_limits<std::uint64_t>::max()), warmup, samples, summary));
        EXPECT_NO_THROW(ev::ValidateHostClockRecords(f, Clock(f, 0)));
    }
}

TEST(Ex2Stage5EvidenceBundle, RejectsWarmupCountSequenceAndWrongPhase)
{
    const auto f = Foundation(); const auto good = Warmup(f);
    for (int defect = 0; defect < 4; ++defect)
    {
        auto rows = good;
        if (defect == 0) rows.pop_back();
        if (defect == 1) rows.push_back(rows.back());
        if (defect == 2) rows[10].sequenceIndex = 9;
        if (defect == 3) rows[10].sequenceIndex = 11;
        EXPECT_THROW(ev::ValidateWarmupRecords(f, rows), std::invalid_argument);
    }
    const auto d1 = Foundation(Phase::D1Sample);
    EXPECT_THROW(ev::ValidateWarmupRecords(d1, good), std::invalid_argument);
    EXPECT_THROW((void)ev::MakeWarmupRecord(d1, 0), std::invalid_argument);
    EXPECT_THROW((void)ev::MakeSampleRecord(f, 0), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceBundle, RejectsSampleCountSequenceCrossProcessAndStaleIdentity)
{
    const auto f = Foundation(Phase::D1Sample); const auto good = Samples(f);
    for (int defect = 0; defect < 11; ++defect)
    {
        auto rows = good;
        if (defect == 0) rows.pop_back();
        if (defect == 1) rows.push_back(rows.back());
        if (defect == 2) rows[10].sequenceIndex = 9;
        if (defect == 3) rows[10].sequenceIndex = 11;
        if (defect == 4) rows[10].plan.runId = "other";
        if (defect == 5) rows[10].plan.seriesId = "stale";
        if (defect == 6) rows[10].plan.comparisonConditionId = "stale";
        if (defect == 7) rows[10].plan.seriesIdentity.processIndex = 1;
        if (defect == 8) rows[10].plan.seriesIdentity.backend = ex2::Backend::Vulkan;
        if (defect == 9) rows[10].plan.seriesIdentity.blockIndex = 1;
        if (defect == 10) rows[10].plan.seriesIdentity.orderSlot = 1;
        EXPECT_THROW(ev::ValidateSampleRecords(f, rows), std::invalid_argument);
    }
    EXPECT_THROW(ev::ValidateSampleRecords(Foundation(), good), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceOperation, RejectsNativeAndContradictoryTimingAndValidation)
{
    const auto f = Foundation(); auto good = ev::MakeWarmupRecord(f, 0); Pass(good);
    for (int defect = 0; defect < 11; ++defect)
    {
        auto r = good;
        if (defect == 0) r.nativeDeviceIntervalNanoseconds = 1;
        if (defect == 1) r.correctness.validationPassed = false;
        if (defect == 2) r.correctness.validationPassed.reset();
        if (defect == 3) r.hostCompletionNanoseconds = 29;
        if (defect == 4) r.hostSubmissionNanoseconds = std::numeric_limits<std::uint64_t>::max();
        if (defect == 5) r.hostSubmissionNanoseconds.reset();
        if (defect == 6) r.hostWaitNanoseconds.reset();
        if (defect == 7) r.hostCompletionNanoseconds.reset();
        if (defect == 8) r.correctness.operationCompleted = false;
        if (defect == 9) r.failurePhase = Failure::Submission;
        if (defect == 10) { Mismatch(r); r.correctness.validationPassed = true; }
        EXPECT_THROW(ev::ValidateOperationRecord(f, r), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceOperation, HonestFailurePrefixesAndCompletedValidationFailure)
{
    const auto f = Foundation(); auto row = ev::MakeWarmupRecord(f, 0);
    row.correctness.expectedOutputGenerated = true; row.status = Status::SubmitFailed;
    row.failurePhase = Failure::Submission; row.errorCode = std::string(old::error_code::SubmissionFailed);
    EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
    row.hostSubmissionNanoseconds = 10; EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
    row.hostWaitNanoseconds = 20; EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
    row.status = Status::WaitFailed; row.failurePhase = Failure::CompletionWait;
    row.errorCode = std::string(old::error_code::CompletionFailed);
    EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
    row.hostWaitNanoseconds.reset();
    EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
    row.hostCompletionNanoseconds = 30; Pass(row); Mismatch(row);
    EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
    row.status = Status::TimestampInvalid; EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
}

void ExpectFailedWaitWithoutT2(Status status, const char* error)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto f = Foundation(phase);
        const auto check = [&](auto row)
        {
            row.correctness.expectedOutputGenerated = true;
            row.status = status; row.failurePhase = Failure::CompletionWait; row.errorCode = error;
            const auto serialize = [&]()
            {
                if constexpr (std::is_same_v<decltype(row), ev::SampleRecord>)
                    return ev::SerializeSampleRow(f, row);
                else return ev::SerializeWarmupRow(f, row);
            };
            // Missing t1 is retainable; a successful submission prefix is also
            // retainable. Neither establishes a successful t2.
            EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
            EXPECT_NO_THROW((void)serialize());
            row.hostSubmissionNanoseconds = 10;
            EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
            const auto csv = serialize();
            if constexpr (std::is_same_v<decltype(row), ev::SampleRecord>)
                EXPECT_TRUE(csv.ends_with(",10,,,\r\n"));
            else EXPECT_TRUE(csv.ends_with(",10,,," + std::string(old::ToString(status)) + "\r\n"));
            for (int fabrication = 0; fabrication < 3; ++fabrication)
            {
                row.hostWaitNanoseconds = fabrication == 2 ? std::nullopt : std::optional<std::uint64_t>{20};
                row.hostCompletionNanoseconds = fabrication == 0 ? std::nullopt : std::optional<std::uint64_t>{30};
                EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
                EXPECT_THROW((void)serialize(), std::invalid_argument);
            }
            // Zero is still a supplied duration, not a null timing field.
            row.hostWaitNanoseconds = 0; row.hostCompletionNanoseconds.reset();
            EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
        };
        if (phase == Phase::D1Sample) check(ev::MakeSampleRecord(f, 0));
        else check(ev::MakeWarmupRecord(f, 0));
    }
}

TEST(Ex2Stage5EvidenceOperation, WaitFailedCannotCarryT2DerivedTiming)
{
    ExpectFailedWaitWithoutT2(Status::WaitFailed, "completion_failed");
}
TEST(Ex2Stage5EvidenceOperation, CompletionWaitTimeoutCannotCarryT2DerivedTiming)
{
    ExpectFailedWaitWithoutT2(Status::Timeout, "operation_timeout");
}
TEST(Ex2Stage5EvidenceOperation, CompletionWaitDeviceLostCannotCarryT2DerivedTiming)
{
    ExpectFailedWaitWithoutT2(Status::DeviceLost, "device_lost");
}
TEST(Ex2Stage5EvidenceOperation, CompletionWaitIncompleteCannotCarryT2DerivedTiming)
{
    ExpectFailedWaitWithoutT2(Status::Incomplete, "completion_failed");
}

TEST(Ex2Stage5EvidenceOperation, RejectsPreFoundationFailuresAndContradictoryStatusPhase)
{
    const auto f = Foundation();
    for (const auto phase : {Failure::Configuration, Failure::InputGeneration, Failure::CompletionWait})
    {
        auto r = ev::MakeWarmupRecord(f, 0); r.correctness.expectedOutputGenerated = true;
        r.status = Status::SubmitFailed; r.failurePhase = phase; r.errorCode = std::string(old::error_code::SubmissionFailed);
        EXPECT_THROW(ev::ValidateOperationRecord(f, r), std::invalid_argument);
    }
    auto r = ev::MakeWarmupRecord(f, 0); Pass(r); Mismatch(r); r.errorCode = "other";
    EXPECT_THROW(ev::ValidateOperationRecord(f, r), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceOperation, RetainsExistingNativeFailureVocabularyWithoutInventedTiming)
{
    const auto f = Foundation();
    for (const auto phase : {Failure::BackendInitialization, Failure::Submission, Failure::CompletionWait, Failure::Readback})
    {
        auto row = ev::MakeWarmupRecord(f, 0); row.correctness.expectedOutputGenerated = true;
        row.status = Status::DeviceLost; row.failurePhase = phase; row.errorCode = std::string(old::error_code::DeviceLost);
        if (phase == Failure::Readback)
        {
            row.correctness.operationCompleted = true;
            row.hostSubmissionNanoseconds = 1; row.hostWaitNanoseconds = 2; row.hostCompletionNanoseconds = 3;
        }
        EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
        row.correctness.validationPassed = true;
        EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
    }
    for (const auto phase : {Failure::Submission, Failure::CompletionWait})
    {
        auto row = ev::MakeWarmupRecord(f, 0); row.correctness.expectedOutputGenerated = true;
        row.status = Status::Timeout; row.failurePhase = phase; row.errorCode = std::string(old::error_code::OperationTimeout);
        EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
    }
    const std::array<std::pair<Failure, const char*>, 7> failures{{
        {Failure::BackendInitialization, "backend_initialization_failed"},
        {Failure::ResourceAllocation, "resource_allocation_failed"}, {Failure::Readback, "readback_failed"},
        {Failure::Timing, "timestamp_invalid"}, {Failure::EvidenceSerialization, "evidence_serialization_failed"},
        {Failure::EvidencePublication, "evidence_publication_failed"}, {Failure::Interrupted, "interrupted"}}};
    for (const auto& [phase, code] : failures)
    {
        auto row = ev::MakeWarmupRecord(f, 0); row.correctness.expectedOutputGenerated = true;
        row.status = Status::Incomplete; row.failurePhase = phase; row.errorCode = code;
        if (phase == Failure::Readback)
        {
            row.correctness.operationCompleted = true;
            row.hostSubmissionNanoseconds = 1; row.hostWaitNanoseconds = 2; row.hostCompletionNanoseconds = 3;
        }
        EXPECT_NO_THROW(ev::ValidateOperationRecord(f, row));
        row.errorCode = "contradictory_code";
        EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
    }
}

TEST(Ex2Stage5EvidenceBundle, DiagnosticFailuresRemainRetainedButCannotClaimSuccessfulProcess)
{
    const auto f = Foundation(); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    const std::vector init{ev::MakeSetupCompleteInitialization(f, "ready")};
    auto warmup = Warmup(f); Mismatch(warmup[10]);
    const auto failure = ev::SummarizeSamples(f, {}, Status::ValidationFailed, Failure::Validation, std::string(old::error_code::OutputMismatch));
    EXPECT_EQ(failure.validationFailures, 0U); EXPECT_EQ(failure.failedSampleCount, 0U);
    EXPECT_NO_THROW(ev::ValidateBundle(f, env, init, Clock(f), warmup, {}, failure));
    const auto success = ev::SummarizeSamples(f, {}, Status::Ok);
    EXPECT_THROW(ev::ValidateBundle(f, env, init, Clock(f), warmup, {}, success), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceFoundation, ValidOtherFoundationCannotSupplyCrossSeriesRows)
{
    const auto f = Foundation(); auto request = Request(); request.sourceRevision[0] = 'c';
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256);
    const auto other = ev::MakeWordFoundation(request, input, ex2::ReferenceA1(input));
    auto row = ev::MakeWarmupRecord(other, 0); Pass(row);
    EXPECT_THROW(ev::ValidateOperationRecord(f, row), std::invalid_argument);
    auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    env.plan = other.Identity(); env.common.gitCommit = other.Identity().seriesIdentity.sourceRevision;
    EXPECT_THROW(ev::ValidateEnvironmentRecord(f, env), std::invalid_argument);
}

TEST(Ex2Stage5EvidenceSummary, IndependentKnownDistributionAndFailedRowExclusion)
{
    const auto f = Foundation(Phase::D1Sample); auto rows = Samples(f);
    for (unsigned i = 0; i < 200; ++i) Pass(rows[i], i + 1, 0);
    auto summary = ev::SummarizeSamples(f, rows, Status::Ok);
    const auto& m = *summary.hostSubmissionNanoseconds;
    EXPECT_EQ(m.sampleCount, 200U); EXPECT_EQ(m.minimum, 1); EXPECT_EQ(m.median, 100.5);
    EXPECT_EQ(m.mean, 100.5); EXPECT_EQ(m.p95, 190);
    EXPECT_DOUBLE_EQ(*m.standardDeviation, std::sqrt(3350.0));
    EXPECT_DOUBLE_EQ(*m.coefficientOfVariation, std::sqrt(3350.0) / 100.5);
    EXPECT_FALSE(summary.hostWaitNanoseconds->coefficientOfVariation); EXPECT_EQ(summary.hostWaitNanoseconds->mean, 0);
    EXPECT_FALSE(summary.nativeDeviceIntervalNanoseconds);
    Mismatch(rows[199]);
    summary = ev::SummarizeSamples(f, rows, Status::ValidationFailed, Failure::Validation, std::string(old::error_code::OutputMismatch));
    EXPECT_EQ(summary.recordedSampleCount, 200U); EXPECT_EQ(summary.validationFailures, 1U); EXPECT_EQ(summary.failedSampleCount, 1U);
    EXPECT_EQ(summary.hostSubmissionNanoseconds->sampleCount, 199U); EXPECT_EQ(summary.hostSubmissionNanoseconds->mean, 100);
    EXPECT_EQ(summary.hostSubmissionNanoseconds->p95, 190);
    EXPECT_EQ(rows[199].sequenceIndex, 199U); EXPECT_EQ(rows[199].status, Status::ValidationFailed);
}

TEST(Ex2Stage5EvidenceSummary, ZeroOneAndNoIncludedValuesFollowNullConvention)
{
    const auto f = Foundation(Phase::D1Sample); auto rows = Samples(f);
    for (auto& row : rows) Pass(row, 0, 0);
    auto summary = ev::SummarizeSamples(f, rows, Status::Ok);
    EXPECT_EQ(summary.hostCompletionNanoseconds->mean, 0); EXPECT_FALSE(summary.hostCompletionNanoseconds->coefficientOfVariation);
    for (auto& row : rows) Mismatch(row);
    summary = ev::SummarizeSamples(f, rows, Status::ValidationFailed, Failure::Validation, std::string(old::error_code::OutputMismatch));
    EXPECT_TRUE(summary.hostCompletionNanoseconds); EXPECT_FALSE(summary.hostCompletionNanoseconds->sampleCount);
    EXPECT_FALSE(summary.hostCompletionNanoseconds->mean); EXPECT_FALSE(summary.hostCompletionNanoseconds->p95);
    Pass(rows[0], 0, 0);
    summary = ev::SummarizeSamples(f, rows, Status::ValidationFailed, Failure::Validation, std::string(old::error_code::OutputMismatch));
    EXPECT_EQ(summary.hostCompletionNanoseconds->sampleCount, 1U); EXPECT_FALSE(summary.hostCompletionNanoseconds->standardDeviation);
    EXPECT_FALSE(summary.hostCompletionNanoseconds->coefficientOfVariation);
}

TEST(Ex2Stage5EvidenceSummary, RejectsContradictoryCountersStatisticsAndStatus)
{
    const auto f = Foundation(Phase::D1Sample); const auto samples = Samples(f);
    const auto good = ev::SummarizeSamples(f, samples, Status::Ok);
    for (int defect = 0; defect < 9; ++defect)
    {
        auto r = good;
        if (defect == 0) ++r.recordedSampleCount;
        if (defect == 1) ++r.validationFailures;
        if (defect == 2) ++r.failedSampleCount;
        if (defect == 3) r.hostCompletionNanoseconds->mean = 1;
        if (defect == 4) r.hostCompletionNanoseconds->p95 = 1;
        if (defect == 5) r.hostCompletionNanoseconds->sampleCount = 199;
        if (defect == 6) r.nativeDeviceIntervalNanoseconds = computelab::results::MetricSummary{};
        if (defect == 7) r.hostSubmissionNanoseconds->mean = std::numeric_limits<double>::infinity();
        if (defect == 8) r.plan.runId = "other";
        EXPECT_THROW((void)ev::SerializeSummaryJson(f, r, samples), std::invalid_argument);
    }
    EXPECT_THROW((void)ev::SummarizeSamples(f, samples, Status::ValidationFailed, Failure::Validation,
        std::string(old::error_code::OutputMismatch)), std::invalid_argument);
}

// Independently specified golden summary, not a production serializer call.
std::string GoldenSummary(Phase phase)
{
    const bool sample = phase == Phase::D1Sample, d1 = phase != Phase::A1Sentinel;
    constexpr std::string_view DWSeries = "e8964dee1c9302f881bf25d4af49c3cf97e1f3252a2b16700d3b45347d3cd233";
    std::string result = "{\"schema_version\":2,\"run_id\":\"stage5-run\",\"experiment_id\":\"EX-2\",\"protocol_version\":\"1.2\","
        "\"evidence_kind\":\"qualification\",\"process_status\":\"ok\",\"failure_phase\":null,\"error_code\":null,"
        "\"sample_groups\":[{\"group\":{\"comparison_condition_id\":\"" + std::string(d1 ? DCondition : ACondition)
        + "\",\"series_id\":\"" + std::string(sample ? DSeries : d1 ? DWSeries : ASeries)
        + "\",\"run_id\":\"stage5-run\",\"backend\":\"cuda\",\"workload\":\"" + (d1 ? "D" : "A")
        + "\",\"variant\":\"" + (d1 ? "D1" : "A1") + "\",\"seed\":81985529216486895,\"element_count\":"
        + (d1 ? "1048576" : "256") + ",\"byte_count\":null,\"index_pattern\":null,\"counter_count\":null,\"iteration_count\":"
        + (d1 ? "64" : "null") + ",\"transfer_direction\":null,\"execution_mode\":\"ordinary\",\"instrument_mode\":\"H\","
        "\"warmup_count\":" + (sample ? "2" : "0") + ",\"planned_sample_count\":" + (sample ? "200" : "0")
        + ",\"block_index\":0,\"order_slot\":0,\"process_index\":0},\"recorded_sample_count\":" + (sample ? "200" : "0")
        + ",\"validation_failures\":0,\"failed_sample_count\":0,\"metric_admission\":{";
    for (const auto metric : {"host_submission_ns", "host_wait_ns", "host_completion_ns"})
    {
        if (metric != std::string_view("host_submission_ns")) result += ',';
        const bool decision = d1 && metric == std::string_view("host_completion_ns");
        result += '"' + std::string(metric) + "\":{\"gate0_admission\":\""
            + (decision ? "pending_gate0_review" : "not_admitted") + "\",\"evidence_scope\":\""
            + (!d1 ? "diagnostic_only_a1_sentinel" : decision ? "stage5_d1_h_decision_host_completion" : "diagnostic_only_host_component")
            + "\",\"summary_sample_inclusion\":\"" + (sample ? "completed_correctness_passing_ok_rows" : "no_steady_state_samples") + "\"}";
    }
    result += ",\"native_device_interval_ns\":{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":\"not_recorded_in_instrument_mode_h\","
        "\"summary_sample_inclusion\":\"inapplicable_instrument_mode_h\"}},\"metrics\":{";
    unsigned value = 10;
    for (const auto metric : {"host_submission_ns", "host_wait_ns", "host_completion_ns"})
    {
        if (value != 10) result += ',';
        result += '"' + std::string(metric) + "\":";
        if (!sample) result += "null";
        else result += "{\"sample_count\":200,\"minimum\":" + std::to_string(value) + ",\"median\":" + std::to_string(value)
            + ",\"mean\":" + std::to_string(value) + ",\"standard_deviation\":0,\"coefficient_of_variation\":0,\"p95\":" + std::to_string(value) + '}';
        value += 10;
    }
    return result + ",\"native_device_interval_ns\":null}}]}\n";
}

TEST(Ex2Stage5EvidenceSummary, ExactGoldenZeroAndD1SampleSummaries)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto f = Foundation(phase); const auto samples = Samples(f);
        const auto summary = ev::SummarizeSamples(f, samples, Status::Ok);
        EXPECT_EQ(ev::SerializeSummaryJson(f, summary, samples), GoldenSummary(phase));
    }
}

void ExpectAdmission(Phase phase, std::string_view metric, std::string_view admission, std::string_view scope)
{
    const auto f = Foundation(phase); const auto samples = Samples(f);
    const auto summary = ev::SummarizeSamples(f, samples, Status::Ok);
    const auto json = ev::SerializeSummaryJson(f, summary, samples);
    const auto expected = '"' + std::string(metric) + "\":{\"gate0_admission\":\""
        + std::string(admission) + "\",\"evidence_scope\":\"" + std::string(scope) + "\",";
    EXPECT_NE(json.find(expected), std::string::npos);
    EXPECT_EQ(json.find("pending_gate0_review") == std::string::npos, phase == Phase::A1Sentinel);
}

TEST(Ex2Stage5EvidenceAdmission, A1SubmissionIsDiagnosticOnly)
{
    ExpectAdmission(Phase::A1Sentinel, "host_submission_ns", "not_admitted", "diagnostic_only_a1_sentinel");
}
TEST(Ex2Stage5EvidenceAdmission, A1WaitIsDiagnosticOnly)
{
    ExpectAdmission(Phase::A1Sentinel, "host_wait_ns", "not_admitted", "diagnostic_only_a1_sentinel");
}
TEST(Ex2Stage5EvidenceAdmission, A1CompletionIsDiagnosticOnly)
{
    ExpectAdmission(Phase::A1Sentinel, "host_completion_ns", "not_admitted", "diagnostic_only_a1_sentinel");
}
TEST(Ex2Stage5EvidenceAdmission, D1WarmupSubmissionIsDiagnosticOnly)
{
    ExpectAdmission(Phase::D1Warmup, "host_submission_ns", "not_admitted", "diagnostic_only_host_component");
}
TEST(Ex2Stage5EvidenceAdmission, D1WarmupWaitIsDiagnosticOnly)
{
    ExpectAdmission(Phase::D1Warmup, "host_wait_ns", "not_admitted", "diagnostic_only_host_component");
}
TEST(Ex2Stage5EvidenceAdmission, D1WarmupCompletionIsPendingReview)
{
    ExpectAdmission(Phase::D1Warmup, "host_completion_ns", "pending_gate0_review", "stage5_d1_h_decision_host_completion");
}
TEST(Ex2Stage5EvidenceAdmission, D1SampleSubmissionIsDiagnosticOnly)
{
    ExpectAdmission(Phase::D1Sample, "host_submission_ns", "not_admitted", "diagnostic_only_host_component");
}
TEST(Ex2Stage5EvidenceAdmission, D1SampleWaitIsDiagnosticOnly)
{
    ExpectAdmission(Phase::D1Sample, "host_wait_ns", "not_admitted", "diagnostic_only_host_component");
}
TEST(Ex2Stage5EvidenceAdmission, D1SampleCompletionIsPendingReview)
{
    ExpectAdmission(Phase::D1Sample, "host_completion_ns", "pending_gate0_review", "stage5_d1_h_decision_host_completion");
}
TEST(Ex2Stage5EvidenceAdmission, NativeIntervalIsInapplicableInEveryHPhase)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
        ExpectAdmission(phase, "native_device_interval_ns", "not_applicable", "not_recorded_in_instrument_mode_h");
}

TEST(Ex2Stage5EvidenceSerialization, LocaleUtf8AndNoPrivateFields)
{
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto previous = std::locale();
    struct Restore { std::locale old; ~Restore() { std::locale::global(old); } } restore{previous};
    const auto f = Foundation(); auto common = Common(f); common.cpuName = "Synthetic CPU \xc3\xa9";
    const auto env = ev::MakeEnvironmentRecord(common, f, Diagnostics(f));
    const auto before = ev::SerializeEnvironmentJson(f, env);
    std::locale::global(std::locale(previous, new Comma));
    EXPECT_EQ(ev::SerializeEnvironmentJson(f, env), before);
    const auto init = ev::MakeSetupCompleteInitialization(f, "UTF-8 \xc3\xa9");
    EXPECT_NE(ev::SerializeInitializationCsv(f, {&init, 1}).find("\xc3\xa9"), std::string::npos);
    auto bad = init; bad.observation = std::string(1, static_cast<char>(0xff));
    EXPECT_THROW((void)ev::SerializeInitializationCsv(f, {&bad, 1}), std::invalid_argument);
    for (const auto word : {"hostname", "username", "private_path", "credentials"}) EXPECT_EQ(before.find(word), std::string::npos);
    const auto d1 = Foundation(Phase::D1Sample); auto samples = Samples(d1);
    for (unsigned i = 0; i < 200; ++i) Pass(samples[i], i + 1, 0);
    const auto summary = ev::SummarizeSamples(d1, samples, Status::Ok);
    const auto json = ev::SerializeSummaryJson(d1, summary, samples);
    EXPECT_NE(json.find("\"mean\":100.5"), std::string::npos);
    EXPECT_EQ(json.find("R_process"), std::string::npos);
}

} // namespace
