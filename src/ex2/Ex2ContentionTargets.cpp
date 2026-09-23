#include "ex2/Ex2ContentionTargets.hpp"

#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace computelab::ex2
{
namespace
{

std::size_t ValidateParametersAndGetCount(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::overflow_error(
            "EX-2 C element count exceeds uint32 counter semantics");
    }

    const auto count = static_cast<std::size_t>(elementCount);
    if (count > std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 C element count exceeds the contiguous buffer maximum");
    }

    if (elementCount == 0U)
    {
        if (activeCounterCount != 0U)
        {
            throw std::invalid_argument(
                "EX-2 C zero elements require zero active counters");
        }
        return count;
    }
    if (activeCounterCount == 0U || activeCounterCount > elementCount)
    {
        throw std::invalid_argument(
            "EX-2 C active counter count must be in [1, N]");
    }
    if (elementCount % activeCounterCount != 0U)
    {
        throw std::invalid_argument(
            "EX-2 C active counter count must divide N exactly");
    }
    if (std::gcd(8191ULL, elementCount) != 1U)
    {
        throw std::invalid_argument(
            "EX-2 C target permutation requires gcd(8191, N) == 1");
    }
    return count;
}

std::uint32_t ContentionTargetAt(
    std::uint64_t index,
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount) noexcept
{
    const std::uint64_t permutation =
        (8191ULL * index + 17ULL) % elementCount;
    return static_cast<std::uint32_t>(permutation % activeCounterCount);
}

} // namespace

std::vector<std::uint32_t> GenerateContentionTargets(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    const std::size_t count = ValidateParametersAndGetCount(
        elementCount, activeCounterCount);
    std::vector<std::uint32_t> targets(count);
    for (std::uint64_t index = 0U; index < elementCount; ++index)
    {
        targets[static_cast<std::size_t>(index)] = ContentionTargetAt(
            index, elementCount, activeCounterCount);
    }
    return targets;
}

void ValidateContentionTargets(
    std::span<const std::uint32_t> targets,
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    const std::size_t expectedCount = ValidateParametersAndGetCount(
        elementCount, activeCounterCount);
    if (targets.size() != expectedCount)
    {
        throw std::invalid_argument(
            "EX-2 C target length must equal the logical element count");
    }

    for (std::uint64_t index = 0U; index < elementCount; ++index)
    {
        const std::uint32_t target = targets[static_cast<std::size_t>(index)];
        if (target >= activeCounterCount)
        {
            throw std::invalid_argument("EX-2 C target is out of active range");
        }
        if (target != ContentionTargetAt(
                index, elementCount, activeCounterCount))
        {
            throw std::invalid_argument(
                "EX-2 C target does not match the deterministic workload");
        }
    }
}

} // namespace computelab::ex2
