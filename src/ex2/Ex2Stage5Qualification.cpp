#include "ex2/Ex2Stage5Qualification.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace computelab::ex2::stage5
{
namespace
{

bool ValidBackend(Stage5Backend backend) noexcept
{
    return backend == Stage5Backend::Cuda || backend == Stage5Backend::Vulkan;
}

bool ValidProcess(const PlannedProcess& process) noexcept
{
    return ValidBackend(process.backend) && process.processIndex < ProcessesPerBackend
        && process.blockIndex == process.processIndex && process.orderSlot < 2U
        && PairedOrder[process.blockIndex][process.orderSlot] == process.backend;
}

std::vector<std::string> ValidateInput(const ProcessInput& input, Stage5Phase phase,
    std::size_t count)
{
    std::vector<std::string> reasons;
    if (!ValidateCondition(input.condition) || input.condition.phase != phase)
        reasons.emplace_back("invalid_condition");
    if (!ValidProcess(input.process)) reasons.emplace_back("invalid_process_identity");
    if (input.expectedObservationCount != count || input.observations.size() != count)
        reasons.emplace_back("invalid_observation_count");
    for (std::size_t i = 0U; i < input.observations.size(); ++i)
    {
        const auto& observation = input.observations[i];
        if (observation.sequenceIndex != i)
        {
            reasons.emplace_back("invalid_observation_sequence");
            break;
        }
    }
    for (const auto& observation : input.observations)
    {
        if (observation.status != OperationStatus::Ok || !observation.validationEligible
            || !observation.hostCompletionNanoseconds
            || *observation.hostCompletionNanoseconds == 0U)
        {
            reasons.emplace_back("invalid_observation_status_validation_or_timing");
            break;
        }
    }
    return reasons;
}

std::vector<std::uint64_t> Values(const ProcessInput& input)
{
    std::vector<std::uint64_t> values;
    values.reserve(input.observations.size());
    for (const auto& observation : input.observations)
        values.push_back(*observation.hostCompletionNanoseconds);
    return values;
}

MedianNanoseconds Difference(MedianNanoseconds a, MedianNanoseconds b) noexcept
{
    if (a < b) std::swap(a, b);
    auto whole = a.whole - b.whole;
    if (!a.half && b.half) --whole;
    return {whole, a.half != b.half};
}

// Exact abs(a/b-1) <= 1/divisor. Dividing first avoids uint64 multiplication
// overflow, cancellation in ratio-1, and any epsilon at the inclusive boundary.
bool Within(MedianNanoseconds a, MedianNanoseconds b, std::uint64_t divisor) noexcept
{
    const auto difference = Difference(a, b);
    const auto quotient = b.whole / divisor;
    if (difference.whole != quotient) return difference.whole < quotient;
    return (difference.half ? divisor : 0U)
        <= 2U * (b.whole % divisor) + (b.half ? 1U : 0U);
}

double RelativeDifference(MedianNanoseconds a, MedianNanoseconds b) noexcept
{
    return Difference(a, b).AsDouble() / b.AsDouble();
}

void AddGates(const ProcessInput& input, std::vector<std::string>& reasons)
{
    if (!input.hostClockResolutionAdequate)
        reasons.emplace_back("host_clock_resolution_inadequate");
    if (input.persistentTrendOrAbruptStateSwitch)
        reasons.emplace_back("persistent_trend_or_abrupt_state_switch");
}

template <typename T>
bool ValidSet(std::span<const T> processes, std::optional<Stage5Backend> backend)
{
    const std::size_t expected = backend ? ProcessesPerBackend : ChildrenPerGroup;
    if (processes.size() != expected || (backend && !ValidBackend(*backend))) return false;
    std::array<std::array<bool, ProcessesPerBackend>, 2> seen{};
    for (const auto& assessment : processes)
    {
        const auto& process = assessment.Process();
        if (!ValidProcess(process) || (backend && process.backend != *backend)
            || !assessment.InputValid()) return false;
        const std::size_t backendIndex = process.backend == Stage5Backend::Cuda ? 0U : 1U;
        if (seen[backendIndex][process.processIndex]) return false;
        seen[backendIndex][process.processIndex] = true;
    }
    return true; // Exact cardinality and uniqueness establish every required index.
}

// Stable reason order is backend then process index, independent of input order.
template <typename T>
void AddProcessReasons(std::vector<std::string>& reasons, std::span<const T> processes,
    std::string_view phase)
{
    for (const auto backend : {Stage5Backend::Cuda, Stage5Backend::Vulkan})
        for (std::size_t index = 0U; index < ProcessesPerBackend; ++index)
            for (const auto& process : processes)
                if (process.Process().backend == backend && process.Process().processIndex == index)
                    for (const auto& reason : process.Reasons())
                        reasons.push_back(std::string(phase) + ":"
                            + (backend == Stage5Backend::Cuda ? "cuda:" : "vulkan:")
                            + std::to_string(index) + ":" + reason);
}

template <typename T>
void AppendNumber(std::string& output, T value)
{
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) throw std::runtime_error("analysis number serialization failed");
    output.append(buffer.data(), end);
}

void AppendValue(std::string& output, bool value) { output += value ? "true" : "false"; }
void AppendValue(std::string& output, std::uint64_t value) { AppendNumber(output, value); }
void AppendValue(std::string& output, double value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("analysis numbers must be finite");
    AppendNumber(output, value);
}

// Reject malformed UTF-8 rather than publishing invalid JSON bytes. No locale.
bool ValidUtf8(std::string_view value)
{
    for (std::size_t i = 0U; i < value.size();)
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80U) continue;
        unsigned count{};
        std::uint32_t code{}, minimum{};
        if (first >= 0xC2U && first <= 0xDFU) { count = 1U; code = first & 0x1FU; minimum = 0x80U; }
        else if (first >= 0xE0U && first <= 0xEFU) { count = 2U; code = first & 0x0FU; minimum = 0x800U; }
        else if (first >= 0xF0U && first <= 0xF4U) { count = 3U; code = first & 0x07U; minimum = 0x10000U; }
        else return false;
        if (count > value.size() - i) return false;
        for (unsigned j = 0U; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0U) != 0x80U) return false;
            code = (code << 6U) | (next & 0x3FU);
        }
        if (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU))
            return false;
    }
    return true;
}

void AppendString(std::string& output, std::string_view value)
{
    if (!ValidUtf8(value)) throw std::invalid_argument("analysis strings must be UTF-8");
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value)
    {
        if (character == '"') output += "\\\"";
        else if (character == '\\') output += "\\\\";
        else if (character < 0x20U)
        {
            output += "\\u00";
            output.push_back(hex[character >> 4U]);
            output.push_back(hex[character & 0x0FU]);
        }
        else output.push_back(static_cast<char>(character));
    }
    output.push_back('"');
}

template <typename T>
void AppendOptional(std::string& output, const std::optional<T>& value)
{
    if (value) AppendValue(output, *value);
    else output += "null";
}

} // namespace

bool IsCandidateWarmup(std::uint64_t value) noexcept
{
    return std::find(CandidateWarmups.begin(), CandidateWarmups.end(), value) != CandidateWarmups.end();
}

bool ValidateCondition(const Condition& condition) noexcept
{
    if (condition.instrumentMode != InstrumentMode) return false;
    switch (condition.phase)
    {
    case Stage5Phase::A1Sentinel:
        return condition.workload == Stage5Workload::A1 && condition.elementCount == A1ElementCount
            && !condition.iterationCount && condition.diagnosticCount == DiagnosticObservationCount
            && !condition.selectedW && condition.measuredSampleCount == 0U;
    case Stage5Phase::D1Warmup:
        return condition.workload == Stage5Workload::D1 && condition.elementCount == D1ElementCount
            && condition.iterationCount == D1IterationCount
            && condition.diagnosticCount == DiagnosticObservationCount
            && !condition.selectedW && condition.measuredSampleCount == 0U;
    case Stage5Phase::D1Sample:
        return condition.workload == Stage5Workload::D1 && condition.elementCount == D1ElementCount
            && condition.iterationCount == D1IterationCount && condition.diagnosticCount == 0U
            && condition.selectedW && IsCandidateWarmup(*condition.selectedW)
            && condition.measuredSampleCount == SampleObservationCount;
    default: return false;
    }
}

std::array<PlannedProcess, ChildrenPerGroup> FrozenProcessPlan() noexcept
{
    std::array<PlannedProcess, ChildrenPerGroup> plan{};
    for (std::size_t block = 0U; block < PairedBlockCount; ++block)
        for (std::size_t slot = 0U; slot < 2U; ++slot)
            plan[2U * block + slot] = {PairedOrder[block][slot], block, block, slot};
    return plan;
}

bool ValidateProcessPlan(std::span<const PlannedProcess> processes) noexcept
{
    const auto expected = FrozenProcessPlan();
    return std::equal(processes.begin(), processes.end(), expected.begin(), expected.end());
}

double MedianNanoseconds::AsDouble() const noexcept
{
    return static_cast<double>(whole) + (half ? 0.5 : 0.0);
}

MedianNanoseconds Median(std::span<const std::uint64_t> values)
{
    if (values.empty()) throw std::invalid_argument("median requires observations");
    std::vector<std::uint64_t> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const auto middle = sorted.size() / 2U;
    if (sorted.size() % 2U != 0U) return {sorted[middle], false};
    const auto a = sorted[middle - 1U], b = sorted[middle];
    const auto remainder = a % 2U + b % 2U;
    return {a / 2U + b / 2U + remainder / 2U, remainder % 2U != 0U};
}

WarmupProcessAssessment AssessWarmupProcess(const ProcessInput& input)
{
    WarmupProcessAssessment result;
    result.process_ = input.process;
    result.reasons_ = ValidateInput(input, Stage5Phase::D1Warmup, DiagnosticObservationCount);
    if (!result.reasons_.empty()) return result;
    result.inputValid_ = true;
    const auto values = Values(input);
    const std::span<const std::uint64_t> timings{values};
    const auto reference = Median(timings.subspan(40U, 8U));
    const auto lateA = Median(timings.subspan(32U, 8U));
    for (const auto w : CandidateWarmups)
    {
        const auto earlyA = Median(timings.subspan(static_cast<std::size_t>(w), 8U));
        const auto earlyB = Median(timings.subspan(static_cast<std::size_t>(w) + 8U, 8U));
        const bool qualified = input.hostClockResolutionAdequate
            && !input.persistentTrendOrAbruptStateSwitch && Within(earlyA, reference, 20U)
            && Within(earlyB, reference, 20U) && Within(lateA, reference, 20U);
        result.candidates_.push_back({w, earlyA, earlyB, reference, lateA,
            RelativeDifference(earlyA, reference), RelativeDifference(earlyB, reference),
            RelativeDifference(lateA, reference), qualified});
        if (qualified && !result.selectedW_) result.selectedW_ = w;
    }
    AddGates(input, result.reasons_);
    if (!result.Qualified()) result.reasons_.emplace_back("no_candidate_warmup_qualified");
    return result;
}

CommonWarmupAssessment AssessCommonWarmup(std::span<const WarmupProcessAssessment> processes)
{
    CommonWarmupAssessment result;
    if (!ValidSet(processes, std::nullopt))
    {
        result.reasons.emplace_back("invalid_warmup_process_set");
        AddProcessReasons(result.reasons, processes, "warmup");
        return result;
    }
    result.inputValid = true;
    AddProcessReasons(result.reasons, processes, "warmup");
    std::uint64_t common = 0U;
    for (const auto& process : processes)
    {
        if (!process.Qualified()) return result;
        common = std::max(common, *process.SelectedW());
    }
    result.qualified = true;
    result.selectedCommonW = common;
    return result;
}

SampleCountProcessAssessment AssessSampleCountProcess(const ProcessInput& input)
{
    SampleCountProcessAssessment result;
    result.process_ = input.process;
    result.selectedW_ = input.condition.selectedW;
    result.reasons_ = ValidateInput(input, Stage5Phase::D1Sample, SampleObservationCount);
    if (!result.reasons_.empty()) return result;
    const auto values = Values(input);
    const std::span<const std::uint64_t> timings{values};
    SampleCountDiagnostics diagnostics;
    diagnostics.median50 = Median(timings.first(50U));
    diagnostics.median100 = Median(timings.first(100U));
    diagnostics.median200 = Median(timings);
    for (std::size_t i = 0U; i < 4U; ++i)
        diagnostics.windows[i] = Median(timings.subspan(i * 50U, 50U));
    diagnostics.prefix100RelativeDifference = RelativeDifference(diagnostics.median100, diagnostics.median200);
    diagnostics.prefix50RelativeDifference = RelativeDifference(diagnostics.median50, diagnostics.median200);
    diagnostics.firstLastRelativeDifference = RelativeDifference(diagnostics.windows[0], diagnostics.windows[3]);
    if (!Within(diagnostics.median100, diagnostics.median200, 50U))
        result.reasons_.emplace_back("prefix100_not_converged");
    if (!Within(diagnostics.median50, diagnostics.median200, 20U))
        result.reasons_.emplace_back("prefix50_not_converged");
    if (!Within(diagnostics.windows[0], diagnostics.windows[3], 20U))
        result.reasons_.emplace_back("first_last_window_drift");
    AddGates(input, result.reasons_);
    result.qualified_ = result.reasons_.empty();
    result.diagnostics_ = diagnostics;
    return result;
}

ProcessStabilityAssessment AssessProcessStability(Stage5Backend backend,
    std::span<const SampleCountProcessAssessment> processes)
{
    ProcessStabilityAssessment result;
    if (!ValidSet(processes, backend))
    {
        result.reasons.emplace_back("invalid_sample_process_set");
        return result;
    }
    result.inputValid = true;
    const auto w = processes.front().SelectedW();
    for (const auto& process : processes)
    {
        if (process.SelectedW() != w)
        {
            result.inputValid = false;
            result.reasons.emplace_back("inconsistent_sample_warmup");
            return result;
        }
    }
    for (const auto& process : processes)
        if (!process.Qualified())
        {
            result.reasons.emplace_back("sample_count_unqualified");
            return result;
        }
    auto minimum = processes.front().Diagnostics()->median100;
    auto maximum = minimum;
    for (const auto& process : processes)
    {
        const auto median = process.Diagnostics()->median100;
        minimum = std::min(minimum, median);
        maximum = std::max(maximum, median);
    }
    result.rProcess = maximum.AsDouble() / minimum.AsDouble();
    result.qualified = Within(maximum, minimum, 10U);
    if (!result.qualified) result.reasons.emplace_back("process_centers_exceed_1_10");
    return result;
}

D1QualificationAssessment AssessD1Qualification(std::span<const ProcessInput> warmup,
    std::optional<std::span<const ProcessInput>> samples)
{
    D1QualificationAssessment result;
    std::vector<WarmupProcessAssessment> warmupAssessments;
    for (const auto& process : warmup) warmupAssessments.push_back(AssessWarmupProcess(process));
    const auto common = AssessCommonWarmup(warmupAssessments);
    result.warmupInputValid = common.inputValid;
    result.d1WarmupQualified = common.qualified;
    result.selectedCommonW = common.selectedCommonW;
    result.reasons = common.reasons;
    result.sampleInputPresent = samples.has_value();
    if (!samples) return result;
    result.sampleInputValid = false;
    if (!common.qualified)
    {
        result.reasons.emplace_back("samples_require_qualified_common_warmup");
        return result;
    }
    std::vector<SampleCountProcessAssessment> sampleAssessments;
    for (const auto& process : *samples) sampleAssessments.push_back(AssessSampleCountProcess(process));
    if (!ValidSet(std::span<const SampleCountProcessAssessment>{sampleAssessments}, std::nullopt))
    {
        result.reasons.emplace_back("invalid_sample_process_set");
        AddProcessReasons(result.reasons, std::span<const SampleCountProcessAssessment>{sampleAssessments}, "sample");
        return result;
    }
    for (const auto& process : sampleAssessments)
        if (process.SelectedW() != common.selectedCommonW)
        {
            result.reasons.emplace_back("sample_warmup_does_not_match_common_w");
            return result;
        }
    result.sampleInputValid = true;
    result.d1SampleCountQualified = std::all_of(sampleAssessments.begin(), sampleAssessments.end(),
        [](const auto& process) { return process.Qualified(); });
    AddProcessReasons(result.reasons, std::span<const SampleCountProcessAssessment>{sampleAssessments}, "sample");
    for (const auto backend : {Stage5Backend::Cuda, Stage5Backend::Vulkan})
    {
        std::vector<SampleCountProcessAssessment> group;
        for (const auto& process : sampleAssessments)
            if (process.Process().backend == backend) group.push_back(process);
        const auto stability = AssessProcessStability(backend, group);
        const bool cuda = backend == Stage5Backend::Cuda;
        (cuda ? result.cudaRProcess : result.vulkanRProcess) = stability.rProcess;
        (cuda ? result.cudaProcessStabilityQualified : result.vulkanProcessStabilityQualified) = stability.qualified;
        for (const auto& reason : stability.reasons)
            result.reasons.push_back(std::string(cuda ? "cuda:" : "vulkan:") + reason);
    }
    result.d1ScopeQualified = *result.d1SampleCountQualified
        && result.cudaProcessStabilityQualified == true && result.vulkanProcessStabilityQualified == true;
    return result;
}

std::string SerializeAnalysisJson(const D1QualificationAssessment& assessment)
{
    if (assessment.analysisVersion != AnalysisSchemaVersion || assessment.protocolVersion != ProtocolVersion)
        throw std::invalid_argument("unsupported Stage-5 analysis version");
    std::string output{"{\"analysis_schema_version\":"};
    AppendNumber(output, assessment.analysisVersion);
    output += ",\"experiment_id\":\"EX-2\",\"protocol_version\":\"1.2\","
        "\"evidence_kind\":\"qualification\",\"analysis_kind\":\"d1-qualification\"";
    // Key order is part of analysis schema version 1.
    output += ",\"warmup_input_valid\":"; AppendValue(output, assessment.warmupInputValid);
    output += ",\"d1_warmup_qualified\":"; AppendValue(output, assessment.d1WarmupQualified);
    output += ",\"selected_common_w\":"; AppendOptional(output, assessment.selectedCommonW);
    output += ",\"sample_input_present\":"; AppendValue(output, assessment.sampleInputPresent);
    output += ",\"sample_input_valid\":"; AppendOptional(output, assessment.sampleInputValid);
    output += ",\"d1_sample_count_qualified\":"; AppendOptional(output, assessment.d1SampleCountQualified);
    output += ",\"cuda_r_process\":"; AppendOptional(output, assessment.cudaRProcess);
    output += ",\"cuda_process_stability_qualified\":"; AppendOptional(output, assessment.cudaProcessStabilityQualified);
    output += ",\"vulkan_r_process\":"; AppendOptional(output, assessment.vulkanRProcess);
    output += ",\"vulkan_process_stability_qualified\":"; AppendOptional(output, assessment.vulkanProcessStabilityQualified);
    output += ",\"d1_scope_qualified\":"; AppendValue(output, assessment.d1ScopeQualified);
    output += ",\"reasons\":[";
    for (std::size_t i = 0U; i < assessment.reasons.size(); ++i)
    {
        if (i != 0U) output.push_back(',');
        AppendString(output, assessment.reasons[i]);
    }
    output += "]}\n";
    return output;
}

} // namespace computelab::ex2::stage5
