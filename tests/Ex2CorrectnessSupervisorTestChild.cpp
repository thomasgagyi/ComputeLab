#include "ex2/Ex2CorrectnessSupervisor.hpp"
#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    namespace c = computelab::ex2::correctness::control;
    std::vector<std::string_view> args(argv + 1, argv + argc);
    std::uintptr_t raw{};
    for (std::size_t i = 0; i + 1U < args.size(); ++i)
        if (args[i] == "--supervisor-progress-handle") raw = std::stoull(std::string(args[i + 1U]));
    auto reporter = c::ExtractProgressReporter(args);
    const auto observer = reporter.Observer();
    if (args.empty()) return 2;
    const std::string mode(args[0]);
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob) return 91;
    const auto event = [&](c::AttemptBackend backend, c::AttemptEvent type) { observer.Report(backend, type); };
    const auto rawEvent = [&](c::ProgressEvent e, DWORD length = sizeof(c::ProgressEvent)) {
        DWORD written{};
        WriteFile(reinterpret_cast<HANDLE>(raw), &e, length, &written, nullptr);
    };
    if (mode == "no-progress") Sleep(10000U);
    if (mode == "empty-success") return 0;
    if (mode == "abnormal") { TerminateProcess(GetCurrentProcess(), 0xc0000005U); return 99; }
    if (mode == "quote")
    {
        if (args.size() != 4U || args[1] != "space and \"quote\"" || args[2] != "trailing slash\\" || args[3] != "") return 92;
    }
    if (mode == "restricted-handle")
    {
        HANDLE unrelated = reinterpret_cast<HANDLE>(std::stoull(std::string(args[1])));
        // A known inheritable event must not be inherited by HANDLE_LIST.
        if (SetEvent(unrelated)) return 93;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    c::ProgressEvent e{c::ProgressMagic, c::ProgressVersion, c::AttemptEvent::Started, c::AttemptBackend::Cuda, now.QuadPart};
    if (mode == "bad-magic") { e.magic = 0; rawEvent(e); return 7; }
    if (mode == "bad-version") { e.version = 2; rawEvent(e); return 7; }
    if (mode == "truncated") { rawEvent(e, sizeof(e) - 1U); return 7; }
    if (mode == "returned-first") { e.event = c::AttemptEvent::Returned; rawEvent(e); return 7; }
    event(c::AttemptBackend::Cuda, c::AttemptEvent::Started);
    if (mode == "cuda-hang" || mode == "delayed-marker")
    {
        Sleep(1500U);
        if (args.size() > 1U) std::ofstream(std::string(args[1])) << "survived";
        return 7;
    }
    if (mode == "duplicate-start") { event(c::AttemptBackend::Cuda, c::AttemptEvent::Started); return 7; }
    if (mode == "backward") { e.event = c::AttemptEvent::Returned; e.performanceCounter = 1; rawEvent(e); return 7; }
    if (mode == "vulkan-first") { event(c::AttemptBackend::Vulkan, c::AttemptEvent::Started); return 7; }
    event(c::AttemptBackend::Cuda, c::AttemptEvent::Returned);
    if (mode == "prefix") return 3;
    if (mode == "duplicate-return") { event(c::AttemptBackend::Cuda, c::AttemptEvent::Returned); return 7; }
    event(c::AttemptBackend::Vulkan, c::AttemptEvent::Started);
    if (mode == "vulkan-hang") Sleep(10000U);
    event(c::AttemptBackend::Vulkan, c::AttemptEvent::Returned);
    if (mode == "extra") rawEvent(e, 1U);
    return 0;
}
