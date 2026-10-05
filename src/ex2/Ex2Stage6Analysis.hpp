#pragma once
#include "ex2/Ex2Stage6Evidence.hpp"

namespace computelab::ex2::stage6::analysis
{
inline constexpr std::uint64_t AnalysisVersion = 1;
enum class ProcessTerminalState { Success, DiagnosticFailure };
enum class SlotDisposition { ResolvedSuccess, ResolvedDiagnosticFailure, UnresolvedCampaignFatal, NotLaunched };
enum class CoverageStatus { Complete, Incomplete };
struct WindowDescription
{
    std::uint64_t begin{}, count{25}, successfulRowCount{};
    std::optional<double> hostCompletionMedianNanoseconds;
};
struct ProcessInput
{
    std::string packageSha256;
    evidence::Plan plan;
    std::vector<evidence::SampleRecord> samples;
    evidence::SummaryRecord summary;
};
class ProcessDescription final
{
public:
    [[nodiscard]] const evidence::Plan& Plan() const noexcept { return summary_.plan; }
    [[nodiscard]] const std::string& PackageSha256() const noexcept { return packageSha256_; }
    [[nodiscard]] const evidence::SummaryRecord& Summary() const noexcept { return summary_; }
    [[nodiscard]] ProcessTerminalState TerminalState() const noexcept { return terminalState_; }
    [[nodiscard]] const std::array<WindowDescription, 4>& Windows() const noexcept { return windows_; }
private:
    ProcessDescription(std::string, evidence::SummaryRecord, std::array<WindowDescription, 4>);
    std::string packageSha256_;
    evidence::SummaryRecord summary_;
    ProcessTerminalState terminalState_;
    std::array<WindowDescription, 4> windows_;
    friend ProcessDescription DescribeProcess(const ProcessInput&);
};
[[nodiscard]] ProcessDescription DescribeProcess(const ProcessInput&);
struct CellSlotInput
{
    SlotDisposition disposition{SlotDisposition::NotLaunched};
    std::optional<ProcessDescription> process;
};
struct CellInput
{
    std::uint64_t cellIndex{};
    std::array<CellSlotInput, ChildrenPerCell> slots;
};
struct BackendDescription
{
    std::uint64_t resolvedProcessCount{}, successfulProcessCount{}, diagnosticFailureProcessCount{};
    std::optional<double> processMedianSpanRatio;
    std::array<std::optional<ProcessDescription>, ProcessesPerBackend> processes;
};
class CellDescription final
{
public:
    [[nodiscard]] std::uint64_t CellIndex() const noexcept { return cellIndex_; }
    [[nodiscard]] CoverageStatus Status() const noexcept { return status_; }
    [[nodiscard]] std::uint64_t ResolvedSlotCount() const noexcept { return resolved_; }
    [[nodiscard]] std::uint64_t SuccessfulSlotCount() const noexcept { return successful_; }
    [[nodiscard]] std::uint64_t DiagnosticFailureSlotCount() const noexcept { return failures_; }
    [[nodiscard]] const BackendDescription& Cuda() const noexcept { return cuda_; }
    [[nodiscard]] const BackendDescription& Vulkan() const noexcept { return vulkan_; }
private:
    CellDescription() = default;
    std::uint64_t cellIndex_{}, resolved_{}, successful_{}, failures_{};
    CoverageStatus status_{CoverageStatus::Incomplete};
    BackendDescription cuda_, vulkan_;
    friend CellDescription DescribeCell(const CellInput&);
};
[[nodiscard]] BackendDescription DescribeBackend(std::uint64_t cellIndex, Backend,
    const std::array<std::optional<ProcessDescription>, ProcessesPerBackend>&);
[[nodiscard]] CellDescription DescribeCell(const CellInput&);
struct CampaignInput
{
    std::array<SlotDisposition, TotalChildCount> slots;
    std::array<std::optional<std::string>, CoreCellCount> cellAnalysisSha256;
};
class CampaignDescription final
{
public:
    [[nodiscard]] CoverageStatus Status() const noexcept { return status_; }
    [[nodiscard]] std::uint64_t AttemptedSlots() const noexcept { return attempted_; }
    [[nodiscard]] std::uint64_t ResolvedSlots() const noexcept { return resolved_; }
    [[nodiscard]] std::uint64_t SuccessfulSlots() const noexcept { return successful_; }
    [[nodiscard]] std::uint64_t DiagnosticFailureSlots() const noexcept { return failures_; }
    [[nodiscard]] std::uint64_t UnlaunchedSlots() const noexcept { return unlaunched_; }
    [[nodiscard]] std::uint64_t FullyResolvedCells() const noexcept { return fullCells_; }
    [[nodiscard]] std::uint64_t CellsWithDiagnosticFailures() const noexcept { return failureCells_; }
    [[nodiscard]] const auto& CellAnalysisSha256() const noexcept { return hashes_; }
private:
    CampaignDescription() = default;
    CoverageStatus status_{CoverageStatus::Incomplete};
    std::uint64_t attempted_{}, resolved_{}, successful_{}, failures_{}, unlaunched_{}, fullCells_{}, failureCells_{};
    std::array<std::optional<std::string>, CoreCellCount> hashes_;
    friend CampaignDescription DescribeCampaign(const CampaignInput&);
};
[[nodiscard]] CampaignDescription DescribeCampaign(const CampaignInput&);
[[nodiscard]] std::string SerializeCellJson(const CellDescription&);
[[nodiscard]] std::string SerializeCampaignJson(const CampaignDescription&);
} // namespace computelab::ex2::stage6::analysis
