#include "ex2/Ex2Gate0.hpp"
#include "ex2/Ex2Gate0Supervisor.hpp"

#include <gtest/gtest.h>

#include <process.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;
namespace supervisor = computelab::ex2::gate0::supervisor;

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

void WriteAll(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.good());
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    ASSERT_TRUE(stream.good());
}

std::string JsonStringField(std::string_view json, std::string_view field)
{
    const std::string prefix = "\"" + std::string(field) + "\":\"";
    const auto start = json.find(prefix);
    if (start == std::string_view::npos) return {};
    const auto valueStart = start + prefix.size();
    const auto end = json.find('"', valueStart);
    if (end == std::string_view::npos) return {};
    return std::string(json.substr(valueStart, end - valueStart));
}

void RunChildSmoke(
    std::string_view backend,
    std::string_view mode,
    std::uint64_t plannedSamples = 2U,
    bool testInspectionTampering = false)
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
        "--planned-sample-count", std::to_string(plannedSamples),
        "--process-index", "0",
        "--block-index", "0",
        "--order-slot", "0",
        "--run-id", runId,
        "--output-package-directory", package.string(),
        "--machine-id", "g004-smoke-machine",
        "--protocol-version", "1.0"};
    const std::vector<std::string> childArguments(arguments.begin() + 1U, arguments.end());
    const auto execution = supervisor::RunSupervisedProcess(
        arguments.front(), childArguments,
        std::chrono::seconds(60), std::chrono::minutes(20),
        std::chrono::steady_clock::now() + std::chrono::minutes(20));
    ASSERT_EQ(execution.exitCode, 0U) << "qualification child failed for "
        << backend << " mode " << mode;
    EXPECT_FALSE(execution.terminationReason.has_value());
    EXPECT_EQ(execution.operationStarts, plannedSamples);
    EXPECT_EQ(execution.operationCompletions, plannedSamples);
    EXPECT_EQ(execution.progressValidation, "passed");

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
    EXPECT_EQ(std::count(samples.begin(), samples.end(), '\n'),
        static_cast<std::ptrdiff_t>(plannedSamples + 1U));
    EXPECT_NE(samples.find(",0,true,ok,,,"), std::string::npos);
    if (mode == "H") EXPECT_TRUE(samples.ends_with(",\r\n"));
    else EXPECT_FALSE(samples.ends_with(",\r\n"));

    const std::string summary = ReadAll(package / "summary.json");
    EXPECT_NE(summary.find("\"recorded_sample_count\":"
        + std::to_string(plannedSamples)), std::string::npos);
    EXPECT_NE(summary.find("\"validation_failures\":0"), std::string::npos);
    EXPECT_NE(summary.find("\"failed_sample_count\":0"), std::string::npos);
    if (plannedSamples == 1U)
    {
        EXPECT_NE(summary.find("\"standard_deviation\":null"),
            std::string::npos);
        EXPECT_NE(summary.find("\"coefficient_of_variation\":null"),
            std::string::npos);
    }

    gate0::Configuration configuration{
        gate0::QualificationPhase::SampleCountQualification,
        backend == "cuda" ? gate0::Backend::Cuda : gate0::Backend::Vulkan,
        mode == "H" ? gate0::InstrumentMode::H : gate0::InstrumentMode::N,
        0U, 257U, 123456789U, 0U, plannedSamples, 0U, 0U, 0U,
        runId, package, "g004-smoke-machine", "1.0"};
    supervisor::ManifestChild child{
        0U,
        "smoke-pair",
        configuration,
        JsonStringField(environment, "gpu_uuid_identity"),
        JsonStringField(environment, "source_revision"),
        runningExecutableHash,
        backend == "vulkan"
            ? std::optional<std::string>{JsonStringField(environment, "shader_sha256")}
            : std::nullopt,
        "results/local/" + runId};
    const auto inspection = supervisor::InspectPackage(child, 0U);
    EXPECT_TRUE(inspection.admitted);
    EXPECT_TRUE(inspection.structurallyValid);
    EXPECT_TRUE(inspection.integrityErrors.empty());

    if (testInspectionTampering)
    {
        const auto uncertainOutcome = supervisor::InspectPackage(child, std::nullopt);
        EXPECT_TRUE(uncertainOutcome.structurallyValid);
        EXPECT_FALSE(uncertainOutcome.admitted);
        EXPECT_FALSE(uncertainOutcome.artifacts.empty());
        EXPECT_NE(std::find(uncertainOutcome.admissionReasons.begin(),
            uncertainOutcome.admissionReasons.end(),
            "child_exit_outcome_unavailable"),
            uncertainOutcome.admissionReasons.end());

        std::string invalidHeader = samples;
        const auto headerField = invalidHeader.find("host_wait_ns");
        ASSERT_NE(headerField, std::string::npos);
        invalidHeader.replace(headerField, std::string("host_wait_ns").size(),
            "host_wait_xx");
        WriteAll(package / "samples.csv", invalidHeader);
        const auto headerInspection = supervisor::InspectPackage(child, 0U);
        EXPECT_FALSE(headerInspection.structurallyValid);
        EXPECT_FALSE(headerInspection.admitted);
        EXPECT_TRUE(std::any_of(headerInspection.integrityErrors.begin(),
            headerInspection.integrityErrors.end(), [](const std::string& error) {
                return error.find("samples.csv header is invalid")
                    != std::string::npos;
            }));

        WriteAll(package / "samples.csv", samples);
        for (const std::string_view field : {"sample_count", "minimum", "median",
                 "mean", "standard_deviation", "coefficient_of_variation", "p95"})
        {
            std::string invalidSummary = summary;
            const std::string key = "\"" + std::string(field) + "\":";
            const auto position = invalidSummary.find(key);
            ASSERT_NE(position, std::string::npos);
            const auto valueStart = position + key.size();
            const auto comma = invalidSummary.find(',', valueStart);
            const auto brace = invalidSummary.find('}', valueStart);
            const auto valueEnd = comma < brace ? comma : brace;
            ASSERT_NE(valueEnd, std::string::npos);
            const auto original = invalidSummary.substr(
                valueStart, valueEnd - valueStart);
            invalidSummary.replace(valueStart, valueEnd - valueStart,
                original == "0" ? "1" : "0");
            WriteAll(package / "summary.json", invalidSummary);
            const auto statisticInspection = supervisor::InspectPackage(child, 0U);
            EXPECT_FALSE(statisticInspection.structurallyValid) << field;
            EXPECT_FALSE(statisticInspection.admitted) << field;
            EXPECT_TRUE(std::any_of(statisticInspection.integrityErrors.begin(),
                statisticInspection.integrityErrors.end(),
                [field](const std::string& error) {
                    return error.find("." + std::string(field))
                        != std::string::npos;
                })) << field;
        }
    }
}

TEST(Ex2Gate0ChildSmoke, CudaH)
{
    RunChildSmoke("cuda", "H", 1U);
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

TEST(Ex2Gate0ChildSmoke, CompletedPackageInspectionRejectsTamperedHeaderAndStatistics)
{
    RunChildSmoke("cuda", "H", 2U, true);
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
