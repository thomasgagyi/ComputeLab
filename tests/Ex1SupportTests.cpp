#include "environment/EnvironmentCollector.hpp"
#include "ex1/Ex1Support.hpp"
#include "results/ResultRecords.hpp"

#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace ex1 = computelab::ex1;
namespace environment = computelab::environment;
namespace results = computelab::results;

ex1::Configuration SeriesConfiguration()
{
    return {
        ex1::Mode::Series,
        ex1::Backend::Cpu,
        1024U,
        42U,
        "ordinary",
        "run-one",
        "2026-09-13T12:34:56Z",
        "0123456789abcdef",
        true,
        "q0-test",
        false,
        false,
        3U,
        10U};
}

std::vector<std::string_view> CompleteSeriesArguments()
{
    return {
        "--mode", "series",
        "--backend", "cpu",
        "--element-count", "1024",
        "--seed", "42",
        "--variant", "ordinary",
        "--run-id", "run-one",
        "--timestamp-utc", "2026-09-13T12:34:56Z",
        "--git-commit", "0123456789abcdef",
        "--git-dirty", "true",
        "--machine-id", "q0-test",
        "--validation-enabled", "false",
        "--diagnostic-instrumentation", "false",
        "--warmup-count", "3",
        "--planned-sample-count", "10"};
}

void SetOption(std::vector<std::string_view>& arguments,
    std::string_view name, std::string_view value)
{
    const auto found = std::find(arguments.begin(), arguments.end(), name);
    ASSERT_NE(found, arguments.end());
    *(found + 1) = value;
}

void RemoveOption(std::vector<std::string_view>& arguments, std::string_view name)
{
    const auto found = std::find(arguments.begin(), arguments.end(), name);
    ASSERT_NE(found, arguments.end());
    arguments.erase(found, found + 2);
}

results::SampleRecord Sample(
    std::string seriesId,
    std::uint64_t sampleIndex,
    bool validationPassed,
    std::optional<std::uint64_t> endToEnd,
    std::string backend = "cpu",
    std::uint64_t seed = 42U)
{
    return {
        1U, "run-one", "EX-1", std::move(seriesId), std::move(backend),
        "ex1-transform-v1", "ordinary", seed, 1024U, 3U, 100U,
        sampleIndex, validationPassed, std::nullopt, std::nullopt,
        std::nullopt, endToEnd, std::nullopt};
}

results::MetricSummary EndToEnd(
    const results::SummaryRecord& summary,
    std::size_t groupIndex = 0U)
{
    EXPECT_TRUE(summary.sampleGroups[groupIndex].endToEndNs.has_value());
    return *summary.sampleGroups[groupIndex].endToEndNs;
}

results::SummaryRecord Summary(const std::vector<results::SampleRecord>& samples)
{
    return ex1::SummarizeSamples(1U, "run-one", "EX-1", samples);
}

results::EnvironmentRecord PackageEnvironment(std::string runId)
{
    results::EnvironmentRecord record;
    record.schemaVersion = 1U;
    record.experimentId = "EX-1";
    record.runId = std::move(runId);
    return record;
}

class ScopedTestDirectory final
{
public:
    ScopedTestDirectory()
        : path{std::filesystem::temp_directory_path()
              / ("computelab-a8-" + std::to_string(GetCurrentProcessId()))}
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    ~ScopedTestDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    std::filesystem::path path;
};

TEST(Ex1Configuration, ApprovedWarmupValuesAreAccepted)
{
    for (const std::string_view value : {"0", "1", "3", "5", "10", "20"})
    {
        auto arguments = CompleteSeriesArguments();
        SetOption(arguments, "--warmup-count", value);
        EXPECT_NO_THROW(static_cast<void>(ex1::ParseArguments(arguments))) << value;
    }
}

TEST(Ex1Configuration, NonApprovedWarmupValuesAreRejected)
{
    for (const std::string_view value : {"2", "4", "21"})
    {
        auto arguments = CompleteSeriesArguments();
        SetOption(arguments, "--warmup-count", value);
        EXPECT_THROW(static_cast<void>(ex1::ParseArguments(arguments)), std::invalid_argument)
            << value;
    }
}

TEST(Ex1Configuration, ApprovedSampleCountsAreAccepted)
{
    for (const std::string_view value : {"10", "30", "100"})
    {
        auto arguments = CompleteSeriesArguments();
        SetOption(arguments, "--planned-sample-count", value);
        EXPECT_NO_THROW(static_cast<void>(ex1::ParseArguments(arguments))) << value;
    }
}

TEST(Ex1Configuration, NonApprovedSampleCountsAreRejected)
{
    for (const std::string_view value : {"0", "9", "31", "101"})
    {
        auto arguments = CompleteSeriesArguments();
        SetOption(arguments, "--planned-sample-count", value);
        EXPECT_THROW(static_cast<void>(ex1::ParseArguments(arguments)), std::invalid_argument)
            << value;
    }
}

TEST(Ex1Configuration, RejectsBackendAll)
{
    auto arguments = CompleteSeriesArguments();
    SetOption(arguments, "--backend", "all");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(arguments)), std::invalid_argument);
}

TEST(Ex1Configuration, SeriesRequiresExplicitSeedAndElementCount)
{
    auto withoutSeed = CompleteSeriesArguments();
    RemoveOption(withoutSeed, "--seed");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(withoutSeed)), std::invalid_argument);

    auto withoutElementCount = CompleteSeriesArguments();
    RemoveOption(withoutElementCount, "--element-count");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(withoutElementCount)), std::invalid_argument);
}

TEST(Ex1Configuration, InitializationRejectsCpu)
{
    auto arguments = CompleteSeriesArguments();
    SetOption(arguments, "--mode", "initialization");
    RemoveOption(arguments, "--warmup-count");
    RemoveOption(arguments, "--planned-sample-count");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(arguments)), std::invalid_argument);
}

TEST(Ex1Configuration, RejectsMalformedTimestampAndGitCommitContext)
{
    auto invalidTimestamp = CompleteSeriesArguments();
    SetOption(invalidTimestamp, "--timestamp-utc", "2026-02-30T12:00:00Z");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(invalidTimestamp)), std::invalid_argument);

    auto invalidCommit = CompleteSeriesArguments();
    SetOption(invalidCommit, "--git-commit", "not-a-commit");
    EXPECT_THROW(static_cast<void>(ex1::ParseArguments(invalidCommit)), std::invalid_argument);
}

TEST(Ex1SeriesId, IsDeterministic)
{
    const auto configuration = SeriesConfiguration();
    EXPECT_EQ(ex1::SeriesId(configuration), ex1::SeriesId(configuration));
}

TEST(Ex1SeriesId, DoesNotIncludeRunId)
{
    auto first = SeriesConfiguration();
    auto second = first;
    second.runId = "independent-repeat-run";
    second.timestampUtc = "2026-09-14T00:00:00Z";
    EXPECT_EQ(ex1::SeriesId(first), ex1::SeriesId(second));
}

TEST(Ex1SeriesId, IncludesEveryVariableSemanticSeriesParameter)
{
    const auto original = SeriesConfiguration();
    const std::string identifier = ex1::SeriesId(original);

    auto changed = original;
    changed.backend = ex1::Backend::Cuda;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    changed.variant = "diagnostic";
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    ++changed.seed;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    ++changed.elementCount;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    changed.warmupCount = 5U;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    changed.plannedSampleCount = 30U;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    changed.validationEnabled = true;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
    changed = original;
    changed.diagnosticInstrumentation = true;
    EXPECT_NE(ex1::SeriesId(changed), identifier);
}

TEST(Ex1Summary, GroupsByFullApprovedSampleGroupKey)
{
    const auto summary = Summary({
        Sample("series-cpu", 0U, true, 10U, "cpu", 42U),
        Sample("series-cuda", 0U, true, 20U, "cuda", 42U),
        Sample("series-seed", 0U, true, 30U, "cpu", 43U)});
    ASSERT_EQ(summary.sampleGroups.size(), 3U);
    EXPECT_EQ(summary.sampleGroups[0].group.seriesId, "series-cpu");
    EXPECT_EQ(summary.sampleGroups[1].group.seriesId, "series-cuda");
    EXPECT_EQ(summary.sampleGroups[2].group.seriesId, "series-seed");
}

TEST(Ex1Summary, EmitsGroupsInDeterministicOrder)
{
    const std::vector<results::SampleRecord> forward{
        Sample("b-series", 0U, true, 2U), Sample("a-series", 0U, true, 1U)};
    const std::vector<results::SampleRecord> reverse{
        Sample("a-series", 0U, true, 1U), Sample("b-series", 0U, true, 2U)};
    const auto first = results::SerializeSummaryJson(Summary(forward));
    const auto second = results::SerializeSummaryJson(Summary(reverse));
    EXPECT_EQ(first, second);
}

TEST(Ex1Summary, RejectsDuplicateSampleIndicesWithinSeries)
{
    EXPECT_THROW(static_cast<void>(Summary({
        Sample("series", 0U, true, 1U), Sample("series", 0U, true, 2U)})),
        std::invalid_argument);
}

TEST(Ex1Summary, RejectedSamplesRemainRecordedButAreExcludedFromMetrics)
{
    const auto summary = Summary({
        Sample("series", 0U, true, 10U),
        Sample("series", 1U, false, 1'000U),
        Sample("series", 2U, true, 30U)});
    ASSERT_EQ(summary.sampleGroups.size(), 1U);
    EXPECT_EQ(summary.sampleGroups[0].recordedSampleCount, 3U);
    EXPECT_EQ(summary.sampleGroups[0].validationFailures, 1U);
    const auto& metric = EndToEnd(summary);
    ASSERT_TRUE(metric.sampleCount.has_value());
    EXPECT_EQ(*metric.sampleCount, 2U);
    EXPECT_EQ(metric.minimum, 10.0);
    EXPECT_EQ(metric.mean, 20.0);
}

TEST(Ex1Summary, CalculatesMinimumAndOddMedian)
{
    const auto& metric = EndToEnd(Summary({
        Sample("series", 2U, true, 9U),
        Sample("series", 0U, true, 5U),
        Sample("series", 1U, true, 1U)}));
    EXPECT_EQ(metric.minimum, 1.0);
    EXPECT_EQ(metric.median, 5.0);
}

TEST(Ex1Summary, CalculatesEvenMedianAsBinary64Average)
{
    const auto& metric = EndToEnd(Summary({
        Sample("series", 0U, true, 1U),
        Sample("series", 1U, true, 2U),
        Sample("series", 2U, true, 8U),
        Sample("series", 3U, true, 10U)}));
    EXPECT_EQ(metric.median, 5.0);
}

TEST(Ex1Summary, MeanUsesAscendingSampleIndexOrder)
{
    constexpr std::uint64_t beyondExactInteger = 9'007'199'254'740'992ULL;
    const auto& metric = EndToEnd(Summary({
        Sample("series", 2U, true, 1U),
        Sample("series", 0U, true, beyondExactInteger),
        Sample("series", 1U, true, 1U)}));
    double approvedSum = 0.0;
    approvedSum += static_cast<double>(beyondExactInteger);
    approvedSum += 1.0;
    approvedSum += 1.0;
    EXPECT_EQ(metric.mean, approvedSum / 3.0);
}

TEST(Ex1Summary, UsesSampleStandardDeviationWithNMinusOne)
{
    const auto& metric = EndToEnd(Summary({
        Sample("series", 0U, true, 2U),
        Sample("series", 1U, true, 4U),
        Sample("series", 2U, true, 4U),
        Sample("series", 3U, true, 4U),
        Sample("series", 4U, true, 5U),
        Sample("series", 5U, true, 5U),
        Sample("series", 6U, true, 7U),
        Sample("series", 7U, true, 9U)}));
    ASSERT_TRUE(metric.standardDeviation.has_value());
    EXPECT_DOUBLE_EQ(*metric.standardDeviation, std::sqrt(32.0 / 7.0));
}

TEST(Ex1Summary, StandardDeviationIsNullBelowTwoSamples)
{
    const auto& metric = EndToEnd(Summary({Sample("series", 0U, true, 7U)}));
    EXPECT_FALSE(metric.standardDeviation.has_value());
    EXPECT_FALSE(metric.coefficientOfVariation.has_value());
}

TEST(Ex1Summary, CoefficientOfVariationIsStandardDeviationOverMean)
{
    const auto& metric = EndToEnd(Summary({
        Sample("series", 0U, true, 2U), Sample("series", 1U, true, 4U)}));
    ASSERT_TRUE(metric.standardDeviation.has_value());
    ASSERT_TRUE(metric.coefficientOfVariation.has_value());
    EXPECT_DOUBLE_EQ(*metric.coefficientOfVariation,
        *metric.standardDeviation / *metric.mean);
}

TEST(Ex1Summary, CoefficientOfVariationIsNullForZeroMean)
{
    const auto& metric = EndToEnd(Summary({
        Sample("series", 0U, true, 0U), Sample("series", 1U, true, 0U)}));
    ASSERT_EQ(metric.mean, 0.0);
    EXPECT_FALSE(metric.coefficientOfVariation.has_value());
}

TEST(Ex1Summary, P95UsesNearestRank)
{
    std::vector<results::SampleRecord> samples;
    for (std::uint64_t value = 1U; value <= 20U; ++value)
    {
        samples.push_back(Sample("series", value - 1U, true, value));
    }
    EXPECT_EQ(EndToEnd(Summary(samples)).p95, 19.0);
}

TEST(Ex1Summary, ApplicableMetricWithNoAcceptedValuesUsesApprovedNulls)
{
    const auto summary = Summary({Sample("series", 0U, false, 100U)});
    const auto& metric = EndToEnd(summary);
    EXPECT_FALSE(metric.sampleCount.has_value());
    EXPECT_FALSE(metric.minimum.has_value());
    EXPECT_FALSE(metric.median.has_value());
    EXPECT_FALSE(metric.mean.has_value());
    EXPECT_FALSE(metric.standardDeviation.has_value());
    EXPECT_FALSE(metric.coefficientOfVariation.has_value());
    EXPECT_FALSE(metric.p95.has_value());
    EXPECT_FALSE(summary.sampleGroups[0].hostSubmissionNs.has_value());
}

TEST(Ex1Summary, HandlesMultipleIndependentGroups)
{
    const auto summary = Summary({
        Sample("series-b", 0U, true, 20U),
        Sample("series-a", 0U, true, 10U),
        Sample("series-b", 1U, true, 40U),
        Sample("series-a", 1U, true, 30U)});
    ASSERT_EQ(summary.sampleGroups.size(), 2U);
    EXPECT_EQ(summary.sampleGroups[0].group.seriesId, "series-a");
    EXPECT_EQ(EndToEnd(summary, 0U).mean, 20.0);
    EXPECT_EQ(summary.sampleGroups[1].group.seriesId, "series-b");
    EXPECT_EQ(EndToEnd(summary, 1U).mean, 30.0);
}

TEST(Ex1Samples, CpuRecordHasOnlyEndToEndTiming)
{
    const auto sample = ex1::MakeSampleRecord(
        SeriesConfiguration(), 0U, true,
        {std::nullopt, std::nullopt, std::nullopt, 123U, std::nullopt});
    EXPECT_FALSE(sample.uploadNs.has_value());
    EXPECT_FALSE(sample.hostSubmissionNs.has_value());
    EXPECT_FALSE(sample.deviceExecutionNs.has_value());
    EXPECT_EQ(sample.endToEndNs, 123U);
    EXPECT_FALSE(sample.downloadNs.has_value());
}

TEST(Ex1Samples, FailedCorrectnessIsRejectedEvidenceNotAcceptedPerformance)
{
    const auto sample = ex1::MakeSampleRecord(
        SeriesConfiguration(), 0U, false,
        {std::nullopt, std::nullopt, std::nullopt, 123U, std::nullopt});
    EXPECT_FALSE(sample.validationPassed);
    const auto summary = Summary({sample});
    EXPECT_EQ(summary.sampleGroups[0].recordedSampleCount, 1U);
    EXPECT_EQ(summary.sampleGroups[0].validationFailures, 1U);
    EXPECT_FALSE(EndToEnd(summary).sampleCount.has_value());
}

TEST(Ex1Environment, CallerAndSourceContextSurvivesIntoEnvironmentRecord)
{
    const auto configuration = SeriesConfiguration();
    const auto context = ex1::EnvironmentContext(configuration);
    const environment::WindowsHostMetadata host{"Windows", "11", "CPU", 1024U};
    const environment::BuildMetadata build{
        "MSVC", "19.51", "4.3", "1.13", "x64-debug", "Debug", "13.4", "1.4"};
    const environment::CudaDeviceMetadata cuda{{{1U}}, "GPU", 2048U, 8, 6, "13.4"};
    const environment::VulkanDeviceMetadata vulkan{{{1U}}, 0x10DEU, 0x1234U, "1.4"};
    const auto record = environment::ComposeEnvironmentRecord(
        context, host, build, cuda, {vulkan}, "driver");
    EXPECT_EQ(record.runId, configuration.runId);
    EXPECT_EQ(record.timestampUtc, configuration.timestampUtc);
    EXPECT_EQ(record.gitCommit, configuration.gitCommit);
    EXPECT_EQ(record.gitDirty, configuration.gitDirty);
    EXPECT_EQ(record.machineId, configuration.machineId);
    EXPECT_EQ(record.validationEnabled, configuration.validationEnabled);
    EXPECT_EQ(record.diagnosticInstrumentation, configuration.diagnosticInstrumentation);
}

TEST(Ex1ResultPackage, WritesExactlyApprovedSchemaValidEmptyEvidenceFiles)
{
    ScopedTestDirectory directory;
    const auto environment = PackageEnvironment("empty-package");
    const results::SummaryRecord summary{1U, "empty-package", "EX-1", {}};
    ex1::WriteResultPackage(directory.path, environment, {}, {}, summary);

    const auto package = directory.path / "empty-package";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(package))
    {
        files.push_back(entry.path().filename());
    }
    std::sort(files.begin(), files.end());
    EXPECT_EQ(files, (std::vector<std::filesystem::path>{
        "environment.json", "initialization.csv", "samples.csv", "summary.json"}));

    std::ifstream initialization(package / "initialization.csv", std::ios::binary);
    const std::string initializationText{
        std::istreambuf_iterator<char>(initialization), std::istreambuf_iterator<char>()};
    EXPECT_EQ(std::count(initializationText.begin(), initializationText.end(), '\n'), 1);

    std::ifstream samples(package / "samples.csv", std::ios::binary);
    const std::string sampleText{
        std::istreambuf_iterator<char>(samples), std::istreambuf_iterator<char>()};
    EXPECT_EQ(std::count(sampleText.begin(), sampleText.end(), '\n'), 1);
    EXPECT_NE(sampleText.find("schema_version,run_id,experiment_id,series_id"),
        std::string::npos);
}

TEST(Ex1ResultPackage, RejectsResultDirectoryCollision)
{
    ScopedTestDirectory directory;
    const auto environment = PackageEnvironment("collision-package");
    const results::SummaryRecord summary{1U, "collision-package", "EX-1", {}};
    ex1::WriteResultPackage(directory.path, environment, {}, {}, summary);
    EXPECT_THROW(
        ex1::WriteResultPackage(directory.path, environment, {}, {}, summary),
        std::runtime_error);
}

} // namespace
