#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace computelab::results
{

struct EnvironmentRecord
{
    std::uint32_t schemaVersion{};
    std::string experimentId;
    std::string runId;
    std::string timestampUtc;
    std::string gitCommit;
    bool gitDirty{};
    std::string machineId;
    std::string osName;
    std::string osVersion;
    std::string cpuName;
    std::uint64_t systemMemoryBytes{};
    std::optional<std::string> gpuName;
    std::optional<std::string> gpuVendor;
    std::optional<std::string> gpuDeviceId;
    std::optional<std::uint64_t> gpuMemoryBytes;
    std::optional<std::string> nvidiaDriverVersion;
    std::optional<std::string> cudaToolkitVersion;
    std::optional<std::string> cudaRuntimeVersion;
    std::optional<std::string> cudaComputeCapability;
    std::optional<std::string> vulkanSdkVersion;
    std::optional<std::string> vulkanDeviceApiVersion;
    std::string compilerName;
    std::string compilerVersion;
    std::string cmakeVersion;
    std::string ninjaVersion;
    std::string configurePreset;
    std::string buildType;
    bool validationEnabled{};
    bool diagnosticInstrumentation{};
};

struct InitializationRecord
{
    std::uint32_t schemaVersion{};
    std::string runId;
    std::string experimentId;
    std::string backend;
    std::uint64_t processIndex{};
    std::uint64_t sequenceIndex{};
    std::string category;
    std::optional<std::string> workload;
    std::optional<std::string> variant;
    std::optional<std::uint64_t> elementCount;
    std::string metric;
    std::optional<std::uint64_t> durationNs;
    std::optional<std::string> observation;
};

struct SampleRecord
{
    std::uint32_t schemaVersion{};
    std::string runId;
    std::string experimentId;
    std::string seriesId;
    std::string backend;
    std::string workload;
    std::optional<std::string> variant;
    std::uint64_t seed{};
    std::uint64_t elementCount{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
    std::uint64_t sampleIndex{};
    bool validationPassed{};
    std::optional<std::uint64_t> uploadNs;
    std::optional<std::uint64_t> hostSubmissionNs;
    std::optional<std::uint64_t> deviceExecutionNs;
    std::optional<std::uint64_t> endToEndNs;
    std::optional<std::uint64_t> downloadNs;
};

struct SampleGroupKey
{
    std::string seriesId;
    std::string backend;
    std::string workload;
    std::optional<std::string> variant;
    std::uint64_t seed{};
    std::uint64_t elementCount{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
};

struct MetricSummary
{
    std::optional<std::uint64_t> sampleCount;
    std::optional<double> minimum;
    std::optional<double> median;
    std::optional<double> mean;
    std::optional<double> standardDeviation;
    std::optional<double> coefficientOfVariation;
    std::optional<double> p95;
};

struct SampleGroupSummary
{
    SampleGroupKey group;
    std::uint64_t recordedSampleCount{};
    std::uint64_t validationFailures{};
    std::optional<MetricSummary> uploadNs;
    std::optional<MetricSummary> hostSubmissionNs;
    std::optional<MetricSummary> deviceExecutionNs;
    std::optional<MetricSummary> endToEndNs;
    std::optional<MetricSummary> downloadNs;
};

struct SummaryRecord
{
    std::uint32_t schemaVersion{};
    std::string runId;
    std::string experimentId;
    std::vector<SampleGroupSummary> sampleGroups;
};

[[nodiscard]] std::string SerializeEnvironmentJson(const EnvironmentRecord& record);
[[nodiscard]] std::string SerializeInitializationCsv(const std::vector<InitializationRecord>& records);
[[nodiscard]] std::string SerializeSamplesCsv(const std::vector<SampleRecord>& records);
[[nodiscard]] std::string SerializeSummaryJson(const SummaryRecord& record);

} // namespace computelab::results
