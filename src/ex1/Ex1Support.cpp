#include "ex1/Ex1Support.hpp"

#include "oracle/DeterministicTransform.hpp"
#include "timing/HostTiming.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

namespace computelab::ex1
{
namespace
{

[[noreturn]] void InvalidConfiguration(std::string_view detail)
{
    throw std::invalid_argument("invalid EX-1 configuration: " + std::string(detail));
}

bool IsIdentifierCharacter(char value) noexcept
{
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '.' || value == '_' || value == '-';
}

void RequireIdentifier(std::string_view value, std::string_view field)
{
    if (value.empty() || value.size() > 128U || value == "." || value == ".."
        || !std::all_of(value.begin(), value.end(), IsIdentifierCharacter))
    {
        InvalidConfiguration(std::string(field) +
            " must contain only ASCII letters, digits, '.', '_', or '-' and cannot be a path segment");
    }
}

unsigned int ParseFixedDecimal(std::string_view value, std::size_t offset,
    std::size_t length, std::string_view field)
{
    unsigned int result = 0U;
    for (std::size_t index = offset; index < offset + length; ++index)
    {
        if (index >= value.size() || value[index] < '0' || value[index] > '9')
        {
            InvalidConfiguration(std::string(field) + " must be a UTC ISO 8601 timestamp");
        }
        result = result * 10U + static_cast<unsigned int>(value[index] - '0');
    }
    return result;
}

void RequireUtcTimestamp(std::string_view value)
{
    const bool baseShape = value.size() >= 20U
        && value[4] == '-' && value[7] == '-' && value[10] == 'T'
        && value[13] == ':' && value[16] == ':' && value.back() == 'Z';
    const bool fractionShape = value.size() == 20U
        || (value.size() > 21U && value[19] == '.'
            && std::all_of(value.begin() + 20, value.end() - 1,
                [](char character) { return character >= '0' && character <= '9'; }));
    if (!baseShape || !fractionShape)
    {
        InvalidConfiguration("timestamp-utc must be a UTC ISO 8601 timestamp ending in 'Z'");
    }

    const unsigned int year = ParseFixedDecimal(value, 0U, 4U, "timestamp-utc");
    const unsigned int month = ParseFixedDecimal(value, 5U, 2U, "timestamp-utc");
    const unsigned int day = ParseFixedDecimal(value, 8U, 2U, "timestamp-utc");
    const unsigned int hour = ParseFixedDecimal(value, 11U, 2U, "timestamp-utc");
    const unsigned int minute = ParseFixedDecimal(value, 14U, 2U, "timestamp-utc");
    const unsigned int second = ParseFixedDecimal(value, 17U, 2U, "timestamp-utc");
    constexpr std::array<unsigned int, 12> daysPerMonth{
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if (year == 0U || month == 0U || month > 12U || day == 0U
        || hour > 23U || minute > 59U || second > 59U)
    {
        InvalidConfiguration("timestamp-utc contains an out-of-range date or time");
    }
    unsigned int maximumDay = daysPerMonth[month - 1U];
    const bool leapYear = year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
    if (month == 2U && leapYear) maximumDay = 29U;
    if (day > maximumDay)
    {
        InvalidConfiguration("timestamp-utc contains an out-of-range calendar date");
    }
}

void RequireGitCommit(std::string_view value)
{
    const bool hexadecimal = std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
    });
    if (value.size() < 7U || value.size() > 64U || !hexadecimal)
    {
        InvalidConfiguration("git-commit must be a 7-64 character hexadecimal commit identifier");
    }
}

std::uint64_t ParseUnsigned(std::string_view value, std::string_view field)
{
    if (value.empty() || value.front() == '+' || value.front() == '-')
    {
        InvalidConfiguration(std::string(field) + " must be an unsigned decimal integer");
    }

    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size())
    {
        InvalidConfiguration(std::string(field) + " must be an unsigned decimal integer");
    }
    return parsed;
}

bool ParseBoolean(std::string_view value, std::string_view field)
{
    if (value == "true") return true;
    if (value == "false") return false;
    InvalidConfiguration(std::string(field) + " must be 'true' or 'false'");
}

Mode ParseMode(std::string_view value)
{
    if (value == "initialization") return Mode::Initialization;
    if (value == "series") return Mode::Series;
    InvalidConfiguration("mode must be 'initialization' or 'series'");
}

Backend ParseBackend(std::string_view value)
{
    if (value == "cpu") return Backend::Cpu;
    if (value == "cuda") return Backend::Cuda;
    if (value == "vulkan") return Backend::Vulkan;
    InvalidConfiguration("backend must be exactly one of 'cpu', 'cuda', or 'vulkan'");
}

using OptionMap = std::map<std::string, std::string, std::less<>>;

const std::string& RequireOption(const OptionMap& options, std::string_view name)
{
    const auto found = options.find(name);
    if (found == options.end())
    {
        InvalidConfiguration("missing required option --" + std::string(name));
    }
    return found->second;
}

struct SampleGroupKeyLess
{
    bool operator()(const results::SampleGroupKey& left,
        const results::SampleGroupKey& right) const noexcept
    {
        return std::tie(left.seriesId, left.backend, left.workload, left.variant,
                   left.seed, left.elementCount, left.warmupCount, left.plannedSampleCount)
            < std::tie(right.seriesId, right.backend, right.workload, right.variant,
                   right.seed, right.elementCount, right.warmupCount, right.plannedSampleCount);
    }
};

bool SameGroupKey(const results::SampleGroupKey& left,
    const results::SampleGroupKey& right) noexcept
{
    const SampleGroupKeyLess less;
    return !less(left, right) && !less(right, left);
}

results::SampleGroupKey GroupKey(const results::SampleRecord& sample)
{
    return {
        sample.seriesId,
        sample.backend,
        sample.workload,
        sample.variant,
        sample.seed,
        sample.elementCount,
        sample.warmupCount,
        sample.plannedSampleCount};
}

template <typename SelectMetric>
std::optional<results::MetricSummary> SummarizeMetric(
    const std::vector<const results::SampleRecord*>& orderedSamples,
    SelectMetric selectMetric)
{
    const bool applicable = std::any_of(
        orderedSamples.begin(), orderedSamples.end(),
        [&selectMetric](const results::SampleRecord* sample) {
            return selectMetric(*sample).has_value();
        });
    if (!applicable)
    {
        return std::nullopt;
    }

    std::vector<double> orderedValues;
    orderedValues.reserve(orderedSamples.size());
    for (const results::SampleRecord* sample : orderedSamples)
    {
        const auto value = selectMetric(*sample);
        if (sample->validationPassed && value.has_value())
        {
            orderedValues.push_back(static_cast<double>(*value));
        }
    }

    if (orderedValues.empty())
    {
        return results::MetricSummary{};
    }

    double sum = 0.0;
    for (const double value : orderedValues)
    {
        sum += value;
    }
    const double mean = sum / static_cast<double>(orderedValues.size());

    std::optional<double> standardDeviation;
    if (orderedValues.size() >= 2U)
    {
        double squaredDeviationSum = 0.0;
        for (const double value : orderedValues)
        {
            const double deviation = value - mean;
            squaredDeviationSum += deviation * deviation;
        }
        standardDeviation = std::sqrt(
            squaredDeviationSum / static_cast<double>(orderedValues.size() - 1U));
    }

    std::vector<double> sortedValues = orderedValues;
    std::sort(sortedValues.begin(), sortedValues.end());
    double median{};
    const std::size_t middle = sortedValues.size() / 2U;
    if (sortedValues.size() % 2U == 0U)
    {
        median = (sortedValues[middle - 1U] + sortedValues[middle]) / 2.0;
    }
    else
    {
        median = sortedValues[middle];
    }

    const std::size_t p95OneBasedRank =
        sortedValues.size() - sortedValues.size() / 20U;
    const double p95 = sortedValues[p95OneBasedRank - 1U];

    std::optional<double> coefficientOfVariation;
    if (standardDeviation.has_value() && mean != 0.0)
    {
        coefficientOfVariation = *standardDeviation / mean;
    }

    return results::MetricSummary{
        static_cast<std::uint64_t>(orderedValues.size()),
        sortedValues.front(),
        median,
        mean,
        standardDeviation,
        coefficientOfVariation,
        p95};
}

void AppendHashByte(std::uint64_t& hash, unsigned char value) noexcept
{
    constexpr std::uint64_t prime = 1099511628211ULL;
    hash ^= value;
    hash *= prime;
}

void AppendHashString(std::uint64_t& hash, std::string_view value) noexcept
{
    const std::uint64_t length = static_cast<std::uint64_t>(value.size());
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
    {
        AppendHashByte(hash, static_cast<unsigned char>(length >> shift));
    }
    for (const unsigned char character : value)
    {
        AppendHashByte(hash, character);
    }
}

void AppendHashInteger(std::uint64_t& hash, std::uint64_t value) noexcept
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
    {
        AppendHashByte(hash, static_cast<unsigned char>(value >> shift));
    }
}

void WriteFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream)
    {
        throw std::runtime_error("unable to create result file: " + path.string());
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    if (!stream)
    {
        throw std::runtime_error("unable to write result file: " + path.string());
    }
}

} // namespace

std::string_view ToString(Backend backend) noexcept
{
    switch (backend)
    {
    case Backend::Cpu: return "cpu";
    case Backend::Cuda: return "cuda";
    case Backend::Vulkan: return "vulkan";
    }
    return "unknown";
}

bool IsApprovedWarmupCount(std::uint64_t value) noexcept
{
    constexpr std::array approved{0ULL, 1ULL, 3ULL, 5ULL, 10ULL, 20ULL};
    return std::find(approved.begin(), approved.end(), value) != approved.end();
}

bool IsApprovedSampleCount(std::uint64_t value) noexcept
{
    constexpr std::array approved{10ULL, 30ULL, 100ULL};
    return std::find(approved.begin(), approved.end(), value) != approved.end();
}

Configuration ParseArguments(std::span<const std::string_view> arguments)
{
    constexpr std::array<std::string_view, 13> allowedOptions{
        "mode", "backend", "element-count", "seed", "variant", "run-id",
        "timestamp-utc", "git-commit", "git-dirty", "machine-id",
        "validation-enabled", "diagnostic-instrumentation", "warmup-count"};
    constexpr std::string_view plannedSampleOption = "planned-sample-count";

    OptionMap options;
    for (std::size_t index = 0U; index < arguments.size(); index += 2U)
    {
        if (index + 1U >= arguments.size() || !arguments[index].starts_with("--"))
        {
            InvalidConfiguration("options must be supplied as --name value pairs");
        }
        const std::string_view name = arguments[index].substr(2U);
        const bool allowed = std::find(allowedOptions.begin(), allowedOptions.end(), name)
                != allowedOptions.end()
            || name == plannedSampleOption;
        if (!allowed)
        {
            InvalidConfiguration("unknown option --" + std::string(name));
        }
        if (!options.emplace(std::string(name), std::string(arguments[index + 1U])).second)
        {
            InvalidConfiguration("duplicate option --" + std::string(name));
        }
    }

    Configuration configuration;
    configuration.mode = ParseMode(RequireOption(options, "mode"));
    configuration.backend = ParseBackend(RequireOption(options, "backend"));
    configuration.elementCount = ParseUnsigned(
        RequireOption(options, "element-count"), "element-count");
    configuration.seed = ParseUnsigned(RequireOption(options, "seed"), "seed");
    configuration.variant = RequireOption(options, "variant");
    configuration.runId = RequireOption(options, "run-id");
    configuration.timestampUtc = RequireOption(options, "timestamp-utc");
    configuration.gitCommit = RequireOption(options, "git-commit");
    configuration.gitDirty = ParseBoolean(RequireOption(options, "git-dirty"), "git-dirty");
    configuration.machineId = RequireOption(options, "machine-id");
    configuration.validationEnabled = ParseBoolean(
        RequireOption(options, "validation-enabled"), "validation-enabled");
    configuration.diagnosticInstrumentation = ParseBoolean(
        RequireOption(options, "diagnostic-instrumentation"), "diagnostic-instrumentation");

    RequireIdentifier(configuration.variant, "variant");
    RequireIdentifier(configuration.runId, "run-id");
    RequireIdentifier(configuration.machineId, "machine-id");
    RequireUtcTimestamp(configuration.timestampUtc);
    RequireGitCommit(configuration.gitCommit);

    const auto warmup = options.find("warmup-count");
    const auto planned = options.find(plannedSampleOption);
    if (configuration.mode == Mode::Series)
    {
        if (warmup == options.end() || planned == options.end())
        {
            InvalidConfiguration(
                "series mode requires explicit --warmup-count and --planned-sample-count");
        }
        configuration.warmupCount = ParseUnsigned(warmup->second, "warmup-count");
        configuration.plannedSampleCount = ParseUnsigned(
            planned->second, "planned-sample-count");
        if (!IsApprovedWarmupCount(*configuration.warmupCount))
        {
            InvalidConfiguration("warmup-count is not an approved EX-1 value");
        }
        if (!IsApprovedSampleCount(*configuration.plannedSampleCount))
        {
            InvalidConfiguration("planned-sample-count is not an approved EX-1 value");
        }
    }
    else
    {
        if (configuration.backend == Backend::Cpu)
        {
            InvalidConfiguration("initialization mode applies only to CUDA or Vulkan");
        }
        if (warmup != options.end() || planned != options.end())
        {
            InvalidConfiguration(
                "initialization mode does not accept series warm-up or sample-count options");
        }
    }

    return configuration;
}

std::string SeriesId(const Configuration& configuration)
{
    if (configuration.mode != Mode::Series
        || !configuration.warmupCount.has_value()
        || !configuration.plannedSampleCount.has_value())
    {
        InvalidConfiguration("series_id requires a complete series-mode configuration");
    }

    std::uint64_t hash = 14695981039346656037ULL;
    AppendHashString(hash, ToString(configuration.backend));
    AppendHashString(hash, WorkloadId);
    AppendHashString(hash, configuration.variant);
    AppendHashInteger(hash, configuration.seed);
    AppendHashInteger(hash, configuration.elementCount);
    AppendHashInteger(hash, *configuration.warmupCount);
    AppendHashInteger(hash, *configuration.plannedSampleCount);
    AppendHashInteger(hash, configuration.validationEnabled ? 1U : 0U);
    AppendHashInteger(hash, configuration.diagnosticInstrumentation ? 1U : 0U);

    constexpr char hexadecimal[] = "0123456789abcdef";
    std::string identifier = "ex1-series-v1-";
    identifier.resize(identifier.size() + 16U);
    for (std::size_t index = 0U; index < 16U; ++index)
    {
        const unsigned int shift = static_cast<unsigned int>((15U - index) * 4U);
        identifier[identifier.size() - 16U + index] = hexadecimal[(hash >> shift) & 0xFU];
    }
    return identifier;
}

environment::EnvironmentRunContext EnvironmentContext(
    const Configuration& configuration)
{
    return {
        std::string(ExperimentId),
        configuration.runId,
        configuration.timestampUtc,
        configuration.gitCommit,
        configuration.gitDirty,
        configuration.machineId,
        configuration.validationEnabled,
        configuration.diagnosticInstrumentation};
}

bool ValidateCpuOutput(
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& output) noexcept
{
    if (input.size() != output.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        if (output[index] != computelab::TransformValue(
                input[index], static_cast<std::uint32_t>(index)))
        {
            return false;
        }
    }
    return true;
}

CpuSeriesExecution ExecuteCpuSeries(
    const Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    CpuTransformFunction transform)
{
    if (configuration.mode != Mode::Series
        || configuration.backend != Backend::Cpu
        || !configuration.warmupCount.has_value()
        || !configuration.plannedSampleCount.has_value()
        || transform == nullptr)
    {
        InvalidConfiguration("CPU execution requires a complete CPU series configuration");
    }

    for (std::uint64_t index = 0U; index < *configuration.warmupCount; ++index)
    {
        const auto output = transform(input);
        if (!ValidateCpuOutput(input, output))
        {
            throw std::runtime_error("CPU warm-up correctness validation failed");
        }
    }

    CpuSeriesExecution execution;
    execution.samples.reserve(static_cast<std::size_t>(*configuration.plannedSampleCount));
    for (std::uint64_t index = 0U; index < *configuration.plannedSampleCount; ++index)
    {
        const auto begin = timing::CaptureHostTime();
        const auto output = transform(input);
        const auto end = timing::CaptureHostTime();
        const bool valid = ValidateCpuOutput(input, output);
        execution.samples.push_back(MakeSampleRecord(
            configuration, index, valid,
            {std::nullopt, std::nullopt, std::nullopt,
                timing::ElapsedNanoseconds(begin, end), std::nullopt}));
        if (!valid)
        {
            execution.validationPassed = false;
            break;
        }
    }
    return execution;
}

results::InitializationRecord MakeInitializationSeedRecord(
    const Configuration& configuration,
    std::uint64_t sequenceIndex)
{
    if (configuration.mode != Mode::Initialization)
    {
        InvalidConfiguration("initialization seed record requires initialization mode");
    }
    return {
        SchemaVersion,
        configuration.runId,
        std::string(ExperimentId),
        std::string(ToString(configuration.backend)),
        0U,
        sequenceIndex,
        "input",
        std::string(WorkloadId),
        configuration.variant,
        configuration.elementCount,
        "seed",
        std::nullopt,
        std::to_string(configuration.seed)};
}

results::SampleRecord MakeSampleRecord(
    const Configuration& configuration,
    std::uint64_t sampleIndex,
    bool validationPassed,
    const SampleTimings& timings)
{
    if (configuration.mode != Mode::Series
        || !configuration.warmupCount.has_value()
        || !configuration.plannedSampleCount.has_value())
    {
        InvalidConfiguration("SampleRecord requires a complete series-mode configuration");
    }
    if (sampleIndex >= *configuration.plannedSampleCount)
    {
        InvalidConfiguration("sample_index must be less than planned_sample_count");
    }

    return {
        SchemaVersion,
        configuration.runId,
        std::string(ExperimentId),
        SeriesId(configuration),
        std::string(ToString(configuration.backend)),
        std::string(WorkloadId),
        configuration.variant,
        configuration.seed,
        configuration.elementCount,
        *configuration.warmupCount,
        *configuration.plannedSampleCount,
        sampleIndex,
        validationPassed,
        timings.uploadNs,
        timings.hostSubmissionNs,
        timings.deviceExecutionNs,
        timings.endToEndNs,
        timings.downloadNs};
}

results::SummaryRecord SummarizeSamples(
    std::uint32_t schemaVersion,
    std::string runId,
    std::string experimentId,
    const std::vector<results::SampleRecord>& samples)
{
    if (schemaVersion == 0U || runId.empty() || experimentId.empty())
    {
        throw std::invalid_argument("malformed summary input: invalid top-level identity");
    }

    std::map<results::SampleGroupKey,
        std::vector<const results::SampleRecord*>, SampleGroupKeyLess> groups;
    std::map<std::string, results::SampleGroupKey, std::less<>> seriesKeys;
    for (const results::SampleRecord& sample : samples)
    {
        if (sample.schemaVersion != schemaVersion
            || sample.runId != runId
            || sample.experimentId != experimentId
            || sample.seriesId.empty()
            || sample.backend.empty()
            || sample.workload.empty()
            || sample.sampleIndex >= sample.plannedSampleCount)
        {
            throw std::invalid_argument("malformed summary input: inconsistent or invalid sample identity");
        }

        const results::SampleGroupKey key = GroupKey(sample);
        const auto [series, inserted] = seriesKeys.emplace(sample.seriesId, key);
        if (!inserted && !SameGroupKey(series->second, key))
        {
            throw std::invalid_argument(
                "malformed summary input: one series_id has multiple SampleGroupKey values");
        }
        groups[key].push_back(&sample);
    }

    results::SummaryRecord summary{schemaVersion, std::move(runId),
        std::move(experimentId), {}};
    summary.sampleGroups.reserve(groups.size());
    for (auto& [key, groupSamples] : groups)
    {
        std::sort(groupSamples.begin(), groupSamples.end(),
            [](const results::SampleRecord* left, const results::SampleRecord* right) {
                return left->sampleIndex < right->sampleIndex;
            });
        for (std::size_t index = 1U; index < groupSamples.size(); ++index)
        {
            if (groupSamples[index - 1U]->sampleIndex == groupSamples[index]->sampleIndex)
            {
                throw std::invalid_argument(
                    "malformed summary input: duplicate sample_index within a series");
            }
        }

        results::SampleGroupSummary groupSummary;
        groupSummary.group = key;
        groupSummary.recordedSampleCount = static_cast<std::uint64_t>(groupSamples.size());
        groupSummary.validationFailures = static_cast<std::uint64_t>(std::count_if(
            groupSamples.begin(), groupSamples.end(),
            [](const results::SampleRecord* sample) { return !sample->validationPassed; }));
        groupSummary.uploadNs = SummarizeMetric(groupSamples,
            [](const results::SampleRecord& sample) { return sample.uploadNs; });
        groupSummary.hostSubmissionNs = SummarizeMetric(groupSamples,
            [](const results::SampleRecord& sample) { return sample.hostSubmissionNs; });
        groupSummary.deviceExecutionNs = SummarizeMetric(groupSamples,
            [](const results::SampleRecord& sample) { return sample.deviceExecutionNs; });
        groupSummary.endToEndNs = SummarizeMetric(groupSamples,
            [](const results::SampleRecord& sample) { return sample.endToEndNs; });
        groupSummary.downloadNs = SummarizeMetric(groupSamples,
            [](const results::SampleRecord& sample) { return sample.downloadNs; });
        summary.sampleGroups.push_back(std::move(groupSummary));
    }
    return summary;
}

void WriteResultPackage(
    const std::filesystem::path& localResultsRoot,
    const results::EnvironmentRecord& environment,
    const std::vector<results::InitializationRecord>& initialization,
    const std::vector<results::SampleRecord>& samples,
    const results::SummaryRecord& summary)
{
    RequireIdentifier(environment.runId, "run-id");
    if (summary.runId != environment.runId
        || summary.experimentId != environment.experimentId
        || summary.schemaVersion != environment.schemaVersion)
    {
        throw std::invalid_argument("result package top-level records disagree");
    }
    for (const auto& record : initialization)
    {
        if (record.runId != environment.runId
            || record.experimentId != environment.experimentId
            || record.schemaVersion != environment.schemaVersion)
        {
            throw std::invalid_argument("initialization record identity disagrees with package");
        }
    }
    for (const auto& record : samples)
    {
        if (record.runId != environment.runId
            || record.experimentId != environment.experimentId
            || record.schemaVersion != environment.schemaVersion)
        {
            throw std::invalid_argument("sample record identity disagrees with package");
        }
    }

    const std::string environmentJson = results::SerializeEnvironmentJson(environment);
    const std::string initializationCsv = results::SerializeInitializationCsv(initialization);
    const std::string samplesCsv = results::SerializeSamplesCsv(samples);
    const std::string summaryJson = results::SerializeSummaryJson(summary);

    std::error_code error;
    std::filesystem::create_directories(localResultsRoot, error);
    if (error)
    {
        throw std::runtime_error(
            "unable to create local results root: " + error.message());
    }

    const std::filesystem::path runDirectory = localResultsRoot / environment.runId;
    if (!std::filesystem::create_directory(runDirectory, error))
    {
        if (error)
        {
            throw std::runtime_error(
                "unable to create unique result directory: " + error.message());
        }
        throw std::runtime_error(
            "result directory already exists; refusing to overwrite: " + runDirectory.string());
    }

    try
    {
        WriteFile(runDirectory / "environment.json", environmentJson);
        WriteFile(runDirectory / "initialization.csv", initializationCsv);
        WriteFile(runDirectory / "samples.csv", samplesCsv);
        WriteFile(runDirectory / "summary.json", summaryJson);
    }
    catch (...)
    {
        std::error_code cleanupError;
        std::filesystem::remove_all(runDirectory, cleanupError);
        throw;
    }
}

} // namespace computelab::ex1
