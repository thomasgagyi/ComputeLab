#include "ex2/Ex2Input.hpp"

#include "ex2/Ex2Sha256.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace computelab::ex2
{
namespace
{

template <typename T>
std::size_t CheckedElementCount(std::uint64_t logicalCount)
{
    if (logicalCount > std::numeric_limits<std::size_t>::max())
    {
        throw std::length_error(
            "EX-2 input count exceeds this process's addressable size");
    }

    const auto count = static_cast<std::size_t>(logicalCount);
    if (count > std::vector<T>{}.max_size())
    {
        throw std::length_error(
            "EX-2 input count exceeds the contiguous buffer maximum");
    }
    return count;
}

} // namespace

std::uint64_t Mix(std::uint64_t seed, std::uint64_t index) noexcept
{
    std::uint64_t value = seed + index + 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint32_t Value(std::uint64_t seed, std::uint64_t index) noexcept
{
    return static_cast<std::uint32_t>(Mix(seed, index) & 0xFFFFFFFFULL);
}

std::uint8_t Byte(std::uint64_t seed, std::uint64_t index) noexcept
{
    return static_cast<std::uint8_t>(Mix(seed, index) & 0xFFULL);
}

std::vector<std::uint32_t> GenerateWordInput(
    std::uint64_t seed,
    std::uint64_t logicalCount)
{
    std::vector<std::uint32_t> words(
        CheckedElementCount<std::uint32_t>(logicalCount));
    for (std::uint64_t index = 0U; index < logicalCount; ++index)
    {
        words[static_cast<std::size_t>(index)] = Value(seed, index);
    }
    return words;
}

std::vector<std::uint8_t> GenerateByteInput(
    std::uint64_t seed,
    std::uint64_t logicalCount)
{
    std::vector<std::uint8_t> bytes(
        CheckedElementCount<std::uint8_t>(logicalCount));
    for (std::uint64_t index = 0U; index < logicalCount; ++index)
    {
        bytes[static_cast<std::size_t>(index)] = Byte(seed, index);
    }
    return bytes;
}

std::vector<std::uint8_t> EncodeWordInputLittleEndian(
    std::span<const std::uint32_t> words)
{
    constexpr std::size_t bytesPerWord = sizeof(std::uint32_t);
    const std::size_t maximumByteCount = std::vector<std::uint8_t>{}.max_size();
    if (words.size() > maximumByteCount / bytesPerWord)
    {
        throw std::length_error(
            "EX-2 word input byte representation exceeds the contiguous buffer maximum");
    }

    std::vector<std::uint8_t> bytes(words.size() * bytesPerWord);
    for (std::size_t index = 0U; index < words.size(); ++index)
    {
        const std::uint32_t word = words[index];
        const std::size_t offset = index * bytesPerWord;
        bytes[offset] = static_cast<std::uint8_t>(word & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((word >> 8U) & 0xFFU);
        bytes[offset + 2U] = static_cast<std::uint8_t>((word >> 16U) & 0xFFU);
        bytes[offset + 3U] = static_cast<std::uint8_t>(word >> 24U);
    }
    return bytes;
}

std::string WordInputSha256(std::span<const std::uint32_t> words)
{
    const auto bytes = EncodeWordInputLittleEndian(words);
    return Sha256(std::as_bytes(std::span{bytes}));
}

std::string ByteInputSha256(std::span<const std::uint8_t> bytes)
{
    return Sha256(std::as_bytes(bytes));
}

} // namespace computelab::ex2
