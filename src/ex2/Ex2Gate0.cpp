#include "ex2/Ex2Gate0.hpp"

#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <system_error>

namespace computelab::ex2::gate0
{
namespace
{

constexpr std::string_view kInitializationHeader =
    "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation";
constexpr std::string_view kSamplesHeader =
    "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns";
constexpr std::string_view kWarmupHeader =
    "schema_version,run_id,series_id,process_index,sequence_index,host_submission_ns,host_wait_ns,host_completion_ns,status";

[[noreturn]] void InvalidConfiguration(std::string_view detail)
{
    throw std::invalid_argument(
        "invalid EX-2 Gate-0 configuration: " + std::string(detail));
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
            " must be an anonymous ASCII identifier, not a path or free-form value");
    }
}

std::uint64_t ParseUnsigned(std::string_view value, std::string_view field)
{
    if (value.empty() || value.front() == '+' || value.front() == '-')
    {
        InvalidConfiguration(std::string(field) +
            " must be an unsigned decimal integer");
    }
    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size())
    {
        InvalidConfiguration(std::string(field) +
            " must be an unsigned decimal integer");
    }
    return parsed;
}

QualificationPhase ParsePhase(std::string_view value)
{
    if (value == "warmup-characterization")
    {
        return QualificationPhase::WarmupCharacterization;
    }
    if (value == "sample-count-qualification")
    {
        return QualificationPhase::SampleCountQualification;
    }
    if (value == "instrumentation-control")
    {
        return QualificationPhase::InstrumentationControl;
    }
    InvalidConfiguration(
        "phase must be warmup-characterization, sample-count-qualification, or instrumentation-control");
}

Backend ParseBackend(std::string_view value)
{
    if (value == "cuda") return Backend::Cuda;
    if (value == "vulkan") return Backend::Vulkan;
    InvalidConfiguration("backend must be cuda or vulkan");
}

InstrumentMode ParseInstrumentMode(std::string_view value)
{
    if (value == "H") return InstrumentMode::H;
    if (value == "N") return InstrumentMode::N;
    InvalidConfiguration("instrument-mode must be H or N");
}

bool IsSelectedWarmupCount(std::uint64_t value) noexcept
{
    constexpr std::array values{0ULL, 1ULL, 2ULL, 4ULL, 8ULL, 16ULL};
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
void AppendInteger(std::string& output, T value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
    {
        throw std::runtime_error("integer serialization failed");
    }
    output.append(buffer.data(), end);
}

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void AppendJsonBoolean(std::string& output, bool value)
{
    output += value ? "true" : "false";
}

void AppendJsonDouble(std::string& output, double value)
{
    if (!std::isfinite(value))
    {
        throw std::invalid_argument("JSON binary64 values must be finite");
    }
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
    {
        throw std::runtime_error("binary64 serialization failed");
    }
    output.append(buffer.data(), end);
}

template <typename T, typename Append>
void AppendJsonOptional(
    std::string& output,
    const std::optional<T>& value,
    Append append)
{
    if (value.has_value()) append(*value);
    else output += "null";
}

void AppendCsvField(std::string& output, std::string_view value)
{
    if (value.find_first_of(",\"\r\n") == std::string_view::npos)
    {
        output.append(value);
        return;
    }
    output.push_back('"');
    for (const char character : value)
    {
        if (character == '"') output.push_back('"');
        output.push_back(character);
    }
    output.push_back('"');
}

template <typename T, typename Append>
void AppendCsvOptional(
    std::string& output,
    const std::optional<T>& value,
    Append append)
{
    if (value.has_value()) append(*value);
}

void AppendJsonMetric(
    std::string& output,
    const std::optional<MetricSummary>& metric)
{
    if (!metric.has_value())
    {
        output += "null";
        return;
    }
    const auto& value = *metric;
    output += "{\"sample_count\":";
    AppendJsonOptional(output, value.sampleCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"minimum\":";
    AppendJsonOptional(output, value.minimum,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"median\":";
    AppendJsonOptional(output, value.median,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"mean\":";
    AppendJsonOptional(output, value.mean,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"standard_deviation\":";
    AppendJsonOptional(output, value.standardDeviation,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"coefficient_of_variation\":";
    AppendJsonOptional(output, value.coefficientOfVariation,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"p95\":";
    AppendJsonOptional(output, value.p95,
        [&output](double item) { AppendJsonDouble(output, item); });
    output.push_back('}');
}

void AppendMetricAdmission(
    std::string& output,
    std::string_view gate0Admission,
    std::string_view evidenceScope,
    std::string_view summarySampleInclusion)
{
    output += "{\"gate0_admission\":";
    AppendJsonString(output, gate0Admission);
    output += ",\"evidence_scope\":";
    AppendJsonString(output, evidenceScope);
    output += ",\"summary_sample_inclusion\":";
    AppendJsonString(output, summarySampleInclusion);
    output.push_back('}');
}

bool SameConfiguration(
    const Configuration& left,
    const Configuration& right) noexcept
{
    return left.phase == right.phase
        && left.backend == right.backend
        && left.instrumentMode == right.instrumentMode
        && left.deviceIndex == right.deviceIndex
        && left.elementCount == right.elementCount
        && left.seed == right.seed
        && left.warmupCount == right.warmupCount
        && left.plannedSampleCount == right.plannedSampleCount
        && left.processIndex == right.processIndex
        && left.blockIndex == right.blockIndex
        && left.orderSlot == right.orderSlot
        && left.runId == right.runId
        && left.outputPackageDirectory == right.outputPackageDirectory
        && left.machineId == right.machineId
        && left.protocolVersion == right.protocolVersion;
}

bool IsLowerHex(std::string_view value, std::size_t expectedSize) noexcept
{
    return value.size() == expectedSize
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

void RequireMetricSummaryCount(
    const std::optional<MetricSummary>& metric,
    bool applicable,
    std::uint64_t expectedCount,
    std::string_view name)
{
    if (!applicable)
    {
        if (metric.has_value())
            throw std::invalid_argument(
                std::string(name) + " summary must be null when inapplicable");
        return;
    }
    if (!metric.has_value())
        throw std::invalid_argument(
            std::string(name) + " summary is missing for an applicable metric");
    const std::optional<std::uint64_t> expected = expectedCount == 0U
        ? std::nullopt : std::optional<std::uint64_t>{expectedCount};
    if (metric->sampleCount != expected)
        throw std::invalid_argument(
            std::string(name) + " summary sample_count disagrees with appended rows");
}

template <typename SelectMetric>
std::optional<MetricSummary> SummarizeMetric(
    const std::vector<const SampleRecord*>& orderedSamples,
    bool applicable,
    SelectMetric selectMetric)
{
    if (!applicable) return std::nullopt;

    std::vector<double> orderedValues;
    orderedValues.reserve(orderedSamples.size());
    for (const SampleRecord* sample : orderedSamples)
    {
        const auto metric = selectMetric(*sample);
        if (sample->status == "ok" && sample->validationPassed == true
            && metric.has_value())
        {
            orderedValues.push_back(static_cast<double>(*metric));
        }
    }
    if (orderedValues.empty()) return MetricSummary{};

    double sum = 0.0;
    for (const double value : orderedValues) sum += value;
    const double mean = sum / static_cast<double>(orderedValues.size());

    std::optional<double> standardDeviation;
    if (orderedValues.size() >= 2U)
    {
        double squaredDeviationSum = 0.0;
        for (const double value : orderedValues)
        {
            const double difference = value - mean;
            squaredDeviationSum += difference * difference;
        }
        standardDeviation = std::sqrt(
            squaredDeviationSum / static_cast<double>(orderedValues.size() - 1U));
    }

    std::vector<double> sortedValues = orderedValues;
    std::sort(sortedValues.begin(), sortedValues.end());
    const std::size_t middle = sortedValues.size() / 2U;
    const double median = sortedValues.size() % 2U == 0U
        ? (sortedValues[middle - 1U] + sortedValues[middle]) / 2.0
        : sortedValues[middle];
    const std::size_t p95OneBasedRank =
        sortedValues.size() - sortedValues.size() / 20U;

    std::optional<double> coefficientOfVariation;
    if (standardDeviation.has_value() && mean != 0.0)
    {
        coefficientOfVariation = *standardDeviation / mean;
    }
    return MetricSummary{
        static_cast<std::uint64_t>(orderedValues.size()),
        sortedValues.front(), median, mean, standardDeviation,
        coefficientOfVariation, sortedValues[p95OneBasedRank - 1U]};
}

void WriteFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream)
    {
        throw std::runtime_error("unable to create qualification evidence file");
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.close();
    if (!stream)
    {
        throw std::runtime_error("unable to write qualification evidence file");
    }
}

std::filesystem::path NormalizedAbsolute(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) InvalidConfiguration("output-package-directory is invalid");
    return absolute.lexically_normal();
}

void AppendCommonEnvironment(
    std::string& output,
    const results::EnvironmentRecord& record)
{
    output += "\"schema_version\":2,\"experiment_id\":";
    AppendJsonString(output, record.experimentId);
    output += ",\"run_id\":"; AppendJsonString(output, record.runId);
    output += ",\"timestamp_utc\":"; AppendJsonString(output, record.timestampUtc);
    output += ",\"git_commit\":"; AppendJsonString(output, record.gitCommit);
    output += ",\"git_dirty\":"; AppendJsonBoolean(output, record.gitDirty);
    output += ",\"machine_id\":"; AppendJsonString(output, record.machineId);
    output += ",\"os_name\":"; AppendJsonString(output, record.osName);
    output += ",\"os_version\":"; AppendJsonString(output, record.osVersion);
    output += ",\"cpu_name\":"; AppendJsonString(output, record.cpuName);
    output += ",\"system_memory_bytes\":"; AppendInteger(output, record.systemMemoryBytes);
#define APPEND_OPTIONAL_STRING(name, member) \
    output += ",\"" name "\":"; AppendJsonOptional(output, record.member, [&output](const std::string& item){ AppendJsonString(output, item); })
    APPEND_OPTIONAL_STRING("gpu_name", gpuName);
    APPEND_OPTIONAL_STRING("gpu_vendor", gpuVendor);
    APPEND_OPTIONAL_STRING("gpu_device_id", gpuDeviceId);
    output += ",\"gpu_memory_bytes\":";
    AppendJsonOptional(output, record.gpuMemoryBytes,
        [&output](std::uint64_t item){ AppendInteger(output, item); });
    APPEND_OPTIONAL_STRING("nvidia_driver_version", nvidiaDriverVersion);
    APPEND_OPTIONAL_STRING("cuda_toolkit_version", cudaToolkitVersion);
    APPEND_OPTIONAL_STRING("cuda_runtime_version", cudaRuntimeVersion);
    APPEND_OPTIONAL_STRING("cuda_compute_capability", cudaComputeCapability);
    APPEND_OPTIONAL_STRING("vulkan_sdk_version", vulkanSdkVersion);
    APPEND_OPTIONAL_STRING("vulkan_device_api_version", vulkanDeviceApiVersion);
#undef APPEND_OPTIONAL_STRING
    output += ",\"compiler_name\":"; AppendJsonString(output, record.compilerName);
    output += ",\"compiler_version\":"; AppendJsonString(output, record.compilerVersion);
    output += ",\"cmake_version\":"; AppendJsonString(output, record.cmakeVersion);
    output += ",\"ninja_version\":"; AppendJsonString(output, record.ninjaVersion);
    output += ",\"configure_preset\":"; AppendJsonString(output, record.configurePreset);
    output += ",\"build_type\":"; AppendJsonString(output, record.buildType);
    output += ",\"validation_enabled\":"; AppendJsonBoolean(output, record.validationEnabled);
    output += ",\"diagnostic_instrumentation\":";
    AppendJsonBoolean(output, record.diagnosticInstrumentation);
}

void AppendDiagnostics(std::string& output, const BackendDiagnostics& value)
{
    output += "{\"implementation\":"; AppendJsonString(output, value.implementation);
    output += ",\"stream_flags\":";
    AppendJsonOptional(output, value.streamFlags,
        [&output](const std::string& item){ AppendJsonString(output, item); });
#define APPEND_OPTIONAL_INTEGER(name, member) \
    output += ",\"" name "\":"; AppendJsonOptional(output, value.member, [&output](std::uint64_t item){ AppendInteger(output, item); })
    APPEND_OPTIONAL_INTEGER("queue_family_index", queueFamilyIndex);
    APPEND_OPTIONAL_INTEGER("queue_flags", queueFlags);
    APPEND_OPTIONAL_INTEGER("queue_count", queueCount);
    APPEND_OPTIONAL_INTEGER("timestamp_valid_bits", timestampValidBits);
    output += ",\"timestamp_period_ns\":";
    AppendJsonOptional(output, value.timestampPeriodNanoseconds,
        [&output](double item){ AppendJsonDouble(output, item); });
    APPEND_OPTIONAL_INTEGER("input_memory_flags", inputMemoryFlags);
    APPEND_OPTIONAL_INTEGER("output_memory_flags", outputMemoryFlags);
    APPEND_OPTIONAL_INTEGER("upload_memory_flags", uploadMemoryFlags);
    APPEND_OPTIONAL_INTEGER("readback_memory_flags", readbackMemoryFlags);
    output += ",\"native_markers_enabled\":";
    AppendJsonBoolean(output, value.nativeMarkersEnabled);
    output += ",\"native_timing_method\":";
    AppendJsonOptional(output, value.nativeTimingMethod,
        [&output](const std::string& item){ AppendJsonString(output, item); });
    APPEND_OPTIONAL_INTEGER("native_timing_resolution_ns", nativeTimingResolutionNanoseconds);
    APPEND_OPTIONAL_INTEGER("native_timing_start_stage", nativeTimingStartStage);
    APPEND_OPTIONAL_INTEGER("native_timing_stop_stage", nativeTimingStopStage);
    APPEND_OPTIONAL_INTEGER("native_duration_envelope_ns", nativeDurationEnvelopeNanoseconds);
#undef APPEND_OPTIONAL_INTEGER
    output.push_back('}');
}

} // namespace

std::string_view ToString(QualificationPhase value) noexcept
{
    switch (value)
    {
    case QualificationPhase::WarmupCharacterization: return "warmup-characterization";
    case QualificationPhase::SampleCountQualification: return "sample-count-qualification";
    case QualificationPhase::InstrumentationControl: return "instrumentation-control";
    }
    return "unknown";
}

std::string_view ToString(Backend value) noexcept
{
    switch (value)
    {
    case Backend::Cuda: return "cuda";
    case Backend::Vulkan: return "vulkan";
    }
    return "unknown";
}

std::string_view ToString(InstrumentMode value) noexcept
{
    return value == InstrumentMode::H ? "H" : "N";
}

Configuration ParseArguments(std::span<const std::string_view> arguments)
{
    constexpr std::array<std::string_view, 14> allowed{
        "phase", "backend", "instrument-mode", "device-index",
        "element-count", "seed", "warmup-count", "planned-sample-count",
        "process-index", "block-index", "order-slot", "run-id",
        "output-package-directory", "machine-id"};
    constexpr std::string_view protocolOption = "protocol-version";

    std::map<std::string, std::string, std::less<>> options;
    for (std::size_t index = 0; index < arguments.size(); index += 2U)
    {
        if (index + 1U >= arguments.size() || !arguments[index].starts_with("--"))
        {
            InvalidConfiguration("options must be supplied as --name value pairs");
        }
        const std::string_view name = arguments[index].substr(2U);
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()
            && name != protocolOption)
        {
            InvalidConfiguration("unknown option --" + std::string(name));
        }
        if (!options.emplace(std::string(name), std::string(arguments[index + 1U])).second)
        {
            InvalidConfiguration("duplicate option --" + std::string(name));
        }
    }
    const auto require = [&options](std::string_view name) -> const std::string& {
        const auto found = options.find(name);
        if (found == options.end())
        {
            InvalidConfiguration("missing required option --" + std::string(name));
        }
        return found->second;
    };

    Configuration result;
    result.phase = ParsePhase(require("phase"));
    result.backend = ParseBackend(require("backend"));
    result.instrumentMode = ParseInstrumentMode(require("instrument-mode"));
    const auto deviceIndex = ParseUnsigned(require("device-index"), "device-index");
    if (deviceIndex > std::numeric_limits<std::uint32_t>::max())
        InvalidConfiguration("device-index exceeds the backend-native index range");
    result.deviceIndex = static_cast<std::uint32_t>(deviceIndex);
    result.elementCount = ParseUnsigned(require("element-count"), "element-count");
    result.seed = ParseUnsigned(require("seed"), "seed");
    result.warmupCount = ParseUnsigned(require("warmup-count"), "warmup-count");
    result.plannedSampleCount = ParseUnsigned(
        require("planned-sample-count"), "planned-sample-count");
    result.processIndex = ParseUnsigned(require("process-index"), "process-index");
    result.blockIndex = ParseUnsigned(require("block-index"), "block-index");
    result.orderSlot = ParseUnsigned(require("order-slot"), "order-slot");
    result.runId = require("run-id");
    result.outputPackageDirectory = require("output-package-directory");
    result.machineId = require("machine-id");
    result.protocolVersion = require(protocolOption);

    RequireIdentifier(result.runId, "run-id");
    RequireIdentifier(result.machineId, "machine-id");
    if (result.protocolVersion != "1.0")
        InvalidConfiguration("protocol-version must be exactly 1.0");
    if (result.elementCount == 0U
        || result.elementCount > std::numeric_limits<std::uint32_t>::max())
        InvalidConfiguration("element-count must be in [1, 4294967295]");
    if (result.backend == Backend::Cuda
        && result.deviceIndex > static_cast<std::uint32_t>(
            std::numeric_limits<int>::max()))
        InvalidConfiguration("CUDA device-index exceeds the backend-native int range");
    if (result.processIndex > 4U || result.blockIndex > 4U)
        InvalidConfiguration("process-index and block-index must be in [0, 4]");
    if (result.orderSlot > 1U)
        InvalidConfiguration("order-slot must be 0 or 1");

    if (result.phase == QualificationPhase::WarmupCharacterization)
    {
        if (result.warmupCount != 0U || result.plannedSampleCount != 0U)
            InvalidConfiguration(
                "warmup-characterization requires warmup-count 0 and planned-sample-count 0");
    }
    else
    {
        if (!IsSelectedWarmupCount(result.warmupCount))
            InvalidConfiguration("warmup-count is not a selectable Gate-0 W value");
        if (result.plannedSampleCount == 0U
            || result.plannedSampleCount > MaximumQualificationSampleCount)
            InvalidConfiguration("planned-sample-count must be in [1, 200]");
    }
    return result;
}

void ValidateOutputPackageDirectory(
    const Configuration& configuration,
    const std::filesystem::path& localResultsRoot)
{
    const auto root = NormalizedAbsolute(localResultsRoot);
    const auto package = NormalizedAbsolute(configuration.outputPackageDirectory);
    if (package.parent_path() != root || package.filename() != configuration.runId)
    {
        InvalidConfiguration(
            "output-package-directory must be results/local/<run-id> and its leaf must equal run-id");
    }
}

std::string CanonicalJson(std::vector<CanonicalField> fields)
{
    for (const auto& [key, unused] : fields)
    {
        static_cast<void>(unused);
        if (key.empty() || !std::all_of(key.begin(), key.end(), [](char character) {
                return (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9')
                    || character == '_';
            }))
        {
            throw std::invalid_argument(
                "canonical identity keys must use lowercase ASCII names");
        }
    }
    std::sort(fields.begin(), fields.end(),
        [](const CanonicalField& left, const CanonicalField& right) {
            return left.first < right.first;
        });
    for (std::size_t index = 1; index < fields.size(); ++index)
    {
        if (fields[index - 1U].first == fields[index].first)
            throw std::invalid_argument("canonical identity contains a duplicate key");
    }

    std::string output{"{"};
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        if (index != 0U) output.push_back(',');
        AppendJsonString(output, fields[index].first);
        output.push_back(':');
        std::visit([&output](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
            else if constexpr (std::is_same_v<T, std::string>) AppendJsonString(output, value);
            else AppendInteger(output, value);
        }, fields[index].second);
    }
    output.push_back('}');
    return output;
}

std::string Sha256(std::span<const std::byte> bytes)
{
    return computelab::ex2::Sha256(bytes);
}

std::string Sha256(std::string_view bytes)
{
    return computelab::ex2::Sha256(bytes);
}

std::string Sha256File(const std::filesystem::path& path)
{
    return computelab::ex2::Sha256File(path);
}

std::string ComparisonConditionCanonicalJson(const IdentityContext& context)
{
    return CanonicalJson({
        {"byte_count", nullptr},
        {"counter_count", nullptr},
        {"element_count", context.elementCount},
        {"execution_mode", std::string(ExecutionMode)},
        {"generator_revision", std::string(GeneratorRevision)},
        {"gpu_uuid_identity", context.gpuUuidIdentity},
        {"index_pattern", nullptr},
        {"instrument_mode", std::string(ToString(context.instrumentMode))},
        {"iteration_count", nullptr},
        {"machine_id", context.machineId},
        {"operation_boundary", std::string(OperationBoundary)},
        {"protocol_version", context.protocolVersion},
        {"seed", context.seed},
        {"transfer_direction", nullptr},
        {"variant", std::string(VariantId)},
        {"workload", std::string(WorkloadId)}});
}

std::string ComparisonConditionId(const IdentityContext& context)
{
    return Sha256(ComparisonConditionCanonicalJson(context));
}

std::string SeriesCanonicalJson(const SeriesIdentityContext& context)
{
    return CanonicalJson({
        {"backend", std::string(ToString(context.backend))},
        {"block_index", context.blockIndex},
        {"byte_count", nullptr},
        {"counter_count", nullptr},
        {"element_count", context.condition.elementCount},
        {"execution_mode", std::string(ExecutionMode)},
        {"executable_sha256", context.executableSha256},
        {"generator_revision", std::string(GeneratorRevision)},
        {"gpu_uuid_identity", context.condition.gpuUuidIdentity},
        {"index_pattern", nullptr},
        {"instrument_mode", std::string(ToString(context.condition.instrumentMode))},
        {"iteration_count", nullptr},
        {"machine_id", context.condition.machineId},
        {"operation_boundary", std::string(OperationBoundary)},
        {"order_slot", context.orderSlot},
        {"planned_sample_count", context.plannedSampleCount},
        {"process_index", context.processIndex},
        {"protocol_version", context.condition.protocolVersion},
        {"seed", context.condition.seed},
        {"shader_sha256", context.shaderSha256.has_value()
            ? CanonicalValue{*context.shaderSha256} : CanonicalValue{nullptr}},
        {"source_revision", context.sourceRevision},
        {"transfer_direction", nullptr},
        {"variant", std::string(VariantId)},
        {"workload", std::string(WorkloadId)},
        {"warmup_count", context.warmupCount}});
}

std::string SeriesId(const SeriesIdentityContext& context)
{
    return Sha256(SeriesCanonicalJson(context));
}

std::string InitializationCsvHeader() { return std::string(kInitializationHeader); }
std::string SamplesCsvHeader() { return std::string(kSamplesHeader); }
std::string WarmupCsvHeader() { return std::string(kWarmupHeader); }

std::string SerializeInitializationRow(const InitializationRecord& record)
{
    if (!record.durationNanoseconds.has_value()
        && (!record.observation.has_value() || record.observation->empty()))
        throw std::invalid_argument("initialization row needs duration or observation");
    std::string output;
    AppendInteger(output, SchemaVersion); output.push_back(',');
    AppendCsvField(output, record.runId); output.push_back(',');
    AppendCsvField(output, ExperimentId); output.push_back(',');
    AppendCsvField(output, ToString(record.backend)); output.push_back(',');
    AppendInteger(output, record.processIndex); output.push_back(',');
    AppendInteger(output, record.sequenceIndex); output.push_back(',');
    AppendCsvField(output, record.category); output.push_back(',');
    AppendCsvOptional(output, record.workload,
        [&output](const std::string& value){ AppendCsvField(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.variant,
        [&output](const std::string& value){ AppendCsvField(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.elementCount,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvField(output, record.metric); output.push_back(',');
    AppendCsvOptional(output, record.durationNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.observation,
        [&output](const std::string& value){ AppendCsvField(output, value); });
    output += "\r\n";
    return output;
}

std::string SerializeSampleRow(const SampleRecord& record)
{
    std::string output;
    AppendInteger(output, SchemaVersion); output.push_back(',');
    AppendCsvField(output, record.runId); output.push_back(',');
    AppendCsvField(output, ExperimentId); output.push_back(',');
    AppendCsvField(output, record.comparisonConditionId); output.push_back(',');
    AppendCsvField(output, record.seriesId); output.push_back(',');
    AppendCsvField(output, ToString(record.backend)); output.push_back(',');
    AppendCsvField(output, WorkloadId); output.push_back(',');
    AppendCsvField(output, VariantId); output.push_back(',');
    AppendInteger(output, record.seed); output.push_back(',');
    AppendInteger(output, record.elementCount); output.push_back(',');
    output.push_back(','); // byte_count
    output.push_back(','); // index_pattern
    output.push_back(','); // counter_count
    output.push_back(','); // iteration_count
    output.push_back(','); // transfer_direction
    AppendCsvField(output, ExecutionMode); output.push_back(',');
    AppendCsvField(output, ToString(record.instrumentMode)); output.push_back(',');
    AppendInteger(output, record.warmupCount); output.push_back(',');
    AppendInteger(output, record.plannedSampleCount); output.push_back(',');
    AppendInteger(output, record.blockIndex); output.push_back(',');
    AppendInteger(output, record.orderSlot); output.push_back(',');
    AppendInteger(output, record.processIndex); output.push_back(',');
    AppendInteger(output, record.sampleIndex); output.push_back(',');
    AppendCsvOptional(output, record.validationPassed,
        [&output](bool value){ AppendJsonBoolean(output, value); }); output.push_back(',');
    AppendCsvField(output, record.status); output.push_back(',');
    AppendCsvOptional(output, record.failurePhase,
        [&output](const std::string& value){ AppendCsvField(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.errorCode,
        [&output](const std::string& value){ AppendCsvField(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.hostSubmissionNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.hostWaitNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.hostCompletionNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.nativeDeviceIntervalNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); });
    output += "\r\n";
    return output;
}

std::string SerializeWarmupRow(const WarmupRecord& record)
{
    std::string output;
    AppendInteger(output, SchemaVersion); output.push_back(',');
    AppendCsvField(output, record.runId); output.push_back(',');
    AppendCsvField(output, record.seriesId); output.push_back(',');
    AppendInteger(output, record.processIndex); output.push_back(',');
    AppendInteger(output, record.sequenceIndex); output.push_back(',');
    AppendCsvOptional(output, record.hostSubmissionNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.hostWaitNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvOptional(output, record.hostCompletionNanoseconds,
        [&output](std::uint64_t value){ AppendInteger(output, value); }); output.push_back(',');
    AppendCsvField(output, record.status);
    output += "\r\n";
    return output;
}

std::string SerializeEnvironmentJson(const EnvironmentRecord& record)
{
    if (record.common.schemaVersion != 1U && record.common.schemaVersion != SchemaVersion)
        throw std::invalid_argument("common environment metadata has an unsupported schema");
    std::string output{"{"};
    AppendCommonEnvironment(output, record.common);
    output += ",\"protocol_version\":"; AppendJsonString(output, record.configuration.protocolVersion);
    output += ",\"evidence_kind\":\"qualification\",\"qualification_phase\":";
    AppendJsonString(output, ToString(record.configuration.phase));
    output += ",\"backend\":"; AppendJsonString(output, ToString(record.configuration.backend));
    output += ",\"instrument_mode\":"; AppendJsonString(output, ToString(record.configuration.instrumentMode));
    output += ",\"device_index\":"; AppendInteger(output, record.configuration.deviceIndex);
    output += ",\"warmup_count\":"; AppendInteger(output, record.configuration.warmupCount);
    output += ",\"planned_sample_count\":";
    AppendInteger(output, record.configuration.plannedSampleCount);
    output += ",\"comparison_condition_id\":"; AppendJsonString(output, record.comparisonConditionId);
    output += ",\"series_id\":"; AppendJsonString(output, record.seriesId);
    output += ",\"block_index\":"; AppendInteger(output, record.configuration.blockIndex);
    output += ",\"process_index\":"; AppendInteger(output, record.configuration.processIndex);
    output += ",\"order_slot\":"; AppendInteger(output, record.configuration.orderSlot);
    output += ",\"source_revision\":"; AppendJsonString(output, record.sourceRevision);
    output += ",\"executable_sha256\":"; AppendJsonString(output, record.executableSha256);
    output += ",\"shader_sha256\":";
    AppendJsonOptional(output, record.shaderSha256,
        [&output](const std::string& value){ AppendJsonString(output, value); });
    output += ",\"input_sha256\":"; AppendJsonString(output, record.inputSha256);
    output += ",\"expected_output_sha256\":"; AppendJsonString(output, record.expectedOutputSha256);
    output += ",\"gpu_uuid_identity\":"; AppendJsonString(output, record.gpuUuidIdentity);
    output += ",\"workload\":"; AppendJsonString(output, WorkloadId);
    output += ",\"variant\":"; AppendJsonString(output, VariantId);
    output += ",\"generator_revision\":"; AppendJsonString(output, GeneratorRevision);
    output += ",\"element_count\":"; AppendInteger(output, record.configuration.elementCount);
    output += ",\"seed\":"; AppendInteger(output, record.configuration.seed);
    output += ",\"execution_mode\":"; AppendJsonString(output, ExecutionMode);
    output += ",\"operation_boundary\":"; AppendJsonString(output, OperationBoundary);
    output += ",\"backend_native\":"; AppendDiagnostics(output, record.backendDiagnostics);
    output += "}\n";
    return output;
}

std::string SerializeSummaryJson(const SummaryRecord& record)
{
    std::string output{"{\"schema_version\":2,\"run_id\":"};
    AppendJsonString(output, record.configuration.runId);
    output += ",\"experiment_id\":\"EX-2\",\"evidence_kind\":\"qualification\",\"process_status\":";
    AppendJsonString(output, record.processStatus);
    output += ",\"failure_phase\":";
    AppendJsonOptional(output, record.failurePhase,
        [&output](const std::string& value){ AppendJsonString(output, value); });
    output += ",\"error_code\":";
    AppendJsonOptional(output, record.errorCode,
        [&output](const std::string& value){ AppendJsonString(output, value); });
    output += ",\"sample_groups\":[{\"group\":{\"comparison_condition_id\":";
    AppendJsonString(output, record.comparisonConditionId);
    output += ",\"series_id\":"; AppendJsonString(output, record.seriesId);
    output += ",\"run_id\":"; AppendJsonString(output, record.configuration.runId);
    output += ",\"qualification_phase\":"; AppendJsonString(output, ToString(record.configuration.phase));
    output += ",\"backend\":"; AppendJsonString(output, ToString(record.configuration.backend));
    output += ",\"workload\":"; AppendJsonString(output, WorkloadId);
    output += ",\"variant\":"; AppendJsonString(output, VariantId);
    output += ",\"seed\":"; AppendInteger(output, record.configuration.seed);
    output += ",\"element_count\":"; AppendInteger(output, record.configuration.elementCount);
    output += ",\"byte_count\":null,\"index_pattern\":null,\"counter_count\":null,\"iteration_count\":null,\"transfer_direction\":null,\"execution_mode\":";
    AppendJsonString(output, ExecutionMode);
    output += ",\"instrument_mode\":"; AppendJsonString(output, ToString(record.configuration.instrumentMode));
    output += ",\"warmup_count\":"; AppendInteger(output, record.configuration.warmupCount);
    output += ",\"planned_sample_count\":"; AppendInteger(output, record.configuration.plannedSampleCount);
    output += ",\"block_index\":"; AppendInteger(output, record.configuration.blockIndex);
    output += ",\"order_slot\":"; AppendInteger(output, record.configuration.orderSlot);
    output += ",\"process_index\":"; AppendInteger(output, record.configuration.processIndex);
    output += "},\"recorded_sample_count\":"; AppendInteger(output, record.recordedSampleCount);
    output += ",\"validation_failures\":"; AppendInteger(output, record.validationFailures);
    output += ",\"failed_sample_count\":"; AppendInteger(output, record.failedSampleCount);
    const bool steadyState = record.configuration.phase != QualificationPhase::WarmupCharacterization;
    output += ",\"metric_admission\":{\"host_submission_ns\":";
    if (steadyState)
        AppendMetricAdmission(output, "pending_gate0_review",
            "qualification_only_host_observation",
            "completed_correctness_passing_ok_rows");
    else
        AppendMetricAdmission(output, "not_applicable",
            "warmup_artifact_only", "inapplicable_no_steady_state_samples");
    output += ",\"host_wait_ns\":";
    if (steadyState)
        AppendMetricAdmission(output, "pending_gate0_review",
            "qualification_only_host_observation",
            "completed_correctness_passing_ok_rows");
    else
        AppendMetricAdmission(output, "not_applicable",
            "warmup_artifact_only", "inapplicable_no_steady_state_samples");
    output += ",\"host_completion_ns\":";
    if (steadyState)
        AppendMetricAdmission(output, "pending_gate0_review",
            "qualification_only_host_observation",
            "completed_correctness_passing_ok_rows");
    else
        AppendMetricAdmission(output, "not_applicable",
            "warmup_artifact_only", "inapplicable_no_steady_state_samples");
    output += ",\"native_device_interval_ns\":";
    if (!steadyState)
        AppendMetricAdmission(output, "not_applicable",
            "warmup_artifact_only", "inapplicable_no_steady_state_samples");
    else if (record.configuration.instrumentMode == InstrumentMode::H)
        AppendMetricAdmission(output, "not_applicable",
            "not_recorded_in_instrument_mode_h", "inapplicable_instrument_mode_h");
    else
        AppendMetricAdmission(output, "excluded_cross_api_v1",
            "backend_native_explanatory_within_backend_only",
            "completed_correctness_passing_ok_rows");
    output += "},\"metrics\":{\"host_submission_ns\":";
    AppendJsonMetric(output, record.hostSubmissionNanoseconds);
    output += ",\"host_wait_ns\":"; AppendJsonMetric(output, record.hostWaitNanoseconds);
    output += ",\"host_completion_ns\":"; AppendJsonMetric(output, record.hostCompletionNanoseconds);
    output += ",\"native_device_interval_ns\":"; AppendJsonMetric(output, record.nativeDeviceIntervalNanoseconds);
    output += "}}]}\n";
    return output;
}

SummaryRecord SummarizeSamples(
    const Configuration& configuration,
    std::string comparisonConditionId,
    std::string seriesId,
    const std::vector<SampleRecord>& samples,
    std::string processStatus,
    std::optional<std::string> failurePhase,
    std::optional<std::string> errorCode)
{
    std::vector<const SampleRecord*> ordered;
    ordered.reserve(samples.size());
    for (const auto& sample : samples)
    {
        if (sample.runId != configuration.runId
            || sample.comparisonConditionId != comparisonConditionId
            || sample.seriesId != seriesId)
            throw std::invalid_argument("sample identity disagrees with summary identity");
        ordered.push_back(&sample);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const SampleRecord* left, const SampleRecord* right) {
            return left->sampleIndex < right->sampleIndex;
        });
    for (std::size_t index = 0; index < ordered.size(); ++index)
    {
        if (ordered[index]->sampleIndex != index)
            throw std::invalid_argument("sample sequence is not contiguous and zero-based");
    }

    const bool applicable = configuration.phase
        != QualificationPhase::WarmupCharacterization;
    SummaryRecord summary;
    summary.configuration = configuration;
    summary.comparisonConditionId = std::move(comparisonConditionId);
    summary.seriesId = std::move(seriesId);
    summary.processStatus = std::move(processStatus);
    summary.failurePhase = std::move(failurePhase);
    summary.errorCode = std::move(errorCode);
    summary.recordedSampleCount = static_cast<std::uint64_t>(samples.size());
    summary.validationFailures = static_cast<std::uint64_t>(std::count_if(
        samples.begin(), samples.end(), [](const SampleRecord& sample) {
            return sample.validationPassed == false;
        }));
    summary.failedSampleCount = static_cast<std::uint64_t>(std::count_if(
        samples.begin(), samples.end(), [](const SampleRecord& sample) {
            return sample.status != "ok";
        }));
    summary.hostSubmissionNanoseconds = SummarizeMetric(
        ordered, applicable, [](const SampleRecord& sample) {
            return sample.hostSubmissionNanoseconds;
        });
    summary.hostWaitNanoseconds = SummarizeMetric(
        ordered, applicable, [](const SampleRecord& sample) {
            return sample.hostWaitNanoseconds;
        });
    summary.hostCompletionNanoseconds = SummarizeMetric(
        ordered, applicable, [](const SampleRecord& sample) {
            return sample.hostCompletionNanoseconds;
        });
    summary.nativeDeviceIntervalNanoseconds = SummarizeMetric(
        ordered, applicable && configuration.instrumentMode == InstrumentMode::N,
        [](const SampleRecord& sample) {
            return sample.nativeDeviceIntervalNanoseconds;
        });
    return summary;
}

QualificationPackage::QualificationPackage(Configuration configuration)
    : configuration_{std::move(configuration)},
      stagingDirectory_{configuration_.outputPackageDirectory.string() + ".incomplete"}
{
    std::error_code error;
    if (std::filesystem::exists(configuration_.outputPackageDirectory, error)
        || std::filesystem::exists(stagingDirectory_, error))
        throw std::invalid_argument("qualification package path collision; refusing to overwrite");
    std::filesystem::create_directories(
        configuration_.outputPackageDirectory.parent_path(), error);
    if (error) throw std::runtime_error("unable to create local qualification root");
    if (!std::filesystem::create_directory(stagingDirectory_, error))
        throw std::runtime_error("unable to create qualification staging directory");

    samplesStream_.open(stagingDirectory_ / "samples.csv",
        std::ios::binary | std::ios::out | std::ios::trunc);
    if (!samplesStream_) throw std::runtime_error("unable to create staged samples.csv");
    samplesStream_ << kSamplesHeader << "\r\n";
    samplesStream_.flush();
    if (!samplesStream_) throw std::runtime_error("unable to flush staged samples.csv header");

    if (configuration_.phase == QualificationPhase::WarmupCharacterization)
    {
        warmupStream_.open(stagingDirectory_ / "warmup.csv",
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!warmupStream_) throw std::runtime_error("unable to create staged warmup.csv");
        warmupStream_ << kWarmupHeader << "\r\n";
        warmupStream_.flush();
        if (!warmupStream_) throw std::runtime_error("unable to flush staged warmup.csv header");
    }
}

QualificationPackage::~QualificationPackage() noexcept = default;

void QualificationPackage::WriteInitialization(
    const std::vector<InitializationRecord>& records)
{
    std::string contents{kInitializationHeader};
    contents += "\r\n";
    for (const auto& record : records)
    {
        if (record.runId != configuration_.runId
            || record.backend != configuration_.backend
            || record.processIndex != configuration_.processIndex)
            throw std::invalid_argument("initialization identity disagrees with package");
        contents += SerializeInitializationRow(record);
    }
    WriteFile(stagingDirectory_ / "initialization.csv", contents);
    initializationWritten_ = true;
}

void QualificationPackage::AppendSample(const SampleRecord& record)
{
    if (configuration_.phase == QualificationPhase::WarmupCharacterization)
        throw std::logic_error("warmup-characterization cannot append steady-state samples");
    if (record.runId != configuration_.runId
        || record.backend != configuration_.backend
        || record.instrumentMode != configuration_.instrumentMode
        || record.seed != configuration_.seed
        || record.elementCount != configuration_.elementCount
        || record.warmupCount != configuration_.warmupCount
        || record.plannedSampleCount != configuration_.plannedSampleCount
        || record.blockIndex != configuration_.blockIndex
        || record.orderSlot != configuration_.orderSlot
        || record.processIndex != configuration_.processIndex
        || record.sampleIndex != sampleRows_)
        throw std::invalid_argument("sample identity or ordering disagrees with package");
    if (record.comparisonConditionId.empty() || record.seriesId.empty()
        || (sampleComparisonConditionId_.has_value()
            && *sampleComparisonConditionId_ != record.comparisonConditionId)
        || (sampleSeriesId_.has_value() && *sampleSeriesId_ != record.seriesId))
        throw std::invalid_argument("sample IDs disagree with earlier appended rows");
    samplesStream_ << SerializeSampleRow(record);
    samplesStream_.flush();
    if (!samplesStream_) throw std::runtime_error("unable to flush staged sample row");
    if (!sampleComparisonConditionId_.has_value())
        sampleComparisonConditionId_ = record.comparisonConditionId;
    if (!sampleSeriesId_.has_value()) sampleSeriesId_ = record.seriesId;
    if (record.validationPassed == false) ++validationFailures_;
    if (record.status != "ok") ++failedSampleRows_;
    if (record.status == "ok" && record.validationPassed == true)
    {
        if (record.hostSubmissionNanoseconds.has_value())
            ++hostSubmissionSummaryRows_;
        if (record.hostWaitNanoseconds.has_value()) ++hostWaitSummaryRows_;
        if (record.hostCompletionNanoseconds.has_value())
            ++hostCompletionSummaryRows_;
        if (record.nativeDeviceIntervalNanoseconds.has_value())
            ++nativeDeviceSummaryRows_;
    }
    ++sampleRows_;
}

void QualificationPackage::AppendWarmup(const WarmupRecord& record)
{
    if (!warmupStream_.is_open())
        throw std::logic_error("warmup rows apply only to warmup-characterization");
    if (record.runId != configuration_.runId
        || record.processIndex != configuration_.processIndex
        || record.sequenceIndex != warmupRows_
        || record.seriesId.empty()
        || (warmupSeriesId_.has_value() && *warmupSeriesId_ != record.seriesId))
        throw std::invalid_argument("warmup identity or ordering disagrees with package");
    warmupStream_ << SerializeWarmupRow(record);
    warmupStream_.flush();
    if (!warmupStream_) throw std::runtime_error("unable to flush staged warmup row");
    if (!warmupSeriesId_.has_value()) warmupSeriesId_ = record.seriesId;
    ++warmupRows_;
}

void QualificationPackage::Complete(
    const EnvironmentRecord& environment,
    const SummaryRecord& summary)
{
    if (completed_) throw std::logic_error("qualification package is already complete");
    if (!SameConfiguration(environment.configuration, configuration_)
        || !SameConfiguration(summary.configuration, configuration_))
        throw std::invalid_argument(
            "environment or summary configuration disagrees with package");
    if (environment.common.schemaVersion != SchemaVersion
        || environment.common.experimentId != ExperimentId
        || environment.common.runId != configuration_.runId
        || environment.common.machineId != configuration_.machineId
        || environment.common.gitCommit != environment.sourceRevision
        || environment.backendDiagnostics.nativeMarkersEnabled
            != (configuration_.instrumentMode == InstrumentMode::N))
        throw std::invalid_argument(
            "environment provenance or timing mode disagrees with package");
    if (!IsLowerHex(environment.sourceRevision, 40U)
        || !IsLowerHex(environment.executableSha256, 64U)
        || !IsLowerHex(environment.inputSha256, 64U)
        || !IsLowerHex(environment.expectedOutputSha256, 64U)
        || (configuration_.backend == Backend::Vulkan
            ? !environment.shaderSha256.has_value()
                || !IsLowerHex(*environment.shaderSha256, 64U)
            : environment.shaderSha256.has_value()))
        throw std::invalid_argument("environment provenance digests are inconsistent");

    const IdentityContext condition{
        configuration_.protocolVersion,
        configuration_.machineId,
        environment.gpuUuidIdentity,
        configuration_.elementCount,
        configuration_.seed,
        configuration_.instrumentMode};
    const std::string expectedComparisonConditionId =
        ComparisonConditionId(condition);
    const std::string expectedSeriesId = SeriesId({
        condition,
        configuration_.backend,
        configuration_.processIndex,
        configuration_.blockIndex,
        configuration_.orderSlot,
        configuration_.warmupCount,
        configuration_.plannedSampleCount,
        environment.sourceRevision,
        environment.executableSha256,
        environment.shaderSha256});
    if (environment.comparisonConditionId != expectedComparisonConditionId
        || environment.seriesId != expectedSeriesId
        || environment.comparisonConditionId != summary.comparisonConditionId
        || environment.seriesId != summary.seriesId)
        throw std::invalid_argument("qualification package top-level identities disagree");
    if ((sampleComparisonConditionId_.has_value()
            && *sampleComparisonConditionId_ != environment.comparisonConditionId)
        || (sampleSeriesId_.has_value()
            && *sampleSeriesId_ != environment.seriesId)
        || (warmupSeriesId_.has_value()
            && *warmupSeriesId_ != environment.seriesId))
        throw std::invalid_argument("appended row IDs disagree with package identities");
    if (summary.recordedSampleCount != sampleRows_
        || summary.validationFailures != validationFailures_
        || summary.failedSampleCount != failedSampleRows_)
        throw std::invalid_argument("summary row counts disagree with appended samples");

    const bool steadyState = configuration_.phase
        != QualificationPhase::WarmupCharacterization;
    RequireMetricSummaryCount(summary.hostSubmissionNanoseconds,
        steadyState, hostSubmissionSummaryRows_, "host_submission_ns");
    RequireMetricSummaryCount(summary.hostWaitNanoseconds,
        steadyState, hostWaitSummaryRows_, "host_wait_ns");
    RequireMetricSummaryCount(summary.hostCompletionNanoseconds,
        steadyState, hostCompletionSummaryRows_, "host_completion_ns");
    RequireMetricSummaryCount(summary.nativeDeviceIntervalNanoseconds,
        steadyState && configuration_.instrumentMode == InstrumentMode::N,
        nativeDeviceSummaryRows_, "native_device_interval_ns");
    if (!initializationWritten_)
        throw std::invalid_argument("qualification package has no initialization.csv");
    if (configuration_.phase == QualificationPhase::WarmupCharacterization)
    {
        if (sampleRows_ != 0U
            || (summary.processStatus == "ok"
                && warmupRows_ != WarmupCharacterizationCount))
            throw std::invalid_argument(
                "warmup-characterization row counts disagree with the protocol");
    }
    else if (warmupRows_ != 0U
        || (summary.processStatus == "ok"
            && sampleRows_ != configuration_.plannedSampleCount))
    {
        throw std::invalid_argument(
            "measured qualification row counts disagree with the declared plan");
    }

    samplesStream_.close();
    if (warmupStream_.is_open()) warmupStream_.close();
    WriteFile(stagingDirectory_ / "environment.json", SerializeEnvironmentJson(environment));
    WriteFile(stagingDirectory_ / "summary.json", SerializeSummaryJson(summary));

    std::error_code error;
    std::filesystem::rename(
        stagingDirectory_, configuration_.outputPackageDirectory, error);
    if (error) throw std::runtime_error("unable to mark qualification package complete");
    completed_ = true;
}

const std::filesystem::path&
QualificationPackage::StagingDirectory() const noexcept
{
    return stagingDirectory_;
}

} // namespace computelab::ex2::gate0
