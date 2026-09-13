#include "input/SeededInput.hpp"

#include <random>

namespace computelab
{

std::vector<std::uint32_t> GenerateSeededInput(
    std::uint64_t seed,
    std::size_t elementCount)
{
    std::mt19937_64 engine(seed);
    std::vector<std::uint32_t> values;
    values.reserve(elementCount);

    constexpr std::uint64_t low32Mask = (std::uint64_t{1} << 32U) - 1U;
    for (std::size_t index = 0; index < elementCount; ++index)
    {
        values.push_back(static_cast<std::uint32_t>(engine() & low32Mask));
    }

    return values;
}

} // namespace computelab
