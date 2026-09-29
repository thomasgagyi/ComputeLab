#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2
{

inline constexpr std::string_view LogicalInputDomain =
    "ComputeLab/EX-2/logical-input/v1";

// Returns the canonical byte length for count uint32 words, rejecting any
// count whose four-byte representation cannot fit in the frozen uint64 field.
[[nodiscard]] std::uint64_t LogicalInputWordPayloadByteLength(
    std::uint64_t wordCount);

// Convenience for the declared C pre-operation state. The returned storage
// contains exactly wordCount uint32 zero words or throws before allocation.
[[nodiscard]] std::vector<std::uint32_t> MakeZeroInitialCounterState(
    std::uint64_t wordCount);

// ex2-logical-input-v1 typed workload contracts. All supplied words are fed
// to SHA-256 in canonical little-endian form without retaining the spans or
// constructing a second concatenated payload.
[[nodiscard]] std::string IndexedLogicalInputSha256(
    std::span<const std::uint32_t> primary,
    std::span<const std::uint32_t> permutation);
[[nodiscard]] std::string ContentionLogicalInputSha256(
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> initialCounters);

} // namespace computelab::ex2
