#pragma once

#include "ex2/Ex2Stage5Qualification.hpp"
#include <cstdint>
#include <type_traits>

namespace computelab::ex2::stage5::control
{
enum class AttemptEvent : std::uint32_t { Started = 1U, Returned = 2U };
enum class AttemptKind : std::uint32_t
{
    DiagnosticObservation = 1U, SelectedWarmupPreparation = 2U, MeasuredObservation = 3U,
};
struct AttemptIdentity
{
    Stage5Phase phase{};
    Stage5Backend backend{};
    AttemptKind kind{};
    std::uint32_t planIndex{}, attemptIndex{};
    bool operator==(const AttemptIdentity&) const = default;
};
// App-only control observation. No native timing/resource interface.
struct AttemptObserver
{
    void* context{};
    void (*callback)(void*, const AttemptIdentity&, AttemptEvent) noexcept{};
    void Report(const AttemptIdentity& identity, AttemptEvent event) const noexcept
    { if (callback) callback(context, identity, event); }
};
inline constexpr std::uint32_t ProgressMagic = 0x53355043U;
inline constexpr std::uint32_t ProgressVersion = 1U;
struct ProgressEvent
{
    std::uint32_t magic{ProgressMagic}, version{ProgressVersion};
    AttemptEvent event{};
    std::uint32_t phase{}, backend{};
    AttemptKind kind{};
    std::uint32_t planIndex{}, attemptIndex{};
    std::int64_t performanceCounter{};
};
static_assert(sizeof(ProgressEvent) == 40U);
static_assert(offsetof(ProgressEvent, performanceCounter) == 32U);
static_assert(std::is_trivially_copyable_v<ProgressEvent>);
static_assert(std::is_standard_layout_v<ProgressEvent>);

class ProgressReporter final
{
public:
    explicit ProgressReporter(std::uintptr_t handle = 0U);
    [[nodiscard]] AttemptObserver Observer() noexcept;
private:
    static void Publish(void*, const AttemptIdentity&, AttemptEvent) noexcept;
    std::uintptr_t handle_{};
};
[[nodiscard]] ProgressReporter ExtractProgressReporter(std::vector<std::string_view>& arguments);
} // namespace computelab::ex2::stage5::control
