#pragma once

#include "environment/EnvironmentCollector.hpp"
#include "results/ResultRecords.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex1
{

inline constexpr std::uint32_t SchemaVersion = 1U;
inline constexpr std::string_view ExperimentId = "EX-1";
inline constexpr std::string_view WorkloadId = "ex1-transform-v1";

enum class Mode
{
    Initialization,
    Series
};

enum class Backend
{
    Cpu,
    Cuda,
    Vulkan
};

struct Configuration
{
    Mode mode{};
    Backend backend{};
    std::uint64_t elementCount{};
    std::uint64_t seed{};
    std::string variant;
    std::string runId;
    std::string timestampUtc;
    std::string gitCommit;
    bool gitDirty{};
    std::string machineId;
    bool validationEnabled{};
    bool diagnosticInstrumentation{};
    std::optional<std::uint64_t> warmupCount;
    std::optional<std::uint64_t> plannedSampleCount;
};

struct SampleTimings
{
    std::optional<std::uint64_t> uploadNs;
    std::optional<std::uint64_t> hostSubmissionNs;
    std::optional<std::uint64_t> deviceExecutionNs;
    std::optional<std::uint64_t> endToEndNs;
    std::optional<std::uint64_t> downloadNs;
};

[[nodiscard]] std::string_view ToString(Backend backend) noexcept;
[[nodiscard]] bool IsApprovedWarmupCount(std::uint64_t value) noexcept;
[[nodiscard]] bool IsApprovedSampleCount(std::uint64_t value) noexcept;
[[nodiscard]] Configuration ParseArguments(std::span<const std::string_view> arguments);
[[nodiscard]] std::string SeriesId(const Configuration& configuration);
[[nodiscard]] environment::EnvironmentRunContext EnvironmentContext(
    const Configuration& configuration);
[[nodiscard]] results::SampleRecord MakeSampleRecord(
    const Configuration& configuration,
    std::uint64_t sampleIndex,
    bool validationPassed,
    const SampleTimings& timings);
[[nodiscard]] results::SummaryRecord SummarizeSamples(
    std::uint32_t schemaVersion,
    std::string runId,
    std::string experimentId,
    const std::vector<results::SampleRecord>& samples);

void WriteResultPackage(
    const std::filesystem::path& localResultsRoot,
    const results::EnvironmentRecord& environment,
    const std::vector<results::InitializationRecord>& initialization,
    const std::vector<results::SampleRecord>& samples,
    const results::SummaryRecord& summary);

} // namespace computelab::ex1
