#include "ex2/Ex2Stage6Analysis.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::stage6::analysis
{
namespace
{
using namespace evidence::detail;
namespace old = computelab::ex2::evidence;
void Require(bool valid, const char* message) { if (!valid) throw std::invalid_argument(message); }
bool Resolved(SlotDisposition d)
{ return d == SlotDisposition::ResolvedSuccess || d == SlotDisposition::ResolvedDiagnosticFailure; }
void ValidateDisposition(SlotDisposition d)
{
    Require(Resolved(d) || d == SlotDisposition::UnresolvedCampaignFatal || d == SlotDisposition::NotLaunched,
        "invalid slot disposition");
}
std::string ProcessJson(const ProcessDescription& p)
{
    const auto& slot = p.Plan().slot; const auto& s = p.Summary();
    std::string out = "{\"slot_sequence_index\":" + Json(slot.sequenceIndex);
    Field(out, "block_index", slot.process.blockIndex); Field(out, "order_slot", slot.process.orderSlot);
    Field(out, "process_index", slot.process.processIndex); Field(out, "package_sha256", p.PackageSha256());
    Field(out, "series_id", p.Plan().seriesId);
    Field(out, "terminal_state", p.TerminalState() == ProcessTerminalState::Success ? "success" : "diagnostic_failure");
    Field(out, "recorded_sample_count", s.recordedSampleCount); Field(out, "successful_sample_count", s.successfulSampleCount);
    Field(out, "validation_failures", s.validationFailures); Field(out, "failed_sample_count", s.failedSampleCount);
    Field(out, "failure_phase", s.failurePhase ? std::optional<std::string>{old::ToString(*s.failurePhase)} : std::nullopt);
    Field(out, "error_code", s.errorCode);
    out += ",\"metrics\":{\"host_submission_ns\":" + evidence::SerializeMetricJson(s.hostSubmissionNanoseconds)
        + ",\"host_wait_ns\":" + evidence::SerializeMetricJson(s.hostWaitNanoseconds)
        + ",\"host_completion_ns\":" + evidence::SerializeMetricJson(s.hostCompletionNanoseconds) + "},\"windows\":[";
    for (std::size_t i = 0; i < p.Windows().size(); ++i)
    {
        if (i) out += ',';
        const auto& w = p.Windows()[i]; out += "{\"begin\":" + Json(w.begin);
        Field(out, "count", w.count); Field(out, "successful_row_count", w.successfulRowCount);
        Field(out, "host_completion_median_ns", w.hostCompletionMedianNanoseconds); out += '}';
    }
    return out + "]}";
}
std::string BackendJson(const BackendDescription& b)
{
    std::string out = "{\"resolved_process_count\":" + Json(b.resolvedProcessCount);
    Field(out, "successful_process_count", b.successfulProcessCount);
    Field(out, "diagnostic_failure_process_count", b.diagnosticFailureProcessCount);
    Field(out, "process_median_span_ratio", b.processMedianSpanRatio); out += ",\"processes\":[";
    for (std::size_t i = 0; i < b.processes.size(); ++i)
    { if (i) out += ','; out += b.processes[i] ? ProcessJson(*b.processes[i]) : "null"; }
    return out + "]}";
}
} // namespace

ProcessDescription::ProcessDescription(std::string hash, evidence::SummaryRecord s, std::array<WindowDescription, 4> w)
    : packageSha256_(std::move(hash)), summary_(std::move(s)),
      terminalState_(summary_.processStatus == evidence::Status::Ok ? ProcessTerminalState::Success : ProcessTerminalState::DiagnosticFailure),
      windows_(std::move(w)) {}
ProcessDescription DescribeProcess(const ProcessInput& input)
{
    Require(evidence::detail::IsSha256(input.packageSha256), "invalid raw-package hash");
    evidence::ValidatePlan(input.plan); evidence::ValidateSummary(input.summary, input.samples);
    const auto& other = input.summary.plan;
    Require(input.plan.slot == other.slot && input.plan.runId == other.runId && input.plan.seriesId == other.seriesId
        && input.plan.comparisonConditionId == other.comparisonConditionId
        && SeriesCanonicalJson(input.plan.seriesIdentity) == SeriesCanonicalJson(other.seriesIdentity), "analysis plan drift");
    const auto s = evidence::SummarizeSamples(input.plan, input.samples, input.summary.processStatus,
        input.summary.failurePhase, input.summary.errorCode);
    std::array<WindowDescription, 4> windows{};
    for (std::size_t i = 0; i < windows.size(); ++i)
    {
        std::vector<std::uint64_t> values; auto& w = windows[i]; w.begin = i * 25;
        for (const auto& row : input.samples)
            if (row.sampleIndex >= w.begin && row.sampleIndex < w.begin + w.count
                && row.status == evidence::Status::Ok && row.correctness.validationPassed == true)
                values.push_back(*row.hostCompletionNanoseconds);
        w.successfulRowCount = values.size(); w.hostCompletionMedianNanoseconds = evidence::SummarizeMetric(values).median;
    }
    return {input.packageSha256, s, windows};
}
BackendDescription DescribeBackend(std::uint64_t cellIndex, Backend backend,
    const std::array<std::optional<ProcessDescription>, ProcessesPerBackend>& processes)
{
    (void)WorkloadForCell(cellIndex); Require(backend == Backend::Cuda || backend == Backend::Vulkan, "invalid backend");
    BackendDescription b; b.processes = processes; bool span = true; std::vector<double> medians;
    for (std::size_t i = 0; i < processes.size(); ++i)
    {
        if (!processes[i]) { span = false; continue; }
        const auto& p = *processes[i]; const auto& slot = p.Plan().slot; ValidatePlannedSlot(slot);
        Require(slot.cellIndex == cellIndex && slot.process.backend == backend && slot.process.processIndex == i,
            "backend process placement drift");
        ++b.resolvedProcessCount;
        if (p.TerminalState() == ProcessTerminalState::Success) ++b.successfulProcessCount;
        else ++b.diagnosticFailureProcessCount;
        const auto& s = p.Summary(); const auto m = s.hostCompletionNanoseconds.median;
        if (p.TerminalState() != ProcessTerminalState::Success || s.successfulSampleCount != PlannedSampleCount || !m || *m <= 0)
            span = false;
        else medians.push_back(*m);
    }
    if (span) b.processMedianSpanRatio = *std::max_element(medians.begin(), medians.end()) / *std::min_element(medians.begin(), medians.end());
    return b;
}
CellDescription DescribeCell(const CellInput& input)
{
    (void)WorkloadForCell(input.cellIndex); CellDescription c; c.cellIndex_ = input.cellIndex;
    const auto plan = FrozenCampaignPlan();
    for (std::size_t i = 0; i < input.slots.size(); ++i)
    {
        const auto& slot = input.slots[i]; ValidateDisposition(slot.disposition);
        if (Resolved(slot.disposition))
        {
            Require(slot.process.has_value(), "resolved cell slot requires process");
            const auto& p = *slot.process; const auto& expected = plan[input.cellIndex * ChildrenPerCell + i];
            Require(p.Plan().slot == expected && (slot.disposition == SlotDisposition::ResolvedSuccess)
                == (p.TerminalState() == ProcessTerminalState::Success), "cell identity/terminal disposition drift");
            auto& b = expected.process.backend == Backend::Cuda ? c.cuda_ : c.vulkan_;
            b.processes[expected.process.processIndex] = p;
        }
        else
        {
            Require(!slot.process, "unresolved slot cannot have final process description");
        }
    }
    c.cuda_ = DescribeBackend(input.cellIndex, Backend::Cuda, c.cuda_.processes);
    c.vulkan_ = DescribeBackend(input.cellIndex, Backend::Vulkan, c.vulkan_.processes);
    c.resolved_ = c.cuda_.resolvedProcessCount + c.vulkan_.resolvedProcessCount;
    c.successful_ = c.cuda_.successfulProcessCount + c.vulkan_.successfulProcessCount;
    c.failures_ = c.cuda_.diagnosticFailureProcessCount + c.vulkan_.diagnosticFailureProcessCount;
    c.status_ = c.resolved_ == ChildrenPerCell ? CoverageStatus::Complete : CoverageStatus::Incomplete; return c;
}
CampaignDescription DescribeCampaign(const CampaignInput& input)
{
    CampaignDescription c; c.hashes_ = input.cellAnalysisSha256;
    bool fatal = false; std::optional<std::size_t> fatalCell;
    std::array<std::uint64_t, CoreCellCount> resolved{}, failures{};
    for (std::size_t i = 0; i < input.slots.size(); ++i)
    {
        const auto d = input.slots[i]; ValidateDisposition(d);
        if (Resolved(d))
        {
            Require(!fatal, "resolved after campaign fatal"); ++c.attempted_; ++c.resolved_; ++resolved[i / ChildrenPerCell];
            if (d == SlotDisposition::ResolvedSuccess) ++c.successful_;
            else { ++c.failures_; ++failures[i / ChildrenPerCell]; }
        }
        else if (d == SlotDisposition::UnresolvedCampaignFatal)
        {
            Require(!fatal, "second campaign fatal"); fatal = true; fatalCell = i / ChildrenPerCell; ++c.attempted_;
        }
        else { Require(fatal, "NotLaunched without preceding fatal"); ++c.unlaunched_; }
    }
    for (std::size_t i = 0; i < CoreCellCount; ++i)
    {
        const auto& hash = c.hashes_[i]; if (hash) Require(evidence::detail::IsSha256(*hash), "invalid cell-analysis hash");
        if (resolved[i] == ChildrenPerCell) { ++c.fullCells_; Require(hash.has_value(), "fully resolved cell hash missing"); }
        else if (!fatalCell || i != *fatalCell) Require(!hash, "untouched cell cannot have analysis hash");
        if (failures[i]) ++c.failureCells_;
    }
    c.status_ = c.resolved_ == TotalChildCount ? CoverageStatus::Complete : CoverageStatus::Incomplete; return c;
}
std::string SerializeCellJson(const CellDescription& c)
{
    using namespace evidence::detail;
    std::string out = "{\"analysis_version\":1,\"protocol_version\":\"1.3\",\"analysis_kind\":\"stage6-cell-diagnostic\",\"cell_index\":" + Json(c.CellIndex());
    out += ",\"workload\":" + WorkloadJson(WorkloadForCell(c.CellIndex()));
    Field(out, "status", c.Status() == CoverageStatus::Complete ? "complete" : "incomplete");
    Field(out, "resolved_slot_count", c.ResolvedSlotCount()); Field(out, "successful_slot_count", c.SuccessfulSlotCount());
    Field(out, "diagnostic_failure_slot_count", c.DiagnosticFailureSlotCount());
    return out + ",\"backends\":{\"cuda\":" + BackendJson(c.Cuda()) + ",\"vulkan\":" + BackendJson(c.Vulkan()) + "}}\n";
}
std::string SerializeCampaignJson(const CampaignDescription& c)
{
    using namespace evidence::detail;
    std::string out = "{\"analysis_version\":1,\"protocol_version\":\"1.3\",\"analysis_kind\":\"stage6-campaign-diagnostic\"";
    Field(out, "status", c.Status() == CoverageStatus::Complete ? "complete" : "incomplete");
    Field(out, "declared_slots", static_cast<std::uint64_t>(TotalChildCount)); Field(out, "attempted_slots", c.AttemptedSlots());
    Field(out, "resolved_slots", c.ResolvedSlots()); Field(out, "successful_slots", c.SuccessfulSlots());
    Field(out, "diagnostic_failure_slots", c.DiagnosticFailureSlots()); Field(out, "unlaunched_slots", c.UnlaunchedSlots());
    Field(out, "fully_resolved_cells", c.FullyResolvedCells()); Field(out, "cells_with_diagnostic_failures", c.CellsWithDiagnosticFailures());
    out += ",\"cell_analysis_sha256\":[";
    for (std::size_t i = 0; i < c.CellAnalysisSha256().size(); ++i)
    { if (i) out += ','; out += Json(c.CellAnalysisSha256()[i]); }
    return out + "],\"claim_scope\":{\"cross_backend_performance_admitted\":false,\"stage2_authorized\":false,\"production_backend_selected\":false}}\n";
}
} // namespace computelab::ex2::stage6::analysis
