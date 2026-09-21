#include "ex2/Ex2CpuOracles.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace computelab::ex2
{

void ValidateUint32IndexedOracleElementCount(std::uint64_t elementCount)
{
    if (elementCount > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument(
            "EX-2 oracle element count exceeds uint32 index semantics");
    }
}

std::uint32_t TransformA1Value(
    std::uint32_t input,
    std::uint32_t index) noexcept
{
    return input ^ (0x9E3779B9U + index);
}

std::uint32_t TransformA2Value(
    std::uint32_t input,
    std::uint32_t index) noexcept
{
    std::uint32_t value = input ^ (0x9E3779B9U + index);
    for (std::uint32_t round = 0U; round < 16U; ++round)
    {
        value = (value ^ (value >> 16U)) * 0x7FEB352DU;
        value = (value ^ (value >> 15U)) * 0x846CA68BU;
    }
    return value ^ (value >> 16U);
}

std::vector<std::uint32_t> ReferenceA1(
    std::span<const std::uint32_t> input)
{
    ValidateUint32IndexedOracleElementCount(input.size());
    std::vector<std::uint32_t> output(input.size());
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        output[index] = TransformA1Value(
            input[index], static_cast<std::uint32_t>(index));
    }
    return output;
}

std::vector<std::uint32_t> ReferenceA2(
    std::span<const std::uint32_t> input)
{
    ValidateUint32IndexedOracleElementCount(input.size());
    std::vector<std::uint32_t> output(input.size());
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        output[index] = TransformA2Value(
            input[index], static_cast<std::uint32_t>(index));
    }
    return output;
}

} // namespace computelab::ex2
