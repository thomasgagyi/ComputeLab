#include "results/ResultRecords.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace computelab::results
{
namespace
{

constexpr std::string_view kInitializationHeader =
    "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation";
constexpr std::string_view kSamplesHeader =
    "schema_version,run_id,experiment_id,series_id,backend,workload,variant,seed,element_count,warmup_count,planned_sample_count,sample_index,validation_passed,upload_ns,host_submission_ns,device_execution_ns,end_to_end_ns,download_ns";

template <typename T>
void AppendInteger(std::string& output, T value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
    {
        throw std::runtime_error("integer serialization failed");
    }
    output.append(buffer.data(), end);
}

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hexDigits[] = "0123456789ABCDEF";
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
                output.push_back(hexDigits[character >> 4U]);
                output.push_back(hexDigits[character & 0x0FU]);
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

template <typename T, typename AppendValue>
void AppendJsonOptional(std::string& output, const std::optional<T>& value, AppendValue appendValue)
{
    if (value.has_value())
    {
        appendValue(*value);
    }
    else
    {
        output += "null";
    }
}

void AppendJsonDouble(std::string& output, double value)
{
    if (!std::isfinite(value))
    {
        throw std::invalid_argument("summary metric values must be finite binary64 values");
    }

    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
    {
        throw std::runtime_error("binary64 serialization failed");
    }
    output.append(buffer.data(), end);
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
        if (character == '"')
        {
            output.push_back('"');
        }
        output.push_back(character);
    }
    output.push_back('"');
}

template <typename T>
void AppendCsvInteger(std::string& output, T value)
{
    AppendInteger(output, value);
}

void AppendCsvBoolean(std::string& output, bool value)
{
    AppendJsonBoolean(output, value);
}

template <typename T, typename AppendValue>
void AppendCsvOptional(std::string& output, const std::optional<T>& value, AppendValue appendValue)
{
    if (value.has_value())
    {
        appendValue(*value);
    }
}

void AppendJsonMetricSummary(std::string& output, const MetricSummary& summary)
{
    output += "{\"sample_count\":";
    AppendJsonOptional(output, summary.sampleCount, [&output](std::uint64_t value) { AppendInteger(output, value); });
    output += ",\"minimum\":";
    AppendJsonOptional(output, summary.minimum, [&output](double value) { AppendJsonDouble(output, value); });
    output += ",\"median\":";
    AppendJsonOptional(output, summary.median, [&output](double value) { AppendJsonDouble(output, value); });
    output += ",\"mean\":";
    AppendJsonOptional(output, summary.mean, [&output](double value) { AppendJsonDouble(output, value); });
    output += ",\"standard_deviation\":";
    AppendJsonOptional(output, summary.standardDeviation, [&output](double value) { AppendJsonDouble(output, value); });
    output += ",\"coefficient_of_variation\":";
    AppendJsonOptional(output, summary.coefficientOfVariation, [&output](double value) { AppendJsonDouble(output, value); });
    output += ",\"p95\":";
    AppendJsonOptional(output, summary.p95, [&output](double value) { AppendJsonDouble(output, value); });
    output.push_back('}');
}

void AppendJsonOptionalMetricSummary(
    std::string& output,
    const std::optional<MetricSummary>& summary)
{
    AppendJsonOptional(output, summary, [&output](const MetricSummary& value) {
        AppendJsonMetricSummary(output, value);
    });
}

} // namespace

std::string SerializeEnvironmentJson(const EnvironmentRecord& record)
{
    std::string output;
    output.reserve(1024U);
    output += "{\"schema_version\":";
    AppendInteger(output, record.schemaVersion);
    output += ",\"experiment_id\":";
    AppendJsonString(output, record.experimentId);
    output += ",\"run_id\":";
    AppendJsonString(output, record.runId);
    output += ",\"timestamp_utc\":";
    AppendJsonString(output, record.timestampUtc);
    output += ",\"git_commit\":";
    AppendJsonString(output, record.gitCommit);
    output += ",\"git_dirty\":";
    AppendJsonBoolean(output, record.gitDirty);
    output += ",\"machine_id\":";
    AppendJsonString(output, record.machineId);
    output += ",\"os_name\":";
    AppendJsonString(output, record.osName);
    output += ",\"os_version\":";
    AppendJsonString(output, record.osVersion);
    output += ",\"cpu_name\":";
    AppendJsonString(output, record.cpuName);
    output += ",\"system_memory_bytes\":";
    AppendInteger(output, record.systemMemoryBytes);
    output += ",\"gpu_name\":";
    AppendJsonOptional(output, record.gpuName, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"gpu_vendor\":";
    AppendJsonOptional(output, record.gpuVendor, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"gpu_device_id\":";
    AppendJsonOptional(output, record.gpuDeviceId, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"gpu_memory_bytes\":";
    AppendJsonOptional(output, record.gpuMemoryBytes, [&output](std::uint64_t value) { AppendInteger(output, value); });
    output += ",\"nvidia_driver_version\":";
    AppendJsonOptional(output, record.nvidiaDriverVersion, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"cuda_toolkit_version\":";
    AppendJsonOptional(output, record.cudaToolkitVersion, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"cuda_runtime_version\":";
    AppendJsonOptional(output, record.cudaRuntimeVersion, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"cuda_compute_capability\":";
    AppendJsonOptional(output, record.cudaComputeCapability, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"vulkan_sdk_version\":";
    AppendJsonOptional(output, record.vulkanSdkVersion, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"vulkan_device_api_version\":";
    AppendJsonOptional(output, record.vulkanDeviceApiVersion, [&output](const std::string& value) { AppendJsonString(output, value); });
    output += ",\"compiler_name\":";
    AppendJsonString(output, record.compilerName);
    output += ",\"compiler_version\":";
    AppendJsonString(output, record.compilerVersion);
    output += ",\"cmake_version\":";
    AppendJsonString(output, record.cmakeVersion);
    output += ",\"ninja_version\":";
    AppendJsonString(output, record.ninjaVersion);
    output += ",\"configure_preset\":";
    AppendJsonString(output, record.configurePreset);
    output += ",\"build_type\":";
    AppendJsonString(output, record.buildType);
    output += ",\"validation_enabled\":";
    AppendJsonBoolean(output, record.validationEnabled);
    output += ",\"diagnostic_instrumentation\":";
    AppendJsonBoolean(output, record.diagnosticInstrumentation);
    output += "}\n";
    return output;
}

std::string SerializeInitializationCsv(const std::vector<InitializationRecord>& records)
{
    std::string output{kInitializationHeader};
    output += "\r\n";
    for (const InitializationRecord& record : records)
    {
        if (!record.durationNs.has_value()
            && (!record.observation.has_value() || record.observation->empty()))
        {
            throw std::invalid_argument("initialization record requires duration_ns or observation");
        }

        AppendCsvInteger(output, record.schemaVersion); output.push_back(',');
        AppendCsvField(output, record.runId); output.push_back(',');
        AppendCsvField(output, record.experimentId); output.push_back(',');
        AppendCsvField(output, record.backend); output.push_back(',');
        AppendCsvInteger(output, record.processIndex); output.push_back(',');
        AppendCsvInteger(output, record.sequenceIndex); output.push_back(',');
        AppendCsvField(output, record.category); output.push_back(',');
        AppendCsvOptional(output, record.workload, [&output](const std::string& value) { AppendCsvField(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.variant, [&output](const std::string& value) { AppendCsvField(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.elementCount, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvField(output, record.metric); output.push_back(',');
        AppendCsvOptional(output, record.durationNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.observation, [&output](const std::string& value) { AppendCsvField(output, value); });
        output += "\r\n";
    }
    return output;
}

std::string SerializeSamplesCsv(const std::vector<SampleRecord>& records)
{
    std::string output{kSamplesHeader};
    output += "\r\n";
    for (const SampleRecord& record : records)
    {
        AppendCsvInteger(output, record.schemaVersion); output.push_back(',');
        AppendCsvField(output, record.runId); output.push_back(',');
        AppendCsvField(output, record.experimentId); output.push_back(',');
        AppendCsvField(output, record.seriesId); output.push_back(',');
        AppendCsvField(output, record.backend); output.push_back(',');
        AppendCsvField(output, record.workload); output.push_back(',');
        AppendCsvOptional(output, record.variant, [&output](const std::string& value) { AppendCsvField(output, value); }); output.push_back(',');
        AppendCsvInteger(output, record.seed); output.push_back(',');
        AppendCsvInteger(output, record.elementCount); output.push_back(',');
        AppendCsvInteger(output, record.warmupCount); output.push_back(',');
        AppendCsvInteger(output, record.plannedSampleCount); output.push_back(',');
        AppendCsvInteger(output, record.sampleIndex); output.push_back(',');
        AppendCsvBoolean(output, record.validationPassed); output.push_back(',');
        AppendCsvOptional(output, record.uploadNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.hostSubmissionNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.deviceExecutionNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.endToEndNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); }); output.push_back(',');
        AppendCsvOptional(output, record.downloadNs, [&output](std::uint64_t value) { AppendCsvInteger(output, value); });
        output += "\r\n";
    }
    return output;
}

std::string SerializeSummaryJson(const SummaryRecord& record)
{
    std::string output;
    output.reserve(1024U);
    output += "{\"schema_version\":";
    AppendInteger(output, record.schemaVersion);
    output += ",\"run_id\":";
    AppendJsonString(output, record.runId);
    output += ",\"experiment_id\":";
    AppendJsonString(output, record.experimentId);
    output += ",\"sample_groups\":[";
    for (std::size_t index = 0; index < record.sampleGroups.size(); ++index)
    {
        if (index != 0U)
        {
            output.push_back(',');
        }

        const SampleGroupSummary& sampleGroup = record.sampleGroups[index];
        output += "{\"group\":{\"series_id\":";
        AppendJsonString(output, sampleGroup.group.seriesId);
        output += ",\"backend\":";
        AppendJsonString(output, sampleGroup.group.backend);
        output += ",\"workload\":";
        AppendJsonString(output, sampleGroup.group.workload);
        output += ",\"variant\":";
        AppendJsonOptional(output, sampleGroup.group.variant, [&output](const std::string& value) { AppendJsonString(output, value); });
        output += ",\"seed\":";
        AppendInteger(output, sampleGroup.group.seed);
        output += ",\"element_count\":";
        AppendInteger(output, sampleGroup.group.elementCount);
        output += ",\"warmup_count\":";
        AppendInteger(output, sampleGroup.group.warmupCount);
        output += ",\"planned_sample_count\":";
        AppendInteger(output, sampleGroup.group.plannedSampleCount);
        output += "},\"recorded_sample_count\":";
        AppendInteger(output, sampleGroup.recordedSampleCount);
        output += ",\"validation_failures\":";
        AppendInteger(output, sampleGroup.validationFailures);
        output += ",\"metrics\":{\"upload_ns\":";
        AppendJsonOptionalMetricSummary(output, sampleGroup.uploadNs);
        output += ",\"host_submission_ns\":";
        AppendJsonOptionalMetricSummary(output, sampleGroup.hostSubmissionNs);
        output += ",\"device_execution_ns\":";
        AppendJsonOptionalMetricSummary(output, sampleGroup.deviceExecutionNs);
        output += ",\"end_to_end_ns\":";
        AppendJsonOptionalMetricSummary(output, sampleGroup.endToEndNs);
        output += ",\"download_ns\":";
        AppendJsonOptionalMetricSummary(output, sampleGroup.downloadNs);
        output += "}}";
    }
    output += "]}\n";
    return output;
}

} // namespace computelab::results
