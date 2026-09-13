#include "oracle/DeterministicTransform.hpp"

#include <cstddef>

namespace computelab
{

std::uint32_t TransformValue(
    std::uint32_t value,
    std::uint32_t index) noexcept
{
    value ^= index * 0x9E3779B9U;
    value ^= value >> 16U;
    value *= 0x85EBCA6BU;
    value ^= value >> 13U;
    value *= 0xC2B2AE35U;
    value ^= value >> 16U;
    return value;
}

std::vector<std::uint32_t> TransformSequence(
    const std::vector<std::uint32_t>& input)
{
    std::vector<std::uint32_t> output;
    output.reserve(input.size());

    for (std::size_t index = 0; index < input.size(); ++index)
    {
        output.push_back(TransformValue(
            input[index],
            static_cast<std::uint32_t>(index)));
    }

    return output;
}

} // namespace computelab
