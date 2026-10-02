#include "ex2/Ex2Stage5Supervisor.hpp"
#define NOMINMAX
#include <Windows.h>
#include <iostream>

int main(int argc, char** argv)
{
    namespace c = computelab::ex2::stage5::control;
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    { std::cout << "ComputeLabEx2Stage5Supervisor --manifest <repository-relative-json>\n"; return 0; }
    try
    {
        const std::vector<std::string_view> args(argv + 1, argv + argc);
        const auto relative = c::ParseSupervisorArguments(args);
        std::wstring executable(32768, L'\0');
        const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!size || size >= executable.size()) throw std::runtime_error("supervisor module unavailable"); executable.resize(size);
        const auto manifest = c::LoadManifestFromRepository(COMPUTELAB_REPOSITORY_ROOT, relative);
        return static_cast<int>(c::ExecuteManifest(manifest, {COMPUTELAB_REPOSITORY_ROOT, executable,
            COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}));
    }
    catch (const std::exception&) { std::cerr << "Stage-5 supervisor preflight rejected; use --help.\n"; return 2; }
}
