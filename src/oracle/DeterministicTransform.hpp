#pragma once

#include <cstdint>
#include <vector>

namespace computelab
{

[[nodiscard]] std::uint32_t TransformValue(
    std::uint32_t value,
    std::uint32_t index) noexcept;

[[nodiscard]] std::vector<std::uint32_t> TransformSequence(
    const std::vector<std::uint32_t>& input);

} // namespace computelab
