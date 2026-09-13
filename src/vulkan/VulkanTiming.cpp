#include "vulkan/VulkanTiming.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace computelab::vulkan
{

std::uint64_t TimestampNanoseconds(
    std::uint64_t start, std::uint64_t stop,
    std::uint32_t validBits, long double timestampPeriod)
{
    if (validBits < 36 || validBits > 64)
        throw std::invalid_argument("Vulkan timestampValidBits must be in [36, 64]");
    if (!std::isfinite(timestampPeriod) || timestampPeriod <= 0)
        throw std::invalid_argument("Vulkan timestampPeriod must be finite and positive");

    std::uint64_t delta;
    if (validBits == 64)
        delta = stop - start;
    else
    {
        const auto mask = (std::uint64_t{1} << validBits) - 1;
        start &= mask;
        stop &= mask;
        delta = (stop - start) & mask;
    }
    // Modular subtraction handles an ordinary rollover. Multiple complete wraps
    // cannot be inferred and are outside EX-1's short-dispatch envelope.
    // Preserve the exact integer identity, including UINT64_MAX, when no scale
    // conversion is needed (MSVC long double cannot represent every uint64).
    if (timestampPeriod == 1.0L) return delta;
    const long double nanoseconds = static_cast<long double>(delta) * timestampPeriod;
    const long double rounded = std::round(nanoseconds);
    // An exact exclusive 2^64 bound is safe even on MSVC, where long double has
    // binary64 precision and converting UINT64_MAX to long double rounds upward.
    if (!std::isfinite(nanoseconds) || nanoseconds < 0 ||
        !std::isfinite(rounded) || rounded < 0 || rounded >= std::ldexp(1.0L, 64))
        throw std::overflow_error("Vulkan timestamp duration is outside uint64 nanoseconds");
    return static_cast<std::uint64_t>(rounded);
}

namespace detail
{
void CheckResult(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult=" +
            std::to_string(static_cast<int>(result)) + ")");
}

std::uint64_t DecodeTimestampQueries(
    VkResult result, const std::array<TimestampQuery, 2>& queries,
    std::uint32_t validBits, long double timestampPeriod)
{
    CheckResult(result, "vkGetQueryPoolResults after explicit fence completion");
    if (queries[0].available == 0 || queries[1].available == 0)
        throw std::runtime_error("vkGetQueryPoolResults: timestamp unavailable after fence completion");
    return TimestampNanoseconds(queries[0].ticks, queries[1].ticks, validBits, timestampPeriod);
}
} // namespace detail
} // namespace computelab::vulkan
