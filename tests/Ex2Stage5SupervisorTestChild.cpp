#include "ex2/Ex2Stage5Supervisor.hpp"
#define NOMINMAX
#include <Windows.h>
#include <fstream>

int main(int argc, char** argv)
{
    namespace c = computelab::ex2::stage5::control;
    namespace s5 = computelab::ex2::stage5;
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() == 2U && args[0] == "orphan-marker") { Sleep(1500U); std::ofstream(std::string(args[1])) << "survived"; return 0; }
    std::uintptr_t raw{};
    for (std::size_t i = 0; i + 1U < args.size(); ++i) if (args[i] == "--supervisor-progress-handle") raw = std::stoull(std::string(args[i + 1U]));
    auto reporter = c::ExtractProgressReporter(args); auto observer = reporter.Observer(); if (args.empty()) return 2;
    BOOL job = FALSE; if (!IsProcessInJob(GetCurrentProcess(), nullptr, &job) || !job) return 91;
    const auto mode = args[0];
    if (mode == "ledger-death")
    {
        std::ifstream in(std::string(args[2]), std::ios::binary); const std::string bytes{std::istreambuf_iterator<char>(in), {}};
        c::ExecutionRecord r; r.manifest = c::ParseManifest(bytes); r.startTimeUtc = "2026-10-02T00:00:00Z";
        const std::filesystem::path root(args[1]); std::filesystem::create_directories(root / "results/local");
        c::WriteExecutionRecord(c::ControlPath(r.manifest, root), r, false); TerminateProcess(GetCurrentProcess(), 99U); return 99;
    }
    s5::Stage5Phase phase = s5::Stage5Phase::A1Sentinel; std::size_t plan = 0U; std::optional<std::uint64_t> w;
    if (args.size() > 1U && (mode == "complete" || mode == "sample-hang"))
    { phase = static_cast<s5::Stage5Phase>(std::stoul(std::string(args[1]))); plan = std::stoull(std::string(args[2])); if (phase == s5::Stage5Phase::D1Sample) w = std::stoull(std::string(args[3])); }
    if (mode == "vulkan-hang") plan = 1U;
    c::ProgressState expected({phase, plan, w}); auto identity = expected.NextIdentity();
    const auto send = [&](const c::AttemptIdentity& i, c::AttemptEvent event) { observer.Report(i, event); };
    LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    c::ProgressEvent event{c::ProgressMagic, 1U, c::AttemptEvent::Started, static_cast<std::uint32_t>(phase), static_cast<std::uint32_t>(identity.backend), identity.kind, identity.planIndex, 0U, now.QuadPart};
    const auto wire = [&](DWORD length = sizeof(c::ProgressEvent)) { DWORD written{}; ::WriteFile(reinterpret_cast<HANDLE>(raw), &event, length, &written, nullptr); };
    if (mode == "no-progress") { Sleep(10000U); return 7; }
    if (mode == "empty-success") return 0;
    if (mode == "abnormal") { TerminateProcess(GetCurrentProcess(), 0xc0000005U); return 99; }
    if (mode == "pipe-failure") { CloseHandle(reinterpret_cast<HANDLE>(raw)); send(identity, c::AttemptEvent::Started); return 99; }
    if (mode == "quote" && (args.size() != 4U || args[1] != "space and \"quote\"" || args[2] != "trailing slash\\" || args[3] != "")) return 92;
    if (mode == "restricted-handle" && SetEvent(reinterpret_cast<HANDLE>(std::stoull(std::string(args[1]))))) return 93;
    if (mode == "bad-magic") { event.magic = 0U; wire(); return 7; }
    if (mode == "bad-version") { event.version = 2U; wire(); return 7; }
    if (mode == "bad-phase") { event.phase = 99U; wire(); return 7; }
    if (mode == "bad-backend") { event.backend = 99U; wire(); return 7; }
    if (mode == "bad-kind") { event.kind = static_cast<c::AttemptKind>(99U); wire(); return 7; }
    if (mode == "bad-plan") { event.planIndex = 1U; wire(); return 7; }
    if (mode == "bad-index") { event.attemptIndex = 1U; wire(); return 7; }
    if (mode == "returned-first") { event.event = c::AttemptEvent::Returned; wire(); return 7; }
    if (mode == "future") { event.performanceCounter += frequency.QuadPart * 10; wire(); return 7; }
    if (mode == "zero-qpc") { event.performanceCounter = 0; wire(); return 7; }
    if (mode == "truncated") { wire(sizeof(event) - 1U); return 7; }
    if (mode == "delayed-pipe") { event.performanceCounter -= frequency.QuadPart; wire(); return 7; }
    send(identity, c::AttemptEvent::Started);
    if (mode == "cuda-hang" || mode == "vulkan-hang" || mode == "sample-hang") { Sleep(10000U); return 7; }
    if (mode == "delayed-marker" || mode == "descendant-marker")
    {
        if (mode == "descendant-marker")
        {
            std::wstring exe(32768, L'\0'); const auto n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())); exe.resize(n);
            const std::filesystem::path marker(args[1]); auto command = L"\"" + exe + L"\" orphan-marker \"" + marker.wstring() + L"\"";
            STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 94;
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
        Sleep(1500U); std::ofstream(std::string(args[1])) << "survived"; return 7;
    }
    if (mode == "duplicate-start") { send(identity, c::AttemptEvent::Started); return 7; }
    if (mode == "backward") { event.event = c::AttemptEvent::Returned; event.performanceCounter = 1; wire(); return 7; }
    send(identity, c::AttemptEvent::Returned);
    if (mode == "prefix") return 3;
    if (mode == "duplicate-return") { send(identity, c::AttemptEvent::Returned); return 7; }
    // All remaining attempts retain the exact frozen phase/kind/index order.
    const auto count = expected.ExpectedEventCount() / 2U;
    for (std::uint32_t n = 1U; n < count; ++n)
    {
        const auto prep = phase == s5::Stage5Phase::D1Sample && n < *w;
        c::AttemptIdentity i{phase, s5::FrozenProcessPlan()[plan].backend,
            phase != s5::Stage5Phase::D1Sample ? c::AttemptKind::DiagnosticObservation
                : prep ? c::AttemptKind::SelectedWarmupPreparation : c::AttemptKind::MeasuredObservation,
            static_cast<std::uint32_t>(plan), phase == s5::Stage5Phase::D1Sample && !prep ? n - static_cast<std::uint32_t>(*w) : n};
        send(i, c::AttemptEvent::Started); send(i, c::AttemptEvent::Returned);
    }
    if (mode == "extra") wire(1U);
    return mode == "full-nonzero" ? 3 : 0;
}
