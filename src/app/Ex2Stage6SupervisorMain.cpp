#include "ex2/Ex2Stage6Supervisor.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <iostream>

int main(int argc, char** argv)
{
    namespace c = computelab::ex2::stage6::control;
    try {
        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        const auto relative = c::ParseSupervisorArguments(arguments);
        std::wstring module(32768, L'\0'); const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!length || length >= module.size()) throw std::runtime_error("supervisor module unavailable"); module.resize(length);
        const auto manifest = c::LoadManifestFromRepository(COMPUTELAB_REPOSITORY_ROOT, relative);
        return static_cast<int>(c::ExecuteManifest(manifest, {COMPUTELAB_REPOSITORY_ROOT, module,
            {COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH, COMPUTELAB_EX2_B1_SPIRV_PATH,
             COMPUTELAB_EX2_B2_SPIRV_PATH, COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}}));
    } catch (...) { std::cerr << "Stage-6 supervisor configuration/preflight rejected.\n"; return static_cast<int>(c::ExitCode::ConfigurationOrPreflightRejected); }
}
