#include "ex2/Ex2CpuOracles.hpp"

#include <cstddef>
#include <stdexcept>

namespace computelab::ex2
{
namespace
{

void ValidateInputAndIndices(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices)
{
    if (input.size() != indices.size())
    {
        throw std::invalid_argument(
            "EX-2 B input and index lengths must match");
    }
    ValidateIndexPermutation(indices, input.size());
}

} // namespace

std::vector<std::uint32_t> ReferenceB1Gather(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices)
{
    ValidateInputAndIndices(input, indices);
    std::vector<std::uint32_t> output(input.size());
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        output[index] = TransformA1Value(
            input[indices[index]], static_cast<std::uint32_t>(index));
    }
    return output;
}

std::vector<std::uint32_t> ReferenceB2Scatter(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices)
{
    ValidateInputAndIndices(input, indices);
    std::vector<std::uint32_t> output(input.size());
    for (std::size_t index = 0U; index < input.size(); ++index)
    {
        output[indices[index]] = TransformA1Value(
            input[index], static_cast<std::uint32_t>(index));
    }
    return output;
}

} // namespace computelab::ex2
