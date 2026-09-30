#include "app/Ex2CorrectnessExecution.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <charconv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using computelab::ex2::correctness::CorrectnessChildConfiguration;
using computelab::ex2::correctness::CorrectnessRuntimePaths;

void PrintUsage()
{
    std::cout
        << "Usage: ComputeLabEx2Correctness --core-cell-index <0..21> "
        << "--cuda-device <ordinal> --vulkan-device <physical-index> "
        << "--machine-id <anonymous-id> --session-id <anonymous-id>\n";
}

template <class T>
T ParseUnsigned(std::string_view value, std::string_view name)
{
    unsigned long long parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
        || parsed > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
    {
        throw std::invalid_argument(std::string(name) + " is invalid");
    }
    return static_cast<T>(parsed);
}

std::filesystem::path RunningExecutablePath()
{
    std::wstring buffer(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size())
        throw std::runtime_error("running executable path acquisition failed");
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

CorrectnessChildConfiguration Parse(int argc, char** argv)
{
    if (argc != 11)
        throw std::invalid_argument("exactly five named options are required");
    CorrectnessChildConfiguration result;
    bool core = false, cuda = false, vulkan = false, machine = false, session = false;
    for (int index = 1; index < argc; index += 2)
    {
        const std::string_view name = argv[index];
        const std::string_view value = argv[index + 1];
        if (name == "--core-cell-index" && !core)
        {
            result.coreCellIndex = ParseUnsigned<std::size_t>(value, name);
            core = true;
        }
        else if (name == "--cuda-device" && !cuda)
        {
            result.cudaDeviceOrdinal = ParseUnsigned<int>(value, name);
            cuda = true;
        }
        else if (name == "--vulkan-device" && !vulkan)
        {
            result.vulkanPhysicalDeviceIndex = ParseUnsigned<std::uint32_t>(value, name);
            vulkan = true;
        }
        else if (name == "--machine-id" && !machine)
        {
            result.machineId = value;
            machine = true;
        }
        else if (name == "--session-id" && !session)
        {
            result.sessionId = value;
            session = true;
        }
        else
        {
            throw std::invalid_argument("unknown or duplicate option");
        }
    }
    if (!core || !cuda || !vulkan || !machine || !session)
        throw std::invalid_argument("all five named options are required");
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage();
        return 0;
    }
    try
    {
        const auto configuration = Parse(argc, argv);
        const CorrectnessRuntimePaths paths{
            COMPUTELAB_REPOSITORY_ROOT,
            std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT) / "results" / "local",
            RunningExecutablePath(),
            COMPUTELAB_EX2_A1_SPIRV_PATH,
            COMPUTELAB_EX2_A2_SPIRV_PATH,
            COMPUTELAB_EX2_B1_SPIRV_PATH,
            COMPUTELAB_EX2_B2_SPIRV_PATH,
            COMPUTELAB_EX2_C_SPIRV_PATH,
            COMPUTELAB_EX2_D1_SPIRV_PATH};
        const auto result = computelab::ex2::correctness::RunCorrectnessChild(
            configuration, paths);
        if (result == computelab::ex2::correctness::ExitCode::Completed)
            std::cout << "I7 paired correctness package completed.\n";
        else
            std::cerr << "I7 correctness child failed with category "
                << static_cast<int>(result) << ".\n";
        return static_cast<int>(result);
    }
    catch (const std::exception& error)
    {
        std::cerr << "I7 invocation failed: " << error.what() << '\n';
        PrintUsage();
        return static_cast<int>(
            computelab::ex2::correctness::ExitCode::PreFoundationFailure);
    }
}
