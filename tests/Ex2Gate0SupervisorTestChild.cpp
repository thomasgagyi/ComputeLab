#include "ex2/Ex2Gate0Supervisor.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

std::uint64_t ParseUnsigned(std::string_view value)
{
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::invalid_argument("invalid unsigned test-child argument");
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        std::vector<std::string_view> arguments;
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        std::uintptr_t rawProgressHandle{};
        for (std::size_t index = 0; index + 1U < arguments.size(); ++index)
        {
            if (arguments[index] == "--supervisor-progress-handle")
                rawProgressHandle = static_cast<std::uintptr_t>(
                    ParseUnsigned(arguments[index + 1U]));
        }
        const auto progress =
            computelab::ex2::gate0::supervisor::ExtractProgressReporter(arguments);
        std::uint64_t operations{1U};
        std::uint64_t sleepMilliseconds{};
        std::uint64_t preSleepMilliseconds{};
        std::uint64_t exitCode{};
        bool omitCompletion{};
        bool deferProgressWrite{};
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (arguments[index] == "--omit-completion")
            {
                omitCompletion = true;
                continue;
            }
            if (arguments[index] == "--defer-progress-write")
            {
                deferProgressWrite = true;
                continue;
            }
            if (index + 1U >= arguments.size()) return 2;
            if (arguments[index] == "--operations")
                operations = ParseUnsigned(arguments[++index]);
            else if (arguments[index] == "--sleep-ms")
                sleepMilliseconds = ParseUnsigned(arguments[++index]);
            else if (arguments[index] == "--pre-sleep-ms")
                preSleepMilliseconds = ParseUnsigned(arguments[++index]);
            else if (arguments[index] == "--exit-code")
                exitCode = ParseUnsigned(arguments[++index]);
            else return 2;
        }
        if (!progress.has_value()) return 2;
        Sleep(static_cast<DWORD>(preSleepMilliseconds));
        for (std::uint64_t index = 0; index < operations; ++index)
        {
            if (!deferProgressWrite)
            {
                progress->OperationStarted(index);
                Sleep(static_cast<DWORD>(sleepMilliseconds));
                if (!omitCompletion) progress->OperationCompleted(index);
                continue;
            }
            LARGE_INTEGER start{};
            LARGE_INTEGER end{};
            if (!QueryPerformanceCounter(&start)) return 6;
            Sleep(static_cast<DWORD>(sleepMilliseconds));
            if (!QueryPerformanceCounter(&end)) return 6;
            const computelab::ex2::gate0::supervisor::ProgressEvent events[]{
                {computelab::ex2::gate0::supervisor::ProgressMagic,
                 computelab::ex2::gate0::supervisor::ProgressVersion,
                 computelab::ex2::gate0::supervisor::ProgressEventType::OperationStarted,
                 index, start.QuadPart},
                {computelab::ex2::gate0::supervisor::ProgressMagic,
                 computelab::ex2::gate0::supervisor::ProgressVersion,
                 computelab::ex2::gate0::supervisor::ProgressEventType::OperationCompleted,
                 index, end.QuadPart}};
            DWORD written{};
            if (!WriteFile(reinterpret_cast<HANDLE>(rawProgressHandle), events,
                    sizeof(events), &written, nullptr)
                || written != sizeof(events))
                return 6;
            if (index + 1U == operations)
                ExitProcess(static_cast<UINT>(exitCode));
        }
        return static_cast<int>(exitCode);
    }
    catch (...)
    {
        return 6;
    }
}
