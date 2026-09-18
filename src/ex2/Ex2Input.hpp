#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2
{

inline constexpr std::string_view InputGeneratorRevision = "ex2-mix64-v1";
inline constexpr std::uint64_t CoreInputSeed = 0x0123456789ABCDEFULL;

[[nodiscard]] std::uint64_t Mix(
    std::uint64_t seed,
    std::uint64_t index) noexcept;
[[nodiscard]] std::uint32_t Value(
    std::uint64_t seed,
    std::uint64_t index) noexcept;
[[nodiscard]] std::uint8_t Byte(
    std::uint64_t seed,
    std::uint64_t index) noexcept;

// Returned vectors own exactly logicalCount contiguous elements. Requests that
// cannot be represented by the process or the vector type throw length_error.
[[nodiscard]] std::vector<std::uint32_t> GenerateWordInput(
    std::uint64_t seed,
    std::uint64_t logicalCount);
[[nodiscard]] std::vector<std::uint8_t> GenerateByteInput(
    std::uint64_t seed,
    std::uint64_t logicalCount);

// Each uint32 word is encoded least-significant byte first. These are the
// canonical bytes to hash and later supply to an EX-2 GPU word buffer.
[[nodiscard]] std::vector<std::uint8_t> EncodeWordInputLittleEndian(
    std::span<const std::uint32_t> words);
[[nodiscard]] std::string WordInputSha256(
    std::span<const std::uint32_t> words);
[[nodiscard]] std::string ByteInputSha256(
    std::span<const std::uint8_t> bytes);

} // namespace computelab::ex2
