#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace computelab::ex2
{
namespace
{

void ValidateIterationCount(std::uint64_t iterationCount)
{
    if (iterationCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 D iteration count exceeds uint32 pass semantics");
    }
}

} // namespace

std::uint32_t TransformD1Value(
    std::uint32_t previous,
    std::uint32_t index,
    std::uint32_t pass) noexcept
{
    const std::uint32_t value =
        previous ^ (0x9E3779B9U + index + pass);
    const std::uint32_t rotated = (value << 5U) | (value >> 27U);
    return rotated + 0x7F4A7C15U;
}

IterativeReference ReferenceD1(
    std::span<const std::uint32_t> initialState,
    std::uint64_t iterationCount)
{
    ValidateIterationCount(iterationCount);
    std::vector<std::uint32_t> stateA(initialState.begin(), initialState.end());
    std::vector<std::uint32_t> stateB(initialState.size());
    bool previousIsA = true;

    for (std::uint64_t pass = 0U; pass < iterationCount; ++pass)
    {
        const auto& previous = previousIsA ? stateA : stateB;
        auto& next = previousIsA ? stateB : stateA;
        for (std::size_t index = 0U; index < previous.size(); ++index)
        {
            next[index] = TransformD1Value(
                previous[index],
                static_cast<std::uint32_t>(index),
                static_cast<std::uint32_t>(pass));
        }
        previousIsA = !previousIsA;
    }

    return IterativeReference{
        previousIsA ? std::move(stateA) : std::move(stateB),
        previousIsA
            ? IterativeFinalBuffer::StateA
            : IterativeFinalBuffer::StateB};
}

IterativeReference ReferenceD1(
    std::uint64_t seed,
    std::uint64_t elementCount,
    std::uint64_t iterationCount)
{
    ValidateIterationCount(iterationCount);
    const auto initialState = GenerateWordInput(seed, elementCount);
    return ReferenceD1(initialState, iterationCount);
}

} // namespace computelab::ex2
