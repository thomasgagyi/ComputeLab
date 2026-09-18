#include "ex2/Ex2Gate0.hpp"

#include <gtest/gtest.h>

#include <process.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;

struct RemovePackage final
{
    std::filesystem::path finalPath;
    ~RemovePackage()
    {
        std::error_code error;
        std::filesystem::remove_all(finalPath, error);
        std::filesystem::remove_all(finalPath.string() + ".incomplete", error);
    }
};

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, {}};
}

void RunChildSmoke(std::string_view backend, std::string_view mode)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::string runId = "g004-smoke-" + std::string(backend) + "-"
        + std::string(mode) + "-" + std::to_string(GetCurrentProcessId())
        + "-" + std::to_string(ticks);
    const auto package = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local" / runId;
    RemovePackage cleanup{package};

    std::vector<std::string> arguments{
        COMPUTELAB_EX2_GATE0_EXE,
        "--phase", "sample-count-qualification",
        "--backend", std::string(backend),
        "--instrument-mode", std::string(mode),
        "--device-index", "0",
        "--element-count", "257",
        "--seed", "123456789",
        "--warmup-count", "0",
        "--planned-sample-count", "1",
        "--process-index", "0",
        "--block-index", "0",
        "--order-slot", "0",
        "--run-id", runId,
        "--output-package-directory", package.string(),
        "--machine-id", "g004-smoke-machine",
        "--protocol-version", "1.0"};
    std::vector<const char*> pointers;
    pointers.reserve(arguments.size() + 1U);
    for (const auto& argument : arguments) pointers.push_back(argument.c_str());
    pointers.push_back(nullptr);

    const intptr_t exitCode = _spawnv(
        _P_WAIT, arguments.front().c_str(), pointers.data());
    ASSERT_EQ(exitCode, 0) << "qualification child failed for "
        << backend << " mode " << mode;

    EXPECT_TRUE(std::filesystem::is_regular_file(package / "environment.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(package / "initialization.csv"));
    EXPECT_TRUE(std::filesystem::is_regular_file(package / "samples.csv"));
    EXPECT_TRUE(std::filesystem::is_regular_file(package / "summary.json"));
    EXPECT_FALSE(std::filesystem::exists(package / "warmup.csv"));
    EXPECT_FALSE(std::filesystem::exists(package.string() + ".incomplete"));

    const std::string environment = ReadAll(package / "environment.json");
    EXPECT_NE(environment.find("\"schema_version\":2"), std::string::npos);
    EXPECT_NE(environment.find("\"evidence_kind\":\"qualification\""),
        std::string::npos);
    EXPECT_NE(environment.find("\"backend\":\"" + std::string(backend) + "\""),
        std::string::npos);
    EXPECT_NE(environment.find("\"instrument_mode\":\"" + std::string(mode) + "\""),
        std::string::npos);
    EXPECT_NE(environment.find("\"gpu_uuid_identity\":"), std::string::npos);
    const std::string runningExecutableHash =
        gate0::Sha256File(COMPUTELAB_EX2_GATE0_EXE);
    EXPECT_NE(environment.find(
        "\"executable_sha256\":\"" + runningExecutableHash + "\""),
        std::string::npos);
    EXPECT_NE(environment.find("\"input_sha256\":"), std::string::npos);
    if (backend == "cuda")
    {
        EXPECT_NE(environment.find("\"cuda_runtime_version\":\""),
            std::string::npos);
        EXPECT_NE(environment.find("\"vulkan_device_api_version\":null"),
            std::string::npos);
    }
    else
    {
        EXPECT_NE(environment.find("\"cuda_runtime_version\":null"),
            std::string::npos);
        EXPECT_NE(environment.find("\"vulkan_device_api_version\":\""),
            std::string::npos);
    }

    const std::string initialization = ReadAll(package / "initialization.csv");
    EXPECT_NE(initialization.find("setup_complete,,true\r\n"), std::string::npos);
    EXPECT_NE(initialization.find("warmup_complete,,true\r\n"), std::string::npos);

    const std::string samples = ReadAll(package / "samples.csv");
    EXPECT_EQ(std::count(samples.begin(), samples.end(), '\n'), 2);
    EXPECT_NE(samples.find(",0,true,ok,,,"), std::string::npos);
    if (mode == "H") EXPECT_TRUE(samples.ends_with(",\r\n"));
    else EXPECT_FALSE(samples.ends_with(",\r\n"));

    const std::string summary = ReadAll(package / "summary.json");
    EXPECT_NE(summary.find("\"recorded_sample_count\":1"), std::string::npos);
    EXPECT_NE(summary.find("\"validation_failures\":0"), std::string::npos);
    EXPECT_NE(summary.find("\"failed_sample_count\":0"), std::string::npos);
}

TEST(Ex2Gate0ChildSmoke, CudaH)
{
    RunChildSmoke("cuda", "H");
}

TEST(Ex2Gate0ChildSmoke, CudaN)
{
    RunChildSmoke("cuda", "N");
}

TEST(Ex2Gate0ChildSmoke, VulkanH)
{
    RunChildSmoke("vulkan", "H");
}

TEST(Ex2Gate0ChildSmoke, VulkanN)
{
    RunChildSmoke("vulkan", "N");
}

TEST(Ex2Gate0ChildExitClassification, MalformedArgumentsAreConfigurationErrors)
{
    const std::vector<std::string> arguments{
        COMPUTELAB_EX2_GATE0_EXE, "--unknown-option", "value"};
    std::vector<const char*> pointers;
    for (const auto& argument : arguments) pointers.push_back(argument.c_str());
    pointers.push_back(nullptr);
    EXPECT_EQ(_spawnv(_P_WAIT, arguments.front().c_str(), pointers.data()),
        static_cast<int>(gate0::ExitCode::ConfigurationError));
}

TEST(Ex2Gate0ChildExitClassification, PostValidationProvenanceFailureIsNotConfigurationError)
{
    const auto ticks = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::string runId = "g004-evidence-exit-"
        + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(ticks);
    const auto package = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local" / runId;
    RemovePackage cleanup{package};
    std::vector<std::string> arguments{
        COMPUTELAB_EX2_GATE0_EXE,
        "--phase", "sample-count-qualification",
        "--backend", "cuda",
        "--instrument-mode", "H",
        "--device-index", "0",
        "--element-count", "1",
        "--seed", "1",
        "--warmup-count", "0",
        "--planned-sample-count", "1",
        "--process-index", "0",
        "--block-index", "0",
        "--order-slot", "0",
        "--run-id", runId,
        "--output-package-directory", package.string(),
        "--machine-id", "g004-exit-machine",
        "--protocol-version", "1.0"};
    std::vector<const char*> pointers;
    for (const auto& argument : arguments) pointers.push_back(argument.c_str());
    pointers.push_back(nullptr);

    const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0U);
    std::vector<wchar_t> oldPath(required == 0U ? 1U : required);
    const bool hadPath = required != 0U;
    if (hadPath) GetEnvironmentVariableW(L"PATH", oldPath.data(), required);
    ASSERT_TRUE(SetEnvironmentVariableW(L"PATH", L""));
    const intptr_t exitCode = _spawnv(
        _P_WAIT, arguments.front().c_str(), pointers.data());
    ASSERT_TRUE(SetEnvironmentVariableW(
        L"PATH", hadPath ? oldPath.data() : nullptr));

    EXPECT_EQ(exitCode,
        static_cast<int>(gate0::ExitCode::EvidenceOrProvenanceFailure));
    EXPECT_FALSE(std::filesystem::exists(package));
    EXPECT_FALSE(std::filesystem::exists(package.string() + ".incomplete"));
}

} // namespace
