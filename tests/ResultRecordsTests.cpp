#include "results/ResultRecords.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using computelab::results::EnvironmentRecord;
using computelab::results::InitializationRecord;
using computelab::results::MetricSummary;
using computelab::results::SampleGroupKey;
using computelab::results::SampleGroupSummary;
using computelab::results::SampleRecord;
using computelab::results::SerializeEnvironmentJson;
using computelab::results::SerializeInitializationCsv;
using computelab::results::SerializeSamplesCsv;
using computelab::results::SerializeSummaryJson;
using computelab::results::SummaryRecord;

EnvironmentRecord CompleteEnvironment()
{
    return {
        1U, "EX-1", "run-001", "2026-09-12T10:11:12Z", "22bde3a", false,
        "q0-ampere", "Windows", "11", "Generic CPU", 34359738368ULL,
        "Example GPU", "NVIDIA", "10DE:1234", 8589934592ULL,
        "580.1", "13.4", "13.4", "8.6", "1.4.335", "1.4",
        "MSVC", "19.40", "3.25.0", "1.12.1", "x64-debug", "Debug", true, false};
}

TEST(ResultRecords, EnvironmentJsonUsesEveryApprovedFieldNameInDocumentOrder)
{
    EXPECT_EQ(
        SerializeEnvironmentJson(CompleteEnvironment()),
        "{\"schema_version\":1,\"experiment_id\":\"EX-1\",\"run_id\":\"run-001\",\"timestamp_utc\":\"2026-09-12T10:11:12Z\",\"git_commit\":\"22bde3a\",\"git_dirty\":false,\"machine_id\":\"q0-ampere\",\"os_name\":\"Windows\",\"os_version\":\"11\",\"cpu_name\":\"Generic CPU\",\"system_memory_bytes\":34359738368,\"gpu_name\":\"Example GPU\",\"gpu_vendor\":\"NVIDIA\",\"gpu_device_id\":\"10DE:1234\",\"gpu_memory_bytes\":8589934592,\"nvidia_driver_version\":\"580.1\",\"cuda_toolkit_version\":\"13.4\",\"cuda_runtime_version\":\"13.4\",\"cuda_compute_capability\":\"8.6\",\"vulkan_sdk_version\":\"1.4.335\",\"vulkan_device_api_version\":\"1.4\",\"compiler_name\":\"MSVC\",\"compiler_version\":\"19.40\",\"cmake_version\":\"3.25.0\",\"ninja_version\":\"1.12.1\",\"configure_preset\":\"x64-debug\",\"build_type\":\"Debug\",\"validation_enabled\":true,\"diagnostic_instrumentation\":false}\n");
}

TEST(ResultRecords, EnvironmentNullableFieldsSerializeAsJsonNull)
{
    auto record = CompleteEnvironment();
    record.gpuName.reset();
    record.gpuVendor.reset();
    record.gpuDeviceId.reset();
    record.gpuMemoryBytes.reset();
    record.nvidiaDriverVersion.reset();
    record.cudaToolkitVersion.reset();
    record.cudaRuntimeVersion.reset();
    record.cudaComputeCapability.reset();
    record.vulkanSdkVersion.reset();
    record.vulkanDeviceApiVersion.reset();

    const std::string serialized = SerializeEnvironmentJson(record);
    EXPECT_NE(serialized.find("\"gpu_name\":null"), std::string::npos);
    EXPECT_NE(serialized.find("\"cuda_toolkit_version\":null"), std::string::npos);
    EXPECT_NE(serialized.find("\"vulkan_device_api_version\":null"), std::string::npos);
}

TEST(ResultRecords, JsonStringsEscapeQuotesBackslashesAndControlCharacters)
{
    auto record = CompleteEnvironment();
    record.machineId = "machine\"\\\n\r\t\x01";

    EXPECT_NE(
        SerializeEnvironmentJson(record).find("\"machine_id\":\"machine\\\"\\\\\\n\\r\\t\\u0001\""),
        std::string::npos);
}

TEST(ResultRecords, InitializationCsvSerializesNumericAndQualitativeRows)
{
    const std::vector<InitializationRecord> records{
        {1U, "run", "EX-1", "cuda", 0U, 0U, "context", std::nullopt, std::nullopt, std::nullopt, "elapsed", 123U, std::nullopt},
        {1U, "run", "EX-1", "vulkan", 1U, 2U, "pipeline", "small", "diagnostic", 64U, "note", std::nullopt, "created"}};

    EXPECT_EQ(
        SerializeInitializationCsv(records),
        "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation\r\n"
        "1,run,EX-1,cuda,0,0,context,,,,elapsed,123,\r\n"
        "1,run,EX-1,vulkan,1,2,pipeline,small,diagnostic,64,note,,created\r\n");
}

TEST(ResultRecords, InitializationWithoutDurationOrObservationIsRejected)
{
    const InitializationRecord record{
        1U, "run", "EX-1", "cuda", 0U, 0U, "context", std::nullopt, std::nullopt, std::nullopt, "elapsed", std::nullopt, std::nullopt};

    try
    {
        static_cast<void>(SerializeInitializationCsv({record}));
        FAIL() << "expected an invalid_argument";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_STREQ("initialization record requires duration_ns or observation", error.what());
    }
}

TEST(ResultRecords, InitializationWithAnEmptyObservationAndNoDurationIsRejected)
{
    const InitializationRecord record{
        1U, "run", "EX-1", "cuda", 0U, 0U, "context", std::nullopt, std::nullopt, std::nullopt, "elapsed", std::nullopt, ""};

    EXPECT_THROW(static_cast<void>(SerializeInitializationCsv({record})), std::invalid_argument);
}

TEST(ResultRecords, CsvFieldsUseRfc4180Escaping)
{
    const InitializationRecord record{
        1U, "run,1", "EX-1", "cuda", 0U, 0U, "cat\"egory", "line1\r\nline2", std::nullopt, std::nullopt, "metric", std::nullopt, "quote \" and comma,"};

    EXPECT_EQ(
        SerializeInitializationCsv({record}),
        "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation\r\n"
        "1,\"run,1\",EX-1,cuda,0,0,\"cat\"\"egory\",\"line1\r\nline2\",,,metric,,\"quote \"\" and comma,\"\r\n");
}

TEST(ResultRecords, SamplesCsvUsesApprovedHeaderAndEmptyOptionalTimings)
{
    const SampleRecord record{
        1U, "run", "EX-1", "series", "cuda", "small", std::nullopt, 42U, 128U, 3U, 30U, 0U, false,
        std::nullopt, 11U, std::nullopt, 22U, std::nullopt};

    EXPECT_EQ(
        SerializeSamplesCsv({record}),
        "schema_version,run_id,experiment_id,series_id,backend,workload,variant,seed,element_count,warmup_count,planned_sample_count,sample_index,validation_passed,upload_ns,host_submission_ns,device_execution_ns,end_to_end_ns,download_ns\r\n"
        "1,run,EX-1,series,cuda,small,,42,128,3,30,0,false,,11,,22,\r\n");
}

TEST(ResultRecords, SummaryJsonRepresentsMetricsGroupsAndNulls)
{
    const MetricSummary endToEnd{3U, 10.0, 20.0, 18.0, 2.0, 2.0 / 18.0, 30.0};
    const SampleGroupSummary sampleGroup{
        {"series", "cuda", "large", std::nullopt, 99U, 4096U, 5U, 30U},
        4U,
        1U,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        endToEnd,
        MetricSummary{std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt}};
    const SummaryRecord record{1U, "run", "EX-1", {sampleGroup}};

    const std::string serialized = SerializeSummaryJson(record);
    EXPECT_NE(serialized.find("\"group\":{\"series_id\":\"series\",\"backend\":\"cuda\",\"workload\":\"large\",\"variant\":null,\"seed\":99,\"element_count\":4096,\"warmup_count\":5,\"planned_sample_count\":30}"), std::string::npos);
    EXPECT_NE(serialized.find("\"recorded_sample_count\":4,\"validation_failures\":1"), std::string::npos);
    EXPECT_NE(serialized.find("\"upload_ns\":null"), std::string::npos);
    EXPECT_NE(serialized.find("\"end_to_end_ns\":{\"sample_count\":3,\"minimum\":10,\"median\":20,\"mean\":18,\"standard_deviation\":2,\"coefficient_of_variation\":0.1111111111111111,\"p95\":30}"), std::string::npos);
    EXPECT_NE(serialized.find("\"download_ns\":{\"sample_count\":null,\"minimum\":null,\"median\":null,\"mean\":null,\"standard_deviation\":null,\"coefficient_of_variation\":null,\"p95\":null}"), std::string::npos);
}

TEST(ResultRecords, SerializersAreDeterministicForIdenticalRecords)
{
    const auto environment = CompleteEnvironment();
    const InitializationRecord initialization{
        1U, "run", "EX-1", "cpu", 0U, 0U, "startup", std::nullopt, std::nullopt, std::nullopt, "elapsed", 1U, std::nullopt};
    const SampleRecord sample{
        1U, "run", "EX-1", "series", "cpu", "small", std::nullopt, 1U, 10U, 0U, 10U, 0U, true,
        std::nullopt, 2U, std::nullopt, 3U, std::nullopt};
    const SummaryRecord summary{1U, "run", "EX-1", {}};

    EXPECT_EQ(SerializeEnvironmentJson(environment), SerializeEnvironmentJson(environment));
    EXPECT_EQ(SerializeInitializationCsv({initialization}), SerializeInitializationCsv({initialization}));
    EXPECT_EQ(SerializeSamplesCsv({sample}), SerializeSamplesCsv({sample}));
    EXPECT_EQ(SerializeSummaryJson(summary), SerializeSummaryJson(summary));
}

} // namespace
