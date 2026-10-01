#include "ex2/Ex2Stage5Qualification.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <locale>
#include <type_traits>

namespace
{
namespace s5 = computelab::ex2::stage5;
using Phase = s5::Stage5Phase;
using Workload = s5::Stage5Workload;
using Backend = s5::Stage5Backend;

s5::Condition Condition(Phase phase, std::uint64_t w = 0U)
{
    if (phase == Phase::A1Sentinel)
        return {phase, Workload::A1, 256U, std::nullopt, "H", 48U, std::nullopt, 0U};
    if (phase == Phase::D1Warmup)
        return {phase, Workload::D1, 1'048'576U, 64U, "H", 48U, std::nullopt, 0U};
    return {phase, Workload::D1, 1'048'576U, 64U, "H", 0U, w, 200U};
}

s5::ProcessInput Input(Phase phase = Phase::D1Warmup, std::size_t planIndex = 0U,
    std::uint64_t value = 1000U, std::uint64_t w = 0U)
{
    s5::ProcessInput input;
    input.condition = Condition(phase, w);
    input.process = s5::FrozenProcessPlan()[planIndex];
    input.expectedObservationCount = phase == Phase::D1Sample ? 200U : 48U;
    input.clockCalibration.deltasNanoseconds.assign(s5::ClockDeltaCount, 1U);
    for (std::uint64_t i = 0U; i < input.expectedObservationCount; ++i)
        input.observations.push_back({i, s5::OperationStatus::Ok, true, value});
    return input;
}

void Fill(s5::ProcessInput& input, std::size_t begin, std::size_t end, std::uint64_t value)
{
    for (std::size_t i = begin; i < end; ++i)
        input.observations[i].hostCompletionNanoseconds = value;
}

void CoarseClock(s5::ProcessInput& input)
{
    input.clockCalibration.deltasNanoseconds.assign(s5::ClockDeltaCount, 11U);
}

void StateSwitch(s5::ProcessInput& input)
{
    if (input.condition.phase == Phase::D1Sample) Fill(input, 50U, 100U, 1200U);
    else Fill(input, 40U, 48U, 1200U);
}

s5::ProcessInput WarmupFor(std::uint64_t w, std::size_t planIndex = 0U)
{
    auto input = Input(Phase::D1Warmup, planIndex);
    if (w != 0U) Fill(input, 0U, static_cast<std::size_t>(w) + 3U, 2000U);
    return input;
}

std::vector<s5::ProcessInput> Group(Phase phase, std::uint64_t w = 0U)
{
    std::vector<s5::ProcessInput> group;
    for (std::size_t i = 0U; i < 10U; ++i) group.push_back(Input(phase, i, 1000U, w));
    return group;
}

std::vector<s5::WarmupProcessAssessment> Warmups(const std::vector<s5::ProcessInput>& inputs)
{
    std::vector<s5::WarmupProcessAssessment> assessments;
    for (const auto& input : inputs) assessments.push_back(s5::AssessWarmupProcess(input));
    return assessments;
}

std::vector<s5::SampleCountProcessAssessment> Samples(Backend backend,
    std::uint64_t high = 1000U)
{
    auto inputs = Group(Phase::D1Sample);
    std::vector<s5::SampleCountProcessAssessment> assessments;
    for (auto& input : inputs)
        if (input.process.backend == backend)
        {
            if (input.process.processIndex == 4U) Fill(input, 0U, 200U, high);
            assessments.push_back(s5::AssessSampleCountProcess(input));
        }
    return assessments;
}

bool HasReason(const std::vector<std::string>& reasons, std::string_view reason)
{
    return std::find(reasons.begin(), reasons.end(), reason) != reasons.end();
}

// Independently specified schema-2 golden operational diagnostics for the
// constant 1000 ns fixtures, including every frozen window and process.
std::string GoldenFacts(std::size_t candidate, bool adequate = true)
{
    const std::array<std::array<unsigned, 4>, 6> lengths{{
        {12,12,12,12}, {12,12,12,11}, {12,12,11,11},
        {11,11,11,11}, {10,10,10,10}, {8,8,8,8}}};
    const std::array<unsigned, 6> starts{0,1,2,4,8,16};
    std::string output = "{\"decision_scale\":{\"whole_ns\":1000,\"half_ns\":false},\"clock_adequate\":";
    output += adequate ? "true" : "false";
    output += ",\"windows\":[";
    unsigned begin = candidate == 6U ? 0U : starts[candidate];
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        if (i != 0U) output.push_back(',');
        const auto count = candidate == 6U ? 50U : lengths[candidate][i];
        output += "{\"begin\":" + std::to_string(begin) + ",\"count\":" + std::to_string(count)
            + ",\"median\":{\"whole_ns\":1000,\"half_ns\":false}}";
        begin += count;
    }
    return output + "],\"persistent_trend\":false,\"abrupt_state_switch\":false,\"state_structure\":false}";
}

std::string GoldenProcesses(bool samples = false, bool coarseFirst = false)
{
    const std::array<unsigned, 6> warmups{0,1,2,4,8,16};
    std::string output = "[";
    for (const auto backend : {"cuda", "vulkan"})
        for (unsigned i = 0U; i < 5U; ++i)
        {
            if (output.size() != 1U) output.push_back(',');
            const bool coarse = coarseFirst && std::string_view(backend) == "cuda" && i == 0U;
            output += "{\"backend\":\"" + std::string(backend) + "\",\"process_index\":" + std::to_string(i)
                + ",\"input_valid\":true,\"clock_calibration_valid\":true,\"effective_step_ns\":";
            output += coarse ? "11" : "1";
            if (samples) output += ",\"operational_facts\":" + GoldenFacts(6U);
            else
            {
                output += ",\"candidates\":[";
                for (std::size_t j = 0U; j < 6U; ++j)
                {
                    if (j != 0U) output.push_back(',');
                    output += "{\"w\":" + std::to_string(warmups[j]) + ",\"qualified\":";
                    output += coarse ? "false" : "true";
                    output += ",\"operational_facts\":" + GoldenFacts(j, !coarse) + "}";
                }
                output.push_back(']');
            }
            output.push_back('}');
        }
    return output + "]";
}

TEST(Ex2Stage5QualificationPlan, FrozenVocabulary)
{
    EXPECT_EQ(s5::SchemaVersion, 2U);
    EXPECT_EQ(s5::AnalysisSchemaVersion, 2U);
    EXPECT_EQ(s5::ExperimentId, "EX-2");
    EXPECT_EQ(s5::ProtocolVersion, "1.2");
    EXPECT_EQ(s5::EvidenceKind, "qualification");
    EXPECT_EQ(s5::InstrumentMode, "H");
    EXPECT_EQ(s5::PairedBlockCount, 5U);
    EXPECT_EQ(s5::ProcessesPerBackend, 5U);
    EXPECT_EQ(s5::ChildrenPerGroup, 10U);
    EXPECT_EQ(s5::DiagnosticObservationCount, 48U);
    EXPECT_EQ(s5::SampleObservationCount, 200U);
    EXPECT_EQ(s5::AdmittedOrdinarySampleCount, 100U);
    EXPECT_EQ(s5::A1ElementCount, 256U);
    EXPECT_EQ(s5::D1ElementCount, 1'048'576U);
    EXPECT_EQ(s5::D1IterationCount, 64U);
    EXPECT_EQ(s5::CandidateWarmups, (std::array<std::uint64_t, 6>{0, 1, 2, 4, 8, 16}));
}

TEST(Ex2Stage5QualificationPlan, AcceptsExactlyThreeShapesAndFrozenWarmups)
{
    EXPECT_TRUE(s5::ValidateCondition(Condition(Phase::A1Sentinel)));
    EXPECT_TRUE(s5::ValidateCondition(Condition(Phase::D1Warmup)));
    for (std::uint64_t w = 0U; w <= 65U; ++w)
    {
        const bool expected = w == 0U || w == 1U || w == 2U || w == 4U || w == 8U || w == 16U;
        EXPECT_EQ(s5::IsCandidateWarmup(w), expected) << w;
        EXPECT_EQ(s5::ValidateCondition(Condition(Phase::D1Sample, w)), expected) << w;
    }
}

TEST(Ex2Stage5QualificationPlan, RejectsMixedAndWidenedShapes)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto valid = Condition(phase);
        auto invalid = valid; ++invalid.elementCount; EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.workload = phase == Phase::A1Sentinel ? Workload::D1 : Workload::A1;
        EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.iterationCount = phase == Phase::A1Sentinel ? 0U : 63U;
        EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.iterationCount.reset();
        EXPECT_EQ(s5::ValidateCondition(invalid), phase == Phase::A1Sentinel);
        for (const auto mode : {"N", "P", "", "h"})
        { invalid = valid; invalid.instrumentMode = mode; EXPECT_FALSE(s5::ValidateCondition(invalid)); }
        invalid = valid; ++invalid.diagnosticCount; EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.measuredSampleCount = phase == Phase::D1Sample ? 100U : 200U;
        EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.selectedW = phase == Phase::D1Sample ? std::nullopt : std::optional<std::uint64_t>{0U};
        EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.phase = static_cast<Phase>(3); EXPECT_FALSE(s5::ValidateCondition(invalid));
        invalid = valid; invalid.workload = static_cast<Workload>(2); EXPECT_FALSE(s5::ValidateCondition(invalid));
    }
    for (const auto count : {0U, 100U, 201U, 400U})
    { auto invalid = Condition(Phase::D1Sample); invalid.measuredSampleCount = count;
      EXPECT_FALSE(s5::ValidateCondition(invalid)); }
}

TEST(Ex2Stage5QualificationPlan, ExactFiveBlockCounterbalancedOrder)
{
    const std::array<Backend, 10> backends{Backend::Cuda, Backend::Vulkan, Backend::Vulkan,
        Backend::Cuda, Backend::Cuda, Backend::Vulkan, Backend::Vulkan, Backend::Cuda,
        Backend::Cuda, Backend::Vulkan};
    const auto plan = s5::FrozenProcessPlan();
    ASSERT_TRUE(s5::ValidateProcessPlan(plan));
    for (std::size_t i = 0U; i < plan.size(); ++i)
    {
        EXPECT_EQ(plan[i].backend, backends[i]);
        EXPECT_EQ(plan[i].processIndex, i / 2U);
        EXPECT_EQ(plan[i].blockIndex, i / 2U);
        EXPECT_EQ(plan[i].orderSlot, i % 2U);
    }
    auto invalid = plan; std::swap(invalid[0], invalid[1]);
    EXPECT_FALSE(s5::ValidateProcessPlan(invalid));
    EXPECT_FALSE(s5::ValidateProcessPlan(std::span{plan}.first(9U)));
    auto extension = std::vector(plan.begin(), plan.end()); extension.push_back(plan[0]);
    EXPECT_FALSE(s5::ValidateProcessPlan(extension));
}

TEST(Ex2Stage5QualificationMedian, PreservesHalfNanosecondsAndInputOrder)
{
    const std::array<std::uint64_t, 4> values{4U, 1U, 3U, 2U};
    EXPECT_EQ(s5::Median(values), (s5::MedianNanoseconds{2U, true}));
    EXPECT_DOUBLE_EQ(s5::Median(values).AsDouble(), 2.5);
    EXPECT_EQ(values[0], 4U);
    EXPECT_EQ(s5::Median(std::array<std::uint64_t, 3>{9U, 1U, 3U}),
        (s5::MedianNanoseconds{3U, false}));
    EXPECT_THROW((void)s5::Median({}), std::invalid_argument);
}

TEST(Ex2Stage5QualificationMedian, NearUint64MaximumDoesNotOverflowOrTruncate)
{
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(s5::Median(std::array{max, max}), (s5::MedianNanoseconds{max, false}));
    EXPECT_EQ(s5::Median(std::array{max - 1U, max}), (s5::MedianNanoseconds{max - 1U, true}));
    EXPECT_EQ(s5::Median(std::array{std::uint64_t{0}, max}),
        (s5::MedianNanoseconds{max / 2U, true}));
    EXPECT_TRUE(s5::AssessWarmupProcess(Input(Phase::D1Warmup, 0U, max)).Qualified());
    EXPECT_TRUE(s5::AssessSampleCountProcess(Input(Phase::D1Sample, 0U, max)).Qualified());
}

TEST(Ex2Stage5QualificationWarmup, RequiresExactly48AndDeclaredCount)
{
    for (const auto size : {0U, 47U, 49U, 200U})
    { auto input = Input(); input.observations.resize(size);
      EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid()); }
    auto input = Input(); input.expectedObservationCount = 47U;
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
}

TEST(Ex2Stage5QualificationWarmup, RejectsUnorderedDuplicateAndMissingSequences)
{
    auto input = Input(); std::swap(input.observations[0], input.observations[1]);
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
    input = Input(); input.observations[1].sequenceIndex = 0U;
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
    input = Input(); input.observations.back().sequenceIndex = 48U;
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
}

TEST(Ex2Stage5QualificationWarmup, RejectsEveryNonOkStatusAndIneligibleValidation)
{
    for (int status = 1; status <= 8; ++status)
    { auto input = Input(); input.observations[20].status = static_cast<s5::OperationStatus>(status);
      EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid()); }
    auto input = Input(); input.observations[20].validationEligible = false;
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
}

TEST(Ex2Stage5QualificationWarmup, RejectsMissingAndZeroTimingWithoutRescue)
{
    auto input = Input(); input.observations[47].hostCompletionNanoseconds.reset();
    const auto missing = s5::AssessWarmupProcess(input);
    EXPECT_FALSE(missing.InputValid()); EXPECT_TRUE(missing.Candidates().empty());
    input = Input(); input.observations[0].hostCompletionNanoseconds = 0U;
    EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
}

TEST(Ex2Stage5QualificationWarmup, RejectsWrongPhaseAndProcessIdentity)
{
    EXPECT_FALSE(s5::AssessWarmupProcess(Input(Phase::A1Sentinel)).InputValid());
    EXPECT_FALSE(s5::AssessWarmupProcess(Input(Phase::D1Sample)).InputValid());
    for (int mutation = 0; mutation < 4; ++mutation)
    {
        auto input = Input();
        if (mutation == 0) input.process.backend = static_cast<Backend>(2);
        if (mutation == 1) input.process.processIndex = 5U;
        if (mutation == 2) input.process.blockIndex = 1U;
        if (mutation == 3) input.process.orderSlot = 1U;
        EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
    }
}

TEST(Ex2Stage5QualificationWarmup, ResolutionAndStateFactsBlockQualification)
{
    for (int gate = 0; gate < 2; ++gate)
    {
        auto input = Input();
        if (gate == 0) CoarseClock(input);
        else StateSwitch(input);
        const auto result = s5::AssessWarmupProcess(input);
        EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
        EXPECT_FALSE(result.SelectedW()); ASSERT_EQ(result.Candidates().size(), 6U);
        for (const auto& candidate : result.Candidates()) EXPECT_FALSE(candidate.qualified);
    }
}

TEST(Ex2Stage5QualificationWarmup, FirstPassingCandidateRatherThanSmallestError)
{
    auto input = Input(); Fill(input, 0U, 8U, 1040U);
    const auto result = s5::AssessWarmupProcess(input);
    ASSERT_TRUE(result.Qualified()); EXPECT_EQ(result.SelectedW(), 0U);
    EXPECT_GT(result.Candidates()[0].earlyARelativeDifference,
        result.Candidates().back().earlyARelativeDifference);
}

TEST(Ex2Stage5QualificationWarmup, EachFrozenCandidateCanBeFirst)
{
    for (const auto w : s5::CandidateWarmups)
    { const auto result = s5::AssessWarmupProcess(WarmupFor(w));
      ASSERT_TRUE(result.InputValid()); EXPECT_EQ(result.SelectedW(), w) << w; }
}

TEST(Ex2Stage5QualificationWarmup, AllFailIsValidWithoutWarmupAbove16)
{
    auto input = Input(); Fill(input, 0U, 32U, 2000U);
    const auto result = s5::AssessWarmupProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified()); EXPECT_FALSE(result.SelectedW());
    ASSERT_EQ(result.Candidates().size(), 6U); EXPECT_EQ(result.Candidates().back().w, 16U);
}

TEST(Ex2Stage5QualificationWarmup, ExactFivePercentBothDirectionsInclusive)
{
    for (const auto value : {950U, 1050U})
    {
        auto input = Input(); Fill(input, 0U, 40U, value);
        const auto result = s5::AssessWarmupProcess(input);
        ASSERT_TRUE(result.Qualified()); EXPECT_EQ(result.SelectedW(), 0U);
        EXPECT_DOUBLE_EQ(result.Candidates()[0].earlyARelativeDifference, 0.05);
        EXPECT_DOUBLE_EQ(result.Candidates()[0].earlyBRelativeDifference, 0.05);
        EXPECT_DOUBLE_EQ(result.Candidates()[0].lateRelativeDifference, 0.05);
    }
}

TEST(Ex2Stage5QualificationWarmup, OverFivePercentBothDirectionsUnqualified)
{
    for (const auto value : {949U, 1051U})
    { auto input = Input(); Fill(input, 0U, 40U, value);
      const auto result = s5::AssessWarmupProcess(input);
      EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified()); }
}

TEST(Ex2Stage5QualificationWarmup, EarlyAAndEarlyBAndLateWindowsIndependentlyGate)
{
    for (const auto begin : {0U, 8U, 32U})
    {
        auto input = Input(); Fill(input, begin, begin + 8U, 1051U);
        const auto result = s5::AssessWarmupProcess(input);
        ASSERT_EQ(result.Candidates().size(), 6U);
        EXPECT_FALSE(result.Candidates()[0].qualified);
        EXPECT_DOUBLE_EQ(result.Candidates()[0].reference.AsDouble(), 1000.0);
        if (begin == 32U) EXPECT_FALSE(result.Qualified());
        else EXPECT_TRUE(result.Qualified());
    }
}

TEST(Ex2Stage5QualificationWarmup, HalfNanosecondThresholdDecisionIsExact)
{
    auto input = Input(Phase::D1Warmup, 0U, 110U);
    Fill(input, 0U, 4U, 121U); // median 115.5 versus 110: exactly 5%.
    EXPECT_TRUE(s5::AssessWarmupProcess(input).Candidates()[0].qualified);
    Fill(input, 0U, 5U, 121U); // median 121: 10%.
    EXPECT_FALSE(s5::AssessWarmupProcess(input).Candidates()[0].qualified);
}

TEST(Ex2Stage5QualificationCommonW, ExactlyTenAndMaximumSelectedWarmup)
{
    auto inputs = Group(Phase::D1Warmup);
    inputs[0] = WarmupFor(4U, 0U); inputs[9] = WarmupFor(16U, 9U);
    const auto result = s5::AssessCommonWarmup(Warmups(inputs));
    EXPECT_TRUE(result.inputValid); EXPECT_TRUE(result.qualified); EXPECT_EQ(result.selectedCommonW, 16U);
}

TEST(Ex2Stage5QualificationCommonW, SingleScientificNonqualificationIsValid)
{
    auto inputs = Group(Phase::D1Warmup); StateSwitch(inputs[3]);
    const auto result = s5::AssessCommonWarmup(Warmups(inputs));
    EXPECT_TRUE(result.inputValid); EXPECT_FALSE(result.qualified); EXPECT_FALSE(result.selectedCommonW);
}

TEST(Ex2Stage5QualificationCommonW, MissingDuplicateAndWrongCardinalityAreInvalid)
{
    auto assessments = Warmups(Group(Phase::D1Warmup)); assessments.pop_back();
    EXPECT_FALSE(s5::AssessCommonWarmup(assessments).inputValid);
    assessments = Warmups(Group(Phase::D1Warmup)); assessments[1] = assessments[0];
    EXPECT_FALSE(s5::AssessCommonWarmup(assessments).inputValid);
    assessments = Warmups(Group(Phase::D1Warmup)); assessments[2] = assessments[1];
    EXPECT_FALSE(s5::AssessCommonWarmup(assessments).inputValid);
    assessments = Warmups(Group(Phase::D1Warmup)); assessments.push_back(assessments[0]);
    EXPECT_FALSE(s5::AssessCommonWarmup(assessments).inputValid);
}

TEST(Ex2Stage5QualificationCommonW, InvalidUnderlyingAssessmentBlocksAnalysis)
{
    auto inputs = Group(Phase::D1Warmup); inputs[0].observations.pop_back();
    EXPECT_FALSE(s5::AssessCommonWarmup(Warmups(inputs)).inputValid);
}

TEST(Ex2Stage5QualificationCommonW, BackendAndIndexUniquenessAllowsReorderedSet)
{
    auto assessments = Warmups(Group(Phase::D1Warmup));
    std::reverse(assessments.begin(), assessments.end());
    const auto result = s5::AssessCommonWarmup(assessments);
    EXPECT_TRUE(result.inputValid); EXPECT_TRUE(result.qualified); EXPECT_EQ(result.selectedCommonW, 0U);
}

TEST(Ex2Stage5QualificationSample, RequiresExactly200NoFallbackOrExtension)
{
    for (const auto count : {0U, 100U, 199U, 201U, 400U})
    {
        auto input = Input(Phase::D1Sample); input.observations.resize(count);
        const auto result = s5::AssessSampleCountProcess(input);
        EXPECT_FALSE(result.InputValid()); EXPECT_FALSE(result.Qualified()); EXPECT_FALSE(result.Diagnostics());
    }
    auto input = Input(Phase::D1Sample); input.expectedObservationCount = 100U;
    EXPECT_FALSE(s5::AssessSampleCountProcess(input).InputValid());
}

TEST(Ex2Stage5QualificationSample, RejectsUnorderedDuplicateAndIneligibleRows)
{
    for (int mutation = 0; mutation < 6; ++mutation)
    {
        auto input = Input(Phase::D1Sample);
        if (mutation == 0) std::swap(input.observations[0], input.observations[1]);
        if (mutation == 1) input.observations.back().sequenceIndex = 198U;
        if (mutation == 2) input.observations[0].status = s5::OperationStatus::TimestampInvalid;
        if (mutation == 3) input.observations[0].validationEligible = false;
        if (mutation == 4) input.observations[0].hostCompletionNanoseconds.reset();
        if (mutation == 5) input.observations[0].hostCompletionNanoseconds = 0U;
        EXPECT_FALSE(s5::AssessSampleCountProcess(input).InputValid());
    }
}

TEST(Ex2Stage5QualificationSample, PrefixAndWindowMediansUseExactOrderedRanges)
{
    auto input = Input(Phase::D1Sample);
    Fill(input, 0U, 50U, 1000U); Fill(input, 50U, 100U, 2000U);
    Fill(input, 100U, 150U, 3000U); Fill(input, 150U, 200U, 4000U);
    const auto result = s5::AssessSampleCountProcess(input);
    ASSERT_TRUE(result.InputValid()); const auto& d = *result.Diagnostics();
    EXPECT_DOUBLE_EQ(d.median50.AsDouble(), 1000.0);
    EXPECT_DOUBLE_EQ(d.median100.AsDouble(), 1500.0);
    EXPECT_DOUBLE_EQ(d.median200.AsDouble(), 2500.0);
    for (std::size_t i = 0U; i < 4U; ++i)
        EXPECT_DOUBLE_EQ(d.windows[i].AsDouble(), 1000.0 * static_cast<double>(i + 1U));
    EXPECT_FALSE(result.Qualified());
}

TEST(Ex2Stage5QualificationSample, ExactTwoPercentInclusive)
{
    for (const auto first : {980U, 1020U})
    {
        auto input = Input(Phase::D1Sample); Fill(input, 0U, 100U, first);
        Fill(input, 100U, 200U, 2000U - first);
        const auto result = s5::AssessSampleCountProcess(input);
        ASSERT_TRUE(result.InputValid()); EXPECT_TRUE(result.Qualified());
        EXPECT_DOUBLE_EQ(result.Diagnostics()->median200.AsDouble(), 1000.0);
        EXPECT_DOUBLE_EQ(result.Diagnostics()->prefix100RelativeDifference, 0.02);
    }
}

TEST(Ex2Stage5QualificationSample, OverTwoPercentFailsWithoutOrdinary200Fallback)
{
    auto input = Input(Phase::D1Sample); Fill(input, 0U, 100U, 1021U); Fill(input, 100U, 200U, 979U);
    const auto result = s5::AssessSampleCountProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
    EXPECT_TRUE(HasReason(result.Reasons(), "prefix100_not_converged"));
    EXPECT_EQ(s5::AdmittedOrdinarySampleCount, 100U);
}

TEST(Ex2Stage5QualificationSample, ExactFivePercentPrefixAndWindowInclusive)
{
    for (const auto first : {950U, 1050U})
    {
        auto input = Input(Phase::D1Sample); Fill(input, 0U, 50U, first);
        Fill(input, 50U, 100U, first == 950U ? 1010U : 990U);
        const auto result = s5::AssessSampleCountProcess(input);
        ASSERT_TRUE(result.InputValid()); EXPECT_TRUE(result.Qualified());
        EXPECT_DOUBLE_EQ(result.Diagnostics()->prefix50RelativeDifference, 0.05);
        EXPECT_DOUBLE_EQ(result.Diagnostics()->firstLastRelativeDifference, 0.05);
        EXPECT_DOUBLE_EQ(result.Diagnostics()->prefix100RelativeDifference, 0.02);
    }
}

TEST(Ex2Stage5QualificationSample, OverFivePercentPrefixAndWindowFails)
{
    auto input = Input(Phase::D1Sample); Fill(input, 0U, 50U, 1051U); Fill(input, 50U, 100U, 989U);
    const auto result = s5::AssessSampleCountProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
    EXPECT_TRUE(HasReason(result.Reasons(), "prefix50_not_converged"));
    EXPECT_TRUE(HasReason(result.Reasons(), "first_last_window_drift"));
    EXPECT_FALSE(HasReason(result.Reasons(), "prefix100_not_converged"));
}

TEST(Ex2Stage5QualificationSample, FirstLastWindowCanFailWithBothPrefixesPassing)
{
    auto input = Input(Phase::D1Sample); Fill(input, 150U, 200U, 1060U);
    const auto result = s5::AssessSampleCountProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
    EXPECT_TRUE(HasReason(result.Reasons(), "first_last_window_drift"));
    EXPECT_TRUE(HasReason(result.Reasons(), "persistent_trend"));
}

TEST(Ex2Stage5QualificationSample, ResolutionAndStateFactsGateValidInputs)
{
    for (int gate = 0; gate < 2; ++gate)
    {
        auto input = Input(Phase::D1Sample);
        if (gate == 0) CoarseClock(input);
        else StateSwitch(input);
        const auto result = s5::AssessSampleCountProcess(input);
        EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified()); EXPECT_TRUE(result.Diagnostics());
    }
}

TEST(Ex2Stage5QualificationSample, ZeroMedianAndWrongPhaseAreInvalid)
{
    EXPECT_FALSE(s5::AssessSampleCountProcess(Input(Phase::D1Sample, 0U, 0U)).InputValid());
    EXPECT_FALSE(s5::AssessSampleCountProcess(Input()).InputValid());
}

TEST(Ex2Stage5QualificationStability, ExactOnePointTenInclusiveForEachBackend)
{
    for (const auto backend : {Backend::Cuda, Backend::Vulkan})
    {
        const auto result = s5::AssessProcessStability(backend, Samples(backend, 1100U));
        EXPECT_TRUE(result.inputValid); EXPECT_TRUE(result.qualified);
        ASSERT_TRUE(result.rProcess); EXPECT_DOUBLE_EQ(*result.rProcess, 1.10);
    }
}

TEST(Ex2Stage5QualificationStability, OverOnePointTenFailsWithoutOutlierRemoval)
{
    const auto result = s5::AssessProcessStability(Backend::Cuda, Samples(Backend::Cuda, 1101U));
    EXPECT_TRUE(result.inputValid); EXPECT_FALSE(result.qualified);
    ASSERT_TRUE(result.rProcess); EXPECT_DOUBLE_EQ(*result.rProcess, 1.101);
}

TEST(Ex2Stage5QualificationStability, UsesFirst100MedianRatherThanMedian200)
{
    auto inputs = Group(Phase::D1Sample);
    std::vector<s5::SampleCountProcessAssessment> assessments;
    for (auto& input : inputs)
        if (input.process.backend == Backend::Cuda)
        {
            if (input.process.processIndex == 4U)
            { Fill(input, 0U, 100U, 1020U); Fill(input, 100U, 200U, 980U); }
            assessments.push_back(s5::AssessSampleCountProcess(input));
        }
    const auto result = s5::AssessProcessStability(Backend::Cuda, assessments);
    EXPECT_TRUE(result.qualified); ASSERT_TRUE(result.rProcess); EXPECT_DOUBLE_EQ(*result.rProcess, 1.02);
}

TEST(Ex2Stage5QualificationStability, MissingDuplicateWrongBackendAndExtraInvalid)
{
    auto assessments = Samples(Backend::Cuda); assessments.pop_back();
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Cuda, assessments).inputValid);
    assessments = Samples(Backend::Cuda); assessments[4] = assessments[0];
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Cuda, assessments).inputValid);
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Vulkan, Samples(Backend::Cuda)).inputValid);
    assessments = Samples(Backend::Cuda); assessments.push_back(assessments[0]);
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Cuda, assessments).inputValid);
    EXPECT_FALSE(s5::AssessProcessStability(static_cast<Backend>(2), Samples(Backend::Cuda)).inputValid);
}

TEST(Ex2Stage5QualificationStability, InvalidUnderlyingInputAndZeroMedianAreInvalid)
{
    auto assessments = Samples(Backend::Cuda);
    assessments[0] = s5::AssessSampleCountProcess(Input(Phase::D1Sample, 0U, 0U));
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Cuda, assessments).inputValid);
}

TEST(Ex2Stage5QualificationStability, UnqualifiedProcessCannotContributeRatio)
{
    auto assessments = Samples(Backend::Cuda);
    auto input = Input(Phase::D1Sample); CoarseClock(input);
    assessments[0] = s5::AssessSampleCountProcess(input);
    const auto result = s5::AssessProcessStability(Backend::Cuda, assessments);
    EXPECT_TRUE(result.inputValid); EXPECT_FALSE(result.qualified); EXPECT_FALSE(result.rProcess);
}

TEST(Ex2Stage5QualificationStability, InconsistentAppliedWarmupsInvalid)
{
    auto assessments = Samples(Backend::Cuda);
    assessments[0] = s5::AssessSampleCountProcess(Input(Phase::D1Sample, 0U, 1000U, 1U));
    EXPECT_FALSE(s5::AssessProcessStability(Backend::Cuda, assessments).inputValid);
}

TEST(Ex2Stage5QualificationD1, WarmupOnlyFieldsAreNotEvaluated)
{
    const auto result = s5::AssessD1Qualification(Group(Phase::D1Warmup));
    EXPECT_TRUE(result.warmupInputValid); EXPECT_TRUE(result.d1WarmupQualified);
    EXPECT_EQ(result.selectedCommonW, 0U); EXPECT_FALSE(result.sampleInputPresent);
    EXPECT_FALSE(result.sampleInputValid); EXPECT_FALSE(result.d1SampleCountQualified);
    EXPECT_FALSE(result.cudaRProcess); EXPECT_FALSE(result.cudaProcessStabilityQualified);
    EXPECT_FALSE(result.vulkanRProcess); EXPECT_FALSE(result.vulkanProcessStabilityQualified);
    EXPECT_FALSE(result.d1ScopeQualified);
}

TEST(Ex2Stage5QualificationD1, ScientificWarmupFailureIsCompleteAnalysisFact)
{
    auto warmup = Group(Phase::D1Warmup); StateSwitch(warmup[0]);
    const auto result = s5::AssessD1Qualification(warmup);
    EXPECT_TRUE(result.warmupInputValid); EXPECT_FALSE(result.d1WarmupQualified);
    EXPECT_FALSE(result.selectedCommonW); EXPECT_FALSE(result.sampleInputPresent);
    EXPECT_FALSE(result.d1SampleCountQualified); EXPECT_FALSE(result.d1ScopeQualified);
}

TEST(Ex2Stage5QualificationD1, FullScopeQualifiedOnlyWithAllScientificCriteria)
{
    const auto warmup = Group(Phase::D1Warmup); const auto samples = Group(Phase::D1Sample);
    const auto result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_TRUE(result.sampleInputPresent); EXPECT_EQ(result.sampleInputValid, true);
    EXPECT_EQ(result.d1SampleCountQualified, true);
    EXPECT_EQ(result.cudaProcessStabilityQualified, true);
    EXPECT_EQ(result.vulkanProcessStabilityQualified, true);
    EXPECT_EQ(result.cudaRProcess, 1.0); EXPECT_EQ(result.vulkanRProcess, 1.0);
    EXPECT_TRUE(result.d1ScopeQualified); EXPECT_TRUE(result.reasons.empty());
}

TEST(Ex2Stage5QualificationD1, SamplesRequireQualifiedCommonWarmup)
{
    auto warmup = Group(Phase::D1Warmup); CoarseClock(warmup[0]);
    const auto samples = Group(Phase::D1Sample);
    auto result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_TRUE(result.warmupInputValid); EXPECT_EQ(result.sampleInputValid, false);
    EXPECT_FALSE(result.d1SampleCountQualified); EXPECT_FALSE(result.d1ScopeQualified);
    warmup.pop_back(); result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_FALSE(result.warmupInputValid); EXPECT_EQ(result.sampleInputValid, false);
}

TEST(Ex2Stage5QualificationD1, SamplesMustApplyExactlyCommonW)
{
    auto warmup = Group(Phase::D1Warmup); warmup[0] = WarmupFor(4U);
    const auto wrong = Group(Phase::D1Sample, 2U);
    const auto invalid = s5::AssessD1Qualification(warmup, wrong);
    EXPECT_EQ(invalid.selectedCommonW, 4U); EXPECT_EQ(invalid.sampleInputValid, false);
    EXPECT_FALSE(invalid.d1ScopeQualified);
    const auto right = Group(Phase::D1Sample, 4U);
    EXPECT_TRUE(s5::AssessD1Qualification(warmup, right).d1ScopeQualified);
}

TEST(Ex2Stage5QualificationD1, PresentEmptyMalformedAndDuplicateSamplesAreInvalid)
{
    const auto warmup = Group(Phase::D1Warmup);
    auto samples = Group(Phase::D1Sample); samples.clear();
    auto result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_TRUE(result.sampleInputPresent); EXPECT_EQ(result.sampleInputValid, false);
    EXPECT_FALSE(result.d1SampleCountQualified);
    samples = Group(Phase::D1Sample); samples[0].observations.pop_back();
    EXPECT_EQ(s5::AssessD1Qualification(warmup, samples).sampleInputValid, false);
    samples = Group(Phase::D1Sample); samples[1] = samples[0];
    EXPECT_EQ(s5::AssessD1Qualification(warmup, samples).sampleInputValid, false);
}

TEST(Ex2Stage5QualificationD1, SingleSampleScientificFailureBlocksScope)
{
    const auto warmup = Group(Phase::D1Warmup); auto samples = Group(Phase::D1Sample);
    StateSwitch(samples[0]);
    const auto result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_EQ(result.sampleInputValid, true); EXPECT_EQ(result.d1SampleCountQualified, false);
    EXPECT_EQ(result.cudaProcessStabilityQualified, false);
    EXPECT_EQ(result.vulkanProcessStabilityQualified, true); EXPECT_FALSE(result.d1ScopeQualified);
}

TEST(Ex2Stage5QualificationD1, BackendStabilityAssessedSeparately)
{
    const auto warmup = Group(Phase::D1Warmup);
    for (const auto backend : {Backend::Cuda, Backend::Vulkan})
    {
        auto samples = Group(Phase::D1Sample);
        for (auto& sample : samples)
            if (sample.process.backend == backend && sample.process.processIndex == 4U)
                Fill(sample, 0U, 200U, 1101U);
        const auto result = s5::AssessD1Qualification(warmup, samples);
        EXPECT_EQ(result.d1SampleCountQualified, true); EXPECT_FALSE(result.d1ScopeQualified);
        EXPECT_EQ(result.cudaProcessStabilityQualified, backend != Backend::Cuda);
        EXPECT_EQ(result.vulkanProcessStabilityQualified, backend != Backend::Vulkan);
    }
}

TEST(Ex2Stage5QualificationD1, ReasonsStableAcrossProcessSetOrdering)
{
    auto warmup = Group(Phase::D1Warmup); auto samples = Group(Phase::D1Sample);
    CoarseClock(samples[0]); StateSwitch(samples[7]);
    const auto first = s5::SerializeAnalysisJson(s5::AssessD1Qualification(warmup, samples));
    std::reverse(warmup.begin(), warmup.end()); std::reverse(samples.begin(), samples.end());
    EXPECT_EQ(s5::SerializeAnalysisJson(s5::AssessD1Qualification(warmup, samples)), first);
}

TEST(Ex2Stage5QualificationJson, ExactWarmupOnlyGolden)
{
    const auto result = s5::AssessD1Qualification(Group(Phase::D1Warmup));
    EXPECT_EQ(s5::SerializeAnalysisJson(result),
        "{\"analysis_schema_version\":2,\"experiment_id\":\"EX-2\",\"protocol_version\":\"1.2\","
        "\"evidence_kind\":\"qualification\",\"analysis_kind\":\"d1-qualification\","
        "\"warmup_input_valid\":true,\"d1_warmup_qualified\":true,\"selected_common_w\":0,"
        "\"sample_input_present\":false,\"sample_input_valid\":null,\"d1_sample_count_qualified\":null,"
        "\"cuda_r_process\":null,\"cuda_process_stability_qualified\":null,"
        "\"vulkan_r_process\":null,\"vulkan_process_stability_qualified\":null,"
        "\"d1_scope_qualified\":false,\"reasons\":[],\"warmup_processes\":"
        + GoldenProcesses() + ",\"sample_processes\":null}\n");
}

TEST(Ex2Stage5QualificationJson, ExactFullScopeGolden)
{
    const auto warmup = Group(Phase::D1Warmup); const auto samples = Group(Phase::D1Sample);
    const auto result = s5::AssessD1Qualification(warmup, samples);
    EXPECT_EQ(s5::SerializeAnalysisJson(result),
        "{\"analysis_schema_version\":2,\"experiment_id\":\"EX-2\",\"protocol_version\":\"1.2\","
        "\"evidence_kind\":\"qualification\",\"analysis_kind\":\"d1-qualification\","
        "\"warmup_input_valid\":true,\"d1_warmup_qualified\":true,\"selected_common_w\":0,"
        "\"sample_input_present\":true,\"sample_input_valid\":true,\"d1_sample_count_qualified\":true,"
        "\"cuda_r_process\":1,\"cuda_process_stability_qualified\":true,"
        "\"vulkan_r_process\":1,\"vulkan_process_stability_qualified\":true,"
        "\"d1_scope_qualified\":true,\"reasons\":[],\"warmup_processes\":"
        + GoldenProcesses() + ",\"sample_processes\":" + GoldenProcesses(true) + "}\n");
}

TEST(Ex2Stage5QualificationJson, ExactWarmupScientificFailureGolden)
{
    auto warmup = Group(Phase::D1Warmup); CoarseClock(warmup[0]);
    EXPECT_EQ(s5::SerializeAnalysisJson(s5::AssessD1Qualification(warmup)),
        "{\"analysis_schema_version\":2,\"experiment_id\":\"EX-2\",\"protocol_version\":\"1.2\","
        "\"evidence_kind\":\"qualification\",\"analysis_kind\":\"d1-qualification\","
        "\"warmup_input_valid\":true,\"d1_warmup_qualified\":false,\"selected_common_w\":null,"
        "\"sample_input_present\":false,\"sample_input_valid\":null,\"d1_sample_count_qualified\":null,"
        "\"cuda_r_process\":null,\"cuda_process_stability_qualified\":null,"
        "\"vulkan_r_process\":null,\"vulkan_process_stability_qualified\":null,"
        "\"d1_scope_qualified\":false,\"reasons\":["
        "\"warmup:cuda:0:w=0:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:w=1:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:w=2:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:w=4:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:w=8:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:w=16:host_clock_resolution_inadequate\","
        "\"warmup:cuda:0:no_candidate_warmup_qualified\"],\"warmup_processes\":"
        + GoldenProcesses(false, true) + ",\"sample_processes\":null}\n");
}

TEST(Ex2Stage5QualificationJson, EscapesControlsQuotesBackslashesAndRetainsUtf8)
{
    auto assessment = s5::AssessD1Qualification(Group(Phase::D1Warmup));
    assessment.reasons = {std::string("quote\" slash\\\n\t\x01 ") + "\xC3\xA9"};
    const auto json = s5::SerializeAnalysisJson(assessment);
    EXPECT_NE(json.find("\"quote\\\" slash\\\\\\u000a\\u0009\\u0001 \xC3\xA9\""), std::string::npos);
    assessment.reasons = {"\xC0\x80"};
    EXPECT_THROW((void)s5::SerializeAnalysisJson(assessment), std::invalid_argument);
}

TEST(Ex2Stage5QualificationJson, RejectsNanInfinityAndUnsupportedVersions)
{
    auto assessment = s5::AssessD1Qualification(Group(Phase::D1Warmup));
    for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
    {
        assessment.cudaRProcess = value;
        EXPECT_THROW((void)s5::SerializeAnalysisJson(assessment), std::invalid_argument);
    }
    assessment.cudaRProcess.reset(); assessment.analysisVersion = 3U;
    EXPECT_THROW((void)s5::SerializeAnalysisJson(assessment), std::invalid_argument);
    assessment.analysisVersion = 2U; assessment.protocolVersion = "1.0";
    EXPECT_THROW((void)s5::SerializeAnalysisJson(assessment), std::invalid_argument);
}

struct CommaPunctuation : std::numpunct<char>
{
    char do_decimal_point() const override { return ','; }
};

TEST(Ex2Stage5QualificationJson, DeterministicLocaleIndependentWithoutVerdictOrWinner)
{
    const auto warmup = Group(Phase::D1Warmup); auto samples = Group(Phase::D1Sample);
    Fill(samples[9], 0U, 200U, 1100U);
    const auto assessment = s5::AssessD1Qualification(warmup, samples);
    const auto expected = s5::SerializeAnalysisJson(assessment);
    const auto old = std::locale();
    std::locale::global(std::locale(old, new CommaPunctuation));
    const auto actual = s5::SerializeAnalysisJson(assessment);
    std::locale::global(old);
    EXPECT_EQ(actual, expected); EXPECT_NE(actual.find("\"vulkan_r_process\":1.1"), std::string::npos);
    for (const auto forbidden : {"gate0_verdict", "backend_winner", "cuda_vulkan_ratio", "speedup",
             "candidate_performance_admitted", "LIMITED_PASS", "PASS", "FAIL", "INCOMPLETE"})
        EXPECT_EQ(actual.find(forbidden), std::string::npos) << forbidden;
}

TEST(Ex2Stage5QualificationBoundary, AssessmentsExposeConstFactsWithoutFlagMutation)
{
    static_assert(std::is_const_v<std::remove_reference_t<decltype(
        std::declval<s5::SampleCountProcessAssessment>().Diagnostics())>>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(
        std::declval<s5::WarmupProcessAssessment>().Candidates())>>);
    EXPECT_FALSE(s5::WarmupProcessAssessment{}.InputValid());
    EXPECT_FALSE(s5::SampleCountProcessAssessment{}.InputValid());
    EXPECT_FALSE(s5::SampleCountProcessAssessment{}.Qualified());
}

} // namespace
