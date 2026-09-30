#include "ex2/Ex2CorrectnessSupervisor.hpp"

#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char** argv)
{
    namespace control = computelab::ex2::correctness::control;
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            std::cout << "Usage: ComputeLabEx2CorrectnessSupervisor --manifest <path>\n"
                "       ComputeLabEx2CorrectnessSupervisor --print-manifest-hash <path>\n";
            return 0;
        }
        if (argc != 3 || (std::string_view(argv[1]) != "--manifest"
                && std::string_view(argv[1]) != "--print-manifest-hash"))
            throw std::invalid_argument("expected --manifest <path> or --print-manifest-hash <path>");
        std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
        if (!input) throw std::invalid_argument("cannot open manifest");
        const std::string json{std::istreambuf_iterator<char>(input), {}};
        if (std::string_view(argv[1]) == "--print-manifest-hash")
        {
            const auto hash = control::CalculateManifestSha256(json);
            // Hash printing neither validates runtime provenance nor launches.
            std::cout << hash << '\n';
            return 0;
        }
        const auto manifest = control::ParseManifest(json);
        std::wstring executable(32768U, L'\0');
        const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (size == 0U || size >= executable.size()) throw std::runtime_error("cannot locate supervisor");
        executable.resize(size);
        const control::RuntimePaths paths{COMPUTELAB_REPOSITORY_ROOT, executable,
            {COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH,
             COMPUTELAB_EX2_B1_SPIRV_PATH, COMPUTELAB_EX2_B2_SPIRV_PATH,
             COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}};
        return static_cast<int>(control::ExecuteManifest(manifest, paths));
    }
    catch (const std::exception& error)
    {
        std::cerr << "I7 supervisor rejected: " << error.what() << '\n';
        return static_cast<int>(control::ExitCode::ConfigurationOrPreflightError);
    }
}
