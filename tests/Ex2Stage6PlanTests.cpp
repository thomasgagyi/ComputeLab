#include "ex2/Ex2Stage6Plan.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
TEST(Ex2Stage6Plan, ExactCoreWorkloadOwnershipAndControls)
{
    ASSERT_EQ(ex2::ApprovedCoreCells().size(), 22);
    EXPECT_EQ(s6::Instrument, ex2::InstrumentMode::H);
    EXPECT_EQ(s6::WarmupCount, 0); EXPECT_EQ(s6::PlannedSampleCount, 100);
    for (std::size_t i = 0; i < 22; ++i)
    {
        EXPECT_EQ(&s6::WorkloadForCell(i), &ex2::ApprovedCoreCells()[i]);
        EXPECT_EQ(ex2::ClassifyCellEligibility(s6::WorkloadForCell(i)), ex2::CellEligibility::ApprovedCoreCell);
        if (const auto* d = std::get_if<ex2::IterativeConfiguration>(&s6::WorkloadForCell(i).parameters))
            EXPECT_EQ(d->variant, ex2::IterativeVariant::D1);
        if (const auto* a = std::get_if<ex2::LinearConfiguration>(&s6::WorkloadForCell(i).parameters))
            EXPECT_LE(a->elementCount, 16777216);
    }
    EXPECT_THROW((void)s6::WorkloadForCell(22), std::invalid_argument);
}
TEST(Ex2Stage6Plan, ExactTenProcessOrderAnd220SlotFormula)
{
    const std::array<ex2::Backend, 10> order{ex2::Backend::Cuda, ex2::Backend::Vulkan,
        ex2::Backend::Vulkan, ex2::Backend::Cuda, ex2::Backend::Cuda, ex2::Backend::Vulkan,
        ex2::Backend::Vulkan, ex2::Backend::Cuda, ex2::Backend::Cuda, ex2::Backend::Vulkan};
    const auto cell = s6::FrozenCellProcessPlan(); const auto campaign = s6::FrozenCampaignPlan();
    EXPECT_TRUE(s6::ValidateCellProcessPlan(cell)); EXPECT_TRUE(s6::ValidateCampaignPlan(campaign));
    for (std::size_t c = 0; c < 22; ++c)
    {
        std::size_t cuda = 0, vulkan = 0;
        for (std::size_t i = 0; i < 10; ++i)
        {
            const auto& p = cell[i]; const auto& slot = campaign[c * 10 + i];
            EXPECT_EQ(p.backend, order[i]); EXPECT_EQ(p.processIndex, i / 2);
            EXPECT_EQ(p.blockIndex, i / 2); EXPECT_EQ(p.orderSlot, i % 2);
            EXPECT_EQ(slot.cellIndex, c); EXPECT_EQ(slot.sequenceIndex, c * 10 + p.blockIndex * 2 + p.orderSlot);
            EXPECT_EQ(slot.process, p); EXPECT_NO_THROW(s6::ValidatePlannedSlot(slot));
            (p.backend == ex2::Backend::Cuda ? cuda : vulkan)++;
        }
        EXPECT_EQ(cuda, 5); EXPECT_EQ(vulkan, 5);
    }
}
TEST(Ex2Stage6Plan, RejectsMalformedDuplicateReorderedAndInvalidBackend)
{
    const auto good = s6::FrozenCampaignPlan();
    for (unsigned defect = 0; defect < 8; ++defect)
    {
        auto slots = good;
        if (defect == 0) std::swap(slots[0], slots[1]);
        if (defect == 1) slots[1] = slots[0];
        if (defect == 2) slots[219].cellIndex = 22;
        if (defect == 3) slots[0].sequenceIndex = 220;
        if (defect == 4) slots[0].process.backend = static_cast<ex2::Backend>(99);
        if (defect == 5) slots[0].process.processIndex = 5;
        if (defect == 6) slots[0].process.blockIndex = 1;
        if (defect == 7) slots[0].process.orderSlot = 2;
        EXPECT_FALSE(s6::ValidateCampaignPlan(slots));
    }
    EXPECT_FALSE(s6::ValidateCampaignPlan(std::span{good}.first(219)));
    auto cell = s6::FrozenCellProcessPlan(); cell[0].backend = static_cast<ex2::Backend>(99);
    EXPECT_FALSE(s6::ValidateCellProcessPlan(cell));
    cell = s6::FrozenCellProcessPlan(); std::reverse(cell.begin(), cell.end()); EXPECT_FALSE(s6::ValidateCellProcessPlan(cell));
}
} // namespace
