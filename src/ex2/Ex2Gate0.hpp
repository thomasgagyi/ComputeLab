#pragma once

#include "results/ResultRecords.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace computelab::ex2::gate0
{

inline constexpr std::uint32_t SchemaVersion = 2U;
inline constexpr std::string_view ExperimentId = "EX-2";
inline constexpr std::string_view EvidenceKind = "qualification";
inline constexpr std::string_view WorkloadId = "gate0-diagnostic-transform";
inline constexpr std::string_view VariantId = "existing-deterministic-transform-v1";
inline constexpr std::string_view GeneratorRevision = "ex1-mt19937-64-low32-v1";
inline constexpr std::string_view ExecutionMode = "ordinary";
inline constexpr std::string_view OperationBoundary =
    "prepared-transform-submit-immediate-wait";
inline constexpr std::uint64_t WarmupCharacterizationCount = 48U;
inline constexpr std::uint64_t MaximumQualificationSampleCount = 200U;

enum class QualificationPhase
{
    WarmupCharacterization,
    SampleCountQualification,
    InstrumentationControl,
};

enum class Backend
{
    Cuda,
    Vulkan,
};

enum class InstrumentMode
{
    H,
    N,
};

enum class ExitCode : int
{
    Success = 0,
    ConfigurationError = 2,
    CorrectnessFailure = 3,
    NativeExecutionFailure = 4,
    IncompleteOrTimeout = 5,
    EvidenceOrProvenanceFailure = 6,
};

struct Configuration
{
    QualificationPhase phase{};
    Backend backend{};
    InstrumentMode instrumentMode{};
    std::uint32_t deviceIndex{};
    std::uint64_t elementCount{};
    std::uint64_t seed{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
    std::uint64_t processIndex{};
    std::uint64_t blockIndex{};
    std::uint64_t orderSlot{};
    std::string runId;
    std::filesystem::path outputPackageDirectory;
    std::string machineId;
    std::string protocolVersion;
};

using CanonicalValue = std::variant<std::nullptr_t, std::string, std::uint64_t>;
using CanonicalField = std::pair<std::string, CanonicalValue>;

struct IdentityContext
{
    std::string protocolVersion;
    std::string machineId;
    std::string gpuUuidIdentity;
    std::uint64_t elementCount{};
    std::uint64_t seed{};
    InstrumentMode instrumentMode{};
};

struct SeriesIdentityContext
{
    IdentityContext condition;
    Backend backend{};
    std::uint64_t processIndex{};
    std::uint64_t blockIndex{};
    std::uint64_t orderSlot{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
    std::string sourceRevision;
    std::string executableSha256;
    std::optional<std::string> shaderSha256;
};

struct InitializationRecord
{
    std::string runId;
    Backend backend{};
    std::uint64_t processIndex{};
    std::uint64_t sequenceIndex{};
    std::string category;
    std::optional<std::string> workload;
    std::optional<std::string> variant;
    std::optional<std::uint64_t> elementCount;
    std::string metric;
    std::optional<std::uint64_t> durationNanoseconds;
    std::optional<std::string> observation;
};

struct SampleRecord
{
    std::string runId;
    std::string comparisonConditionId;
    std::string seriesId;
    Backend backend{};
    std::uint64_t seed{};
    std::uint64_t elementCount{};
    InstrumentMode instrumentMode{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
    std::uint64_t blockIndex{};
    std::uint64_t orderSlot{};
    std::uint64_t processIndex{};
    std::uint64_t sampleIndex{};
    std::optional<bool> validationPassed;
    std::string status;
    std::optional<std::string> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
};

struct WarmupRecord
{
    std::string runId;
    std::string seriesId;
    std::uint64_t processIndex{};
    std::uint64_t sequenceIndex{};
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
    std::string status;
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

struct SummaryRecord
{
    Configuration configuration;
    std::string comparisonConditionId;
    std::string seriesId;
    std::string processStatus{"ok"};
    std::optional<std::string> failurePhase;
    std::optional<std::string> errorCode;
    std::uint64_t recordedSampleCount{};
    std::uint64_t validationFailures{};
    std::uint64_t failedSampleCount{};
    std::optional<MetricSummary> hostSubmissionNanoseconds;
    std::optional<MetricSummary> hostWaitNanoseconds;
    std::optional<MetricSummary> hostCompletionNanoseconds;
    std::optional<MetricSummary> nativeDeviceIntervalNanoseconds;
};

struct BackendDiagnostics
{
    std::string implementation;
    std::optional<std::string> streamFlags;
    std::optional<std::uint64_t> queueFamilyIndex;
    std::optional<std::uint64_t> queueFlags;
    std::optional<std::uint64_t> queueCount;
    std::optional<std::uint64_t> timestampValidBits;
    std::optional<double> timestampPeriodNanoseconds;
    std::optional<std::uint64_t> inputMemoryFlags;
    std::optional<std::uint64_t> outputMemoryFlags;
    std::optional<std::uint64_t> uploadMemoryFlags;
    std::optional<std::uint64_t> readbackMemoryFlags;
    bool nativeMarkersEnabled{};
    std::optional<std::string> nativeTimingMethod;
    std::optional<std::uint64_t> nativeTimingResolutionNanoseconds;
    std::optional<std::uint64_t> nativeTimingStartStage;
    std::optional<std::uint64_t> nativeTimingStopStage;
    std::optional<std::uint64_t> nativeDurationEnvelopeNanoseconds;
};

struct EnvironmentRecord
{
    results::EnvironmentRecord common;
    Configuration configuration;
    std::string comparisonConditionId;
    std::string seriesId;
    std::string sourceRevision;
    std::string executableSha256;
    std::optional<std::string> shaderSha256;
    std::string inputSha256;
    std::string expectedOutputSha256;
    std::string gpuUuidIdentity;
    BackendDiagnostics backendDiagnostics;
};

[[nodiscard]] std::string_view ToString(QualificationPhase value) noexcept;
[[nodiscard]] std::string_view ToString(Backend value) noexcept;
[[nodiscard]] std::string_view ToString(InstrumentMode value) noexcept;
[[nodiscard]] Configuration ParseArguments(
    std::span<const std::string_view> arguments);
void ValidateOutputPackageDirectory(
    const Configuration& configuration,
    const std::filesystem::path& localResultsRoot);

[[nodiscard]] std::string CanonicalJson(
    std::vector<CanonicalField> fields);
[[nodiscard]] std::string Sha256(std::span<const std::byte> bytes);
[[nodiscard]] std::string Sha256(std::string_view bytes);
[[nodiscard]] std::string Sha256File(const std::filesystem::path& path);
[[nodiscard]] std::string ComparisonConditionCanonicalJson(
    const IdentityContext& context);
[[nodiscard]] std::string ComparisonConditionId(
    const IdentityContext& context);
[[nodiscard]] std::string SeriesCanonicalJson(
    const SeriesIdentityContext& context);
[[nodiscard]] std::string SeriesId(const SeriesIdentityContext& context);

[[nodiscard]] std::string InitializationCsvHeader();
[[nodiscard]] std::string SamplesCsvHeader();
[[nodiscard]] std::string WarmupCsvHeader();
[[nodiscard]] std::string SerializeInitializationRow(
    const InitializationRecord& record);
[[nodiscard]] std::string SerializeSampleRow(const SampleRecord& record);
[[nodiscard]] std::string SerializeWarmupRow(const WarmupRecord& record);
[[nodiscard]] std::string SerializeEnvironmentJson(
    const EnvironmentRecord& record);
[[nodiscard]] std::string SerializeSummaryJson(const SummaryRecord& record);
[[nodiscard]] SummaryRecord SummarizeSamples(
    const Configuration& configuration,
    std::string comparisonConditionId,
    std::string seriesId,
    const std::vector<SampleRecord>& samples,
    std::string processStatus = "ok",
    std::optional<std::string> failurePhase = std::nullopt,
    std::optional<std::string> errorCode = std::nullopt);

class QualificationPackage final
{
public:
    explicit QualificationPackage(Configuration configuration);
    ~QualificationPackage() noexcept;

    QualificationPackage(const QualificationPackage&) = delete;
    QualificationPackage& operator=(const QualificationPackage&) = delete;
    QualificationPackage(QualificationPackage&&) = delete;
    QualificationPackage& operator=(QualificationPackage&&) = delete;

    void WriteInitialization(const std::vector<InitializationRecord>& records);
    void AppendSample(const SampleRecord& record);
    void AppendWarmup(const WarmupRecord& record);
    void Complete(
        const EnvironmentRecord& environment,
        const SummaryRecord& summary);

    [[nodiscard]] const std::filesystem::path& StagingDirectory() const noexcept;

private:
    Configuration configuration_;
    std::filesystem::path stagingDirectory_;
    std::ofstream samplesStream_;
    std::ofstream warmupStream_;
    std::uint64_t sampleRows_{};
    std::uint64_t warmupRows_{};
    std::uint64_t validationFailures_{};
    std::uint64_t failedSampleRows_{};
    std::uint64_t hostSubmissionSummaryRows_{};
    std::uint64_t hostWaitSummaryRows_{};
    std::uint64_t hostCompletionSummaryRows_{};
    std::uint64_t nativeDeviceSummaryRows_{};
    std::optional<std::string> sampleComparisonConditionId_;
    std::optional<std::string> sampleSeriesId_;
    std::optional<std::string> warmupSeriesId_;
    bool initializationWritten_{};
    bool completed_{};
};

} // namespace computelab::ex2::gate0
