#include "ex2/Ex2Stage5OperationalFacts.hpp"
#include "ex2/Ex2Stage5Qualification.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace
{
namespace s5 = computelab::ex2::stage5;
using Phase = s5::Stage5Phase;
using Backend = s5::Stage5Backend;

s5::ClockCalibrationInput Calibration(std::uint64_t step = 1U)
{
    s5::ClockCalibrationInput input;
    input.deltasNanoseconds.assign(4095U, step);
    return input;
}

std::vector<std::uint64_t> StateValues(std::array<std::uint64_t, 4> centers, std::size_t count = 48U)
{
    std::vector<std::uint64_t> values;
    for (const auto center : centers) values.insert(values.end(), count / 4U, center);
    return values;
}

s5::ProcessInput Input(Phase phase = Phase::D1Warmup, std::size_t planIndex = 0U)
{
    s5::ProcessInput input;
    const bool sample = phase == Phase::D1Sample, a1 = phase == Phase::A1Sentinel;
    input.condition = {phase, a1 ? s5::Stage5Workload::A1 : s5::Stage5Workload::D1,
        a1 ? 256U : 1048576U, a1 ? std::nullopt : std::optional<std::uint64_t>{64U},
        "H", sample ? 0U : 48U, sample ? std::optional<std::uint64_t>{0U} : std::nullopt,
        sample ? 200U : 0U};
    input.process = s5::FrozenProcessPlan()[planIndex];
    input.expectedObservationCount = sample ? 200U : 48U;
    input.clockCalibration = Calibration();
    for (std::uint64_t i = 0U; i < input.expectedObservationCount; ++i)
        input.observations.push_back({i, s5::OperationStatus::Ok, true, 1000U});
    return input;
}

void Fill(s5::ProcessInput& input, std::size_t begin, std::size_t end, std::uint64_t value)
{
    for (std::size_t i = begin; i < end; ++i) input.observations[i].hostCompletionNanoseconds = value;
}

TEST(Ex2Stage5OperationalClock, FrozenCountAndFutureHeader)
{
    EXPECT_EQ(s5::ClockCaptureCount, 4096U);
    EXPECT_EQ(s5::ClockDeltaCount, 4095U);
    EXPECT_EQ(s5::HostClockCsvHeader,
        "schema_version,run_id,series_id,process_index,sequence_index,delta_ns");
    EXPECT_TRUE(s5::AssessClockCalibration(Calibration()).InputValid());
    for (const auto count : {0U, 4094U, 4096U})
    {
        auto input = Calibration(); input.deltasNanoseconds.resize(count, 1U);
        EXPECT_FALSE(s5::AssessClockCalibration(input).InputValid());
    }
}

TEST(Ex2Stage5OperationalClock, ZeroDeltasAllowedAndMinimumPositiveStepSelected)
{
    auto input = Calibration(0U);
    input.deltasNanoseconds[0] = 30U; input.deltasNanoseconds[100] = 7U;
    input.deltasNanoseconds.back() = 12U;
    const auto result = s5::AssessClockCalibration(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_EQ(result.EffectiveStep(), 7U);
}

TEST(Ex2Stage5OperationalClock, NoPositiveStepAndInvalidConversionAreInvalid)
{
    EXPECT_FALSE(s5::AssessClockCalibration(Calibration(0U)).InputValid());
    for (const auto index : {0U, 2048U, 4094U})
    {
        auto input = Calibration(); input.deltasNanoseconds[index].reset();
        const auto result = s5::AssessClockCalibration(input);
        EXPECT_FALSE(result.InputValid()); EXPECT_FALSE(result.EffectiveStep());
    }
}

TEST(Ex2Stage5OperationalClock, ExactOnePercentInclusiveAndJustOverFails)
{
    const auto calibration = s5::AssessClockCalibration(Calibration(10U));
    const auto exact = s5::AssessClockAdequacy(calibration, {1000U, false});
    EXPECT_TRUE(exact.InputValid()); EXPECT_TRUE(exact.Adequate());
    EXPECT_TRUE(s5::AssessClockAdequacy(calibration, {1000U, true}).Adequate());
    const auto over = s5::AssessClockAdequacy(calibration, {999U, true});
    EXPECT_TRUE(over.InputValid()); EXPECT_FALSE(over.Adequate());
    EXPECT_FALSE(s5::AssessClockAdequacy(s5::AssessClockCalibration(Calibration(11U)), {1000U, false}).Adequate());
}

TEST(Ex2Stage5OperationalClock, OverflowSafeUint64Boundary)
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT_TRUE(s5::AssessClockAdequacy(s5::AssessClockCalibration(Calibration(maximum / 100U)),
        {maximum, true}).Adequate());
    EXPECT_FALSE(s5::AssessClockAdequacy(s5::AssessClockCalibration(Calibration(maximum / 100U + 1U)),
        {maximum, true}).Adequate());
    const auto large = s5::AssessClockAdequacy(s5::AssessClockCalibration(Calibration(maximum)),
        {maximum, false});
    EXPECT_TRUE(large.InputValid()); EXPECT_FALSE(large.Adequate());
}

TEST(Ex2Stage5OperationalClock, MissingCalibrationAndZeroScaleAreInvalidNotCoarse)
{
    const auto missing = s5::AssessClockAdequacy(s5::ClockCalibrationAssessment{}, {1000U, false});
    EXPECT_FALSE(missing.InputValid()); EXPECT_FALSE(missing.DecisionScale());
    EXPECT_FALSE(s5::AssessClockAdequacy(s5::AssessClockCalibration(Calibration()), {0U, false}).InputValid());
}

TEST(Ex2Stage5OperationalWindows, FrozenShapesCoverEveryObservationExactlyOnce)
{
    const std::array<std::size_t, 7> starts{0,1,2,4,8,16,0};
    const std::array<std::array<std::size_t, 4>, 7> lengths{{
        {12,12,12,12}, {12,12,12,11}, {12,12,11,11}, {11,11,11,11},
        {10,10,10,10}, {8,8,8,8}, {50,50,50,50}}};
    for (std::size_t i = 0U; i < 7U; ++i)
    {
        const auto end = i == 6U ? 200U : 48U;
        const auto windows = s5::PartitionOrderedRange(starts[i], end);
        std::vector<unsigned> coverage(end, 0U);
        auto next = starts[i];
        for (std::size_t j = 0U; j < 4U; ++j)
        {
            EXPECT_EQ(windows[j].begin, next); EXPECT_EQ(windows[j].count, lengths[i][j]);
            for (std::size_t k = windows[j].begin; k < windows[j].begin + windows[j].count; ++k)
                ++coverage[k];
            next += windows[j].count;
        }
        EXPECT_EQ(next, end);
        for (std::size_t k = 0U; k < end; ++k) EXPECT_EQ(coverage[k], k < starts[i] ? 0U : 1U);
    }
}

TEST(Ex2Stage5OperationalWindows, RejectsUnfrozenRanges)
{
    for (const auto begin : {3U, 5U, 32U, 49U})
        EXPECT_THROW((void)s5::PartitionOrderedRange(begin, 48U), std::invalid_argument);
    EXPECT_THROW((void)s5::PartitionOrderedRange(0U, 49U), std::invalid_argument);
    EXPECT_THROW((void)s5::PartitionOrderedRange(1U, 200U), std::invalid_argument);
}

TEST(Ex2Stage5OperationalState, FlatSequenceHasNoStructure)
{
    const auto state = s5::AssessOrderedState(StateValues({1000,1000,1000,1000}));
    EXPECT_TRUE(state.InputValid()); EXPECT_FALSE(state.PersistentTrend());
    EXPECT_FALSE(state.AbruptStateSwitch()); EXPECT_FALSE(state.StateStructure());
}

TEST(Ex2Stage5OperationalState, ExactFivePercentBothDirectionsDoesNotTriggerTrend)
{
    for (const auto centers : {std::array<std::uint64_t, 4>{950,970,990,1000},
             std::array<std::uint64_t, 4>{1050,1030,1010,1000}})
    {
        const auto state = s5::AssessOrderedState(StateValues(centers));
        EXPECT_TRUE(state.InputValid()); EXPECT_FALSE(state.PersistentTrend()); EXPECT_FALSE(state.StateStructure());
    }
}

TEST(Ex2Stage5OperationalState, JustOverFivePercentMonotonicBothDirectionsTriggersTrend)
{
    for (const auto centers : {std::array<std::uint64_t, 4>{949,970,990,1000},
             std::array<std::uint64_t, 4>{1051,1030,1010,1000}})
    {
        const auto state = s5::AssessOrderedState(StateValues(centers));
        EXPECT_TRUE(state.PersistentTrend()); EXPECT_FALSE(state.AbruptStateSwitch()); EXPECT_TRUE(state.StateStructure());
    }
}

TEST(Ex2Stage5OperationalState, NonmonotonicSmallMovementIsNotPersistentTrend)
{
    const auto state = s5::AssessOrderedState(StateValues({1000,1030,1010,1020}));
    EXPECT_FALSE(state.PersistentTrend()); EXPECT_FALSE(state.StateStructure());
}

TEST(Ex2Stage5OperationalState, EqualPlateausMayParticipateInSustainedDirection)
{
    const auto state = s5::AssessOrderedState(StateValues({949,949,1000,1000}));
    EXPECT_TRUE(state.PersistentTrend()); EXPECT_FALSE(state.AbruptStateSwitch());
}

TEST(Ex2Stage5OperationalState, ExactOnePointTenAdjacentAccepted)
{
    const auto state = s5::AssessOrderedState(StateValues({1000,1100,1000,1000}));
    EXPECT_FALSE(state.PersistentTrend()); EXPECT_FALSE(state.AbruptStateSwitch()); EXPECT_FALSE(state.StateStructure());
}

TEST(Ex2Stage5OperationalState, JustOverOnePointTenAdjacentDetected)
{
    const auto state = s5::AssessOrderedState(StateValues({1000,1101,1000,1000}));
    EXPECT_FALSE(state.PersistentTrend()); EXPECT_TRUE(state.AbruptStateSwitch()); EXPECT_TRUE(state.StateStructure());
}

TEST(Ex2Stage5OperationalState, MidSequenceUpDownRegimeDetected)
{
    const auto state = s5::AssessOrderedState(StateValues({1000,2000,2000,1000}, 200U));
    EXPECT_TRUE(state.InputValid()); EXPECT_FALSE(state.PersistentTrend()); EXPECT_TRUE(state.AbruptStateSwitch());
}

TEST(Ex2Stage5OperationalState, SingleRawExtremeDoesNotAutomaticallyBecomeSwitch)
{
    auto values = StateValues({1000,1000,1000,1000});
    values[20] = std::numeric_limits<std::uint64_t>::max();
    const auto state = s5::AssessOrderedState(values);
    EXPECT_FALSE(state.StateStructure());
    EXPECT_EQ(values[20], std::numeric_limits<std::uint64_t>::max());
}

TEST(Ex2Stage5OperationalState, HalfNanosecondMediansAndLargeRatiosStayExact)
{
    auto values = StateValues({110,110,110,110});
    std::fill(values.begin(), values.begin() + 6, 121U); // first median 115.5; last 110.
    const auto state = s5::AssessOrderedState(values);
    ASSERT_TRUE(state.Medians()); EXPECT_EQ((*state.Medians())[0], (s5::MedianNanoseconds{115U, true}));
    EXPECT_FALSE(state.PersistentTrend());
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT_TRUE(s5::WithinRelativeTolerance({maximum, false}, {maximum, true}, 20U));
    EXPECT_FALSE(s5::WithinRelativeTolerance({maximum, false}, {1U, false}, 10U));
}

TEST(Ex2Stage5OperationalState, MalformedRangesAndZeroObservationsAreInvalid)
{
    EXPECT_FALSE(s5::AssessOrderedState(std::vector<std::uint64_t>(47U, 1U)).InputValid());
    auto values = StateValues({1000,1000,1000,1000}); values[30] = 0U;
    EXPECT_FALSE(s5::AssessOrderedState(values).InputValid());
}

TEST(Ex2Stage5OperationalWarmup, PreWarmupTransientDoesNotContaminateLaterCandidate)
{
    auto input = Input(); Fill(input, 0U, 12U, 2000U);
    const auto result = s5::AssessWarmupProcess(input);
    ASSERT_TRUE(result.InputValid()); ASSERT_TRUE(result.Qualified()); EXPECT_EQ(result.SelectedW(), 16U);
    EXPECT_TRUE(result.Candidates()[0].orderedState.StateStructure());
    EXPECT_FALSE(result.Candidates()[5].orderedState.StateStructure());
    EXPECT_EQ(result.Candidates()[5].orderedState.Windows()->front().begin, 16U);
}

TEST(Ex2Stage5OperationalWarmup, CandidateSpecificClockScalesAndBoundaries)
{
    auto input = Input(); input.clockCalibration = Calibration(2U);
    Fill(input, 0U, 12U, 100U); Fill(input, 12U, 48U, 200U);
    const auto result = s5::AssessWarmupProcess(input);
    ASSERT_TRUE(result.InputValid()); EXPECT_EQ(result.SelectedW(), 16U);
    EXPECT_EQ(result.Candidates()[0].clockAdequacy.DecisionScale()->whole, 100U);
    EXPECT_FALSE(result.Candidates()[0].clockAdequacy.Adequate());
    EXPECT_EQ(result.Candidates()[5].clockAdequacy.DecisionScale()->whole, 200U);
    EXPECT_TRUE(result.Candidates()[5].clockAdequacy.Adequate());
    input.clockCalibration = Calibration(3U);
    const auto coarse = s5::AssessWarmupProcess(input);
    EXPECT_TRUE(coarse.InputValid()); EXPECT_FALSE(coarse.Qualified());
}

TEST(Ex2Stage5OperationalWarmup, ClockScaleIncludesLateReferenceBeyondStateMedians)
{
    auto input = Input(); Fill(input, 0U, 48U, 1200U); Fill(input, 40U, 44U, 1000U);
    input.clockCalibration = Calibration(11U);
    const auto exact = s5::AssessWarmupProcess(input);
    ASSERT_TRUE(exact.InputValid()); const auto& candidate = exact.Candidates()[0];
    EXPECT_EQ(candidate.reference.whole, 1100U);
    for (const auto median : *candidate.orderedState.Medians()) EXPECT_EQ(median.whole, 1200U);
    EXPECT_EQ(candidate.clockAdequacy.DecisionScale()->whole, 1100U);
    EXPECT_TRUE(candidate.clockAdequacy.Adequate());
    input.clockCalibration = Calibration(12U);
    EXPECT_FALSE(s5::AssessWarmupProcess(input).Candidates()[0].clockAdequacy.Adequate());
}

TEST(Ex2Stage5OperationalValidity, MalformedClockInvalidatesAllThreeProcessKinds)
{
    for (const auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
        for (int defect = 0; defect < 4; ++defect)
        {
            auto input = Input(phase);
            if (defect == 0) input.clockCalibration.deltasNanoseconds.clear();
            if (defect == 1) input.clockCalibration.deltasNanoseconds.pop_back();
            if (defect == 2) input.clockCalibration = Calibration(0U);
            if (defect == 3) input.clockCalibration.deltasNanoseconds[0].reset();
            if (phase == Phase::A1Sentinel) EXPECT_FALSE(s5::AssessA1Process(input).InputValid());
            if (phase == Phase::D1Warmup) EXPECT_FALSE(s5::AssessWarmupProcess(input).InputValid());
            if (phase == Phase::D1Sample) EXPECT_FALSE(s5::AssessSampleCountProcess(input).InputValid());
        }
}

TEST(Ex2Stage5OperationalSample, CoarseClockIsValidScientificNonqualification)
{
    auto input = Input(Phase::D1Sample); input.clockCalibration = Calibration(11U);
    const auto result = s5::AssessSampleCountProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
    EXPECT_FALSE(result.Diagnostics()->clockAdequacy.Adequate());
    EXPECT_FALSE(result.Diagnostics()->orderedState.StateStructure());
    EXPECT_DOUBLE_EQ(result.Diagnostics()->prefix100RelativeDifference, 0.0);
    input.clockCalibration = Calibration(10U);
    EXPECT_TRUE(s5::AssessSampleCountProcess(input).Qualified());
}

TEST(Ex2Stage5OperationalSample, StateSwitchIndependentlyBlocksPassingConvergence)
{
    auto input = Input(Phase::D1Sample); Fill(input, 100U, 150U, 1101U);
    const auto result = s5::AssessSampleCountProcess(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_FALSE(result.Qualified());
    const auto& d = *result.Diagnostics();
    EXPECT_DOUBLE_EQ(d.prefix100RelativeDifference, 0.0);
    EXPECT_DOUBLE_EQ(d.prefix50RelativeDifference, 0.0);
    EXPECT_DOUBLE_EQ(d.firstLastRelativeDifference, 0.0);
    EXPECT_TRUE(d.clockAdequacy.Adequate()); EXPECT_TRUE(d.orderedState.AbruptStateSwitch());
}

TEST(Ex2Stage5OperationalSample, ClockScaleIncludesEveryWindowMedian)
{
    auto input = Input(Phase::D1Sample); Fill(input, 50U, 100U, 900U);
    input.clockCalibration = Calibration(10U);
    const auto result = s5::AssessSampleCountProcess(input);
    ASSERT_TRUE(result.InputValid()); const auto& diagnostics = *result.Diagnostics();
    EXPECT_EQ(diagnostics.median200.whole, 1000U);
    EXPECT_EQ(diagnostics.clockAdequacy.DecisionScale()->whole, 900U);
    EXPECT_FALSE(diagnostics.clockAdequacy.Adequate());
}

TEST(Ex2Stage5OperationalA1, DescribesAll48MedianStateAndClockWithoutQualification)
{
    auto input = Input(Phase::A1Sentinel); Fill(input, 12U, 24U, 2000U);
    input.clockCalibration = Calibration(11U);
    const auto result = s5::AssessA1Process(input);
    EXPECT_TRUE(result.InputValid()); EXPECT_EQ(result.Median48()->whole, 1000U);
    EXPECT_EQ(result.ClockCalibration().EffectiveStep(), 11U);
    EXPECT_FALSE(result.ClockAdequacy().Adequate()); EXPECT_TRUE(result.OrderedState().AbruptStateSwitch());
    for (const auto window : *result.OrderedState().Windows()) EXPECT_EQ(window.count, 12U);
}

TEST(Ex2Stage5OperationalA1, DescriptiveSpreadIsSeparatePerBackend)
{
    std::vector<s5::ProcessInput> inputs;
    for (std::size_t i = 0U; i < 10U; ++i) inputs.push_back(Input(Phase::A1Sentinel, i));
    Fill(inputs[8], 0U, 48U, 1100U); Fill(inputs[9], 0U, 48U, 1101U);
    const auto result = s5::AssessA1Sentinel(inputs);
    EXPECT_TRUE(result.inputValid); EXPECT_EQ(result.cuda.rA1Process, 1.10);
    EXPECT_EQ(result.cuda.exceedsOnePointTen, false); EXPECT_EQ(result.vulkan.rA1Process, 1.101);
    EXPECT_EQ(result.vulkan.exceedsOnePointTen, true);
}

TEST(Ex2Stage5OperationalA1, MissingDuplicateAndMalformedInputsInvalidateDescription)
{
    std::vector<s5::ProcessInput> inputs;
    for (std::size_t i = 0U; i < 10U; ++i) inputs.push_back(Input(Phase::A1Sentinel, i));
    auto missing = inputs; missing.pop_back(); EXPECT_FALSE(s5::AssessA1Sentinel(missing).inputValid);
    auto duplicate = inputs; duplicate[1] = duplicate[0]; EXPECT_FALSE(s5::AssessA1Sentinel(duplicate).inputValid);
    inputs[0].observations.pop_back(); EXPECT_FALSE(s5::AssessA1Sentinel(inputs).inputValid);
    EXPECT_FALSE(s5::DescribeA1Backend(Backend::Cuda, {}).inputValid);
}

TEST(Ex2Stage5OperationalA1, InstabilityDoesNotBlockIndependentD1Scope)
{
    auto a1 = Input(Phase::A1Sentinel); Fill(a1, 12U, 24U, 2000U);
    ASSERT_TRUE(s5::AssessA1Process(a1).OrderedState().StateStructure());
    std::vector<s5::ProcessInput> warmup, samples;
    for (std::size_t i = 0U; i < 10U; ++i)
    { warmup.push_back(Input(Phase::D1Warmup, i)); samples.push_back(Input(Phase::D1Sample, i)); }
    EXPECT_TRUE(s5::AssessD1Qualification(warmup, samples).d1ScopeQualified);
}

template <typename T>
concept HasSuppliedClockConclusion = requires(T input) { input.hostClockResolutionAdequate; };
template <typename T>
concept HasSuppliedStateConclusion = requires(T input) { input.persistentTrendOrAbruptStateSwitch; };
template <typename T>
concept HasA1Qualification = requires(T assessment) { assessment.Qualified(); };

TEST(Ex2Stage5OperationalBoundary, AssessmentsExposeImmutableDerivedFacts)
{
    static_assert(!HasSuppliedClockConclusion<s5::ProcessInput>);
    static_assert(!HasSuppliedStateConclusion<s5::ProcessInput>);
    static_assert(!HasA1Qualification<s5::A1ProcessAssessment>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(
        std::declval<s5::OrderedStateAssessment>().Medians())>>);
    EXPECT_FALSE(s5::OrderedStateAssessment{}.InputValid());
    EXPECT_FALSE(s5::ClockCalibrationAssessment{}.InputValid());
    EXPECT_FALSE(s5::ClockAdequacyAssessment{}.InputValid());
    EXPECT_FALSE(s5::A1ProcessAssessment{}.InputValid());
}

} // namespace
