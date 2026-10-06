#include "ex2/Ex2Stage6Progress.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <charconv>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace computelab::ex2::stage6::progress
{
std::string_view ToString(TerminalForm value) noexcept
{
    switch (value) {
    case TerminalForm::NotStarted: return "NotStarted";
    case TerminalForm::Empty: return "Empty";
    case TerminalForm::ReturnedPrefix: return "ReturnedPrefix";
    case TerminalForm::Full: return "Full";
    case TerminalForm::OutstandingStartedPrefix: return "OutstandingStartedPrefix";
    default: return "Invalid";
    }
}
Reporter::Reporter(std::uintptr_t handle) : handle_(handle)
{
    if (handle_ && (GetFileType(reinterpret_cast<HANDLE>(handle_)) != FILE_TYPE_PIPE
        || !SetHandleInformation(reinterpret_cast<HANDLE>(handle_), HANDLE_FLAG_INHERIT, 0)))
        throw std::invalid_argument("invalid supervisor progress pipe");
}
AttemptObserver Reporter::Observer() noexcept { return handle_ ? AttemptObserver{this, Publish} : AttemptObserver{}; }
void Reporter::Publish(void* context, const AttemptIdentity& identity, AttemptEvent event) noexcept
{
    ProgressEvent record{ProgressMagic, ProgressVersion, event, 0, identity.slotSequenceIndex, identity.sampleIndex, 0};
    LARGE_INTEGER now{}; DWORD written{};
    const bool clock = QueryPerformanceCounter(&now) && now.QuadPart > 0;
    record.performanceCounter = now.QuadPart;
    if (!clock || !WriteFile(reinterpret_cast<HANDLE>(static_cast<Reporter*>(context)->handle_),
            &record, sizeof(record), &written, nullptr) || written != sizeof(record))
    { TerminateProcess(GetCurrentProcess(), ProgressTransportAbortExitCode); ExitProcess(ProgressTransportAbortExitCode); }
}
Reporter ExtractReporter(std::vector<std::string_view>& args)
{
    std::uintptr_t handle{}; bool found{};
    for (std::size_t i = 0; i < args.size();) {
        if (args[i] != "--supervisor-progress-handle") { ++i; continue; }
        if (found || i + 1 == args.size()) throw std::invalid_argument("duplicate or missing progress handle");
        found = true; const auto value = args[i + 1];
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), handle);
        if (error != std::errc{} || end != value.data() + value.size() || !handle)
            throw std::invalid_argument("invalid progress handle");
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i + 2));
    }
    return Reporter(handle);
}
State::State(std::uint64_t slot) : slot_(slot)
{ if (slot >= TotalChildCount) throw std::invalid_argument("invalid progress slot"); observations.reserve(200); }
void State::Accept(const Observation& observation)
{
    if (invalid) { if (observations.size() < 201) observations.push_back(observation); return; }
    const auto n = observations.size(); const auto& r = observation.record;
    if (n >= 200 || r.magic != ProgressMagic || r.version != ProgressVersion || r.reserved
        || r.event != (n % 2 ? AttemptEvent::Returned : AttemptEvent::Started)
        || r.slotSequenceIndex != slot_ || r.sampleIndex != n / 2
        || r.performanceCounter <= 0 || r.performanceCounter > observation.receiveCounter
        || r.performanceCounter < lastCounter_) { invalid = true; if (observations.size() < 201) observations.push_back(observation); return; }
    observations.push_back(observation); lastCounter_ = r.performanceCounter;
    const AttemptIdentity identity{r.slotSequenceIndex, r.sampleIndex};
    if (r.event == AttemptEvent::Started) { activeAttempt = identity; outstandingStart = r.performanceCounter; }
    else { lastReturnedAttempt = identity; activeAttempt.reset(); outstandingStart.reset(); }
}
TerminalForm State::Form(bool resumed) const noexcept
{
    if (invalid) return TerminalForm::Invalid;
    if (!resumed) return TerminalForm::NotStarted;
    if (activeAttempt) return TerminalForm::OutstandingStartedPrefix;
    if (observations.empty()) return TerminalForm::Empty;
    return observations.size() == 200 ? TerminalForm::Full : TerminalForm::ReturnedPrefix;
}
struct BlockingReader::Impl
{
    HANDLE pipe{}, ready{};
    std::mutex mutex;
    ReaderBatch batch;
    State grammar;
    std::atomic_bool cancel{};
    std::thread thread;
    Impl(HANDLE p, std::uint64_t slot) : pipe(p), ready(CreateEventW(nullptr, TRUE, FALSE, nullptr)), grammar(slot)
    { if (!ready) { CloseHandle(pipe); throw std::runtime_error("progress event creation failed"); } }
    ~Impl() {
        cancel = true;
        if (thread.joinable()) { while (WaitForSingleObject(thread.native_handle(), 0) != WAIT_OBJECT_0) {
            CancelSynchronousIo(thread.native_handle()); WaitForSingleObject(thread.native_handle(), 20);
        } thread.join(); } CloseHandle(pipe); CloseHandle(ready);
    }
    void Run() noexcept
    {
        try {
            std::array<std::byte, 4096 + sizeof(ProgressEvent)> pending{}; std::size_t used{}, total{};
            for (;;) {
                if (cancel) { std::lock_guard lock(mutex); batch.finished = batch.transportFailed = true; SetEvent(ready); return; }
                DWORD read{};
                const bool ok = ReadFile(pipe, pending.data() + used, 4096, &read, nullptr) != 0;
                if (!ok || !read) {
                    const auto error = ok ? ERROR_BROKEN_PIPE : GetLastError();
                    std::lock_guard lock(mutex); batch.finished = true;
                    batch.trailingBytes = static_cast<std::uint32_t>(used);
                    batch.cleanEof = error == ERROR_BROKEN_PIPE && !used;
                    batch.transportFailed = error != ERROR_BROKEN_PIPE;
                    SetEvent(ready); return;
                }
                used += read;
                LARGE_INTEGER now{}; const bool clock = QueryPerformanceCounter(&now) && now.QuadPart > 0;
                std::lock_guard lock(mutex);
                if (!clock) batch.transportFailed = true;
                std::size_t consumed{};
                while (used - consumed >= sizeof(ProgressEvent)) {
                    Observation observation{};
                    std::memcpy(&observation.record, pending.data() + consumed, sizeof(ProgressEvent));
                    observation.receiveCounter = now.QuadPart; grammar.Accept(observation);
                    batch.protocolInvalid |= grammar.invalid;
                    if (++total <= 201) batch.observations.push_back(observation); else batch.overflow = true;
                    consumed += sizeof(ProgressEvent);
                }
                used -= consumed;
                std::memmove(pending.data(), pending.data() + consumed, used);
                if (consumed || !clock) SetEvent(ready);
            }
        } catch (...) { std::lock_guard lock(mutex); batch.finished = true; batch.transportFailed = true; SetEvent(ready); }
    }
};
BlockingReader::BlockingReader(std::uintptr_t handle, std::uint64_t slot) : impl_(std::make_unique<Impl>(reinterpret_cast<HANDLE>(handle), slot))
{ impl_->thread = std::thread([this] { impl_->Run(); }); }
BlockingReader::~BlockingReader() = default;
std::uintptr_t BlockingReader::ReadyHandle() const noexcept { return reinterpret_cast<std::uintptr_t>(impl_->ready); }
ReaderBatch BlockingReader::Drain()
{
    std::lock_guard lock(impl_->mutex); auto result = impl_->batch;
    impl_->batch.observations.clear(); ResetEvent(impl_->ready); return result;
}
void BlockingReader::Cancel() noexcept { impl_->cancel = true; if (impl_->thread.joinable()) CancelSynchronousIo(impl_->thread.native_handle()); }
} // namespace computelab::ex2::stage6::progress
