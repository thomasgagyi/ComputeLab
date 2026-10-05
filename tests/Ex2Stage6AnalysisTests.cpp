#include "ex2/Ex2Stage6Analysis.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <locale>
#include <stdexcept>
#include <type_traits>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace ev = s6::evidence;
namespace an = s6::analysis;
using D = an::SlotDisposition;
an::ProcessInput Input(std::size_t sequence = 0, std::size_t count = 100, bool failure = false, std::uint64_t value = 30)
{
    const auto slot = s6::FrozenCampaignPlan()[sequence];
    ev::FoundationRequest request{slot, "analysis-run", "anonymous-machine", {"00112233-4455-6677-8899-aabbccddeeff", true},
        std::string(40, 'a'), std::string(64, 'b'), slot.process.backend == ex2::Backend::Vulkan ? std::optional<std::string>{std::string(64, 'c')} : std::nullopt};
    const auto& w = s6::WorkloadForCell(0); const auto words = ex2::GenerateWordInput(ex2::CoreInputSeed, 256);
    const auto f = ev::MakeWordFoundation(request, ex2::evidence::MakeI7WordInputIdentity(w, words), ex2::ReferenceA1(words));
    an::ProcessInput in; in.packageSha256 = std::string(64, 'd'); in.plan = f.Identity();
    for (std::size_t i = 0; i < count; ++i)
    {
        auto row = ev::MakeSampleRecord(f, i); row.status = ev::Status::Ok; row.correctness = {true, true, true, true, true};
        row.hostSubmissionNanoseconds = value; row.hostWaitNanoseconds = 0; row.hostCompletionNanoseconds = value;
        in.samples.push_back(row);
    }
    if (failure && !in.samples.empty())
    {
        auto& row = in.samples.back(); row.status = ev::Status::ValidationFailed; row.correctness.validationPassed = false;
        row.failurePhase = ev::FailurePhase::Validation; row.errorCode = "output_mismatch";
    }
    if (in.samples.empty()) in.summary = ev::SummarizeSamples(in.plan, {}, ev::Status::Incomplete,
        ev::FailurePhase::BackendInitialization, "backend_initialization_failed");
    else
    {
        const auto& row = in.samples.back(); in.summary = ev::SummarizeSamples(in.plan, in.samples, row.status, row.failurePhase, row.errorCode);
    }
    return in;
}
an::CellInput Cell(bool failure = false, std::size_t fatal = 10)
{
    an::CellInput in;
    for (std::size_t i = 0; i < 10; ++i)
    {
        if (i == fatal) in.slots[i].disposition = D::UnresolvedCampaignFatal;
        else if (i > fatal) in.slots[i].disposition = D::NotLaunched;
        else
        {
            const bool failed = failure && i == 0;
            in.slots[i] = {failed ? D::ResolvedDiagnosticFailure : D::ResolvedSuccess,
                an::DescribeProcess(Input(i, failed ? 18 : 100, failed))};
        }
    }
    return in;
}
an::CampaignInput Campaign(std::size_t fatal = 220)
{
    an::CampaignInput in;
    for (std::size_t i = 0; i < 220; ++i)
        in.slots[i] = i < fatal ? D::ResolvedSuccess : i == fatal ? D::UnresolvedCampaignFatal : D::NotLaunched;
    for (std::size_t i = 0; i < fatal / 10; ++i) in.cellAnalysisSha256[i] = std::string(64, 'a');
    return in;
}
TEST(Ex2Stage6AnalysisProcess, RegeneratesSummaryAndPreservesHashSeriesAndExactWindows)
{
    const auto in = Input(); const auto p = an::DescribeProcess(in);
    EXPECT_EQ(p.PackageSha256(), in.packageSha256); EXPECT_EQ(p.Plan().seriesId, in.plan.seriesId);
    EXPECT_EQ(p.TerminalState(), an::ProcessTerminalState::Success);
    for (std::size_t i = 0; i < 4; ++i)
    { EXPECT_EQ(p.Windows()[i].begin, i * 25); EXPECT_EQ(p.Windows()[i].count, 25); EXPECT_EQ(p.Windows()[i].successfulRowCount, 25); EXPECT_EQ(p.Windows()[i].hostCompletionMedianNanoseconds, 30); }
    auto bad = in; bad.summary.hostCompletionNanoseconds.mean = 1; EXPECT_THROW((void)an::DescribeProcess(bad), std::invalid_argument);
    bad = in; bad.packageSha256[0] = 'D'; EXPECT_THROW((void)an::DescribeProcess(bad), std::invalid_argument);
    bad = in; bad.plan.slot.sequenceIndex = 1; EXPECT_THROW((void)an::DescribeProcess(bad), std::invalid_argument);
}
TEST(Ex2Stage6AnalysisProcess, FailureDoesNotCompactAndExtremeSuccessRemains)
{
    const auto p = an::DescribeProcess(Input(0, 18, true, 1000000000));
    EXPECT_EQ(p.TerminalState(), an::ProcessTerminalState::DiagnosticFailure);
    EXPECT_EQ(p.Summary().successfulSampleCount, 17); EXPECT_EQ(p.Windows()[0].successfulRowCount, 17);
    EXPECT_EQ(p.Windows()[0].hostCompletionMedianNanoseconds, 1000000000);
    for (std::size_t i = 1; i < 4; ++i)
    { EXPECT_EQ(p.Windows()[i].successfulRowCount, 0); EXPECT_FALSE(p.Windows()[i].hostCompletionMedianNanoseconds); }
    const auto zero = an::DescribeProcess(Input(0, 0)); EXPECT_EQ(zero.Summary().recordedSampleCount, 0);
    auto in = Input(0, 51, true); in.samples[50].hostSubmissionNanoseconds = 999999999;
    in.samples[50].hostCompletionNanoseconds = 999999999;
    in.summary = ev::SummarizeSamples(in.plan, in.samples, ev::Status::ValidationFailed, ev::FailurePhase::Validation, "output_mismatch");
    const auto terminal = an::DescribeProcess(in); EXPECT_EQ(terminal.Windows()[2].successfulRowCount, 0);
    EXPECT_FALSE(terminal.Windows()[2].hostCompletionMedianNanoseconds);
}
TEST(Ex2Stage6AnalysisBackend, FiveExactPositionsAndThresholdFreeHostCompletionSpan)
{
    const auto plan = s6::FrozenCellProcessPlan(); std::array<std::optional<an::ProcessDescription>, 5> positions;
    for (std::size_t i = 0; i < 10; ++i)
        if (plan[i].backend == ex2::Backend::Cuda) positions[plan[i].processIndex] = an::DescribeProcess(Input(i, 100, false, (plan[i].processIndex + 1) * 10));
    const auto b = an::DescribeBackend(0, ex2::Backend::Cuda, positions);
    EXPECT_EQ(b.resolvedProcessCount, 5); EXPECT_EQ(b.successfulProcessCount, 5); EXPECT_EQ(b.processMedianSpanRatio, 5);
    auto missing = positions; missing[2].reset(); EXPECT_FALSE(an::DescribeBackend(0, ex2::Backend::Cuda, missing).processMedianSpanRatio);
    auto failed = positions; failed[0] = an::DescribeProcess(Input(0, 18, true)); EXPECT_FALSE(an::DescribeBackend(0, ex2::Backend::Cuda, failed).processMedianSpanRatio);
    auto zero = positions; zero[0] = an::DescribeProcess(Input(0, 100, false, 0)); EXPECT_FALSE(an::DescribeBackend(0, ex2::Backend::Cuda, zero).processMedianSpanRatio);
    auto misplaced = positions; std::swap(misplaced[0], misplaced[1]); EXPECT_THROW((void)an::DescribeBackend(0, ex2::Backend::Cuda, misplaced), std::invalid_argument);
    EXPECT_THROW((void)an::DescribeBackend(0, ex2::Backend::Vulkan, positions), std::invalid_argument);
}
TEST(Ex2Stage6AnalysisCell, CompleteFailureAndPartialFatalCountsAndCanonicalPlacement)
{
    const auto all = an::DescribeCell(Cell()); EXPECT_EQ(all.Status(), an::CoverageStatus::Complete); EXPECT_EQ(all.SuccessfulSlotCount(), 10);
    const auto failed = an::DescribeCell(Cell(true)); EXPECT_EQ(failed.Status(), an::CoverageStatus::Complete);
    EXPECT_EQ(failed.ResolvedSlotCount(), 10); EXPECT_EQ(failed.SuccessfulSlotCount(), 9); EXPECT_EQ(failed.DiagnosticFailureSlotCount(), 1);
    EXPECT_FALSE(failed.Cuda().processMedianSpanRatio); EXPECT_EQ(failed.Vulkan().processMedianSpanRatio, 1);
    const auto partial = an::DescribeCell(Cell(false, 3)); EXPECT_EQ(partial.Status(), an::CoverageStatus::Incomplete);
    EXPECT_EQ(partial.ResolvedSlotCount(), 3); EXPECT_EQ(partial.Cuda().resolvedProcessCount, 1); EXPECT_EQ(partial.Vulkan().resolvedProcessCount, 2);
    EXPECT_FALSE(partial.Cuda().processes[1]); EXPECT_FALSE(partial.Vulkan().processes[2]);
    auto misplaced = Cell(); std::swap(misplaced.slots[0], misplaced.slots[1]); EXPECT_THROW((void)an::DescribeCell(misplaced), std::invalid_argument);
    misplaced = Cell(); misplaced.cellIndex = 1; EXPECT_THROW((void)an::DescribeCell(misplaced), std::invalid_argument);
    misplaced = Cell(); misplaced.slots[0].disposition = D::ResolvedDiagnosticFailure; EXPECT_THROW((void)an::DescribeCell(misplaced), std::invalid_argument);
}
TEST(Ex2Stage6AnalysisCampaign, CompleteMeansResolvedIncludingDiagnosticFailures)
{
    auto in = Campaign(); const auto success = an::DescribeCampaign(in);
    EXPECT_EQ(success.Status(), an::CoverageStatus::Complete); EXPECT_EQ(success.AttemptedSlots(), 220);
    EXPECT_EQ(success.ResolvedSlots(), 220); EXPECT_EQ(success.SuccessfulSlots(), 220); EXPECT_EQ(success.FullyResolvedCells(), 22);
    in.slots[0] = D::ResolvedDiagnosticFailure; in.slots[219] = D::ResolvedDiagnosticFailure;
    const auto failure = an::DescribeCampaign(in); EXPECT_EQ(failure.Status(), an::CoverageStatus::Complete);
    EXPECT_EQ(failure.SuccessfulSlots(), 218); EXPECT_EQ(failure.DiagnosticFailureSlots(), 2); EXPECT_EQ(failure.CellsWithDiagnosticFailures(), 2);
}
TEST(Ex2Stage6AnalysisCampaign, FatalAttemptedNotResolvedAndPartialCellHashOptional)
{
    for (const auto fatal : {0U, 10U, 17U, 219U})
    {
        auto in = Campaign(fatal); const auto c = an::DescribeCampaign(in);
        EXPECT_EQ(c.Status(), an::CoverageStatus::Incomplete); EXPECT_EQ(c.AttemptedSlots(), fatal + 1);
        EXPECT_EQ(c.ResolvedSlots(), fatal); EXPECT_EQ(c.UnlaunchedSlots(), 219 - fatal); EXPECT_EQ(c.FullyResolvedCells(), fatal / 10);
        in.cellAnalysisSha256[fatal / 10] = std::string(64, 'b'); EXPECT_NO_THROW((void)an::DescribeCampaign(in));
    }
}
TEST(Ex2Stage6AnalysisCampaign, RejectsControlOrderingHashAndUndefinedStateMutations)
{
    for (unsigned defect = 0; defect < 9; ++defect)
    {
        auto in = Campaign(17);
        if (defect == 0) in.slots[2] = D::NotLaunched;
        if (defect == 1) in.slots[18] = D::ResolvedSuccess;
        if (defect == 2) in.slots[18] = D::UnresolvedCampaignFatal;
        if (defect == 3) in.cellAnalysisSha256[0].reset();
        if (defect == 4) in.cellAnalysisSha256[2] = std::string(64, 'b');
        if (defect == 5) in.cellAnalysisSha256[0] = std::string(64, 'A');
        if (defect == 6) in.slots[0] = static_cast<D>(99);
        if (defect == 7) in.slots[17] = D::NotLaunched;
        if (defect == 8) { in = Campaign(); in.cellAnalysisSha256[21].reset(); }
        EXPECT_THROW((void)an::DescribeCampaign(in), std::invalid_argument);
    }
}
TEST(Ex2Stage6AnalysisSerialization, DeterministicFiniteLocaleAndClaimFirewall)
{
    const auto cell = an::DescribeCell(Cell()); const auto campaign = an::DescribeCampaign(Campaign());
    const auto before = an::SerializeCellJson(cell); const auto coverage = an::SerializeCampaignJson(campaign);
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto previous = std::locale(); struct Restore { std::locale old; ~Restore() { std::locale::global(old); } } restore{previous};
    std::locale::global(std::locale(previous, new Comma));
    EXPECT_EQ(an::SerializeCellJson(cell), before); EXPECT_EQ(an::SerializeCampaignJson(campaign), coverage);
    EXPECT_EQ(before.back(), '\n'); EXPECT_EQ(before[before.size() - 2], '}');
    EXPECT_EQ(coverage.back(), '\n'); EXPECT_EQ(coverage[coverage.size() - 2], '}');
    EXPECT_NE(before.find("\"package_sha256\":\"" + std::string(64, 'd') + '"'), std::string::npos);
    EXPECT_NE(coverage.find("\"claim_scope\":{\"cross_backend_performance_admitted\":false,\"stage2_authorized\":false,\"production_backend_selected\":false}"), std::string::npos);
    for (const auto key : {"speedup", "winner", "loser", "rank", "cuda_over_vulkan", "vulkan_over_cuda", "paired_ratio", "percent_difference", "aggregate_backend_score", "gate0_pass", "gate0_fail", "timestamp", "path", "hostname", "supervisor", "native_device_interval_ns"})
    { EXPECT_EQ(before.find(key), std::string::npos); EXPECT_EQ(coverage.find(key), std::string::npos); }
    auto in = Input(); in.summary.hostCompletionNanoseconds.mean = std::numeric_limits<double>::infinity();
    EXPECT_THROW((void)an::DescribeProcess(in), std::invalid_argument);
}
} // namespace

namespace {
TEST(Ex2Stage6AnalysisSerialization, LiteralCompleteCell) {
const std::string expected = R"fixture({"analysis_version":1,"protocol_version":"1.3","analysis_kind":"stage6-cell-diagnostic","cell_index":0,"workload":{"workload":"A","variant":"A1","seed":81985529216486895,"element_count":256,"byte_count":null,"index_pattern":null,"counter_count":null,"iteration_count":null,"transfer_direction":null,"execution_mode":"ordinary","instrument_mode":"H"},"status":"complete","resolved_slot_count":10,"successful_slot_count":10,"diagnostic_failure_slot_count":0,"backends":{"cuda":{"resolved_process_count":5,"successful_process_count":5,"diagnostic_failure_process_count":0,"process_median_span_ratio":1,"processes":[{"slot_sequence_index":0,"block_index":0,"order_slot":0,"process_index":0,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"c23fc6fa4c70cfa5dcfdde7f35d279a3f48c08225d4b43c5b21cbe3c62b7af39","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":nu)fixture"
R"fixture(ll,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":3,"block_index":1,"order_slot":1,"process_index":1,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"03a2f527ca154f4639aa8d0d5cd978ca702018ec3c5fdaac86d5b)fixture"
R"fixture(7d241fb92d7","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":4,"block_index":2,"order_slot":0,"pro)fixture"
R"fixture(cess_index":2,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"3999eb93693e7533077a03c3a3eca5d30983cb5ccfafb82722472665622c80ed","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_compl)fixture"
R"fixture(etion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":7,"block_index":3,"order_slot":1,"process_index":3,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"9f68df6194569a6a22df1f8a70526876d2ab881ab798c978354313eb1886b9c3","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_com)fixture"
R"fixture(pletion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":8,"block_index":4,"order_slot":0,"process_index":4,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"98963e6d168b34c8d113d8c4a62b291574eeac2f45ae63a514d92513655b6591","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"m)fixture"
R"fixture(inimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]}]},"vulkan":{"resolved_process_count":5,"successful_process_count":5,"diagnostic_failure_process_count":0,"process_median_span_ratio":1,"processes":[{"slot_sequence_index":1,"block_index":0,"order_slot":1,"process_index":0,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"d23682a962d7e6272e79d010af80fa970bc28638d9fa0efadb7b5f0be6707d46","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{)fixture"
R"fixture("host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":2,"block_index":1,"order_slot":0,"process_index":1,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"b80db49dba3ef41bc1c092f6a1721c6b22fba065f024452ece17a17de8144af1","terminal_state":"s)fixture"
R"fixture(uccess","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":5,"block_index":2,"order_slot":1,"process_index":2,"package_sha256":")fixture"
R"fixture(dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"ea71a04777d2112f9e720ad73cfd097d41f40f9539f3764326652eb4e13770f9","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75)fixture"
R"fixture(,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":6,"block_index":3,"order_slot":0,"process_index":3,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"acef4896584fc720114d8763fe089380dcf5580e697d9267b22ae3e4ad03aa59","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":)fixture"
R"fixture(25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":9,"block_index":4,"order_slot":1,"process_index":4,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"68a37cf2b30d8da4f0124b6ca06211f671d71420a6d6f21034eafb6293bc9448","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30)fixture"
R"fixture(,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]}]}}})fixture";
EXPECT_EQ(an::SerializeCellJson(an::DescribeCell(Cell())), expected + "\n");
}
TEST(Ex2Stage6AnalysisSerialization, LiteralIncompleteCell) {
const std::string expected = R"fixture({"analysis_version":1,"protocol_version":"1.3","analysis_kind":"stage6-cell-diagnostic","cell_index":0,"workload":{"workload":"A","variant":"A1","seed":81985529216486895,"element_count":256,"byte_count":null,"index_pattern":null,"counter_count":null,"iteration_count":null,"transfer_direction":null,"execution_mode":"ordinary","instrument_mode":"H"},"status":"incomplete","resolved_slot_count":3,"successful_slot_count":3,"diagnostic_failure_slot_count":0,"backends":{"cuda":{"resolved_process_count":1,"successful_process_count":1,"diagnostic_failure_process_count":0,"process_median_span_ratio":null,"processes":[{"slot_sequence_index":0,"block_index":0,"order_slot":0,"process_index":0,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"c23fc6fa4c70cfa5dcfdde7f35d279a3f48c08225d4b43c5b21cbe3c62b7af39","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase")fixture"
R"fixture(:null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},null,null,null,null]},"vulkan":{"resolved_process_count":2,"successful_process_count":2,"diagnostic_failure_process_count":0,"process_median_span_ratio":null,"processes":[{"slot_sequence_index":1,"block_index":0,"order_sl)fixture"
R"fixture(ot":1,"process_index":0,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"d23682a962d7e6272e79d010af80fa970bc28638d9fa0efadb7b5f0be6707d46","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,")fixture"
R"fixture(host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},{"slot_sequence_index":2,"block_index":1,"order_slot":0,"process_index":1,"package_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","series_id":"b80db49dba3ef41bc1c092f6a1721c6b22fba065f024452ece17a17de8144af1","terminal_state":"success","recorded_sample_count":100,"successful_sample_count":100,"validation_failures":0,"failed_sample_count":0,"failure_phase":null,"error_code":null,"metrics":{"host_submission_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30},"host_wait_ns":{"sample_count":100,"minimum":0,"median":0,"mean":0,"standard_deviation":0,"coefficient_of_variation":null,"p95":0},"host_completion_ns":{"sample_count":100,"minimum":30,"median":30,"mean":30,"standard_deviation":0,"coefficient_of_variation":0,"p95":30}},"windows":[{"begin":0,"count":25,"successful_row_count":25)fixture"
R"fixture(,"host_completion_median_ns":30},{"begin":25,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":50,"count":25,"successful_row_count":25,"host_completion_median_ns":30},{"begin":75,"count":25,"successful_row_count":25,"host_completion_median_ns":30}]},null,null,null]}}})fixture";
EXPECT_EQ(an::SerializeCellJson(an::DescribeCell(Cell(false, 3))), expected + "\n");
}
TEST(Ex2Stage6AnalysisSerialization, LiteralCompleteCampaign) {
const std::string expected = R"fixture({"analysis_version":1,"protocol_version":"1.3","analysis_kind":"stage6-campaign-diagnostic","status":"complete","declared_slots":220,"attempted_slots":220,"resolved_slots":220,"successful_slots":220,"diagnostic_failure_slots":0,"unlaunched_slots":0,"fully_resolved_cells":22,"cells_with_diagnostic_failures":0,"cell_analysis_sha256":["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa)fixture"
R"fixture(aa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"],"claim_scope":{"cross_backend_performance_admitted":false,"stage2_authorized":false,"production_backend_selected":false}})fixture";
EXPECT_EQ(an::SerializeCampaignJson(an::DescribeCampaign(Campaign())), expected + "\n");
}
TEST(Ex2Stage6AnalysisSerialization, LiteralIncompleteCampaign) {
const std::string expected = R"fixture({"analysis_version":1,"protocol_version":"1.3","analysis_kind":"stage6-campaign-diagnostic","status":"incomplete","declared_slots":220,"attempted_slots":18,"resolved_slots":17,"successful_slots":17,"diagnostic_failure_slots":0,"unlaunched_slots":202,"fully_resolved_cells":1,"cells_with_diagnostic_failures":0,"cell_analysis_sha256":["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null],"claim_scope":{"cross_backend_performance_admitted":false,"stage2_authorized":false,"production_backend_selected":false}})fixture";
EXPECT_EQ(an::SerializeCampaignJson(an::DescribeCampaign(Campaign(17))), expected + "\n");
}
TEST(Ex2Stage6AnalysisBoundary, DescriptionsExposeOnlyConstCalculatedFacts)
{
    static_assert(!std::is_default_constructible_v<an::ProcessDescription>);
    static_assert(!std::is_default_constructible_v<an::CellDescription>);
    static_assert(!std::is_default_constructible_v<an::CampaignDescription>);
    const auto cell = an::DescribeCell(Cell(false, 0));
    EXPECT_EQ(cell.ResolvedSlotCount(), 0);
    an::CellInput untouched; untouched.cellIndex = 21;
    EXPECT_EQ(an::DescribeCell(untouched).Status(), an::CoverageStatus::Incomplete);
}
TEST(Ex2Stage6AnalysisBackend, SpanUsesCompletionMediansOnly)
{
    const auto slots = s6::FrozenCellProcessPlan();
    std::array<std::optional<an::ProcessDescription>, 5> positions;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i].backend != ex2::Backend::Cuda) continue;
        auto input = Input(i);
        for (auto& row : input.samples)
        {
            row.hostSubmissionNanoseconds = slots[i].processIndex + 1;
            row.hostWaitNanoseconds = 30 - *row.hostSubmissionNanoseconds;
        }
        input.summary = ev::SummarizeSamples(input.plan, input.samples, ev::Status::Ok);
        positions[slots[i].processIndex] = an::DescribeProcess(input);
    }
    EXPECT_EQ(an::DescribeBackend(0, ex2::Backend::Cuda, positions).processMedianSpanRatio, 1);
}
TEST(Ex2Stage6AnalysisSerialization, AssignmentOrderIsIrrelevantAndEveryWorkloadIsRegenerated)
{
    const auto forward = Cell(false, 3); an::CellInput reverse;
    for (std::size_t i = 10; i != 0; --i) reverse.slots[i - 1] = forward.slots[i - 1];
    EXPECT_EQ(an::SerializeCellJson(an::DescribeCell(forward)), an::SerializeCellJson(an::DescribeCell(reverse)));
    const auto campaign = Campaign(); an::CampaignInput reverseCampaign;
    for (std::size_t i = 220; i != 0; --i) reverseCampaign.slots[i - 1] = campaign.slots[i - 1];
    for (std::size_t i = 22; i != 0; --i) reverseCampaign.cellAnalysisSha256[i - 1] = campaign.cellAnalysisSha256[i - 1];
    EXPECT_EQ(an::SerializeCampaignJson(an::DescribeCampaign(campaign)), an::SerializeCampaignJson(an::DescribeCampaign(reverseCampaign)));
    for (std::size_t cell = 0; cell < 22; ++cell)
    {
        an::CellInput untouched; untouched.cellIndex = cell;
        const auto json = an::SerializeCellJson(an::DescribeCell(untouched));
        EXPECT_NE(json.find("\"workload\":" + ev::detail::WorkloadJson(ex2::ApprovedCoreCells()[cell])), std::string::npos);
    }
}
} // namespace
