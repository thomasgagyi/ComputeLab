#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace computelab::ex2::stage5
{

inline constexpr std::size_t ClockCaptureCount = 4096U;
inline constexpr std::size_t ClockDeltaCount = ClockCaptureCount - 1U;
inline constexpr std::string_view HostClockCsvHeader =
    "schema_version,run_id,series_id,process_index,sequence_index,delta_ns";

// Exact Stage-5 integer/half-nanosecond median, even near UINT64_MAX.
struct MedianNanoseconds
{
    std::uint64_t whole{};
    bool half{};
    auto operator<=>(const MedianNanoseconds&) const = default;
    [[nodiscard]] double AsDouble() const noexcept;
};
[[nodiscard]] MedianNanoseconds Median(std::span<const std::uint64_t> values);
[[nodiscard]] double RelativeDifference(MedianNanoseconds a, MedianNanoseconds b);
// Narrow shared Stage-5 reciprocal-tolerance arithmetic, with no epsilon.
// The supported divisors are 20 (5%), 50 (2%) and 10 (10%).
[[nodiscard]] bool WithinRelativeTolerance(MedianNanoseconds a,
    MedianNanoseconds reference, std::uint64_t divisor);

struct ClockCalibrationInput
{
    // Consecutive capture deltas in order. Null represents missing/decreasing
    // capture, invalid conversion, or unrepresentable arithmetic; never zero.
    std::vector<std::optional<std::uint64_t>> deltasNanoseconds;
};

class ClockCalibrationAssessment;
[[nodiscard]] ClockCalibrationAssessment AssessClockCalibration(const ClockCalibrationInput& input);
class ClockCalibrationAssessment final
{
public:
    [[nodiscard]] bool InputValid() const noexcept { return effectiveStep_.has_value(); }
    [[nodiscard]] auto EffectiveStep() const noexcept { return effectiveStep_; }
private:
    friend ClockCalibrationAssessment AssessClockCalibration(const ClockCalibrationInput&);
    std::optional<std::uint64_t> effectiveStep_;
};

class ClockAdequacyAssessment;
[[nodiscard]] ClockAdequacyAssessment AssessClockAdequacy(
    const ClockCalibrationAssessment& calibration, MedianNanoseconds scale);
class ClockAdequacyAssessment final
{
public:
    [[nodiscard]] bool InputValid() const noexcept { return scale_.has_value(); }
    [[nodiscard]] bool Adequate() const noexcept { return adequate_; }
    [[nodiscard]] const auto& DecisionScale() const noexcept { return scale_; }
private:
    friend ClockAdequacyAssessment AssessClockAdequacy(const ClockCalibrationAssessment&, MedianNanoseconds);
    std::optional<MedianNanoseconds> scale_;
    bool adequate_{};
};

struct OrderedWindow
{
    std::size_t begin{}, count{};
    bool operator==(const OrderedWindow&) const = default;
};
// Only the frozen Stage-5 eligible ranges are accepted; no statistics framework.
[[nodiscard]] std::array<OrderedWindow, 4> PartitionOrderedRange(std::size_t begin, std::size_t end);

class OrderedStateAssessment;
[[nodiscard]] OrderedStateAssessment AssessOrderedState(
    std::span<const std::uint64_t> observations, std::size_t begin = 0U);
class OrderedStateAssessment final
{
public:
    [[nodiscard]] bool InputValid() const noexcept { return medians_.has_value(); }
    [[nodiscard]] const auto& Windows() const noexcept { return windows_; }
    [[nodiscard]] const auto& Medians() const noexcept { return medians_; }
    [[nodiscard]] bool PersistentTrend() const noexcept { return persistentTrend_; }
    [[nodiscard]] bool AbruptStateSwitch() const noexcept { return abruptStateSwitch_; }
    [[nodiscard]] bool StateStructure() const noexcept { return persistentTrend_ || abruptStateSwitch_; }
private:
    friend OrderedStateAssessment AssessOrderedState(std::span<const std::uint64_t>, std::size_t);
    std::optional<std::array<OrderedWindow, 4>> windows_;
    std::optional<std::array<MedianNanoseconds, 4>> medians_;
    bool persistentTrend_{}, abruptStateSwitch_{};
};

} // namespace computelab::ex2::stage5
