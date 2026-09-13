#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace computelab
{

[[nodiscard]] std::vector<std::uint32_t> GenerateSeededInput(
    std::uint64_t seed,
    std::size_t elementCount);

} // namespace computelab
