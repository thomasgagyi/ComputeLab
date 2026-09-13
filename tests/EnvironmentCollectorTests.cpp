#include "environment/EnvironmentCollector.hpp"
#include "results/ResultRecords.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace
{

using computelab::environment::BuildMetadata;
using computelab::environment::ComposeEnvironmentRecord;
using computelab::environment::CudaDeviceMetadata;
using computelab::environment::EnvironmentRunContext;
using computelab::environment::FormatGpuDeviceId;
using computelab::environment::GetConfiguredBuildMetadata;
using computelab::environment::VulkanDeviceMetadata;
using computelab::environment::WindowsHostMetadata;

EnvironmentRunContext Context()
{
    return {"EX-1", "caller-run", "2026-09-12T10:11:12Z", "caller-commit", true, "q0-caller-machine", true, false};
}

WindowsHostMetadata Host()
{
    return {"Windows", "10.0.26100", "Test CPU", 1024U};
}

BuildMetadata Build()
{
    return {"MSVC", "19.40", "4.3.0", "1.12.1", "x64-debug", "Debug", "13.4", "1.4.335"};
}

CudaDeviceMetadata CudaDevice()
{
    return {{{0x01U, 0x02U, 0x03U, 0x04U}}, "Test NVIDIA GPU", 4096U, 8, 6, "13.5"};
}

VulkanDeviceMetadata MatchingVulkanDevice()
{
    return {{{0x01U, 0x02U, 0x03U, 0x04U}}, 0x10DEU, 0x00001F06U, "1.4.321"};
}

TEST(EnvironmentCollector, CallerRunContextIsPreservedWithoutHostIdentityDiscovery)
{
    const auto record = ComposeEnvironmentRecord(
        Context(), Host(), Build(), CudaDevice(), {MatchingVulkanDevice()}, "580.1");

    EXPECT_EQ(record.experimentId, "EX-1");
    EXPECT_EQ(record.runId, "caller-run");
    EXPECT_EQ(record.timestampUtc, "2026-09-12T10:11:12Z");
    EXPECT_EQ(record.gitCommit, "caller-commit");
    EXPECT_TRUE(record.gitDirty);
    EXPECT_EQ(record.machineId, "q0-caller-machine");
    EXPECT_TRUE(record.validationEnabled);
    EXPECT_FALSE(record.diagnosticInstrumentation);
}

TEST(EnvironmentCollector, GpuDeviceIdUsesApprovedLowercasePaddedFormat)
{
    EXPECT_EQ(FormatGpuDeviceId(0x00001F06U), "0x00001f06");
    EXPECT_EQ(FormatGpuDeviceId(0xABCDEFFFU), "0xabcdefff");
}

TEST(EnvironmentCollector, MatchesCudaAndVulkanDevicesByUuid)
{
    const auto record = ComposeEnvironmentRecord(
        Context(), Host(), Build(), CudaDevice(), {MatchingVulkanDevice()}, "580.1");

    ASSERT_TRUE(record.gpuDeviceId.has_value());
    EXPECT_EQ(*record.gpuDeviceId, "0x00001f06");
    ASSERT_TRUE(record.gpuVendor.has_value());
    EXPECT_EQ(*record.gpuVendor, "NVIDIA");
}

TEST(EnvironmentCollector, RejectsMismatchedCudaAndVulkanUuids)
{
    auto mismatchedDevice = MatchingVulkanDevice();
    mismatchedDevice.uuid[0] = 0xFFU;

    EXPECT_THROW(
        static_cast<void>(ComposeEnvironmentRecord(
            Context(), Host(), Build(), CudaDevice(), {mismatchedDevice}, "580.1")),
        std::runtime_error);
}

TEST(EnvironmentCollector, PreservesIndependentCudaAndVulkanVersionSources)
{
    const auto record = ComposeEnvironmentRecord(
        Context(), Host(), Build(), CudaDevice(), {MatchingVulkanDevice()}, "580.1");

    ASSERT_TRUE(record.cudaToolkitVersion.has_value());
    ASSERT_TRUE(record.cudaRuntimeVersion.has_value());
    EXPECT_EQ(*record.cudaToolkitVersion, "13.4");
    EXPECT_EQ(*record.cudaRuntimeVersion, "13.5");
    ASSERT_TRUE(record.vulkanSdkVersion.has_value());
    ASSERT_TRUE(record.vulkanDeviceApiVersion.has_value());
    EXPECT_EQ(*record.vulkanSdkVersion, "1.4.335");
    EXPECT_EQ(*record.vulkanDeviceApiVersion, "1.4.321");
}

TEST(EnvironmentCollector, ConfiguredBuildMetadataIsPopulated)
{
    const BuildMetadata build = GetConfiguredBuildMetadata();

    EXPECT_FALSE(build.compilerName.empty());
    EXPECT_FALSE(build.compilerVersion.empty());
    EXPECT_FALSE(build.cmakeVersion.empty());
    EXPECT_FALSE(build.ninjaVersion.empty());
    EXPECT_EQ(build.configurePreset, "x64-debug");
    EXPECT_EQ(build.buildType, "Debug");
    EXPECT_FALSE(build.cudaToolkitVersion.empty());
    EXPECT_FALSE(build.vulkanSdkVersion.empty());
}

TEST(EnvironmentCollector, SuccessfulWindowsHostProbeHasCpuAndMemory)
{
    const WindowsHostMetadata host = computelab::environment::CollectWindowsHostMetadata();

    EXPECT_EQ(host.osName, "Windows");
    EXPECT_FALSE(host.osVersion.empty());
    EXPECT_FALSE(host.cpuName.empty());
    EXPECT_NE(host.systemMemoryBytes, 0U);
}

TEST(EnvironmentCollector, NvidiaDriverVersionIsAcquiredFromNvml)
{
    EXPECT_FALSE(computelab::environment::CollectNvidiaDriverVersion().empty());
}

TEST(EnvironmentCollector, ComposedRecordPassesUnchangedThroughA4aSerializer)
{
    const auto record = ComposeEnvironmentRecord(
        Context(), Host(), Build(), CudaDevice(), {MatchingVulkanDevice()}, "580.1");
    const std::string serialized = computelab::results::SerializeEnvironmentJson(record);

    EXPECT_NE(serialized.find("\"run_id\":\"caller-run\""), std::string::npos);
    EXPECT_NE(serialized.find("\"machine_id\":\"q0-caller-machine\""), std::string::npos);
    EXPECT_NE(serialized.find("\"gpu_device_id\":\"0x00001f06\""), std::string::npos);
}

} // namespace
