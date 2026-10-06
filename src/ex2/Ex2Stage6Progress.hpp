#pragma once
#include "app/Ex2Stage6Execution.hpp"
#include <cstddef>
#include <memory>
#include <type_traits>

namespace computelab::ex2::stage6::progress
{
using execution::AttemptIdentity;
using execution::AttemptEvent;
using execution::AttemptObserver;
inline constexpr std::uint32_t ProgressMagic = 0x53365043U, ProgressVersion = 1U;
inline constexpr std::uint32_t ProgressTransportAbortExitCode = 0xE6000001U;
struct ProgressEvent
{
    std::uint32_t magic{ProgressMagic}, version{ProgressVersion};
    AttemptEvent event{};
    std::uint32_t reserved{};
    std::uint64_t slotSequenceIndex{}, sampleIndex{};
    std::int64_t performanceCounter{};
};
static_assert(sizeof(ProgressEvent) == 40 && offsetof(ProgressEvent, slotSequenceIndex) == 16
    && offsetof(ProgressEvent, sampleIndex) == 24 && offsetof(ProgressEvent, performanceCounter) == 32);
static_assert(std::is_standard_layout_v<ProgressEvent> && std::is_trivially_copyable_v<ProgressEvent>);
struct Observation { ProgressEvent record; std::int64_t receiveCounter{}; };
enum class TerminalForm { NotStarted, Empty, ReturnedPrefix, Full, OutstandingStartedPrefix, Invalid };
[[nodiscard]] std::string_view ToString(TerminalForm) noexcept;
class Reporter final
{
public:
    explicit Reporter(std::uintptr_t handle = 0);
    [[nodiscard]] AttemptObserver Observer() noexcept;
private:
    std::uintptr_t handle_{};
    static void Publish(void*, const AttemptIdentity&, AttemptEvent) noexcept;
};
[[nodiscard]] Reporter ExtractReporter(std::vector<std::string_view>&);
class State final
{
public:
    explicit State(std::uint64_t slot);
    void Accept(const Observation&);
    [[nodiscard]] TerminalForm Form(bool resumed) const noexcept;
    bool invalid{};
    std::vector<Observation> observations;
    std::optional<AttemptIdentity> activeAttempt, lastReturnedAttempt;
    std::optional<std::int64_t> outstandingStart;
private:
    std::uint64_t slot_{};
    std::int64_t lastCounter_{};
};
struct ReaderBatch
{
    std::vector<Observation> observations;
    bool finished{}, cleanEof{}, transportFailed{}, overflow{}, protocolInvalid{};
    std::uint32_t trailingBytes{};
};
// Owns a blocking anonymous-pipe reader. No timeout or campaign policy lives here.
class BlockingReader final
{
public:
    BlockingReader(std::uintptr_t ownedReadHandle, std::uint64_t slot);
    ~BlockingReader();
    BlockingReader(const BlockingReader&) = delete;
    BlockingReader& operator=(const BlockingReader&) = delete;
    [[nodiscard]] std::uintptr_t ReadyHandle() const noexcept;
    [[nodiscard]] ReaderBatch Drain();
    void Cancel() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace computelab::ex2::stage6::progress
