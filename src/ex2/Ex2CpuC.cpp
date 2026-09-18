#include "ex2/Ex2CpuOracles.hpp"

#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace computelab::ex2
{
namespace
{

std::size_t CheckedCounterCount(
    std::uint64_t count,
    std::string_view description)
{
    if (count > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::overflow_error(
            std::string(description) + " exceeds uint32 counter semantics");
    }
    const auto converted = static_cast<std::size_t>(count);
    if (converted > std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            std::string(description) + " exceeds the contiguous buffer maximum");
    }
    return converted;
}

void ValidateContentionParameters(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    static_cast<void>(CheckedCounterCount(elementCount, "EX-2 C element count"));
    if (elementCount == 0U)
    {
        if (activeCounterCount != 0U)
        {
            throw std::invalid_argument(
                "EX-2 C zero elements require zero active counters");
        }
        return;
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
}

} // namespace

std::vector<std::uint32_t> GenerateContentionTargets(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    ValidateContentionParameters(elementCount, activeCounterCount);
    if (elementCount == 0U)
    {
        return {};
    }

    std::vector<std::uint32_t> targets(
        static_cast<std::size_t>(elementCount));
    for (std::uint64_t index = 0U; index < elementCount; ++index)
    {
        const std::uint64_t permutation =
            (8191ULL * index + 17ULL) % elementCount;
        targets[static_cast<std::size_t>(index)] =
            static_cast<std::uint32_t>(permutation % activeCounterCount);
    }
    return targets;
}

std::vector<std::uint32_t> ReferenceContentionHistogram(
    std::span<const std::uint32_t> targets,
    std::uint64_t counterCount)
{
    const std::size_t count = CheckedCounterCount(
        counterCount, "EX-2 C counter count");
    if (targets.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::overflow_error(
            "EX-2 C update count could overflow a uint32 counter");
    }
    if (!targets.empty() && counterCount == 0U)
    {
        throw std::invalid_argument(
            "EX-2 C nonempty targets require a counter buffer");
    }

    std::vector<std::uint32_t> counters(count, 0U);
    for (const std::uint32_t target : targets)
    {
        if (target >= counterCount)
        {
            throw std::invalid_argument("EX-2 C target is out of range");
        }
        if (counters[target] == std::numeric_limits<std::uint32_t>::max())
        {
            throw std::overflow_error("EX-2 C counter increment would overflow");
        }
        ++counters[target];
    }
    return counters;
}

ContentionReference ReferenceContention(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount)
{
    auto targets = GenerateContentionTargets(elementCount, activeCounterCount);
    auto counters = ReferenceContentionHistogram(targets, elementCount);
    return ContentionReference{
        std::move(targets),
        std::move(counters),
        static_cast<std::uint32_t>(activeCounterCount)};
}

bool ValidateContentionResult(
    const ContentionReference& reference,
    std::span<const std::uint32_t> actualCounters) noexcept
{
    if (reference.targets.size() != reference.counters.size()
        || actualCounters.size() != reference.counters.size()
        || reference.activeCounterCount > reference.counters.size())
    {
        return false;
    }

    std::uint64_t total{};
    for (std::size_t index = 0U; index < actualCounters.size(); ++index)
    {
        if (actualCounters[index] != reference.counters[index])
        {
            return false;
        }
        if (index >= reference.activeCounterCount && actualCounters[index] != 0U)
        {
            return false;
        }
        total += actualCounters[index];
    }
    for (const std::uint32_t target : reference.targets)
    {
        if (target >= reference.activeCounterCount)
        {
            return false;
        }
    }
    return total == reference.targets.size();
}

} // namespace computelab::ex2
