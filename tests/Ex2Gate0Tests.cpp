#include "ex2/Ex2Gate0.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;

std::vector<std::string_view> ValidArguments(
    std::string_view phase = "sample-count-qualification",
    std::string_view mode = "H",
    std::string_view warmup = "2",
    std::string_view samples = "10")
{
    return {
        "--phase", phase,
        "--backend", "cuda",
        "--instrument-mode", mode,
        "--device-index", "0",
        "--element-count", "257",
        "--seed", "123456789",
        "--warmup-count", warmup,
        "--planned-sample-count", samples,
        "--process-index", "2",
        "--block-index", "3",
        "--order-slot", "1",
        "--run-id", "test-run",
        "--output-package-directory", "results/local/test-run",
        "--machine-id", "anonymous-machine",
        "--protocol-version", "1.0"};
}

gate0::Configuration Configuration(
    gate0::QualificationPhase phase =
        gate0::QualificationPhase::SampleCountQualification,
    gate0::InstrumentMode mode = gate0::InstrumentMode::H)
{
    return {
        phase,
        gate0::Backend::Cuda,
        mode,
        0U,
        257U,
        123456789U,
        phase == gate0::QualificationPhase::WarmupCharacterization ? 0U : 2U,
        phase == gate0::QualificationPhase::WarmupCharacterization ? 0U : 3U,
        2U,
        3U,
        1U,
        "test-run",
        "results/local/test-run",
        "anonymous-machine",
        "1.0"};
}

gate0::IdentityContext Identity()
{
    return {
        "1.0",
        "anonymous-machine",
        "00112233-4455-6677-8899-aabbccddeeff",
        257U,
        123456789U,
        gate0::InstrumentMode::H};
}

gate0::SampleRecord Sample(
    std::uint64_t index,
    std::string status = "ok",
    std::optional<bool> validation = true,
    std::optional<std::uint64_t> native = std::nullopt)
{
    return {
        "test-run", "condition", "series", gate0::Backend::Cuda,
        123456789U, 257U, native.has_value()
            ? gate0::InstrumentMode::N : gate0::InstrumentMode::H,
        2U, 3U, 3U, 1U, 2U, index, validation, std::move(status),
        std::nullopt, std::nullopt,
        10U + index, 20U + index, 30U + index, native};
}

gate0::EnvironmentRecord Environment(const gate0::Configuration& configuration)
{
    constexpr std::string_view uuid =
        "00112233-4455-6677-8899-aabbccddeeff";
    const std::string sourceRevision(40U, 'a');
    const std::string executableSha256(64U, 'd');
    const std::optional<std::string> shaderSha256 =
        configuration.backend == gate0::Backend::Vulkan
            ? std::optional<std::string>{std::string(64U, 'c')}
            : std::nullopt;
    const gate0::IdentityContext condition{
        configuration.protocolVersion,
        configuration.machineId,
        std::string(uuid),
        configuration.elementCount,
        configuration.seed,
        configuration.instrumentMode};
    const std::string comparisonConditionId =
        gate0::ComparisonConditionId(condition);
    const std::string seriesId = gate0::SeriesId({
        condition,
        configuration.backend,
        configuration.processIndex,
        configuration.blockIndex,
        configuration.orderSlot,
        configuration.warmupCount,
        configuration.plannedSampleCount,
        sourceRevision,
        executableSha256,
        shaderSha256});
    computelab::results::EnvironmentRecord common{
        gate0::SchemaVersion, std::string(gate0::ExperimentId),
        configuration.runId, "2026-09-17T00:00:00Z",
        sourceRevision, true, configuration.machineId,
        "Windows", "11", "CPU", 1024U,
        "GPU", "NVIDIA", "0x00000001", 2048U,
        "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", false,
        configuration.instrumentMode == gate0::InstrumentMode::N};
    return {
        std::move(common), configuration, comparisonConditionId, seriesId,
        sourceRevision, executableSha256, shaderSha256,
        std::string(64U, 'e'), std::string(64U, 'f'), std::string(uuid),
        {.implementation = "qualification-operation",
         .nativeMarkersEnabled =
            configuration.instrumentMode == gate0::InstrumentMode::N}};
}

std::vector<gate0::InitializationRecord> Initialization(
    const gate0::Configuration& configuration)
{
    return {{
        configuration.runId, configuration.backend,
        configuration.processIndex, 0U, "backend_setup",
        std::string(gate0::WorkloadId), std::string(gate0::VariantId),
        configuration.elementCount, "setup_complete", std::nullopt, "true"}};
}

gate0::SampleRecord PackageSample(
    const gate0::Configuration& configuration,
    const gate0::EnvironmentRecord& environment,
    std::uint64_t index = 0U)
{
    auto sample = Sample(index);
    sample.comparisonConditionId = environment.comparisonConditionId;
    sample.seriesId = environment.seriesId;
    sample.backend = configuration.backend;
    sample.instrumentMode = configuration.instrumentMode;
    sample.seed = configuration.seed;
    sample.elementCount = configuration.elementCount;
    sample.warmupCount = configuration.warmupCount;
    sample.plannedSampleCount = configuration.plannedSampleCount;
    sample.blockIndex = configuration.blockIndex;
    sample.orderSlot = configuration.orderSlot;
    sample.processIndex = configuration.processIndex;
    if (configuration.instrumentMode == gate0::InstrumentMode::N)
        sample.nativeDeviceIntervalNanoseconds = 5U + index;
    return sample;
}

std::filesystem::path UniqueTemporaryPath(std::string_view label)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / (std::string("computelab-") + std::string(label) + "-"
            + std::to_string(ticks));
}

struct RemovePath final
{
    std::filesystem::path path;
    ~RemovePath()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::remove_all(path.string() + ".incomplete", error);
    }
};

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, {}};
}

TEST(Ex2Gate0Configuration, ParsesOneFullyIdentifiedCondition)
{
    const auto parsed = gate0::ParseArguments(ValidArguments());
    EXPECT_EQ(parsed.phase, gate0::QualificationPhase::SampleCountQualification);
    EXPECT_EQ(parsed.backend, gate0::Backend::Cuda);
    EXPECT_EQ(parsed.instrumentMode, gate0::InstrumentMode::H);
    EXPECT_EQ(parsed.elementCount, 257U);
    EXPECT_EQ(parsed.processIndex, 2U);
    EXPECT_EQ(parsed.blockIndex, 3U);
    EXPECT_EQ(parsed.orderSlot, 1U);
}

TEST(Ex2Gate0Configuration, WarmupCharacterizationRequiresZeroDeclaredCounts)
{
    EXPECT_NO_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "warmup-characterization", "N", "0", "0"))));
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "warmup-characterization", "H", "1", "0"))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "warmup-characterization", "H", "0", "1"))), std::invalid_argument);
}

TEST(Ex2Gate0Configuration, MeasuredPhasesRequireSelectedWAndBoundedSamples)
{
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "sample-count-qualification", "H", "3", "10"))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "instrumentation-control", "N", "2", "0"))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "instrumentation-control", "N", "2", "201"))), std::invalid_argument);
    EXPECT_NO_THROW(static_cast<void>(gate0::ParseArguments(ValidArguments(
        "instrumentation-control", "N", "16", "200"))));
}

TEST(Ex2Gate0Configuration, RejectsUnknownValuesAndDuplicateOptions)
{
    auto unknown = ValidArguments();
    unknown[1] = "campaign";
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(unknown)), std::invalid_argument);

    auto duplicate = ValidArguments();
    duplicate.insert(duplicate.end(), {"--seed", "2"});
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(duplicate)), std::invalid_argument);
}

TEST(Ex2Gate0Configuration, RejectsInvalidIdentityIndexAndSizeRanges)
{
    auto arguments = ValidArguments();
    arguments[7] = "4294967296"; // device-index
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(arguments)), std::invalid_argument);
    arguments = ValidArguments();
    arguments[9] = "0"; // element-count
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(arguments)), std::invalid_argument);
    arguments = ValidArguments();
    arguments[17] = "5"; // process-index
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(arguments)), std::invalid_argument);
    arguments = ValidArguments();
    arguments[21] = "2"; // order-slot
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(arguments)), std::invalid_argument);
}

TEST(Ex2Gate0Configuration, EnforcesProtocolAndLocalPackageShape)
{
    auto arguments = ValidArguments();
    arguments.back() = "2.0";
    EXPECT_THROW(static_cast<void>(gate0::ParseArguments(arguments)), std::invalid_argument);

    const auto root = UniqueTemporaryPath("root");
    auto configuration = Configuration();
    configuration.outputPackageDirectory = root / "test-run";
    EXPECT_NO_THROW(gate0::ValidateOutputPackageDirectory(configuration, root));
    configuration.outputPackageDirectory = root.parent_path() / "test-run";
    EXPECT_THROW(
        gate0::ValidateOutputPackageDirectory(configuration, root),
        std::invalid_argument);
}

TEST(Ex2Gate0Configuration, ExitClassesAreStable)
{
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::Success), 0);
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::ConfigurationError), 2);
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::CorrectnessFailure), 3);
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::NativeExecutionFailure), 4);
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::IncompleteOrTimeout), 5);
    EXPECT_EQ(static_cast<int>(gate0::ExitCode::EvidenceOrProvenanceFailure), 6);
}

TEST(Ex2Gate0Schema, ExactHeadersAreVersionSeparatedFromEx1)
{
    EXPECT_EQ(gate0::SamplesCsvHeader(),
        "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns");
    EXPECT_EQ(gate0::WarmupCsvHeader(),
        "schema_version,run_id,series_id,process_index,sequence_index,host_submission_ns,host_wait_ns,host_completion_ns,status");
    EXPECT_EQ(gate0::InitializationCsvHeader(),
        "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation");
    EXPECT_NE(gate0::SamplesCsvHeader(),
        computelab::results::SerializeSamplesCsv({}).substr(
            0U, computelab::results::SerializeSamplesCsv({}).find("\r\n")));
}

TEST(Ex2Gate0Schema, CsvUsesExplicitEmptyFieldsAndRfc4180Quoting)
{
    auto sample = Sample(0U);
    sample.failurePhase = "phase,with,comma";
    const std::string row = gate0::SerializeSampleRow(sample);
    EXPECT_NE(row.find(",257,,,,,,ordinary,H,"), std::string::npos);
    EXPECT_NE(row.find("\"phase,with,comma\""), std::string::npos);
    EXPECT_TRUE(row.ends_with(",10,20,30,\r\n"));
}

TEST(Ex2Gate0Schema, WarmupRowsPreserveSequenceAndStatus)
{
    EXPECT_EQ(gate0::WarmupCharacterizationCount, 48U);
    const std::string row = gate0::SerializeWarmupRow({
        "test-run", "series", 2U, 47U, 1U, 2U, 3U, "ok"});
    EXPECT_EQ(row, "2,test-run,series,2,47,1,2,3,ok\r\n");
}

TEST(Ex2Gate0Hash, ImplementsFullSha256)
{
    EXPECT_EQ(gate0::Sha256(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(gate0::Sha256("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Ex2Gate0Hash, MatchesKnownAnswerBeyondOneCompressionBlock)
{
    const std::string millionAs(1'000'000U, 'a');
    ASSERT_GT(millionAs.size(), 64U);
    EXPECT_EQ(gate0::Sha256(millionAs),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Ex2Gate0Identity, CanonicalKeyInsertionOrderCannotChangeIdentity)
{
    const std::string first = gate0::CanonicalJson({
        {"z", std::uint64_t{2}}, {"a", std::string{"value"}}, {"m", nullptr}});
    const std::string second = gate0::CanonicalJson({
        {"m", nullptr}, {"z", std::uint64_t{2}}, {"a", std::string{"value"}}});
    EXPECT_EQ(first, "{\"a\":\"value\",\"m\":null,\"z\":2}");
    EXPECT_EQ(first, second);
    EXPECT_EQ(gate0::Sha256(first), gate0::Sha256(second));
}

TEST(Ex2Gate0Identity, ComparisonConditionIsBackendNeutral)
{
    const auto condition = Identity();
    const gate0::SeriesIdentityContext cuda{
        condition, gate0::Backend::Cuda, 0U, 0U, 0U, 2U, 100U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    auto vulkan = cuda;
    vulkan.backend = gate0::Backend::Vulkan;
    vulkan.shaderSha256 = std::string(64U, 'c');
    EXPECT_EQ(gate0::ComparisonConditionId(cuda.condition),
        gate0::ComparisonConditionId(vulkan.condition));
    EXPECT_NE(gate0::SeriesId(cuda), gate0::SeriesId(vulkan));
}

TEST(Ex2Gate0Identity, EveryStructuredConditionVariableChangesTheId)
{
    const auto baseline = Identity();
    const std::string expected = gate0::ComparisonConditionId(baseline);
    auto changed = baseline;
    changed.seed++;
    EXPECT_NE(gate0::ComparisonConditionId(changed), expected);
    changed = baseline; changed.elementCount++;
    EXPECT_NE(gate0::ComparisonConditionId(changed), expected);
    changed = baseline; changed.machineId = "another-machine";
    EXPECT_NE(gate0::ComparisonConditionId(changed), expected);
    changed = baseline; changed.gpuUuidIdentity[0] = 'f';
    EXPECT_NE(gate0::ComparisonConditionId(changed), expected);
    changed = baseline; changed.instrumentMode = gate0::InstrumentMode::N;
    EXPECT_NE(gate0::ComparisonConditionId(changed), expected);
}

TEST(Ex2Gate0Identity, SeriesChangesForBackendProcessAndBuildFields)
{
    gate0::SeriesIdentityContext baseline{
        Identity(), gate0::Backend::Cuda, 0U, 0U, 0U, 2U, 100U,
        std::string(40U, 'a'), std::string(64U, 'b'), std::nullopt};
    const std::string expected = gate0::SeriesId(baseline);
    auto changed = baseline; changed.processIndex++;
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.blockIndex++;
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.orderSlot++;
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.sourceRevision[0] = 'c';
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.executableSha256[0] = 'd';
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.shaderSha256 = std::string(64U, 'e');
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.warmupCount++;
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.plannedSampleCount++;
    EXPECT_NE(gate0::SeriesId(changed), expected);
    changed = baseline; changed.backend = gate0::Backend::Vulkan;
    EXPECT_NE(gate0::SeriesId(changed), expected);
}

TEST(Ex2Gate0Summary, RegeneratesSpecifiedStatisticsInSampleOrder)
{
    auto configuration = Configuration();
    std::vector<gate0::SampleRecord> samples{
        Sample(0U), Sample(1U), Sample(2U)};
    const auto summary = gate0::SummarizeSamples(
        configuration, "condition", "series", samples);
    ASSERT_TRUE(summary.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(summary.hostCompletionNanoseconds->sampleCount, 3U);
    EXPECT_EQ(summary.hostCompletionNanoseconds->minimum, 30.0);
    EXPECT_EQ(summary.hostCompletionNanoseconds->median, 31.0);
    EXPECT_EQ(summary.hostCompletionNanoseconds->mean, 31.0);
    EXPECT_EQ(summary.hostCompletionNanoseconds->standardDeviation, 1.0);
    EXPECT_EQ(summary.hostCompletionNanoseconds->p95, 32.0);
}

TEST(Ex2Gate0Summary, RetainsFailureRowsButExcludesThemFromEveryMetric)
{
    auto configuration = Configuration();
    auto failed = Sample(1U, "validation_failed", false);
    failed.hostCompletionNanoseconds = 9999U;
    const auto summary = gate0::SummarizeSamples(
        configuration, "condition", "series", {Sample(0U), failed});
    EXPECT_EQ(summary.recordedSampleCount, 2U);
    EXPECT_EQ(summary.validationFailures, 1U);
    EXPECT_EQ(summary.failedSampleCount, 1U);
    ASSERT_TRUE(summary.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(summary.hostCompletionNanoseconds->sampleCount, 1U);
    EXPECT_EQ(summary.hostCompletionNanoseconds->mean, 30.0);
}

TEST(Ex2Gate0Summary, HostModeMakesNativeTimingInapplicable)
{
    const auto summary = gate0::SummarizeSamples(
        Configuration(), "condition", "series", {Sample(0U)});
    EXPECT_FALSE(summary.nativeDeviceIntervalNanoseconds.has_value());
    EXPECT_NE(gate0::SerializeSummaryJson(summary).find(
        "\"native_device_interval_ns\":null"), std::string::npos);
}

TEST(Ex2Gate0Summary, SeparatesSummaryInclusionFromGate0Admission)
{
    const auto hostSummary = gate0::SummarizeSamples(
        Configuration(), "condition", "series", {Sample(0U)});
    const std::string hostJson = gate0::SerializeSummaryJson(hostSummary);
    EXPECT_NE(hostJson.find("\"gate0_admission\":\"pending_gate0_review\""),
        std::string::npos);
    EXPECT_NE(hostJson.find(
        "\"evidence_scope\":\"qualification_only_host_observation\""),
        std::string::npos);
    EXPECT_NE(hostJson.find(
        "\"summary_sample_inclusion\":\"completed_correctness_passing_ok_rows\""),
        std::string::npos);
    EXPECT_EQ(hostJson.find("accepted_ok_samples"), std::string::npos);

    auto nativeConfiguration = Configuration(
        gate0::QualificationPhase::InstrumentationControl,
        gate0::InstrumentMode::N);
    auto nativeSample = Sample(0U, "ok", true, 5U);
    const auto nativeSummary = gate0::SummarizeSamples(
        nativeConfiguration, "condition", "series", {nativeSample});
    const std::string nativeJson = gate0::SerializeSummaryJson(nativeSummary);
    EXPECT_NE(nativeJson.find(
        "\"gate0_admission\":\"excluded_cross_api_v1\""),
        std::string::npos);
    EXPECT_NE(nativeJson.find(
        "\"evidence_scope\":\"backend_native_explanatory_within_backend_only\""),
        std::string::npos);
}

TEST(Ex2Gate0Summary, NativeInvalidRowsAreRecordedAndExcluded)
{
    auto configuration = Configuration(
        gate0::QualificationPhase::InstrumentationControl,
        gate0::InstrumentMode::N);
    auto invalid = Sample(0U, "timestamp_invalid", true);
    invalid.instrumentMode = gate0::InstrumentMode::N;
    invalid.nativeDeviceIntervalNanoseconds.reset();
    const auto summary = gate0::SummarizeSamples(
        configuration, "condition", "series", {invalid},
        "timestamp_invalid", "native_timing", std::nullopt);
    ASSERT_TRUE(summary.nativeDeviceIntervalNanoseconds.has_value());
    EXPECT_FALSE(summary.nativeDeviceIntervalNanoseconds->sampleCount.has_value());
    EXPECT_EQ(summary.failedSampleCount, 1U);
}

TEST(Ex2Gate0Summary, WarmupCharacterizationHasNoSteadyStateMetrics)
{
    const auto summary = gate0::SummarizeSamples(
        Configuration(gate0::QualificationPhase::WarmupCharacterization),
        "condition", "series", {});
    EXPECT_EQ(summary.recordedSampleCount, 0U);
    EXPECT_FALSE(summary.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(summary.hostCompletionNanoseconds.has_value());
}

TEST(Ex2Gate0Package, CharacterizationStartsWithHeaderOnlySamplesAndWarmupFiles)
{
    const auto finalPath = UniqueTemporaryPath("warmup-package");
    RemovePath cleanup{finalPath};
    auto configuration = Configuration(
        gate0::QualificationPhase::WarmupCharacterization);
    configuration.outputPackageDirectory = finalPath;
    {
        gate0::QualificationPackage package{configuration};
        EXPECT_EQ(ReadAll(package.StagingDirectory() / "samples.csv"),
            gate0::SamplesCsvHeader() + "\r\n");
        EXPECT_EQ(ReadAll(package.StagingDirectory() / "warmup.csv"),
            gate0::WarmupCsvHeader() + "\r\n");
        EXPECT_THROW(package.AppendWarmup({
            "test-run", "series", 2U, 1U, 1U, 2U, 3U, "ok"}),
            std::invalid_argument);
        package.AppendWarmup({
            "test-run", "series", 2U, 0U, 1U, 2U, 3U, "ok"});
        EXPECT_NE(ReadAll(package.StagingDirectory() / "warmup.csv").find(
            "2,test-run,series,2,0,1,2,3,ok\r\n"), std::string::npos);
    }
}

TEST(Ex2Gate0Package, RejectsFinalOrIncompletePathCollisions)
{
    const auto finalPath = UniqueTemporaryPath("collision");
    RemovePath cleanup{finalPath};
    auto configuration = Configuration();
    configuration.outputPackageDirectory = finalPath;
    gate0::QualificationPackage first{configuration};
    EXPECT_THROW(gate0::QualificationPackage second{configuration},
        std::invalid_argument);
}

TEST(Ex2Gate0Package, RejectsEnvironmentAndSummaryConfigurationDrift)
{
    const auto finalPath = UniqueTemporaryPath("configuration-drift");
    RemovePath cleanup{finalPath};
    auto configuration = Configuration();
    configuration.outputPackageDirectory = finalPath;
    auto environment = Environment(configuration);
    const auto sample = PackageSample(configuration, environment);
    auto summary = gate0::SummarizeSamples(
        configuration, environment.comparisonConditionId, environment.seriesId,
        {sample}, "incomplete");

    gate0::QualificationPackage package{configuration};
    package.WriteInitialization(Initialization(configuration));
    package.AppendSample(sample);

    auto changedEnvironment = environment;
    changedEnvironment.configuration.phase =
        gate0::QualificationPhase::InstrumentationControl;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    changedEnvironment.configuration.backend = gate0::Backend::Vulkan;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    changedEnvironment.configuration.instrumentMode = gate0::InstrumentMode::N;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.processIndex;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.blockIndex;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    changedEnvironment.configuration.orderSlot = 0U;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.seed;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.elementCount;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.warmupCount;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);
    changedEnvironment = environment;
    ++changedEnvironment.configuration.plannedSampleCount;
    EXPECT_THROW(package.Complete(changedEnvironment, summary), std::invalid_argument);

    auto changedSummary = summary;
    ++changedSummary.configuration.processIndex;
    EXPECT_THROW(package.Complete(environment, changedSummary), std::invalid_argument);
}

TEST(Ex2Gate0Package, RejectsSummaryCountsThatDisagreeWithAppendedRows)
{
    const auto finalPath = UniqueTemporaryPath("summary-count-drift");
    RemovePath cleanup{finalPath};
    auto configuration = Configuration();
    configuration.outputPackageDirectory = finalPath;
    const auto environment = Environment(configuration);
    const auto sample = PackageSample(configuration, environment);
    auto summary = gate0::SummarizeSamples(
        configuration, environment.comparisonConditionId, environment.seriesId,
        {sample}, "incomplete");

    gate0::QualificationPackage package{configuration};
    package.WriteInitialization(Initialization(configuration));
    package.AppendSample(sample);

    ++summary.recordedSampleCount;
    EXPECT_THROW(package.Complete(environment, summary), std::invalid_argument);
    --summary.recordedSampleCount;
    summary.hostCompletionNanoseconds->sampleCount = 2U;
    EXPECT_THROW(package.Complete(environment, summary), std::invalid_argument);
}

TEST(Ex2Gate0Package, RejectsProvenanceAndAppendedRowIdentityDrift)
{
    const auto finalPath = UniqueTemporaryPath("identity-drift");
    RemovePath cleanup{finalPath};
    auto configuration = Configuration();
    configuration.outputPackageDirectory = finalPath;
    auto environment = Environment(configuration);
    auto sample = PackageSample(configuration, environment);
    sample.seriesId = std::string(64U, 'b');
    auto summary = gate0::SummarizeSamples(
        configuration, sample.comparisonConditionId, sample.seriesId,
        {sample}, "incomplete");

    gate0::QualificationPackage package{configuration};
    package.WriteInitialization(Initialization(configuration));
    package.AppendSample(sample);
    EXPECT_THROW(package.Complete(environment, summary), std::invalid_argument);

    environment.common.gitCommit = std::string(40U, 'b');
    EXPECT_THROW(package.Complete(environment, summary), std::invalid_argument);
}

TEST(Ex2Gate0Environment, EmitsQualificationProvenanceWithoutPrivateFields)
{
    auto configuration = Configuration();
    computelab::results::EnvironmentRecord common{
        2U, "EX-2", "test-run", "2026-09-17T00:00:00Z",
        std::string(40U, 'a'), true, "anonymous-machine",
        "Windows", "11", "CPU", 1024U,
        "GPU", "NVIDIA", "0x00000001", 2048U,
        "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", false, false};
    gate0::EnvironmentRecord environment{
        common, configuration, std::string(64U, 'b'), std::string(64U, 'c'),
        std::string(40U, 'a'), std::string(64U, 'd'), std::nullopt,
        std::string(64U, 'e'), std::string(64U, 'f'),
        "00112233-4455-6677-8899-aabbccddeeff",
        {.implementation = "cuda-qualification-operation",
         .streamFlags = "cudaStreamNonBlocking"}};
    const std::string json = gate0::SerializeEnvironmentJson(environment);
    EXPECT_NE(json.find("\"schema_version\":2"), std::string::npos);
    EXPECT_NE(json.find("\"evidence_kind\":\"qualification\""), std::string::npos);
    EXPECT_NE(json.find("\"input_sha256\":"), std::string::npos);
    EXPECT_EQ(json.find("hostname"), std::string::npos);
    EXPECT_EQ(json.find("username"), std::string::npos);
    EXPECT_EQ(json.find("filesystem"), std::string::npos);
}

} // namespace
