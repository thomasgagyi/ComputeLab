#pragma once

#include <cstdint>
#include <type_traits>

namespace computelab::ex2::correctness::control
{

enum class AttemptEvent : std::uint32_t { Started = 1U, Returned = 2U };
enum class AttemptBackend : std::uint32_t { Cuda = 1U, Vulkan = 2U };

// Optional app-level observation only. No native resource or timing interface.
struct AttemptObserver
{
    void* context{};
    void (*callback)(void*, AttemptBackend, AttemptEvent) noexcept{};

    void Report(AttemptBackend backend, AttemptEvent event) const noexcept
    {
        if (callback) callback(context, backend, event);
    }
};

inline constexpr std::uint32_t ProgressMagic = 0x49374350U;
inline constexpr std::uint32_t ProgressVersion = 1U;
struct ProgressEvent
{
    std::uint32_t magic{ProgressMagic};
    std::uint32_t version{ProgressVersion};
    AttemptEvent event{};
    AttemptBackend backend{};
    std::int64_t performanceCounter{};
};
static_assert(sizeof(ProgressEvent) == 24U);
static_assert(std::is_trivially_copyable_v<ProgressEvent>);
static_assert(std::is_standard_layout_v<ProgressEvent>);

} // namespace computelab::ex2::correctness::control
