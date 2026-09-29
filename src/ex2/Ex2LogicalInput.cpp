#include "ex2/Ex2LogicalInput.hpp"

#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace computelab::ex2
{
namespace
{

constexpr std::uint32_t kComponentCount = 2U;
constexpr std::size_t kWordsPerChunk = 1024U;

template <typename T>
void UpdateBytes(Sha256Hasher& hasher, const T& bytes)
{
    hasher.Update(std::as_bytes(std::span{bytes}));
}

void UpdateUint32LittleEndian(Sha256Hasher& hasher, std::uint32_t value)
{
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>(value >> 24U)};
    UpdateBytes(hasher, bytes);
}

void UpdateUint64LittleEndian(Sha256Hasher& hasher, std::uint64_t value)
{
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    UpdateBytes(hasher, bytes);
}

std::uint64_t SpanWordCount(std::span<const std::uint32_t> words)
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t))
    {
        if (words.size() > std::numeric_limits<std::uint64_t>::max())
            throw std::length_error("logical-input word count exceeds uint64");
    }
    return static_cast<std::uint64_t>(words.size());
}

void UpdateWords(
    Sha256Hasher& hasher,
    std::span<const std::uint32_t> words)
{
    std::array<std::uint8_t, kWordsPerChunk * 4U> bytes{};
    std::size_t offset = 0U;
    while (offset < words.size())
    {
        const std::size_t count = std::min(
            kWordsPerChunk, words.size() - offset);
        for (std::size_t index = 0U; index < count; ++index)
        {
            const std::uint32_t word = words[offset + index];
            const std::size_t byteOffset = index * 4U;
            bytes[byteOffset] = static_cast<std::uint8_t>(word & 0xFFU);
            bytes[byteOffset + 1U] =
                static_cast<std::uint8_t>((word >> 8U) & 0xFFU);
            bytes[byteOffset + 2U] =
                static_cast<std::uint8_t>((word >> 16U) & 0xFFU);
            bytes[byteOffset + 3U] =
                static_cast<std::uint8_t>(word >> 24U);
        }
        hasher.Update(std::as_bytes(std::span{bytes.data(), count * 4U}));
        offset += count;
    }
}

void UpdateComponent(
    Sha256Hasher& hasher,
    std::string_view name,
    std::span<const std::uint32_t> words)
{
    if (name.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("logical-input component name exceeds uint32");
    const std::uint64_t payloadBytes = LogicalInputWordPayloadByteLength(
        SpanWordCount(words));
    UpdateUint32LittleEndian(hasher, static_cast<std::uint32_t>(name.size()));
    hasher.Update(std::as_bytes(std::span{name.data(), name.size()}));
    UpdateUint64LittleEndian(hasher, payloadBytes);
    UpdateWords(hasher, words);
}

std::string LogicalInputSha256(
    std::string_view firstName,
    std::span<const std::uint32_t> first,
    std::string_view secondName,
    std::span<const std::uint32_t> second)
{
    Sha256Hasher hasher;
    hasher.Update(std::as_bytes(std::span{
        LogicalInputDomain.data(), LogicalInputDomain.size()}));
    const std::array<std::uint8_t, 1> nul{0U};
    UpdateBytes(hasher, nul);
    UpdateUint32LittleEndian(hasher, kComponentCount);
    UpdateComponent(hasher, firstName, first);
    UpdateComponent(hasher, secondName, second);
    return hasher.Finish();
}

} // namespace

std::uint64_t LogicalInputWordPayloadByteLength(std::uint64_t wordCount)
{
    if (wordCount > std::numeric_limits<std::uint64_t>::max() / 4U)
        throw std::length_error(
            "logical-input word payload byte length exceeds uint64");
    return wordCount * 4U;
}

std::vector<std::uint32_t> MakeZeroInitialCounterState(
    std::uint64_t wordCount)
{
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t))
    {
        if (wordCount > std::numeric_limits<std::size_t>::max())
            throw std::length_error(
                "initial counter count exceeds this process's addressable size");
    }
    const auto count = static_cast<std::size_t>(wordCount);
    if (count > std::vector<std::uint32_t>{}.max_size())
        throw std::length_error(
            "initial counter count exceeds the contiguous buffer maximum");
    return std::vector<std::uint32_t>(count, 0U);
}

std::string IndexedLogicalInputSha256(
    std::span<const std::uint32_t> primary,
    std::span<const std::uint32_t> permutation)
{
    return LogicalInputSha256(
        "primary_words", primary, "permutation_words", permutation);
}

std::string ContentionLogicalInputSha256(
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> initialCounters)
{
    return LogicalInputSha256(
        "targets_words", targets,
        "initial_counters_words", initialCounters);
}

} // namespace computelab::ex2
