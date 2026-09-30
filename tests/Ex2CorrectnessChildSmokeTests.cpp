#include <gtest/gtest.h>

#include <process.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, {}};
}

struct ExactSmokeCleanup
{
    std::filesystem::path root;
    std::string session;
    ~ExactSmokeCleanup()
    {
        std::error_code error;
        std::filesystem::remove_all(root / session, error);
        error.clear();
        std::filesystem::remove_all(root / (session + ".incomplete"), error);
        error.clear();
        std::filesystem::remove(root / (session + ".failure.json"), error);
    }
};

TEST(Ex2I7CorrectnessChildSmoke, ExecutesRealA1AndPublishesVerifiedPair)
{
    const std::string session = "i7-prompt2-a1-smoke";
    const auto localRoot = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local";
    ExactSmokeCleanup cleanup{localRoot, session};
    std::error_code error;
    std::filesystem::remove_all(localRoot / session, error);
    error.clear();
    std::filesystem::remove_all(localRoot / (session + ".incomplete"), error);
    error.clear();
    std::filesystem::remove(localRoot / (session + ".failure.json"), error);

    std::vector<std::string> arguments{
        COMPUTELAB_EX2_CORRECTNESS_EXE,
        "--core-cell-index", "0",
        "--cuda-device", "0",
        "--vulkan-device", "0",
        "--machine-id", "i7-smoke-machine",
        "--session-id", session};
    std::vector<const char*> argv;
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const intptr_t exitCode = _spawnv(_P_WAIT, argv.front(), argv.data());
    ASSERT_EQ(exitCode, 0) << "real CUDA/Vulkan A1 correctness child failed";

    const auto final = localRoot / session;
    EXPECT_TRUE(std::filesystem::is_directory(final));
    EXPECT_FALSE(std::filesystem::exists(localRoot / (session + ".incomplete")));
    EXPECT_FALSE(std::filesystem::exists(localRoot / (session + ".failure.json")));
    const auto cuda = final / (session + "-cuda");
    const auto vulkan = final / (session + "-vulkan");
    for (const auto& directory : {cuda, vulkan})
    {
        ASSERT_TRUE(std::filesystem::is_directory(directory));
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
            std::filesystem::directory_iterator{}), 4);
        for (const auto* name : {"environment.json", "initialization.csv",
                 "samples.csv", "summary.json"})
            EXPECT_TRUE(std::filesystem::is_regular_file(directory / name));
    }

    const auto cudaEnvironment = ReadAll(cuda / "environment.json");
    const auto vulkanEnvironment = ReadAll(vulkan / "environment.json");
    for (const auto& environment : {cudaEnvironment, vulkanEnvironment})
    {
        EXPECT_NE(environment.find("\"schema_version\":2"), std::string::npos);
        EXPECT_NE(environment.find("\"protocol_version\":\"1.1\""),
            std::string::npos);
        EXPECT_NE(environment.find("\"instrument_mode\":\"P\""),
            std::string::npos);
        EXPECT_NE(environment.find("\"planned_sample_count\":1"),
            std::string::npos);
    }
    EXPECT_NE(cudaEnvironment.find("\"shader_sha256\":null"),
        std::string::npos);
    EXPECT_EQ(vulkanEnvironment.find("\"shader_sha256\":null"),
        std::string::npos);

    const auto cudaSamples = ReadAll(cuda / "samples.csv");
    const auto vulkanSamples = ReadAll(vulkan / "samples.csv");
    for (const auto& samples : {cudaSamples, vulkanSamples})
    {
        EXPECT_EQ(std::count(samples.begin(), samples.end(), '\n'), 2);
        EXPECT_NE(samples.find(",true,ok,,,"), std::string::npos);
        EXPECT_TRUE(samples.ends_with(",,,\r\n"));
    }
    for (const auto& summary : {
        ReadAll(cuda / "summary.json"), ReadAll(vulkan / "summary.json")})
    {
        EXPECT_NE(summary.find("\"recorded_sample_count\":1"),
            std::string::npos);
        EXPECT_NE(summary.find("\"validation_failures\":0"),
            std::string::npos);
        EXPECT_NE(summary.find("\"failed_sample_count\":0"),
            std::string::npos);
    }
}

} // namespace
