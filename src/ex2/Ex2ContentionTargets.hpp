#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace computelab::ex2
{

inline constexpr std::uint64_t ContentionElementCount = 1'048'576ULL;
inline constexpr std::uint64_t ContentionActiveAll = ContentionElementCount;
inline constexpr std::uint64_t ContentionActiveOnePer32 =
    ContentionElementCount / 32ULL;
inline constexpr std::uint64_t ContentionActive64 = 64ULL;

[[nodiscard]] std::vector<std::uint32_t> GenerateContentionTargets(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount);

// Validates both the C parameter contract and every supplied deterministic
// target. An arbitrary in-range histogram input is not the declared workload.
void ValidateContentionTargets(
    std::span<const std::uint32_t> targets,
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount);

} // namespace computelab::ex2
