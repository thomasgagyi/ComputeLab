#pragma once

#include "ex2/Ex2SemanticTypes.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace computelab::ex2
{

// A-D invocation indices and D pass indices are defined as uint32_t. This
// shared precondition lets callers reject oversized logical spans without
// constructing an enormous allocation merely to exercise the check.
void ValidateUint32IndexedOracleElementCount(std::uint64_t elementCount);

[[nodiscard]] std::uint32_t TransformA1Value(
    std::uint32_t input,
    std::uint32_t index) noexcept;
[[nodiscard]] std::uint32_t TransformA2Value(
    std::uint32_t input,
    std::uint32_t index) noexcept;
[[nodiscard]] std::vector<std::uint32_t> ReferenceA1(
    std::span<const std::uint32_t> input);
[[nodiscard]] std::vector<std::uint32_t> ReferenceA2(
    std::span<const std::uint32_t> input);

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
[[nodiscard]] std::vector<std::uint32_t> ReferenceB1Gather(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices);
[[nodiscard]] std::vector<std::uint32_t> ReferenceB2Scatter(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> indices);

inline constexpr std::uint64_t ContentionElementCount = 1'048'576ULL;
inline constexpr std::uint64_t ContentionActiveAll = ContentionElementCount;
inline constexpr std::uint64_t ContentionActiveOnePer32 =
    ContentionElementCount / 32ULL;
inline constexpr std::uint64_t ContentionActive64 = 64ULL;

struct ContentionReference
{
    std::vector<std::uint32_t> targets;
    std::vector<std::uint32_t> counters;
    std::uint32_t activeCounterCount{};
};

[[nodiscard]] std::vector<std::uint32_t> GenerateContentionTargets(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount);
[[nodiscard]] std::vector<std::uint32_t> ReferenceContentionHistogram(
    std::span<const std::uint32_t> targets,
    std::uint64_t counterCount);
[[nodiscard]] ContentionReference ReferenceContention(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount);
[[nodiscard]] bool ValidateContentionResult(
    const ContentionReference& reference,
    std::span<const std::uint32_t> actualCounters) noexcept;

enum class IterativeFinalBuffer
{
    StateA,
    StateB,
};

struct IterativeReference
{
    std::vector<std::uint32_t> finalState;
    IterativeFinalBuffer finalBuffer{IterativeFinalBuffer::StateA};
};

[[nodiscard]] std::uint32_t TransformD1Value(
    std::uint32_t previous,
    std::uint32_t index,
    std::uint32_t pass) noexcept;
[[nodiscard]] IterativeReference ReferenceD1(
    std::span<const std::uint32_t> initialState,
    std::uint64_t iterationCount);
[[nodiscard]] IterativeReference ReferenceD1(
    std::uint64_t seed,
    std::uint64_t elementCount,
    std::uint64_t iterationCount);

struct TransferReference
{
    TransferDirection direction{TransferDirection::HostToDevice};
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> expectedDestination;
};

[[nodiscard]] TransferReference ReferenceTransfer(
    std::uint64_t seed,
    std::uint64_t byteCount,
    TransferDirection direction);
[[nodiscard]] bool ValidateTransferOutput(
    const TransferReference& reference,
    std::span<const std::uint8_t> actualDestination) noexcept;

} // namespace computelab::ex2
