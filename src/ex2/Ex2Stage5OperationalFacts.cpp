#include "ex2/Ex2Stage5OperationalFacts.hpp"

#include <algorithm>
#include <stdexcept>

namespace computelab::ex2::stage5
{
namespace
{

bool Positive(MedianNanoseconds value) noexcept { return value.whole != 0U || value.half; }

MedianNanoseconds Difference(MedianNanoseconds a, MedianNanoseconds b) noexcept
{
    if (a < b) std::swap(a, b);
    auto whole = a.whole - b.whole;
    if (!a.half && b.half) --whole;
    return {whole, a.half != b.half};
}

bool FrozenRange(std::size_t begin, std::size_t end) noexcept
{
    if (end == 200U) return begin == 0U;
    if (end != 48U) return false;
    constexpr std::array<std::size_t, 6> starts{0U, 1U, 2U, 4U, 8U, 16U};
    return std::find(starts.begin(), starts.end(), begin) != starts.end();
}

} // namespace

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

bool WithinRelativeTolerance(MedianNanoseconds a, MedianNanoseconds reference, std::uint64_t divisor)
{
    if (!Positive(reference) || (divisor != 10U && divisor != 20U && divisor != 50U))
        throw std::invalid_argument("invalid Stage-5 tolerance reference or divisor");
    const auto difference = Difference(a, reference);
    const auto quotient = reference.whole / divisor;
    if (difference.whole != quotient) return difference.whole < quotient;
    return (difference.half ? divisor : 0U)
        <= 2U * (reference.whole % divisor) + (reference.half ? 1U : 0U);
}

double RelativeDifference(MedianNanoseconds a, MedianNanoseconds reference)
{
    if (!Positive(reference)) throw std::invalid_argument("relative difference requires positive reference");
    return Difference(a, reference).AsDouble() / reference.AsDouble();
}

ClockCalibrationAssessment AssessClockCalibration(const ClockCalibrationInput& input)
{
    ClockCalibrationAssessment result;
    if (input.deltasNanoseconds.size() != ClockDeltaCount) return result;
    for (const auto delta : input.deltasNanoseconds)
    {
        if (!delta) return ClockCalibrationAssessment{};
        if (*delta != 0U && (!result.effectiveStep_ || *delta < *result.effectiveStep_))
            result.effectiveStep_ = *delta;
    }
    return result;
}

ClockAdequacyAssessment AssessClockAdequacy(const ClockCalibrationAssessment& calibration,
    MedianNanoseconds scale)
{
    ClockAdequacyAssessment result;
    if (!calibration.InputValid() || !Positive(scale)) return result;
    result.scale_ = scale;
    // q is integral: 100*q <= (whole + half/2) iff q <= whole/100.
    // Dividing first avoids overflow for any uint64 q or decision scale.
    result.adequate_ = *calibration.EffectiveStep() <= scale.whole / 100U;
    return result;
}

std::array<OrderedWindow, 4> PartitionOrderedRange(std::size_t begin, std::size_t end)
{
    if (!FrozenRange(begin, end)) throw std::invalid_argument("unsupported Stage-5 ordered range");
    const auto count = end - begin;
    std::array<OrderedWindow, 4> windows{};
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        const auto length = count / 4U + (i < count % 4U ? 1U : 0U);
        windows[i] = {begin, length};
        begin += length;
    }
    return windows;
}

OrderedStateAssessment AssessOrderedState(std::span<const std::uint64_t> observations,
    std::size_t begin)
{
    OrderedStateAssessment result;
    if (!FrozenRange(begin, observations.size())) return result;
    const auto eligible = observations.subspan(begin);
    if (std::any_of(eligible.begin(), eligible.end(), [](auto value) { return value == 0U; })) return result;
    result.windows_ = PartitionOrderedRange(begin, observations.size());
    std::array<MedianNanoseconds, 4> medians{};
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        const auto window = (*result.windows_)[i];
        medians[i] = Median(observations.subspan(window.begin, window.count));
    }
    result.medians_ = medians;
    bool increasing = true, decreasing = true;
    for (std::size_t i = 0U; i < 3U; ++i)
    {
        increasing = increasing && medians[i] <= medians[i + 1U];
        decreasing = decreasing && medians[i] >= medians[i + 1U];
        result.abruptStateSwitch_ = result.abruptStateSwitch_
            || !WithinRelativeTolerance(std::max(medians[i], medians[i + 1U]),
                std::min(medians[i], medians[i + 1U]), 10U);
    }
    result.persistentTrend_ = (increasing || decreasing)
        && !WithinRelativeTolerance(medians[0], medians[3], 20U);
    return result;
}

} // namespace computelab::ex2::stage5
