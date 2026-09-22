#include "ex2/Ex2IndexPermutation.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace computelab::ex2
{
namespace
{

std::size_t CheckedIndexCount(std::uint64_t elementCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 oracle element count exceeds uint32 index semantics");
    }

    const auto count = static_cast<std::size_t>(elementCount);
    if (count > std::vector<std::uint32_t>{}.max_size())
    {
        throw std::length_error(
            "EX-2 B permutation exceeds the contiguous buffer maximum");
    }
    return count;
}

std::uint64_t SplitMixFinalizer(std::uint64_t value) noexcept
{
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace

std::vector<std::uint32_t> GenerateStructuredPermutation(
    std::uint64_t elementCount)
{
    const std::size_t count = CheckedIndexCount(elementCount);
    if (elementCount == 0U)
    {
        return {};
    }
    if (std::gcd(8191ULL, elementCount) != 1U)
    {
        throw std::invalid_argument(
            "EX-2 B structured-v1 requires gcd(8191, N) == 1");
    }

    std::vector<std::uint32_t> indices(count);
    for (std::uint64_t index = 0U; index < elementCount; ++index)
    {
        indices[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(
            (8191ULL * index + 17ULL) % elementCount);
    }
    return indices;
}

std::vector<std::uint32_t> GenerateShuffledPermutation(
    std::uint64_t seed,
    std::uint64_t elementCount)
{
    const std::size_t count = CheckedIndexCount(elementCount);
    std::vector<std::uint32_t> indices(count);
    for (std::uint64_t index = 0U; index < elementCount; ++index)
    {
        indices[static_cast<std::size_t>(index)] =
            static_cast<std::uint32_t>(index);
    }

    std::uint64_t state = seed;
    for (std::uint64_t index = elementCount; index > 1U; --index)
    {
        state += 0x9E3779B97F4A7C15ULL;
        const std::uint64_t random = SplitMixFinalizer(state);
        const std::uint64_t selected = random % index;
        std::swap(indices[static_cast<std::size_t>(index - 1U)],
            indices[static_cast<std::size_t>(selected)]);
    }
    return indices;
}

void ValidateIndexPermutation(
    std::span<const std::uint32_t> indices,
    std::uint64_t expectedElementCount)
{
    const std::size_t expectedCount = CheckedIndexCount(expectedElementCount);
    if (indices.size() != expectedCount)
    {
        throw std::invalid_argument(
            "EX-2 B index length must equal the logical element count");
    }

    std::vector<std::uint8_t> seen(expectedCount, 0U);
    for (const std::uint32_t index : indices)
    {
        if (index >= expectedElementCount)
        {
            throw std::invalid_argument("EX-2 B index is out of range");
        }
        if (seen[index] != 0U)
        {
            throw std::invalid_argument(
                "EX-2 B index array contains a duplicate destination");
        }
        seen[index] = 1U;
    }
}

} // namespace computelab::ex2
