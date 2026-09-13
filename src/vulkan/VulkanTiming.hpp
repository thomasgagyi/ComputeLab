#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace computelab::vulkan
{

// Storage units only: these values do not assert nanosecond measurement precision.
[[nodiscard]] std::uint64_t TimestampNanoseconds(
    std::uint64_t start, std::uint64_t stop,
    std::uint32_t validBits, long double timestampPeriod);

namespace detail
{
struct TimestampQuery
{
    std::uint64_t ticks;
    std::uint64_t available;
};

inline constexpr VkQueryResultFlags TimestampResultFlags =
    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;

void CheckResult(VkResult result, const char* operation);
[[nodiscard]] std::uint64_t DecodeTimestampQueries(
    VkResult result, const std::array<TimestampQuery, 2>& queries,
    std::uint32_t validBits, long double timestampPeriod);
} // namespace detail
} // namespace computelab::vulkan
