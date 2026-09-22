#include "app/Ex2A2Integration.hpp"

#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <gtest/gtest.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

namespace
{

namespace a1 = computelab::ex2::a1;
namespace a2 = computelab::ex2::a2;
namespace evidence = computelab::ex2::evidence;
namespace ex2 = computelab::ex2;

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("test artifact could not be opened");
    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

computelab::results::EnvironmentRecord CommonEnvironment(
    std::string runId,
    ex2::Backend backend)
{
    computelab::results::EnvironmentRecord result;
    result.schemaVersion = 2U;
    result.experimentId = "EX-2";
    result.runId = std::move(runId);
    result.timestampUtc = "2026-09-21T12:00:00Z";
    result.gitCommit = "0123456789abcdef0123456789abcdef01234567";
    result.gitDirty = true;
    result.machineId = "test-machine";
    result.osName = "Windows";
    result.osVersion = "test";
    result.cpuName = "test-cpu";
    result.systemMemoryBytes = 1U;
    result.gpuName = "test-gpu";
    result.gpuVendor = "NVIDIA";
    result.gpuDeviceId = "0x00000001";
    result.gpuMemoryBytes = 1U;
    result.nvidiaDriverVersion = "test-driver";
    if (backend == ex2::Backend::Cuda)
    {
        result.cudaToolkitVersion = "test-toolkit";
        result.cudaRuntimeVersion = "test-runtime";
        result.cudaComputeCapability = "test-capability";
    }
    else
    {
        result.vulkanSdkVersion = "test-sdk";
        result.vulkanDeviceApiVersion = "test-api";
    }
    result.compilerName = "test-compiler";
    result.compilerVersion = "test-version";
    result.cmakeVersion = "test-cmake";
    result.ninjaVersion = "test-ninja";
    result.configurePreset = "x64-debug";
    result.buildType = "Debug";
    result.validationEnabled = true;
    result.diagnosticInstrumentation = true;
    return result;
}

a2::CrossBackendObservation SyntheticA2Observation()
{
    a2::CrossBackendObservation result;
    result.configuration = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A2, 4U});
    result.verifiedDeviceUuid = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    result.verifiedDeviceUuidText =
        a2::FormatDeviceUuid(result.verifiedDeviceUuid);
    result.physicalIdentityVerified = true;
    result.input = {
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    result.expectedOutput = {
        0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U};
    result.inputSha256 = ex2::WordInputSha256(result.input);
    result.expectedOutputSha256 = ex2::WordInputSha256(result.expectedOutput);
    result.vulkanShaderSha256 = std::string(64U, 'b');
    result.cuda = a2::CompletedObservation(
        result.expectedOutput, result.expectedOutput,
        result.input, result.input, true);
    result.vulkan = a2::CompletedObservation(
        result.expectedOutput, result.expectedOutput,
        result.input, result.input, true);
    result.crossBackendComparisonPerformed = true;
    result.crossBackendOutputsEqual = true;
    result.vulkanDiagnostics.queueFamilyIndex = 2U;
    result.vulkanDiagnostics.queueFamily.queueFlags = VK_QUEUE_COMPUTE_BIT;
    result.vulkanDiagnostics.queueFamily.queueCount = 1U;
    return result;
}

a2::EvidenceBuildContext EvidenceContext(std::string prefix)
{
    const std::string cudaRun = prefix + "-cuda";
    const std::string vulkanRun = prefix + "-vulkan";
    return {
        "test-machine",
        "0123456789abcdef0123456789abcdef01234567",
        std::string(64U, 'a'),
        std::string(64U, 'b'),
        cudaRun,
        vulkanRun,
        CommonEnvironment(cudaRun, ex2::Backend::Cuda),
        CommonEnvironment(vulkanRun, ex2::Backend::Vulkan)};
}

TEST(Ex2A2Evidence, UsesA2IdentityProvenanceAndNullTimingFields)
{
    const auto observation = SyntheticA2Observation();
    const auto pair = a2::BuildEvidence(
        observation, EvidenceContext("a2-test"));

    EXPECT_EQ(pair.cuda.environment.plan.comparisonConditionId,
        pair.vulkan.environment.plan.comparisonConditionId);
    EXPECT_NE(pair.cuda.environment.plan.seriesId,
        pair.vulkan.environment.plan.seriesId);
    EXPECT_NE(pair.cuda.environment.plan.runId,
        pair.vulkan.environment.plan.runId);
    EXPECT_EQ(pair.cuda.environment.inputSha256,
        pair.vulkan.environment.inputSha256);
    EXPECT_EQ(pair.cuda.environment.expectedOutputSha256,
        pair.vulkan.environment.expectedOutputSha256);
    EXPECT_FALSE(pair.cuda.environment.plan.seriesIdentity.shaderSha256.has_value());
    ASSERT_TRUE(pair.vulkan.environment.plan.seriesIdentity.shaderSha256.has_value());
    EXPECT_EQ(*pair.vulkan.environment.plan.seriesIdentity.shaderSha256,
        std::string(64U, 'b'));
    EXPECT_NE(pair.cuda.samplesCsv.find(",A,A2,"), std::string::npos);
    EXPECT_NE(pair.cuda.initializationCsv.find(",A,A2,"), std::string::npos);
    EXPECT_NE(pair.cuda.environmentJson.find("native_cuda_a2"),
        std::string::npos);
    EXPECT_NE(pair.vulkan.environmentJson.find("native_vulkan_a2"),
        std::string::npos);
    EXPECT_EQ(pair.cuda.samples.front().hostCompletionNanoseconds, std::nullopt);
    EXPECT_EQ(pair.vulkan.samples.front().nativeDeviceIntervalNanoseconds,
        std::nullopt);
    EXPECT_EQ(pair.cuda.summary.recordedSampleCount, 1U);
    EXPECT_EQ(pair.cuda.summary.validationFailures, 0U);
}

TEST(Ex2A2Evidence, RejectsShaderDigestThatDoesNotMatchLoadedArtifact)
{
    const auto observation = SyntheticA2Observation();
    auto context = EvidenceContext("a2-shader-mismatch");
    context.shaderSha256 = std::string(64U, 'c');
    EXPECT_THROW(
        static_cast<void>(a2::BuildEvidence(observation, std::move(context))),
        std::invalid_argument);
}

TEST(Ex2A2Evidence, A1AndA2ConditionsAreDistinct)
{
    const a2::DeviceUuid uuid{
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    const auto condition = [&](ex2::LinearVariant variant) {
        return ex2::ComparisonConditionContext{
            "1.0",
            "test-machine",
            {a2::FormatDeviceUuid(uuid), true},
            ex2::MakeConfiguration(ex2::LinearConfiguration{variant, 4U}),
            ex2::InstrumentMode::P};
    };
    EXPECT_NE(ex2::ComparisonConditionId(condition(ex2::LinearVariant::A1)),
        ex2::ComparisonConditionId(condition(ex2::LinearVariant::A2)));
}

TEST(Ex2A2Identity, PureUuidGateRejectsZeroAndMismatch)
{
    const a2::DeviceUuid first{
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    auto different = first;
    different.front() ^= 1U;
    EXPECT_THROW(
        static_cast<void>(a2::VerifySamePhysicalDevice(first, different)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(a2::FormatDeviceUuid({})),
        std::invalid_argument);
}

TEST(Ex2A2Regression, CudaPassRemainsOkWhenVulkanFails)
{
    auto observation = SyntheticA2Observation();
    auto alteredVulkan = observation.expectedOutput;
    alteredVulkan.back() ^= 1U;
    observation.vulkan = a2::CompletedObservation(
        observation.expectedOutput,
        alteredVulkan,
        observation.input,
        observation.input,
        true);
    a2::ReconcileCrossBackendOutputs(observation);

    EXPECT_FALSE(observation.Passed());
    EXPECT_EQ(observation.cuda.status, evidence::OperationStatus::Ok);
    EXPECT_EQ(observation.cuda.validationPassed, true);
    EXPECT_EQ(observation.vulkan.status,
        evidence::OperationStatus::ValidationFailed);
    EXPECT_EQ(observation.vulkan.validationPassed, false);
    EXPECT_TRUE(observation.crossBackendComparisonPerformed);
    EXPECT_FALSE(observation.crossBackendOutputsEqual);

    const auto pair = a2::BuildEvidence(
        observation, EvidenceContext("a2-disagreement"));
    EXPECT_NE(pair.cuda.samplesCsv.find(",true,ok,"), std::string::npos);
    EXPECT_NE(pair.vulkan.samplesCsv.find(
        ",false,validation_failed,validation,output_mismatch,"),
        std::string::npos);
}

TEST(Ex2A2Failures, NativePhasesMapToExistingEvidenceStates)
{
    EXPECT_EQ(a2::ClassifyCudaFailure(
        computelab::cuda::Ex2CudaA2NativePhase::Submission, 1).status,
        evidence::OperationStatus::SubmitFailed);
    EXPECT_EQ(a2::ClassifyCudaFailure(
        computelab::cuda::Ex2CudaA2NativePhase::OutputReadback, 1).phase,
        evidence::FailurePhase::Readback);
    EXPECT_EQ(a2::ClassifyVulkanFailure(
        computelab::vulkan::Ex2VulkanA2NativePhase::CompletionWait,
        VK_TIMEOUT).status,
        evidence::OperationStatus::Timeout);
    EXPECT_EQ(a2::ClassifyVulkanFailure(
        computelab::vulkan::Ex2VulkanA2NativePhase::Submission,
        VK_ERROR_DEVICE_LOST).status,
        evidence::OperationStatus::DeviceLost);
}

TEST(Ex2A2CrossBackend, RealNativeOperationsMatchForEveryRequiredSize)
{
    constexpr std::array<std::uint64_t, 8> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 4'097U, 262'144U};
    std::optional<a2::DeviceUuid> firstUuid;
    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto observation = a2::RunCrossBackendCorrectness(
            size, ex2::CoreInputSeed, 0, 0U,
            COMPUTELAB_EX2_A2_SPIRV_PATH);
        EXPECT_TRUE(observation.Passed());
        EXPECT_EQ(observation.cuda.output, observation.expectedOutput);
        EXPECT_EQ(observation.vulkan.output, observation.expectedOutput);
        EXPECT_EQ(observation.cuda.output, observation.vulkan.output);
        EXPECT_EQ(observation.cuda.deviceInput, observation.input);
        EXPECT_EQ(observation.vulkan.deviceInput, observation.input);
        EXPECT_EQ(observation.cuda.nativeDispatchExecuted, size != 0U);
        EXPECT_EQ(observation.vulkan.nativeDispatchExecuted, size != 0U);
        if (!firstUuid.has_value()) firstUuid = observation.verifiedDeviceUuid;
        EXPECT_EQ(observation.verifiedDeviceUuid, *firstUuid);
    }
}

TEST(Ex2A2CrossBackend, SelectorsDoNotFallBackToAnotherDevice)
{
    EXPECT_THROW(
        static_cast<void>(a2::RunCrossBackendCorrectness(
            1U, ex2::CoreInputSeed, -1, 0U,
            COMPUTELAB_EX2_A2_SPIRV_PATH)),
        computelab::cuda::Ex2CudaA2NativeError);
    EXPECT_THROW(
        static_cast<void>(a2::RunCrossBackendCorrectness(
            1U, ex2::CoreInputSeed, 0,
            std::numeric_limits<std::uint32_t>::max(),
            COMPUTELAB_EX2_A2_SPIRV_PATH)),
        std::invalid_argument);
}

TEST(Ex2A2EntryPoint, PublishesFourA2ArtifactsWithActualShaderDigest)
{
    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string session = "i3d-e2e-" + std::to_string(GetCurrentProcessId())
        + "-" + std::to_string(unique);
    const auto root = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local";
    const auto cudaDirectory = root / (session + "-cuda");
    const auto vulkanDirectory = root / (session + "-vulkan");

    const std::string command =
        "\"" + std::string(COMPUTELAB_EX2_A1_EXE) + "\""
        " --variant A2 --element-count 4 --cuda-device 0 --vulkan-device 0"
        " --machine-id i3d-test-machine --session-id " + session;
    ASSERT_EQ(std::system(command.c_str()), 0);

    for (const auto& directory : {cudaDirectory, vulkanDirectory})
    {
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / "environment.json"));
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / "initialization.csv"));
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / "samples.csv"));
        EXPECT_TRUE(std::filesystem::is_regular_file(directory / "summary.json"));
        EXPECT_NE(ReadText(directory / "samples.csv").find(",A,A2,"),
            std::string::npos);
        EXPECT_EQ(ReadText(directory / "samples.csv").find(",A,A1,"),
            std::string::npos);
    }
    const std::string executableHash = ex2::Sha256File(COMPUTELAB_EX2_A1_EXE);
    const std::string shaderHash = ex2::Sha256File(COMPUTELAB_EX2_A2_SPIRV_PATH);
    const std::string cudaEnvironment = ReadText(cudaDirectory / "environment.json");
    const std::string vulkanEnvironment = ReadText(vulkanDirectory / "environment.json");
    EXPECT_NE(cudaEnvironment.find(executableHash), std::string::npos);
    EXPECT_NE(cudaEnvironment.find("\"shader_sha256\":null"), std::string::npos);
    EXPECT_NE(vulkanEnvironment.find(shaderHash), std::string::npos);

    std::error_code error;
    std::filesystem::remove_all(cudaDirectory, error);
    EXPECT_FALSE(error);
    error.clear();
    std::filesystem::remove_all(vulkanDirectory, error);
    EXPECT_FALSE(error);
}

TEST(Ex2A2EntryPoint, InvalidA2ConfigurationAndSelectorsReturnFailure)
{
    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string base = "i3d-negative-"
        + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(unique);
    const std::string executable = "\"" + std::string(COMPUTELAB_EX2_A1_EXE)
        + "\" --variant A2";
    const std::string common =
        " --vulkan-device 0 --machine-id i3d-test-machine --session-id ";

    EXPECT_NE(std::system((
        executable
        + " --element-count 16777217 --cuda-device 0"
        + common + base + "-config").c_str()), 0);
    EXPECT_NE(std::system((
        executable
        + " --element-count 1 --cuda-device 2147483647"
        + common + base + "-selector").c_str()), 0);
}

} // namespace
