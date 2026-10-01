#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2::stage5
{

inline constexpr std::uint32_t SchemaVersion = 2U;
inline constexpr std::uint32_t AnalysisSchemaVersion = 1U;
inline constexpr std::string_view ExperimentId = "EX-2";
inline constexpr std::string_view ProtocolVersion = "1.2";
inline constexpr std::string_view EvidenceKind = "qualification";
inline constexpr std::string_view InstrumentMode = "H";
inline constexpr std::size_t PairedBlockCount = 5U;
inline constexpr std::size_t ProcessesPerBackend = 5U;
inline constexpr std::size_t ChildrenPerGroup = 10U;
inline constexpr std::size_t DiagnosticObservationCount = 48U;
inline constexpr std::size_t SampleObservationCount = 200U;
inline constexpr std::size_t AdmittedOrdinarySampleCount = 100U;
inline constexpr std::uint64_t A1ElementCount = 256U;
inline constexpr std::uint64_t D1ElementCount = 1'048'576U;
inline constexpr std::uint64_t D1IterationCount = 64U;
inline constexpr std::array<std::uint64_t, 6> CandidateWarmups{0U, 1U, 2U, 4U, 8U, 16U};

enum class Stage5Phase { A1Sentinel, D1Warmup, D1Sample };
enum class Stage5Workload { A1, D1 };
enum class Stage5Backend { Cuda, Vulkan };
enum class OperationStatus
{
    Ok, ValidationFailed, SubmitFailed, WaitFailed, Timeout,
    DeviceLost, TimestampInvalid, Incomplete,
};

inline constexpr std::array<std::array<Stage5Backend, 2>, PairedBlockCount> PairedOrder{{
    {Stage5Backend::Cuda, Stage5Backend::Vulkan},
    {Stage5Backend::Vulkan, Stage5Backend::Cuda},
    {Stage5Backend::Cuda, Stage5Backend::Vulkan},
    {Stage5Backend::Vulkan, Stage5Backend::Cuda},
    {Stage5Backend::Cuda, Stage5Backend::Vulkan},
}};

struct Condition
{
    Stage5Phase phase{};
    Stage5Workload workload{};
    std::uint64_t elementCount{};
    std::optional<std::uint64_t> iterationCount;
    std::string instrumentMode{InstrumentMode};
    std::uint64_t diagnosticCount{};
    std::optional<std::uint64_t> selectedW;
    std::uint64_t measuredSampleCount{};
    bool operator==(const Condition&) const = default;
};

struct PlannedProcess
{
    Stage5Backend backend{};
    std::uint64_t processIndex{};
    std::uint64_t blockIndex{};
    std::uint64_t orderSlot{};
    bool operator==(const PlannedProcess&) const = default;
};

[[nodiscard]] bool IsCandidateWarmup(std::uint64_t value) noexcept;
[[nodiscard]] bool ValidateCondition(const Condition& condition) noexcept;
// Ordered, pure group shape; no launch, manifest, run ID or publication semantics.
[[nodiscard]] std::array<PlannedProcess, ChildrenPerGroup> FrozenProcessPlan() noexcept;
[[nodiscard]] bool ValidateProcessPlan(std::span<const PlannedProcess> processes) noexcept;

struct Observation
{
    std::uint64_t sequenceIndex{};
    OperationStatus status{OperationStatus::Incomplete};
    bool validationEligible{};
    std::optional<std::uint64_t> hostCompletionNanoseconds;
};

struct ProcessInput
{
    Condition condition;
    PlannedProcess process;
    std::uint64_t expectedObservationCount{};
    std::vector<Observation> observations;
    // Supplied facts: I1 does not invent clock or state-detection heuristics.
    bool hostClockResolutionAdequate{};
    bool persistentTrendOrAbruptStateSwitch{};
};

// Exact integer/half-nanosecond median, including near UINT64_MAX. Binary64
// conversion is only for diagnostics; threshold decisions use the exact value.
struct MedianNanoseconds
{
    std::uint64_t whole{};
    bool half{};
    auto operator<=>(const MedianNanoseconds&) const = default;
    [[nodiscard]] double AsDouble() const noexcept;
};
[[nodiscard]] MedianNanoseconds Median(std::span<const std::uint64_t> values);

struct WarmupCandidateDiagnostics
{
    std::uint64_t w{};
    MedianNanoseconds earlyA, earlyB, reference, lateA;
    double earlyARelativeDifference{}, earlyBRelativeDifference{}, lateRelativeDifference{};
    bool qualified{};
};

struct CommonWarmupAssessment;
class WarmupProcessAssessment;
[[nodiscard]] WarmupProcessAssessment AssessWarmupProcess(const ProcessInput& input);

// Assessments are created only by the pure calculation, so downstream callers
// cannot substitute manually set validity/qualification flags or medians.
class WarmupProcessAssessment final
{
public:
    [[nodiscard]] const PlannedProcess& Process() const noexcept { return process_; }
    [[nodiscard]] bool InputValid() const noexcept { return inputValid_; }
    [[nodiscard]] bool Qualified() const noexcept { return selectedW_.has_value(); }
    [[nodiscard]] std::optional<std::uint64_t> SelectedW() const noexcept { return selectedW_; }
    [[nodiscard]] const auto& Candidates() const noexcept { return candidates_; }
    [[nodiscard]] const auto& Reasons() const noexcept { return reasons_; }
private:
    friend WarmupProcessAssessment AssessWarmupProcess(const ProcessInput&);
    PlannedProcess process_;
    bool inputValid_{};
    std::optional<std::uint64_t> selectedW_;
    std::vector<WarmupCandidateDiagnostics> candidates_;
    std::vector<std::string> reasons_;
};

struct CommonWarmupAssessment
{
    bool inputValid{};
    bool qualified{};
    std::optional<std::uint64_t> selectedCommonW;
    std::vector<std::string> reasons;
};
[[nodiscard]] CommonWarmupAssessment AssessCommonWarmup(
    std::span<const WarmupProcessAssessment> processes);

struct SampleCountDiagnostics
{
    MedianNanoseconds median50, median100, median200;
    std::array<MedianNanoseconds, 4> windows;
    double prefix100RelativeDifference{}, prefix50RelativeDifference{}, firstLastRelativeDifference{};
};
class SampleCountProcessAssessment;
[[nodiscard]] SampleCountProcessAssessment AssessSampleCountProcess(const ProcessInput& input);
class SampleCountProcessAssessment final
{
public:
    [[nodiscard]] const PlannedProcess& Process() const noexcept { return process_; }
    [[nodiscard]] bool InputValid() const noexcept { return diagnostics_.has_value(); }
    [[nodiscard]] bool Qualified() const noexcept { return qualified_; }
    [[nodiscard]] std::optional<std::uint64_t> SelectedW() const noexcept { return selectedW_; }
    [[nodiscard]] const auto& Diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const auto& Reasons() const noexcept { return reasons_; }
private:
    friend SampleCountProcessAssessment AssessSampleCountProcess(const ProcessInput&);
    PlannedProcess process_;
    std::optional<std::uint64_t> selectedW_;
    std::optional<SampleCountDiagnostics> diagnostics_;
    bool qualified_{};
    std::vector<std::string> reasons_;
};

struct ProcessStabilityAssessment
{
    bool inputValid{};
    std::optional<double> rProcess;
    bool qualified{};
    std::vector<std::string> reasons;
};
[[nodiscard]] ProcessStabilityAssessment AssessProcessStability(
    Stage5Backend backend, std::span<const SampleCountProcessAssessment> processes);

// Derived scientific facts only. This record is not evidence, a human Gate-0
// verdict, execution authorization, or a backend performance comparison.
struct D1QualificationAssessment
{
    std::uint32_t analysisVersion{AnalysisSchemaVersion};
    std::string protocolVersion{ProtocolVersion};
    bool warmupInputValid{};
    bool d1WarmupQualified{};
    std::optional<std::uint64_t> selectedCommonW;
    bool sampleInputPresent{};
    std::optional<bool> sampleInputValid;
    std::optional<bool> d1SampleCountQualified;
    std::optional<double> cudaRProcess;
    std::optional<bool> cudaProcessStabilityQualified;
    std::optional<double> vulkanRProcess;
    std::optional<bool> vulkanProcessStabilityQualified;
    bool d1ScopeQualified{};
    std::vector<std::string> reasons;
};

[[nodiscard]] D1QualificationAssessment AssessD1Qualification(
    std::span<const ProcessInput> warmup,
    std::optional<std::span<const ProcessInput>> samples = std::nullopt);
// Stable key order, UTF-8, locale-independent numbers, explicit nulls. No I/O.
[[nodiscard]] std::string SerializeAnalysisJson(const D1QualificationAssessment& assessment);

} // namespace computelab::ex2::stage5
