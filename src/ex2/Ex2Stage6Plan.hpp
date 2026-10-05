#pragma once

#include "ex2/Ex2Configuration.hpp"
#include <array>

namespace computelab::ex2::stage6
{
inline constexpr std::string_view ProtocolVersion = "1.3";
inline constexpr std::size_t CoreCellCount = 22, PairedBlockCount = 5;
inline constexpr std::size_t ProcessesPerBackend = 5, ChildrenPerCell = 10, TotalChildCount = 220;
inline constexpr std::uint64_t WarmupCount = 0, PlannedSampleCount = 100;
inline constexpr InstrumentMode Instrument = InstrumentMode::H;

struct PlannedProcess
{
    Backend backend{};
    std::uint64_t processIndex{}, blockIndex{}, orderSlot{};
    bool operator==(const PlannedProcess&) const = default;
};
struct PlannedSlot
{
    std::uint64_t sequenceIndex{}, cellIndex{};
    PlannedProcess process;
    bool operator==(const PlannedSlot&) const = default;
};
[[nodiscard]] std::array<PlannedProcess, ChildrenPerCell> FrozenCellProcessPlan();
[[nodiscard]] std::array<PlannedSlot, TotalChildCount> FrozenCampaignPlan();
[[nodiscard]] bool ValidateCellProcessPlan(std::span<const PlannedProcess>);
[[nodiscard]] bool ValidateCampaignPlan(std::span<const PlannedSlot>);
[[nodiscard]] const WorkloadConfiguration& WorkloadForCell(std::size_t);
void ValidatePlannedSlot(const PlannedSlot&);
} // namespace computelab::ex2::stage6
