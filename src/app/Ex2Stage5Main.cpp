#include "app/Ex2Stage5Execution.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <iostream>

int main(int argc, char** argv)
{
    namespace child = computelab::ex2::stage5::execution;
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "ComputeLabEx2Stage5 --phase <a1-sentinel|d1-warmup|d1-sample> --plan-index <0..9> "
            "--selected-w <none|0|1|2|4|8|16> --cuda-device <ordinal> --vulkan-device <index> "
            "--machine-id <anonymous-id> --session-id <anonymous-id> --expected-gpu-uuid <canonical-lowercase-uuid>\n";
        return 0;
    }
    try
    {
        std::vector<std::string_view> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        const auto config = child::ParseArguments(args);
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("executable path unavailable");
        executable.resize(length);
        return static_cast<int>(child::RunChild(config, {COMPUTELAB_REPOSITORY_ROOT, executable,
            COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}));
    }
    catch (const std::exception&)
    {
        std::cerr << "Stage-5 child preflight rejected; use --help for the exact interface.\n";
        return static_cast<int>(child::ExitCode::PreflightFailure);
    }
}
