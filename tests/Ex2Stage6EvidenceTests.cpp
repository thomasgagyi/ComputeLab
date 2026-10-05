#include "ex2/Ex2Stage6Evidence.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <locale>
#include <stdexcept>
#include <type_traits>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace ev = s6::evidence;
namespace old = ex2::evidence;
constexpr std::string_view Condition = "5eddcf89ce5f3c609bb4cb36ce11ee0c366d8415b46b9e4a6b9519d032c75295";
constexpr std::string_view Series = "c23fc6fa4c70cfa5dcfdde7f35d279a3f48c08225d4b43c5b21cbe3c62b7af39";
ev::FoundationRequest Request(std::size_t sequence = 0)
{
    const auto slot = s6::FrozenCampaignPlan()[sequence];
    const bool shader = slot.process.backend == ex2::Backend::Vulkan
        && !std::holds_alternative<ex2::TransferConfiguration>(s6::WorkloadForCell(slot.cellIndex).parameters);
    return {slot, "stage6-run", "anonymous-machine", {"00112233-4455-6677-8899-aabbccddeeff", true},
        std::string(40, 'a'), std::string(64, 'b'), shader ? std::optional<std::string>{std::string(64, 'c')} : std::nullopt};
}
ev::Foundation Foundation(std::size_t sequence = 0)
{
    const auto r = Request(sequence); const auto& w = s6::WorkloadForCell(r.slot.cellIndex);
    return std::visit([&](const auto& p) -> ev::Foundation {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, ex2::TransferConfiguration>)
        {
            const auto reference = ex2::ReferenceTransfer(w.common.seed, p.byteCount, p.direction);
            return ev::MakeByteFoundation(r, old::MakeI7ByteInputIdentity(w, reference.source), reference.expectedDestination);
        }
        else if constexpr (std::is_same_v<T, ex2::ContentionConfiguration>)
        {
            const auto reference = ex2::ReferenceContention(p.elementCount, p.activeCounterCount);
            const auto zeros = ex2::MakeZeroInitialCounterState(p.allocatedCounterCount);
            return ev::MakeWordFoundation(r, old::MakeI7ContentionInputIdentity(w, reference.targets, zeros), reference.counters);
        }
        else
        {
            const auto input = ex2::GenerateWordInput(w.common.seed, p.elementCount);
            if constexpr (std::is_same_v<T, ex2::IndexedConfiguration>)
            {
                const auto indices = p.indexPattern == ex2::IndexPattern::StructuredV1 ? ex2::GenerateStructuredPermutation(p.elementCount)
                    : ex2::GenerateShuffledPermutation(w.common.seed, p.elementCount);
                const auto expected = p.variant == ex2::IndexedVariant::B1 ? ex2::ReferenceB1Gather(input, indices) : ex2::ReferenceB2Scatter(input, indices);
                return ev::MakeWordFoundation(r, old::MakeI7IndexedInputIdentity(w, input, indices), expected);
            }
            else if constexpr (std::is_same_v<T, ex2::IterativeConfiguration>)
                return ev::MakeWordFoundation(r, old::MakeI7WordInputIdentity(w, input), ex2::ReferenceD1(input, p.iterationCount).finalState);
            else
                return ev::MakeWordFoundation(r, old::MakeI7WordInputIdentity(w, input),
                    p.variant == ex2::LinearVariant::A1 ? ex2::ReferenceA1(input) : ex2::ReferenceA2(input));
        }
    }, w.parameters);
}
computelab::results::EnvironmentRecord Common(const ev::Foundation& f)
{
    return {2, "EX-2", f.Identity().runId, "2026-10-05T00:00:00Z", std::string(40, 'a'), true,
        "anonymous-machine", "Windows", "11", "Synthetic CPU", 16, "Synthetic GPU", "NVIDIA", "10DE:1234", 8,
        "driver", "toolkit", "runtime", "7.5", "sdk", "1.4", "MSVC", "19", "4", "1", "x64-debug", "Debug", true, true};
}
ev::BackendDiagnostics Diagnostics(const ev::Foundation& f)
{
    if (f.Identity().slot.process.backend == ex2::Backend::Cuda)
        return {.implementation = "synthetic-cuda", .streamFlags = "nonblocking"};
    return {.implementation = "synthetic-vulkan", .queueFamilyIndex = 0, .queueFlags = 2, .queueCount = 1};
}
void Pass(ev::SampleRecord& r, std::uint64_t a = 10, std::uint64_t b = 20)
{
    r.status = ev::Status::Ok; r.correctness = {true, true, true, true, true};
    r.failurePhase.reset(); r.errorCode.reset();
    r.hostSubmissionNanoseconds = a; r.hostWaitNanoseconds = b; r.hostCompletionNanoseconds = a + b;
}
void Mismatch(ev::SampleRecord& r)
{ r.status = ev::Status::ValidationFailed; r.correctness.validationPassed = false; r.failurePhase = ev::FailurePhase::Validation; r.errorCode = "output_mismatch"; }
std::vector<ev::SampleRecord> Samples(const ev::Foundation& f, std::size_t count = 100)
{
    std::vector<ev::SampleRecord> rows;
    for (std::size_t i = 0; i < count; ++i) { auto r = ev::MakeSampleRecord(f, i); Pass(r); rows.push_back(r); }
    return rows;
}
ev::SummaryRecord Summary(const ev::Foundation& f, std::span<const ev::SampleRecord> rows)
{
    if (rows.empty()) return ev::SummarizeSamples(f.Identity(), rows, ev::Status::Incomplete,
        ev::FailurePhase::BackendInitialization, "backend_initialization_failed");
    const auto& last = rows.back();
    return ev::SummarizeSamples(f.Identity(), rows, last.status, last.failurePhase, last.errorCode);
}
TEST(Ex2Stage6EvidenceFoundation, TypedAllFamiliesAndBothBackendProvenance)
{
    static_assert(std::is_same_v<ev::InputIdentity, old::I7CorrectnessInputIdentity>);
    static_assert(!std::is_default_constructible_v<ev::Foundation>);
    for (const auto cell : {0U, 3U, 5U, 8U, 13U, 14U, 16U, 19U})
        for (const auto backendSlot : {0U, 1U})
        {
            const auto f = Foundation(cell * 10 + backendSlot); const auto& p = f.Identity();
            EXPECT_EQ(p.seriesIdentity.condition.workload, s6::WorkloadForCell(cell));
            EXPECT_EQ(p.seriesIdentity.condition.protocolVersion, "1.3"); EXPECT_EQ(p.seriesIdentity.warmupCount, 0);
            EXPECT_EQ(p.seriesIdentity.plannedSampleCount, 100);
            EXPECT_EQ(p.seriesIdentity.shaderSha256.has_value(), backendSlot == 1 && cell < 16);
            EXPECT_NO_THROW((void)ev::SerializeEnvironmentJson(f, ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f))));
        }
}
TEST(Ex2Stage6EvidenceFoundation, RejectsIdentityAndProvenanceMutations)
{
    const auto f = Foundation(); const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 256);
    const auto identity = old::MakeI7WordInputIdentity(s6::WorkloadForCell(0), input); const auto expected = ex2::ReferenceA1(input);
    for (unsigned defect = 0; defect < 11; ++defect)
    {
        auto r = Request();
        if (defect == 0) r.slot.cellIndex = 22;
        if (defect == 1) r.slot.sequenceIndex = 1;
        if (defect == 2) r.slot.process.backend = static_cast<ex2::Backend>(99);
        if (defect == 3) r.runId = "../private";
        if (defect == 4) r.gpuIdentity.externallyVerified = false;
        if (defect == 5) r.gpuIdentity.uuid = "00000000-0000-0000-0000-000000000000";
        if (defect == 6) r.sourceRevision = "short";
        if (defect == 7) r.executableSha256[0] = 'B';
        if (defect == 8) r.shaderSha256 = std::string(64, 'c');
        if (defect == 9) { r = Request(1); r.shaderSha256.reset(); }
        if (defect == 10) r.machineId = "private/path";
        EXPECT_THROW((void)ev::MakeWordFoundation(r, identity, expected), std::invalid_argument);
    }
    EXPECT_THROW((void)ev::MakeWordFoundation(Request(), identity, std::span{expected}.first(255)), std::invalid_argument);
    EXPECT_THROW((void)ev::MakeByteFoundation(Request(), identity, {}), std::invalid_argument);
    EXPECT_THROW((void)ev::MakeWordFoundation(Request(30), identity, expected), std::invalid_argument);
    for (unsigned defect = 0; defect < 10; ++defect)
    {
        auto p = f.Identity();
        if (defect == 0) p.seriesIdentity.condition.protocolVersion = "1.2";
        if (defect == 1) p.seriesIdentity.condition.instrumentMode = ex2::InstrumentMode::N;
        if (defect == 2) p.seriesIdentity.warmupCount = 1;
        if (defect == 3) p.seriesIdentity.plannedSampleCount = 99;
        if (defect == 4) p.seriesIdentity.condition.workload.common.seed++;
        if (defect == 5) p.seriesIdentity.processIndex++;
        if (defect == 6) p.seriesIdentity.orderSlot++;
        if (defect == 7) p.seriesId[0] = 'x';
        if (defect == 8) p.comparisonConditionId[0] = 'x';
        if (defect == 9) p.runId.clear();
        EXPECT_THROW(ev::ValidatePlan(p), std::invalid_argument);
    }
}
TEST(Ex2Stage6EvidenceEnvironment, TruthValidationWithoutCampaignAdmission)
{
    const auto f = Foundation(); const auto good = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    EXPECT_TRUE(good.common.gitDirty); EXPECT_TRUE(good.common.validationEnabled);
    EXPECT_NO_THROW(ev::ValidateEnvironmentRecord(f, good));
    for (unsigned defect = 0; defect < 17; ++defect)
    {
        auto r = good;
        if (defect == 0) r.common.schemaVersion = 1;
        if (defect == 1) r.common.experimentId = "EX-1";
        if (defect == 2) r.common.gitCommit[0] = 'c';
        if (defect == 3) r.common.runId = "other";
        if (defect == 4) r.common.machineId = "other";
        if (defect == 5) r.common.cudaRuntimeVersion.reset();
        if (defect == 6) r.inputSha256[0] = 'c';
        if (defect == 7) r.expectedOutputSha256[0] = 'c';
        if (defect == 8) r.evidenceKind = "qualification";
        if (defect == 9) r.backendDiagnostics.nativeMarkersEnabled = true;
        if (defect == 10) r.backendDiagnostics.timestampPeriodNanoseconds = std::numeric_limits<double>::quiet_NaN();
        if (defect == 11) r.backendDiagnostics.nativeTimingMethod = "event";
        if (defect == 12) r.backendDiagnostics.queueCount = 1;
        if (defect == 13) r.common.osName = "C:\\private";
        if (defect == 14) r.common.cpuName = std::string(1, '\xff');
        if (defect == 15) r.backendDiagnostics.timestampValidBits = 0;
        if (defect == 16) r.common.gpuMemoryBytes.reset();
        EXPECT_THROW((void)ev::SerializeEnvironmentJson(f, r), std::invalid_argument);
    }
}
TEST(Ex2Stage6EvidenceSchema, LiteralHeadersEscapingAndSampleRows)
{
    EXPECT_EQ(ev::InitializationCsvHeader(), "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation");
    EXPECT_EQ(ev::SamplesCsvHeader(), "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns");
    EXPECT_EQ(ev::SamplesCsvHeader(), old::SamplesCsvHeader()); EXPECT_EQ(ev::InitializationCsvHeader(), old::InitializationCsvHeader());
    const auto f = Foundation(); const auto init = ev::MakeSetupCompleteInitialization(f, "ready,\"quoted\"\r\nline");
    EXPECT_EQ(ev::SerializeInitializationCsv(f.Identity(), {&init, 1}), ev::InitializationCsvHeader() + "\r\n"
        "2,stage6-run,EX-2,cuda,0,0,backend_setup,A,A1,256,setup_complete,,\"ready,\"\"quoted\"\"\r\nline\"\r\n");
    auto row = ev::MakeSampleRecord(f, 0); Pass(row);
    const auto prefix = "2,stage6-run,EX-2," + std::string(Condition) + ',' + std::string(Series)
        + ",cuda,A,A1,81985529216486895,256,,,,,,ordinary,H,0,100,0,0,0,0,";
    EXPECT_EQ(ev::SerializeSampleRow(row), prefix + "true,ok,,,10,20,30,\r\n");
    Mismatch(row); EXPECT_EQ(ev::SerializeSampleRow(row), prefix + "false,validation_failed,validation,output_mismatch,10,20,30,\r\n");
    row.correctness = {true, true, false, false, std::nullopt}; row.status = ev::Status::Incomplete;
    row.failurePhase = ev::FailurePhase::Readback; row.errorCode = "readback_failed";
    EXPECT_EQ(ev::SerializeSampleRow(row), prefix + ",incomplete,readback,readback_failed,10,20,30,\r\n");
}
TEST(Ex2Stage6EvidenceTiming, HAllOrNoneCheckedArithmeticAndProgress)
{
    const auto f = Foundation(); auto good = ev::MakeSampleRecord(f, 0); Pass(good);
    for (unsigned defect = 0; defect < 12; ++defect)
    {
        auto row = good;
        if (defect == 0) row.hostSubmissionNanoseconds.reset();
        if (defect == 1) row.hostWaitNanoseconds.reset();
        if (defect == 2) row.hostCompletionNanoseconds.reset();
        if (defect == 3) row.hostCompletionNanoseconds = 29;
        if (defect == 4) { row.hostSubmissionNanoseconds = UINT64_MAX; row.hostWaitNanoseconds = 1; row.hostCompletionNanoseconds = 0; }
        if (defect == 5) row.nativeDeviceIntervalNanoseconds = 0;
        if (defect == 6) row.status = ev::Status::TimestampInvalid;
        if (defect == 7) row.correctness.operationCompleted = false;
        if (defect == 8) row.correctness.expectedOutputGenerated = false;
        if (defect == 9) row.correctness.validationPassed = false;
        if (defect == 10) row.failurePhase = ev::FailurePhase::Readback;
        if (defect == 11) { Mismatch(row); row.hostSubmissionNanoseconds.reset(); row.hostWaitNanoseconds.reset(); row.hostCompletionNanoseconds.reset(); }
        EXPECT_THROW(ev::ValidateSampleRecord(row), std::invalid_argument);
    }
    auto failed = ev::MakeSampleRecord(f, 0); failed.status = ev::Status::SubmitFailed;
    failed.failurePhase = ev::FailurePhase::Submission; failed.errorCode = "submission_failed";
    EXPECT_NO_THROW(ev::ValidateSampleRecord(failed));
    failed.hostSubmissionNanoseconds = 0; failed.hostWaitNanoseconds = 0; failed.hostCompletionNanoseconds = 0;
    EXPECT_THROW(ev::ValidateSampleRecord(failed), std::invalid_argument);
    failed.status = ev::Status::Incomplete; failed.failurePhase = ev::FailurePhase::Readback; failed.errorCode = "readback_failed";
    failed.correctness.operationCompleted = true; EXPECT_NO_THROW(ev::ValidateSampleRecord(failed));
}
TEST(Ex2Stage6EvidenceTerminal, SuccessAndEveryFailurePrefixRetainsTerminalRow)
{
    const auto f = Foundation(); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    const std::vector init{ev::MakeSetupCompleteInitialization(f, "ready")};
    auto success = Samples(f); EXPECT_NO_THROW(ev::ValidateBundle(f, env, init, success, Summary(f, success)));
    for (const auto index : {0U, 17U, 99U})
    {
        auto rows = Samples(f, index + 1); Mismatch(rows.back()); const auto s = Summary(f, rows);
        EXPECT_EQ(s.recordedSampleCount, index + 1); EXPECT_EQ(s.successfulSampleCount, index);
        EXPECT_EQ(s.validationFailures, 1); EXPECT_EQ(s.failedSampleCount, 1);
        EXPECT_NO_THROW(ev::ValidateBundle(f, env, init, rows, s));
        EXPECT_EQ(s.hostCompletionNanoseconds.sampleCount, index ? std::optional<std::uint64_t>{index} : std::nullopt);
        rows.push_back(rows.back()); rows.back().sampleIndex++;
        EXPECT_THROW(ev::ValidateSamples(f.Identity(), rows), std::invalid_argument);
    }
}
TEST(Ex2Stage6EvidenceTerminal, RejectsGapsDuplicatesOversizeAndSummaryMismatch)
{
    const auto f = Foundation(); const auto good = Samples(f);
    for (unsigned defect = 0; defect < 5; ++defect)
    {
        auto rows = good;
        if (defect == 0) rows.erase(rows.begin() + 17);
        if (defect == 1) rows[17] = rows[16];
        if (defect == 2) rows.push_back(rows.back());
        if (defect == 3) std::swap(rows[0], rows[1]);
        if (defect == 4) Mismatch(rows[17]);
        EXPECT_THROW((void)Summary(f, rows), std::invalid_argument);
    }
    EXPECT_THROW((void)Summary(f, Samples(f, 99)), std::invalid_argument);
    EXPECT_THROW((void)ev::SummarizeSamples(f.Identity(), good, ev::Status::Incomplete, ev::FailurePhase::Readback, "readback_failed"), std::invalid_argument);
}
TEST(Ex2Stage6EvidenceTerminal, ZeroSampleFailureHasTruthfulInitializationAndNoInventedSetup)
{
    const auto f = Foundation(); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
    const auto s = Summary(f, {}); auto init = ev::MakeSetupCompleteInitialization(f, "ready");
    init.metric = "initialization_failed"; init.observation = "backend initialization failed";
    EXPECT_NO_THROW(ev::ValidateBundle(f, env, {&init, 1}, {}, s));
    EXPECT_EQ(ev::SerializeSamplesCsv(f.Identity(), {}), ev::SamplesCsvHeader() + "\r\n");
    EXPECT_THROW(ev::ValidateBundle(f, env, {}, {}, s), std::invalid_argument);
    init.metric = "setup_complete"; EXPECT_THROW(ev::ValidateBundle(f, env, {&init, 1}, {}, s), std::invalid_argument);
    init.metric = "initialization_failed"; const auto rows = Samples(f);
    EXPECT_THROW(ev::ValidateBundle(f, env, {&init, 1}, rows, Summary(f, rows)), std::invalid_argument);
    EXPECT_THROW((void)ev::SummarizeSamples(f.Identity(), {}, ev::Status::Ok), std::invalid_argument);
}
TEST(Ex2Stage6EvidenceSummary, IndependentDistributionNullConventionsAndExtremeRetention)
{
    const std::array<std::uint64_t, 4> even{1, 2, 3, 4}; const auto m = ev::SummarizeMetric(even);
    EXPECT_EQ(m.sampleCount, 4); EXPECT_EQ(m.minimum, 1); EXPECT_EQ(m.median, 2.5); EXPECT_EQ(m.mean, 2.5); EXPECT_EQ(m.p95, 4);
    EXPECT_DOUBLE_EQ(*m.standardDeviation, std::sqrt(5.0 / 3.0)); EXPECT_DOUBLE_EQ(*m.coefficientOfVariation, std::sqrt(5.0 / 3.0) / 2.5);
    const std::array<std::uint64_t, 3> odd{3, 1, 2}; EXPECT_EQ(ev::SummarizeMetric(odd).median, 2);
    const std::array<std::uint64_t, 21> ranks{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
    EXPECT_EQ(ev::SummarizeMetric(ranks).p95, 20);
    const std::array<std::uint64_t, 1> one{0}; const auto single = ev::SummarizeMetric(one);
    EXPECT_FALSE(single.standardDeviation); EXPECT_FALSE(single.coefficientOfVariation); EXPECT_EQ(single.mean, 0);
    const auto empty = ev::SummarizeMetric({}); EXPECT_FALSE(empty.sampleCount); EXPECT_FALSE(empty.median); EXPECT_FALSE(empty.mean);
    const auto f = Foundation(); auto rows = Samples(f); Pass(rows.back(), 1000000000, 0);
    const auto summary = Summary(f, rows); EXPECT_EQ(summary.hostCompletionNanoseconds.sampleCount, 100);
    EXPECT_GT(*summary.hostCompletionNanoseconds.mean, 10000000);
    Mismatch(rows.back()); EXPECT_EQ(Summary(f, rows).hostCompletionNanoseconds.mean, 30);
}
TEST(Ex2Stage6EvidenceSummary, RejectsEveryDerivedTamperAndNonfinite)
{
    const auto f = Foundation(); const auto rows = Samples(f); const auto good = Summary(f, rows);
    for (unsigned defect = 0; defect < 11; ++defect)
    {
        auto s = good;
        if (defect == 0) ++s.recordedSampleCount;
        if (defect == 1) ++s.successfulSampleCount;
        if (defect == 2) ++s.validationFailures;
        if (defect == 3) ++s.failedSampleCount;
        if (defect == 4) s.hostCompletionNanoseconds.median = 1;
        if (defect == 5) s.hostCompletionNanoseconds.sampleCount = 99;
        if (defect == 6) s.hostWaitNanoseconds.mean = std::numeric_limits<double>::infinity();
        if (defect == 7) s.hostSubmissionNanoseconds.mean = std::numeric_limits<double>::quiet_NaN();
        if (defect == 8) s.nativeDeviceIntervalNanoseconds = computelab::results::MetricSummary{};
        if (defect == 9) s.plan.runId = "other";
        if (defect == 10) s.failurePhase = ev::FailurePhase::Readback;
        EXPECT_THROW((void)ev::SerializeSummaryJson(s, rows), std::invalid_argument);
    }
    for (const auto value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
    { auto m = ev::SummarizeMetric({}); m.mean = value; EXPECT_THROW((void)ev::SerializeMetricJson(m), std::invalid_argument); }
}
} // namespace

namespace {
// Independent specification fixtures assembled with .NET JSON/SHA256. The
// transfer digest used a separate unchecked C# implementation of Mix(seed,i).
TEST(Ex2Stage6EvidenceSerialization, CudaA1LiteralEnvironment) {
const auto f = Foundation(0); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
EXPECT_EQ(ev::SerializeEnvironmentJson(f, env), std::string(R"fixture({"schema_version":2,"experiment_id":"EX-2","run_id":"stage6-run","timestamp_utc":"2026-10-05T00:00:00Z","git_commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","git_dirty":true,"machine_id":"anonymous-machine","os_name":"Windows","os_version":"11","cpu_name":"Synthetic CPU","system_memory_bytes":16,"gpu_name":"Synthetic GPU","gpu_vendor":"NVIDIA","gpu_device_id":"10DE:1234","gpu_memory_bytes":8,"nvidia_driver_version":"driver","cuda_toolkit_version":"toolkit","cuda_runtime_version":"runtime","cuda_compute_capability":"7.5","vulkan_sdk_version":"sdk","vulkan_device_api_version":"1.4","compiler_name":"MSVC","compiler_version":"19","cmake_version":"4","ninja_version":"1","configure_preset":"x64-debug","build_type":"Debug","validation_enabled":true,"diagnostic_instrumentation":true,"protocol_version":"1.3","evidence_kind":"diagnostic","backend":"cuda","instrument_mode":"H","warmup_count":0,"planned_sample_count":100,"comparison_condition_id":"5eddcf89ce5f3c609bb4cb36ce11ee0c366d8415b46b9e4a6b9519d032c75295","series_id":"c23fc6fa4c70cfa5dcfdde7f35d279a3f48c08225d4b43c5b21cbe3c62b7af39","cell_index":0,"slot_sequence_index":0,"block_index":0,"process_index":0,"order_slot":0,"source_revision":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","executable_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","shader_sha256":null,"input_sha256":"f9d00b3f9c3cb0a45e8574729e9f5d12a7a9ca579e0bd0c546c644227f9fce3f","expected_output_sha256":"456fce584ca45a00fb9528cc00d3487a0be5885118dc8a9d968569588f164532","gpu_uuid_identity":"00112233-4455-6677-8899-aabbccddeeff","workload":"A","variant":"A1","generator_revision":"ex2-mix64-v1","seed":81985529216486895,"element_count":256,"byte_count":null,"index_pattern":null,"counter_count":null,"iteration_count":null,"transfer_direction":null,"execution_mode":"ordinary","operation_boundary":"single-dispatch-completion","backend_native":{"implementation":"synthetic-cuda","stream_flags":"nonblocking","queue_family_index":null,"queue_flags":null,"queue_count":null,"timestamp_valid_bits":null,"timestamp_period_ns":null,"input_memory_flags":null,"output_memory_flags":null,"upload_memory_flags":null,"readback_memory_flags":null,"native_markers_enabled":false,"native_timing_method":null,"native_timing_resolution_ns":null,"native_timing_start_stage":null,"native_timing_stop_stage":null,"native_duration_envelope_ns":null}})fixture") + "\n");
}
TEST(Ex2Stage6EvidenceSerialization, VulkanE2LiteralEnvironment) {
const auto f = Foundation(191); const auto env = ev::MakeEnvironmentRecord(Common(f), f, Diagnostics(f));
EXPECT_EQ(ev::SerializeEnvironmentJson(f, env), std::string(R"fixture({"schema_version":2,"experiment_id":"EX-2","run_id":"stage6-run","timestamp_utc":"2026-10-05T00:00:00Z","git_commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","git_dirty":true,"machine_id":"anonymous-machine","os_name":"Windows","os_version":"11","cpu_name":"Synthetic CPU","system_memory_bytes":16,"gpu_name":"Synthetic GPU","gpu_vendor":"NVIDIA","gpu_device_id":"10DE:1234","gpu_memory_bytes":8,"nvidia_driver_version":"driver","cuda_toolkit_version":"toolkit","cuda_runtime_version":"runtime","cuda_compute_capability":"7.5","vulkan_sdk_version":"sdk","vulkan_device_api_version":"1.4","compiler_name":"MSVC","compiler_version":"19","cmake_version":"4","ninja_version":"1","configure_preset":"x64-debug","build_type":"Debug","validation_enabled":true,"diagnostic_instrumentation":true,"protocol_version":"1.3","evidence_kind":"diagnostic","backend":"vulkan","instrument_mode":"H","warmup_count":0,"planned_sample_count":100,"comparison_condition_id":"fdc0625dd5de13003aa1c2751025f464d3b3128c0ef99f54223c195429805387","series_id":"589bb8fabeb885718dcf5f555ca7b6c56517fef162485a4224b5f3682876c60c","cell_index":19,"slot_sequence_index":191,"block_index":0,"process_index":0,"order_slot":1,"source_revision":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","executable_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","shader_sha256":null,"input_sha256":"1ce394d55e0150c7bf43bb49edf8b74517c7fca48fda1154d0fe9bb3f063b8a1","expected_output_sha256":"1ce394d55e0150c7bf43bb49edf8b74517c7fca48fda1154d0fe9bb3f063b8a1","gpu_uuid_identity":"00112233-4455-6677-8899-aabbccddeeff","workload":"E","variant":"E2","generator_revision":"ex2-mix64-v1","seed":81985529216486895,"element_count":null,"byte_count":1024,"index_pattern":null,"counter_count":null,"iteration_count":null,"transfer_direction":"D2H","execution_mode":"prepared","operation_boundary":"prepared-single-copy-completion","backend_native":{"implementation":"synthetic-vulkan","stream_flags":null,"queue_family_index":0,"queue_flags":2,"queue_count":1,"timestamp_valid_bits":null,"timestamp_period_ns":null,"input_memory_flags":null,"output_memory_flags":null,"upload_memory_flags":null,"readback_memory_flags":null,"native_markers_enabled":false,"native_timing_method":null,"native_timing_resolution_ns":null,"native_timing_start_stage":null,"native_timing_stop_stage":null,"native_duration_envelope_ns":null}})fixture") + "\n");
}
TEST(Ex2Stage6EvidenceSerialization, LiteralZeroAnd100SuccessSummaries)
{
    const auto f = Foundation();
    const std::string group = R"fixture({"comparison_condition_id":"5eddcf89ce5f3c609bb4cb36ce11ee0c366d8415b46b9e4a6b9519d032c75295","series_id":"c23fc6fa4c70cfa5dcfdde7f35d279a3f48c08225d4b43c5b21cbe3c62b7af39","run_id":"stage6-run","cell_index":0,"slot_sequence_index":0,"backend":"cuda","workload":"A","variant":"A1","seed":81985529216486895,"element_count":256,"byte_count":null,"index_pattern":null,"counter_count":null,"iteration_count":null,"transfer_direction":null,"execution_mode":"ordinary","instrument_mode":"H","warmup_count":0,"planned_sample_count":100,"block_index":0,"order_slot":0,"process_index":0})fixture";
    const std::string empty = R"fixture({"sample_count":null,"minimum":null,"median":null,"mean":null,"standard_deviation":null,"coefficient_of_variation":null,"p95":null})fixture";
    const std::string ten = R"fixture({"sample_count":100,"minimum":10,"median":10,"mean":10,"standard_deviation":0,"coefficient_of_variation":0,"p95":10})fixture";
    const std::string twenty = R"fixture({"sample_count":100,"minimum":20,"median":20,"mean":20,"standard_deviation":0,"coefficient_of_variation":0,"p95":20})fixture";
    const std::string thirty = R"fixture({"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30})fixture";
    for (const bool success : {false, true})
    {
        const auto rows = success ? Samples(f) : std::vector<ev::SampleRecord>{};
        const auto summary = Summary(f, rows);
        const std::string expected = std::string(R"fixture({"schema_version":2,"run_id":"stage6-run","experiment_id":"EX-2","protocol_version":"1.3","evidence_kind":"diagnostic","process_status":)fixture")
            + (success ? R"fixture("ok","failure_phase":null,"error_code":null)fixture"
                : R"fixture("incomplete","failure_phase":"backend_initialization","error_code":"backend_initialization_failed")fixture")
            + R"fixture(,"sample_groups":[{"group":)fixture" + group
            + (success ? R"fixture(,"recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0)fixture"
                : R"fixture(,"recorded_sample_count":0,"successful_sample_count":0,"validation_failures":0,"failed_sample_count":0)fixture")
            + R"fixture(,"metrics":{"host_submission_ns":)fixture" + (success ? ten : empty)
            + R"fixture(,"host_wait_ns":)fixture" + (success ? twenty : empty)
            + R"fixture(,"host_completion_ns":)fixture" + (success ? thirty : empty)
            + R"fixture(,"native_device_interval_ns":null}}]})fixture" + "\n";
        EXPECT_EQ(ev::SerializeSummaryJson(summary, rows), expected);
    }
}
TEST(Ex2Stage6EvidenceSerialization, LocaleUtf8AndSingleTrailingLf)
{
    const auto f = Foundation(); auto rows = Samples(f);
    for (std::size_t i = 0; i < rows.size(); ++i) Pass(rows[i], i + 1, 0);
    const auto s = Summary(f, rows); const auto before = ev::SerializeSummaryJson(s, rows);
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto prior = std::locale(); struct Restore { std::locale old; ~Restore() { std::locale::global(old); } } restore{prior};
    std::locale::global(std::locale(prior, new Comma));
    EXPECT_EQ(ev::SerializeSummaryJson(s, rows), before); EXPECT_NE(before.find("\"median\":50.5"), std::string::npos);
    auto common = Common(f); common.cpuName = "CPU \xc3\xa9";
    const auto env = ev::MakeEnvironmentRecord(common, f, Diagnostics(f)); const auto json = ev::SerializeEnvironmentJson(f, env);
    EXPECT_NE(json.find("\xc3\xa9"), std::string::npos); EXPECT_EQ(json.back(), '\n'); EXPECT_EQ(json[json.size()-2], '}');
    const auto init = ev::MakeSetupCompleteInitialization(f, "UTF-8 \xc3\xa9");
    EXPECT_NE(ev::SerializeInitializationCsv(f.Identity(), {&init, 1}).find("\xc3\xa9"), std::string::npos);
    auto bad = init; bad.observation = std::string(1, '\xff');
    EXPECT_THROW((void)ev::SerializeInitializationCsv(f.Identity(), {&bad, 1}), std::invalid_argument);
    for (const auto word : {"metric_admission", "gate0", "qualification", "selected_w", "warmup.csv", "host-clock.csv"})
        EXPECT_EQ(before.find(word), std::string::npos);
}
TEST(Ex2Stage6EvidenceFailures, InheritedOutcomesAndInitializationRejectContradictoryFacts)
{
    const auto f = Foundation();
    struct Failure { ev::Status status; ev::FailurePhase phase; const char* code; bool completed; };
    const std::array failures{
        Failure{ev::Status::Incomplete, ev::FailurePhase::BackendInitialization, "backend_initialization_failed", false},
        Failure{ev::Status::Incomplete, ev::FailurePhase::ResourceAllocation, "resource_allocation_failed", false},
        Failure{ev::Status::SubmitFailed, ev::FailurePhase::Submission, "submission_failed", false},
        Failure{ev::Status::WaitFailed, ev::FailurePhase::CompletionWait, "completion_failed", false},
        Failure{ev::Status::Timeout, ev::FailurePhase::CompletionWait, "operation_timeout", false},
        Failure{ev::Status::DeviceLost, ev::FailurePhase::Readback, "device_lost", true},
        Failure{ev::Status::Incomplete, ev::FailurePhase::Readback, "readback_failed", true},
        Failure{ev::Status::Incomplete, ev::FailurePhase::EvidenceSerialization, "evidence_serialization_failed", false},
        Failure{ev::Status::Incomplete, ev::FailurePhase::EvidencePublication, "evidence_publication_failed", false},
        Failure{ev::Status::Incomplete, ev::FailurePhase::Interrupted, "interrupted", false}};
    for (const auto& failure : failures)
    {
        auto row = ev::MakeSampleRecord(f, 0); row.status = failure.status; row.failurePhase = failure.phase; row.errorCode = failure.code;
        row.correctness.operationCompleted = failure.completed;
        if (failure.completed) { row.hostSubmissionNanoseconds = 1; row.hostWaitNanoseconds = 2; row.hostCompletionNanoseconds = 3; }
        EXPECT_NO_THROW(ev::ValidateSampleRecord(row));
        const auto summary = ev::SummarizeSamples(f.Identity(), {&row, 1}, failure.status, failure.phase, failure.code);
        EXPECT_EQ(summary.failedSampleCount, 1); EXPECT_FALSE(summary.hostCompletionNanoseconds.sampleCount);
        row.errorCode = "contradictory_code"; EXPECT_THROW(ev::ValidateSampleRecord(row), std::invalid_argument);
    }
    const auto good = ev::MakeSetupCompleteInitialization(f, "ready");
    for (unsigned defect = 0; defect < 7; ++defect)
    {
        auto init = good;
        if (defect == 0) init.runId = "other";
        if (defect == 1) init.sequenceIndex = 1;
        if (defect == 2) init.durationNanoseconds = 0;
        if (defect == 3) init.observation.reset();
        if (defect == 4) init.elementCount = 257;
        if (defect == 5) init.variant = "A2";
        if (defect == 6) init.backend = ex2::Backend::Vulkan;
        EXPECT_THROW(ev::ValidateInitializationRecords(f.Identity(), {&init, 1}), std::invalid_argument);
    }
}
} // namespace
