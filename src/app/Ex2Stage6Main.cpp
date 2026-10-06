#include "app/Ex2Stage6Execution.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
    namespace run = computelab::ex2::stage6::execution;
    bool executing = false;
    try
    {
        std::vector<std::string_view> arguments;
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
        const auto config = run::ParseConfiguration(arguments);
        std::vector<wchar_t> buffer(32768);
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size()) throw std::runtime_error("actual executable path unavailable");
        const run::RuntimePaths paths{COMPUTELAB_REPOSITORY_ROOT,
            std::filesystem::path(std::wstring(buffer.data(), length)),
            COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH,
            COMPUTELAB_EX2_B1_SPIRV_PATH, COMPUTELAB_EX2_B2_SPIRV_PATH,
            COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH};
        executing = true;
        const auto exit = run::RunChild(config, paths);
        std::cout << "S6-I2 child exit=" << static_cast<int>(exit) << '\n';
        return static_cast<int>(exit);
    }
    catch (...) { std::cerr << "S6-I2 child-local failure\n";
        return static_cast<int>(executing ? run::ExitCode::InternalFailure : run::ExitCode::PreFoundationFailure); }
}
