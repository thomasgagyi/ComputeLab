#include "app/Ex2A1Integration.hpp"

#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <gtest/gtest.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace a1 = computelab::ex2::a1;
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

a1::CrossBackendObservation SyntheticPassingObservation()
{
    a1::CrossBackendObservation result;
    result.configuration = ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 4U});
    result.verifiedDeviceUuid = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    result.verifiedDeviceUuidText =
        a1::FormatDeviceUuid(result.verifiedDeviceUuid);
    result.physicalIdentityVerified = true;
    result.input = {
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    result.expectedOutput = {
        0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U};
    result.inputSha256 = ex2::WordInputSha256(result.input);
    result.expectedOutputSha256 = ex2::WordInputSha256(result.expectedOutput);
    result.cuda = a1::CompletedObservation(
        result.expectedOutput, result.expectedOutput,
        result.input, result.input, true);
    result.vulkan = a1::CompletedObservation(
        result.expectedOutput, result.expectedOutput,
        result.input, result.input, true);
    result.crossBackendComparisonPerformed = true;
    result.crossBackendOutputsEqual = true;
    result.vulkanDiagnostics.queueFamilyIndex = 2U;
    result.vulkanDiagnostics.queueFamily.queueFlags = VK_QUEUE_COMPUTE_BIT;
    result.vulkanDiagnostics.queueFamily.queueCount = 1U;
    result.vulkanDiagnostics.inputMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    result.vulkanDiagnostics.outputMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    result.vulkanDiagnostics.uploadMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    result.vulkanDiagnostics.readbackMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    return result;
}

TEST(Ex2A1Identity, UsesEveryUuidByteAndRejectsZeroOrMismatch)
{
    const a1::DeviceUuid first{
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    EXPECT_EQ(
        a1::VerifySamePhysicalDevice(first, first),
        "00112233-4455-6677-8899-aabbccddeeff");

    auto different = first;
    different.back() ^= 1U;
    EXPECT_THROW(
        static_cast<void>(a1::VerifySamePhysicalDevice(first, different)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(a1::FormatDeviceUuid({})),
        std::invalid_argument);
}

TEST(Ex2A1Validation, FullBuffersAndInputPreservationDriveTheResult)
{
    const std::vector<std::uint32_t> input{1U, 2U, 3U};
    const std::vector<std::uint32_t> expected{4U, 5U, 6U};
    EXPECT_TRUE(a1::ValidateBuffers(
        expected, expected, input, input).Passed());

    auto altered = expected;
    altered[1] ^= 1U;
    EXPECT_FALSE(a1::ValidateBuffers(
        expected, altered, input, input).Passed());
    EXPECT_FALSE(a1::ValidateBuffers(
        expected,
        std::span<const std::uint32_t>{altered.data(), 2U},
        input,
        input).Passed());
    auto changedInput = input;
    changedInput[0] ^= 1U;
    EXPECT_FALSE(a1::ValidateBuffers(
        expected, expected, input, changedInput).Passed());

    const auto outputFailure = a1::CompletedObservation(
        expected, altered, input, input, true);
    EXPECT_EQ(outputFailure.status, evidence::OperationStatus::ValidationFailed);
    EXPECT_EQ(outputFailure.validationPassed, false);
    EXPECT_EQ(outputFailure.errorCode, evidence::error_code::OutputMismatch);

    const auto inputFailure = a1::CompletedObservation(
        expected, expected, input, changedInput, true);
    EXPECT_EQ(inputFailure.status, evidence::OperationStatus::ValidationFailed);
    EXPECT_EQ(inputFailure.errorCode, "input_preservation_failed");
}

TEST(Ex2A1Validation, BackendSpecificNegativeFixturesCannotBecomeSuccess)
{
    auto observation = SyntheticPassingObservation();
    auto alteredCuda = observation.expectedOutput;
    alteredCuda[0] ^= 1U;
    observation.cuda = a1::CompletedObservation(
        observation.expectedOutput,
        alteredCuda,
        observation.input,
        observation.input,
        true);
    a1::ReconcileCrossBackendOutputs(observation);
    EXPECT_FALSE(observation.Passed());
    EXPECT_EQ(observation.cuda.status,
        evidence::OperationStatus::ValidationFailed);

    observation = SyntheticPassingObservation();
    auto alteredVulkan = observation.expectedOutput;
    alteredVulkan.back() ^= 1U;
    observation.vulkan = a1::CompletedObservation(
        observation.expectedOutput,
        alteredVulkan,
        observation.input,
        observation.input,
        true);
    a1::ReconcileCrossBackendOutputs(observation);
    EXPECT_FALSE(observation.Passed());
    EXPECT_EQ(observation.vulkan.status,
        evidence::OperationStatus::ValidationFailed);

    EXPECT_EQ(observation.cuda.status, evidence::OperationStatus::Ok);
    EXPECT_EQ(observation.cuda.validationPassed, true);
    EXPECT_EQ(observation.vulkan.validationPassed, false);
    EXPECT_TRUE(observation.crossBackendComparisonPerformed);
    EXPECT_FALSE(observation.crossBackendOutputsEqual);

    auto pair = a1::BuildEvidence(
        observation,
        {"test-machine",
            "0123456789abcdef0123456789abcdef01234567",
            std::string(64U, 'a'),
            std::string(64U, 'b'),
            "a1-disagreement-cuda",
            "a1-disagreement-vulkan",
            CommonEnvironment("a1-disagreement-cuda", ex2::Backend::Cuda),
            CommonEnvironment("a1-disagreement-vulkan", ex2::Backend::Vulkan)});
    EXPECT_NE(pair.cuda.samplesCsv.find(",true,ok,"), std::string::npos);
    EXPECT_NE(pair.vulkan.samplesCsv.find(
        ",false,validation_failed,validation,output_mismatch,"),
        std::string::npos);
    EXPECT_EQ(pair.cuda.summary.recordedSampleCount, 1U);
    EXPECT_EQ(pair.cuda.summary.validationFailures, 0U);
    EXPECT_EQ(pair.cuda.summary.failedSampleCount, 0U);
    EXPECT_EQ(pair.vulkan.summary.recordedSampleCount, 1U);
    EXPECT_EQ(pair.vulkan.summary.validationFailures, 1U);
    EXPECT_EQ(pair.vulkan.summary.failedSampleCount, 1U);

    observation = SyntheticPassingObservation();
    observation.cuda = {};
    observation.cuda.expectedOutputGenerated = true;
    observation.cuda.operationCompleted = true;
    observation.cuda.status = evidence::OperationStatus::Incomplete;
    observation.cuda.failurePhase = evidence::FailurePhase::Readback;
    observation.cuda.errorCode =
        std::string(evidence::error_code::ReadbackFailed);
    observation.cuda.safeForFurtherGpuCalls = false;
    observation.vulkan = {};
    observation.vulkan.expectedOutputGenerated = true;
    observation.vulkan.status = evidence::OperationStatus::Incomplete;
    observation.vulkan.failurePhase = evidence::FailurePhase::Interrupted;
    observation.vulkan.errorCode =
        std::string(evidence::error_code::Interrupted);
    observation.vulkan.safeForFurtherGpuCalls = false;
    observation.crossBackendComparisonPerformed = false;
    observation.crossBackendOutputsEqual = false;
    EXPECT_FALSE(observation.Passed());
    EXPECT_NO_THROW(static_cast<void>(a1::BuildEvidence(
        observation,
        {"test-machine",
            "0123456789abcdef0123456789abcdef01234567",
            std::string(64U, 'a'),
            std::string(64U, 'b'),
            "a1-missing-cuda",
            "a1-missing-vulkan",
            CommonEnvironment("a1-missing-cuda", ex2::Backend::Cuda),
            CommonEnvironment("a1-missing-vulkan", ex2::Backend::Vulkan)})));
}

TEST(Ex2A1Failures, CleanupDistinguishesValidationFromUncertainNativeFailure)
{
    auto observation = SyntheticPassingObservation();
    auto altered = observation.expectedOutput;
    altered.front() ^= 1U;
    const auto completedValidationFailure = a1::CompletedObservation(
        observation.expectedOutput,
        altered,
        observation.input,
        observation.input,
        true);
    ASSERT_EQ(completedValidationFailure.status,
        evidence::OperationStatus::ValidationFailed);
    EXPECT_TRUE(completedValidationFailure.safeForFurtherGpuCalls);
    EXPECT_EQ(a1::ClassifyFailedSessionResourceDisposition(
        completedValidationFailure),
        a1::FailedSessionResourceDisposition::DestroyNormally);

    a1::BackendObservation uncertainNativeFailure;
    uncertainNativeFailure.expectedOutputGenerated = true;
    uncertainNativeFailure.status = evidence::OperationStatus::WaitFailed;
    uncertainNativeFailure.failurePhase = evidence::FailurePhase::CompletionWait;
    uncertainNativeFailure.errorCode =
        std::string(evidence::error_code::CompletionFailed);
    uncertainNativeFailure.safeForFurtherGpuCalls = false;
    EXPECT_EQ(a1::ClassifyFailedSessionResourceDisposition(
        uncertainNativeFailure),
        a1::FailedSessionResourceDisposition::PreserveForProcessTeardown);
}

TEST(Ex2A1Failures, NativePhasesMapToTruthfulExistingEvidenceStates)
{
    using CudaPhase = computelab::cuda::Ex2CudaA1NativePhase;
    using VulkanPhase = computelab::vulkan::Ex2VulkanA1NativePhase;
    EXPECT_EQ(
        a1::ClassifyCudaFailure(CudaPhase::InputAllocation, 1).phase,
        evidence::FailurePhase::ResourceAllocation);
    EXPECT_EQ(
        a1::ClassifyCudaFailure(CudaPhase::Submission, 1).status,
        evidence::OperationStatus::SubmitFailed);
    EXPECT_EQ(
        a1::ClassifyCudaFailure(CudaPhase::CompletionWait, 1).status,
        evidence::OperationStatus::WaitFailed);
    EXPECT_EQ(
        a1::ClassifyCudaFailure(CudaPhase::OutputReadback, 1).phase,
        evidence::FailurePhase::Readback);

    EXPECT_EQ(
        a1::ClassifyVulkanFailure(
            VulkanPhase::CompletionWait, VK_TIMEOUT).status,
        evidence::OperationStatus::Timeout);
    EXPECT_EQ(
        a1::ClassifyVulkanFailure(
            VulkanPhase::Submission, VK_ERROR_DEVICE_LOST).status,
        evidence::OperationStatus::DeviceLost);
    EXPECT_EQ(
        a1::ClassifyVulkanFailure(
            VulkanPhase::Submission, VK_ERROR_UNKNOWN).status,
        evidence::OperationStatus::SubmitFailed);
    EXPECT_EQ(
        a1::ClassifyVulkanFailure(
            VulkanPhase::OutputReadback, VK_ERROR_OUT_OF_HOST_MEMORY).phase,
        evidence::FailurePhase::Readback);
}

TEST(Ex2A1Evidence, CommonConditionAndSeparateSeriesValidateAndSerialize)
{
    const auto observation = SyntheticPassingObservation();
    auto pair = a1::BuildEvidence(
        observation,
        {"test-machine",
            "0123456789abcdef0123456789abcdef01234567",
            std::string(64U, 'a'),
            std::string(64U, 'b'),
            "a1-test-cuda",
            "a1-test-vulkan",
            CommonEnvironment("a1-test-cuda", ex2::Backend::Cuda),
            CommonEnvironment("a1-test-vulkan", ex2::Backend::Vulkan)});

    EXPECT_EQ(
        pair.cuda.environment.plan.comparisonConditionId,
        pair.vulkan.environment.plan.comparisonConditionId);
    EXPECT_NE(
        pair.cuda.environment.plan.seriesId,
        pair.vulkan.environment.plan.seriesId);
    EXPECT_NE(
        pair.cuda.environment.plan.runId,
        pair.vulkan.environment.plan.runId);
    EXPECT_EQ(pair.cuda.environment.inputSha256,
        pair.vulkan.environment.inputSha256);
    EXPECT_EQ(pair.cuda.environment.expectedOutputSha256,
        pair.vulkan.environment.expectedOutputSha256);
    EXPECT_FALSE(pair.cuda.environment.plan.seriesIdentity.shaderSha256.has_value());
    ASSERT_TRUE(pair.vulkan.environment.plan.seriesIdentity.shaderSha256.has_value());
    EXPECT_EQ(*pair.vulkan.environment.plan.seriesIdentity.shaderSha256,
        std::string(64U, 'b'));
    EXPECT_EQ(pair.cuda.samples.front().hostCompletionNanoseconds, std::nullopt);
    EXPECT_EQ(pair.vulkan.samples.front().nativeDeviceIntervalNanoseconds,
        std::nullopt);
    EXPECT_NE(pair.cuda.environmentJson.find("\"git_dirty\":true"),
        std::string::npos);
    EXPECT_NE(pair.cuda.samplesCsv.find(",true,ok,,,,,,\r\n"),
        std::string::npos);

    auto repeated = a1::BuildEvidence(
        observation,
        {"test-machine",
            "0123456789abcdef0123456789abcdef01234567",
            std::string(64U, 'a'),
            std::string(64U, 'b'),
            "a1-test-cuda",
            "a1-test-vulkan",
            CommonEnvironment("a1-test-cuda", ex2::Backend::Cuda),
            CommonEnvironment("a1-test-vulkan", ex2::Backend::Vulkan)});
    EXPECT_EQ(pair.cuda.environmentJson, repeated.cuda.environmentJson);
    EXPECT_EQ(pair.cuda.initializationCsv, repeated.cuda.initializationCsv);
    EXPECT_EQ(pair.cuda.samplesCsv, repeated.cuda.samplesCsv);
    EXPECT_EQ(pair.cuda.summaryJson, repeated.cuda.summaryJson);
    EXPECT_EQ(pair.vulkan.environmentJson, repeated.vulkan.environmentJson);
    EXPECT_EQ(pair.vulkan.initializationCsv, repeated.vulkan.initializationCsv);
    EXPECT_EQ(pair.vulkan.samplesCsv, repeated.vulkan.samplesCsv);
    EXPECT_EQ(pair.vulkan.summaryJson, repeated.vulkan.summaryJson);
}

TEST(Ex2A1Evidence, ExternalVerificationCannotBeClaimedBeforeIdentityMatch)
{
    auto observation = SyntheticPassingObservation();
    observation.physicalIdentityVerified = false;
    EXPECT_THROW(
        static_cast<void>(a1::BuildEvidence(
            observation,
            {"test-machine",
                "0123456789abcdef0123456789abcdef01234567",
                std::string(64U, 'a'),
                std::string(64U, 'b'),
                "a1-test-cuda",
                "a1-test-vulkan",
                CommonEnvironment("a1-test-cuda", ex2::Backend::Cuda),
                CommonEnvironment("a1-test-vulkan", ex2::Backend::Vulkan)})),
        std::invalid_argument);
}

TEST(Ex2A1CrossBackend, RealNativeOperationsMatchForEveryRequiredSize)
{
    constexpr std::array<std::uint64_t, 7> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 262'144U};
    std::optional<a1::DeviceUuid> firstUuid;
    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto observation = a1::RunCrossBackendCorrectness(
            size,
            ex2::CoreInputSeed,
            0,
            0U,
            COMPUTELAB_EX2_A1_SPIRV_PATH);
        EXPECT_TRUE(observation.Passed());
        EXPECT_EQ(observation.cuda.output.size(), size);
        EXPECT_EQ(observation.vulkan.output.size(), size);
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

TEST(Ex2A1CrossBackend, SelectorsDoNotFallBackToAnotherDevice)
{
    EXPECT_THROW(
        static_cast<void>(a1::RunCrossBackendCorrectness(
            1U, ex2::CoreInputSeed, -1, 0U,
            COMPUTELAB_EX2_A1_SPIRV_PATH)),
        computelab::cuda::Ex2CudaA1NativeError);
    EXPECT_THROW(
        static_cast<void>(a1::RunCrossBackendCorrectness(
            1U, ex2::CoreInputSeed, 0,
            std::numeric_limits<std::uint32_t>::max(),
            COMPUTELAB_EX2_A1_SPIRV_PATH)),
        std::invalid_argument);
}

TEST(Ex2A1EntryPoint, PublishesFourValidatedExploratoryArtifactsPerBackend)
{
    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string session = "i3c-e2e-" + std::to_string(GetCurrentProcessId())
        + "-" + std::to_string(unique);
    const auto root = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local";
    const auto cudaDirectory = root / (session + "-cuda");
    const auto vulkanDirectory = root / (session + "-vulkan");
    std::error_code cleanupError;
    std::filesystem::remove_all(cudaDirectory, cleanupError);
    cleanupError.clear();
    std::filesystem::remove_all(vulkanDirectory, cleanupError);

    const std::string command =
        "\"" + std::string(COMPUTELAB_EX2_A1_EXE) + "\""
        " --element-count 4 --cuda-device 0 --vulkan-device 0"
        " --machine-id i3c-test-machine --session-id " + session;
    const int exitCode = std::system(command.c_str());
    ASSERT_EQ(exitCode, 0);

    for (const auto& directory : {cudaDirectory, vulkanDirectory})
    {
        EXPECT_TRUE(std::filesystem::is_regular_file(
            directory / "environment.json"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            directory / "initialization.csv"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            directory / "samples.csv"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            directory / "summary.json"));
    }
    const std::string executableHash = ex2::Sha256File(COMPUTELAB_EX2_A1_EXE);
    const std::string shaderHash = ex2::Sha256File(
        COMPUTELAB_EX2_A1_SPIRV_PATH);
    const std::string cudaEnvironment = ReadText(
        cudaDirectory / "environment.json");
    const std::string vulkanEnvironment = ReadText(
        vulkanDirectory / "environment.json");
    EXPECT_NE(cudaEnvironment.find(executableHash), std::string::npos);
    EXPECT_NE(vulkanEnvironment.find(executableHash), std::string::npos);
    EXPECT_NE(cudaEnvironment.find("\"shader_sha256\":null"),
        std::string::npos);
    EXPECT_NE(vulkanEnvironment.find(shaderHash), std::string::npos);
    EXPECT_NE(ReadText(cudaDirectory / "samples.csv").find(",true,ok,"),
        std::string::npos);
    EXPECT_NE(ReadText(vulkanDirectory / "samples.csv").find(",true,ok,"),
        std::string::npos);

    cleanupError.clear();
    std::filesystem::remove_all(cudaDirectory, cleanupError);
    EXPECT_FALSE(cleanupError);
    cleanupError.clear();
    std::filesystem::remove_all(vulkanDirectory, cleanupError);
    EXPECT_FALSE(cleanupError);
}

TEST(Ex2A1EntryPoint, InvalidConfigurationAndSelectorReturnFailure)
{
    const auto unique = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string base = "i3c-negative-"
        + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(unique);
    const std::string executable = "\"" + std::string(COMPUTELAB_EX2_A1_EXE)
        + "\"";
    const std::string common =
        " --vulkan-device 0 --machine-id i3c-test-machine --session-id ";

    const int invalidConfiguration = std::system((
        executable
        + " --element-count 16777217 --cuda-device 0"
        + common + base + "-config").c_str());
    EXPECT_NE(invalidConfiguration, 0);

    const int invalidSelector = std::system((
        executable
        + " --element-count 1 --cuda-device 2147483647"
        + common + base + "-selector").c_str());
    EXPECT_NE(invalidSelector, 0);
}

} // namespace
