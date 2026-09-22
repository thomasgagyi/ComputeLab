#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace computelab::ex2
{

inline constexpr std::string_view StructuredIndexRevision = "structured-v1";
inline constexpr std::string_view ShuffledIndexRevision = "shuffled-v1";

[[nodiscard]] std::vector<std::uint32_t> GenerateStructuredPermutation(
    std::uint64_t elementCount);
[[nodiscard]] std::vector<std::uint32_t> GenerateShuffledPermutation(
    std::uint64_t seed,
    std::uint64_t elementCount);
void ValidateIndexPermutation(
    std::span<const std::uint32_t> indices,
    std::uint64_t expectedElementCount);

} // namespace computelab::ex2
