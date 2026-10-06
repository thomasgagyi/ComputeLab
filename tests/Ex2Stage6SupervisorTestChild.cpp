#include "ex2/Ex2Stage6Supervisor.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fstream>

int main(int argc, char** argv)
{
    namespace p = computelab::ex2::stage6::progress;
    using E = p::AttemptEvent;
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) return 90;
    BOOL inJob{}; if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob) return 91;
    if (args[0] == "helper") { Sleep(static_cast<DWORD>(std::stoul(std::string(args[1])))); if (args.size() == 3) std::ofstream(std::string(args[2])) << "survived"; return 0; }
    std::uintptr_t raw{}; for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == "--supervisor-progress-handle") raw = std::stoull(std::string(args[i + 1]));
    auto reporter = p::ExtractReporter(args); const auto observer = reporter.Observer();
    if (!observer.callback || args.size() < 2) return 92;
    const auto mode = args[0]; const auto slot = std::stoull(std::string(args[1]));
    if (mode == "ledger-owner-death") {
        namespace c = computelab::ex2::stage6::control;
        const c::RuntimePaths paths{std::filesystem::path(args.at(2)), {}, {}};
        const auto manifest = c::LoadManifestFromRepository(paths.repositoryRoot, std::filesystem::path(args.at(3)));
        c::RuntimeServices services;
        services.collectFacts = [](const auto& m, const auto&) { return c::PreflightFacts{m.sourceRevision, false,
            m.childExecutableSha256, m.supervisorExecutableSha256, m.gpuUuid, m.gpuUuid, m.shaderSha256}; };
        services.runProcess = [](const auto&, const auto&, auto, auto, auto) { return c::ProcessResult{}; };
        services.inspectPackage = [](const auto&, auto, const auto&) { return c::PackageInspection{}; };
        services.publishAnalysis = c::PublishAnalysis;
        services.afterDurableUpdate = [](const auto&) { TerminateProcess(GetCurrentProcess(), 0xC0000005U); ExitProcess(0xC0000005U); };
        return static_cast<int>(c::ExecuteManifestWithServices(manifest, paths, services));
    }
    const auto send = [&](std::uint64_t n, E event) { observer.callback(observer.context, {slot, n}, event); };
    LARGE_INTEGER now{}, frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    p::ProgressEvent wire{p::ProgressMagic, p::ProgressVersion, E::Started, 0, slot, 0, now.QuadPart};
    const auto write = [&](DWORD size = sizeof(p::ProgressEvent)) { DWORD count{}; return WriteFile(reinterpret_cast<HANDLE>(raw), &wire, size, &count, nullptr) && count == size; };
    if (mode == "no-progress") { Sleep(10000); return 0; }
    if (mode == "empty") return 0;
    if (mode == "abnormal") { TerminateProcess(GetCurrentProcess(), 0xC0000005U); return 93; }
    if (mode == "transport-abort") { CloseHandle(reinterpret_cast<HANDLE>(raw)); send(0, E::Started); return 94; }
    if (mode == "quote" && (args.size() != 5 || args[2] != "space and \"quote\"" || args[3] != "trailing slash\\" || args[4] != "")) return 95;
    if (mode == "restricted-handle" && SetEvent(reinterpret_cast<HANDLE>(std::stoull(std::string(args[2]))))) return 96;
    if (mode == "helper-clean" || mode == "descendant-survive" || mode == "descendant-hang") {
        std::wstring exe(32768, L'\0'); const auto n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())); exe.resize(n);
        auto command = L"\"" + exe + L"\" helper " + (mode == "helper-clean" ? std::wstring(L"10") : std::wstring(L"1800"));
        if (args.size() == 3) command += L" \"" + std::filesystem::path(args[2]).wstring() + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup, &info)) return 97;
        CloseHandle(info.hThread); if (mode == "helper-clean") WaitForSingleObject(info.hProcess, 5000); CloseHandle(info.hProcess);
        if (mode == "descendant-hang") { send(0, E::Started); Sleep(10000); return 0; }
    }
    if (mode == "bad-magic") wire.magic = 0;
    else if (mode == "bad-version") wire.version = 2;
    else if (mode == "bad-reserved") wire.reserved = 1;
    else if (mode == "bad-enum") wire.event = static_cast<E>(3);
    else if (mode == "wrong-slot") ++wire.slotSequenceIndex;
    else if (mode == "bad-sample") wire.sampleIndex = 100;
    else if (mode == "returned-first") wire.event = E::Returned;
    else if (mode == "future-qpc") wire.performanceCounter += frequency.QuadPart * 10;
    else if (mode == "zero-qpc") wire.performanceCounter = 0;
    if (mode.starts_with("bad-") || mode == "wrong-slot" || mode == "returned-first" || mode == "future-qpc" || mode == "zero-qpc") { write(); return 0; }
    if (mode == "truncated") { write(39); return 0; }
    if (mode == "pair-timeout") {
        wire.performanceCounter -= frequency.QuadPart; write(); wire.event = E::Returned; wire.performanceCounter = now.QuadPart; write(); return 0;
    }
    if (mode == "pair-exact" || mode == "pair-below") {
        const auto ticks = computelab::ex2::stage6::control::DeadlineTicks(100, frequency.QuadPart);
        std::array<p::ProgressEvent, 2> pair{wire, wire};
        pair[0].performanceCounter -= ticks - (mode == "pair-below" ? 1 : 0);
        pair[1].event = E::Returned;
        DWORD count{}; return WriteFile(reinterpret_cast<HANDLE>(raw), pair.data(), sizeof(pair), &count, nullptr) && count == sizeof(pair) ? 0 : 98;
    }
    const auto count = (mode == "prefix" || mode == "outstanding" || mode == "operation-hang") ? std::stoull(std::string(args.at(2))) : 100;
    for (std::uint64_t n = 0; n < count; ++n) {
        if (mode == "split-frame" && n == 0) { DWORD written{};
            WriteFile(reinterpret_cast<HANDLE>(raw), &wire, 13, &written, nullptr);
            WriteFile(reinterpret_cast<HANDLE>(raw), reinterpret_cast<const char*>(&wire) + 13, 27, &written, nullptr);
        } else send(n, E::Started);
        if (mode == "duplicate-start") { send(n, E::Started); return 0; }
        if (mode == "backward-qpc") { wire.event = E::Returned; wire.performanceCounter = 1; write(); return 0; }
        send(n, E::Returned); if (mode == "duplicate-return") { send(n, E::Returned); return 0; }
    }
    if (mode == "outstanding" || mode == "operation-hang") { send(count, E::Started); if (mode == "operation-hang") Sleep(10000); }
    if (mode == "extra-event") write();
    if (mode == "trailing-byte") write(1);
    return mode == "full-nonzero" ? 3 : 0;
}
