#include "ex2/Ex2Stage6Plan.hpp"
#include <algorithm>
#include <stdexcept>

namespace computelab::ex2::stage6
{
const WorkloadConfiguration& WorkloadForCell(std::size_t index)
{
    const auto& cells = ApprovedCoreCells();
    if (cells.size() != CoreCellCount || index >= CoreCellCount)
        throw std::invalid_argument("Stage-6 requires an exact approved core cell");
    return cells[index];
}
std::array<PlannedProcess, ChildrenPerCell> FrozenCellProcessPlan()
{
    std::array<PlannedProcess, ChildrenPerCell> result{};
    for (std::uint64_t block = 0; block < PairedBlockCount; ++block)
        for (std::uint64_t order = 0; order < 2; ++order)
            result[block * 2 + order] = {
                (block + order) % 2 == 0 ? Backend::Cuda : Backend::Vulkan, block, block, order};
    return result;
}
std::array<PlannedSlot, TotalChildCount> FrozenCampaignPlan()
{
    std::array<PlannedSlot, TotalChildCount> result{};
    const auto processes = FrozenCellProcessPlan();
    for (std::size_t cell = 0; cell < CoreCellCount; ++cell)
    {
        (void)WorkloadForCell(cell);
        for (std::size_t i = 0; i < ChildrenPerCell; ++i)
            result[cell * ChildrenPerCell + i] = {cell * ChildrenPerCell + i, cell, processes[i]};
    }
    return result;
}
bool ValidateCellProcessPlan(std::span<const PlannedProcess> input)
{
    const auto expected = FrozenCellProcessPlan();
    return ApprovedCoreCells().size() == CoreCellCount && std::ranges::equal(input, expected);
}
bool ValidateCampaignPlan(std::span<const PlannedSlot> input)
{
    if (ApprovedCoreCells().size() != CoreCellCount) return false;
    return std::ranges::equal(input, FrozenCampaignPlan());
}
void ValidatePlannedSlot(const PlannedSlot& slot)
{
    (void)WorkloadForCell(static_cast<std::size_t>(slot.cellIndex));
    if (slot.cellIndex >= CoreCellCount || slot.sequenceIndex >= TotalChildCount
        || slot != FrozenCampaignPlan()[slot.sequenceIndex])
        throw std::invalid_argument("Stage-6 slot disagrees with frozen schedule");
}
} // namespace computelab::ex2::stage6
