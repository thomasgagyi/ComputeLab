#include "ex2/Ex2Gate0Supervisor.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;
namespace supervisor = computelab::ex2::gate0::supervisor;

constexpr std::string_view kUuid = "00112233-4455-6677-8899-aabbccddeeff";

struct RemovePath final
{
    std::filesystem::path path;
    ~RemovePath()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::remove_all(path.string() + ".incomplete", error);
        std::filesystem::remove_all(path.string() + ".incomplete.tmp", error);
    }
};

std::filesystem::path TemporaryPath(std::string_view name)
{
    return std::filesystem::temp_directory_path()
        / (std::string(name) + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::string Child(
    std::uint64_t sequence,
    std::string_view pair,
    std::string_view phase,
    std::string_view backend,
    std::string_view mode,
    std::uint64_t warmup,
    std::uint64_t samples,
    std::uint64_t block,
    std::uint64_t slot,
    std::string_view uuid = kUuid,
    std::uint64_t process = 999U,
    std::uint64_t device = 0U)
{
    if (process == 999U) process = block;
    const std::string run = "run-" + std::to_string(sequence);
    return "{\"sequence_index\":" + std::to_string(sequence)
        + ",\"pair_id\":\"" + std::string(pair)
        + "\",\"phase\":\"" + std::string(phase)
        + "\",\"backend\":\"" + std::string(backend)
        + "\",\"instrument_mode\":\"" + std::string(mode)
        + "\",\"device_index\":" + std::to_string(device)
        + ",\"expected_gpu_uuid\":\"" + std::string(uuid)
        + "\",\"element_count\":257,\"seed\":123456789"
        + ",\"warmup_count\":" + std::to_string(warmup)
        + ",\"planned_sample_count\":" + std::to_string(samples)
        + ",\"process_index\":" + std::to_string(process)
        + ",\"block_index\":" + std::to_string(block)
        + ",\"order_slot\":" + std::to_string(slot)
        + ",\"run_id\":\"" + run
        + "\",\"output_package_path\":\"results/local/" + run
        + "\",\"protocol_version\":\"1.0\""
        + ",\"expected_source_revision\":\"" + std::string(40U, 'a')
        + "\",\"expected_executable_sha256\":\"" + std::string(64U, 'b')
        + "\",\"expected_shader_sha256\":"
        + (backend == "vulkan" ? "\"" + std::string(64U, 'c') + "\"" : "null")
        + "}";
}

std::string Join(const std::vector<std::string>& values)
{
    std::string output;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index != 0U) output.push_back(',');
        output += values[index];
    }
    return output;
}

std::vector<std::string> DirectChildren(
    std::string_view phase,
    std::uint64_t warmup,
    std::uint64_t samples)
{
    const std::array first{"cuda", "vulkan", "cuda", "vulkan", "cuda"};
    std::vector<std::string> children;
    for (std::uint64_t block = 0; block < 5U; ++block)
    {
        const std::string pair = "pair-" + std::to_string(block);
        children.push_back(Child(block * 2U, pair, phase, first[block], "H",
            warmup, samples, block, 0U));
        children.push_back(Child(block * 2U + 1U, pair, phase,
            first[block] == std::string_view("cuda") ? "vulkan" : "cuda",
            "H", warmup, samples, block, 1U));
    }
    return children;
}

std::vector<std::string> InstrumentChildren()
{
    std::vector<std::string> children;
    std::uint64_t sequence{};
    for (const std::string_view backend : {"cuda", "vulkan"})
    {
        for (std::uint64_t block = 0; block < 5U; ++block)
        {
            const std::string first = block % 2U == 0U ? "H" : "N";
            const std::string second = first == "H" ? "N" : "H";
            const std::string pair = std::string(backend) + "-pair-"
                + std::to_string(block);
            children.push_back(Child(sequence++, pair, "instrumentation-control",
                backend, first, 2U, 100U, block, 0U));
            children.push_back(Child(sequence++, pair, "instrumentation-control",
                backend, second, 2U, 100U, block, 1U));
        }
    }
    return children;
}

std::string FinalizeHash(std::string json)
{
    const std::string marker = "\"manifest_sha256\":\"" + std::string(64U, '0') + "\"";
    const auto position = json.find(marker);
    EXPECT_NE(position, std::string::npos);
    const std::string hash = supervisor::CalculateManifestSha256(json);
    json.replace(position + std::string("\"manifest_sha256\":\"").size(), 64U, hash);
    return json;
}

std::string Manifest(
    std::string_view type,
    std::vector<std::string> children,
    std::uint64_t operations,
    std::string prerequisite,
    std::uint64_t campaign = 20'000U)
{
    std::string json = "{\"manifest_version\":1,\"manifest_type\":\""
        + std::string(type)
        + "\",\"manifest_id\":\"manifest-test\",\"manifest_sha256\":\""
        + std::string(64U, '0')
        + "\",\"machine_id\":\"anonymous-machine\",\"protocol_version\":\"1.0\""
          ",\"child_executable_path\":\"out/build/x64-debug/src/app/ComputeLabEx2Gate0.exe\""
          ",\"vulkan_shader_path\":\"out/build/x64-debug/shaders/transform.spv\""
          ",\"supervisor_record_path\":\"results/local/manifest-test-supervisor.json\""
          ",\"continue_after_fatal_failure\":false,\"operation_timeout_ms\":10"
          ",\"child_timeout_ms\":1000,\"campaign_timeout_ms\":"
        + std::to_string(campaign)
        + ",\"declared_child_count\":" + std::to_string(children.size())
        + ",\"declared_operation_count\":" + std::to_string(operations)
        + ",\"prerequisite\":" + prerequisite
        + ",\"children\":[" + Join(children) + "]}";
    return FinalizeHash(std::move(json));
}

std::string WarmupManifest()
{
    return Manifest("warmup-characterization",
        DirectChildren("warmup-characterization", 0U, 0U), 480U, "null");
}

std::string SampleManifest()
{
    return Manifest("sample-count-qualification",
        DirectChildren("sample-count-qualification", 2U, 200U), 2020U,
        "{\"authorization_id\":\"reviewed-auth\",\"evidence_id\":\"warmup-evidence\""
        ",\"evidence_sha256\":\"" + std::string(64U, 'd')
        + "\",\"selected_warmup_count\":2,\"qualified_sample_count\":null}");
}

std::string InstrumentManifest()
{
    return Manifest("instrumentation-control", InstrumentChildren(), 2040U,
        "{\"authorization_id\":\"reviewed-auth\",\"evidence_id\":\"sample-evidence\""
        ",\"evidence_sha256\":\"" + std::string(64U, 'd')
        + "\",\"selected_warmup_count\":2,\"qualified_sample_count\":100}");
}

std::string RehashAfterReplace(
    std::string json,
    std::string_view from,
    std::string_view to)
{
    const auto position = json.find(from);
    EXPECT_NE(position, std::string::npos);
    json.replace(position, from.size(), to);
    const auto hashPosition = json.find("\"manifest_sha256\":\"")
        + std::string("\"manifest_sha256\":\"").size();
    json.replace(hashPosition, 64U, std::string(64U, '0'));
    return FinalizeHash(std::move(json));
}

void ParseAndDiscard(std::string_view json)
{
    const auto parsed = supervisor::ParseManifest(json, "D:/root");
    static_cast<void>(parsed);
}

TEST(Ex2Gate0SupervisorManifest, ParsesAllThreeBoundedTypes)
{
    const auto root = std::filesystem::path("D:/manifest-test-root");
    EXPECT_EQ(supervisor::ParseManifest(WarmupManifest(), root).children.size(), 10U);
    EXPECT_EQ(supervisor::ParseManifest(SampleManifest(), root).children.size(), 10U);
    EXPECT_EQ(supervisor::ParseManifest(InstrumentManifest(), root).children.size(), 20U);
}

TEST(Ex2Gate0SupervisorManifest, CanonicalHashIgnoresWhitespaceButDetectsMutation)
{
    const std::string baseline = WarmupManifest();
    const std::string spaced = " \n\t" + baseline + "\r\n";
    EXPECT_EQ(supervisor::CalculateManifestSha256(baseline),
        supervisor::CalculateManifestSha256(spaced));
    std::string changed = baseline;
    const auto position = changed.find("\"seed\":123456789");
    ASSERT_NE(position, std::string::npos);
    changed.replace(position, 18U, "\"seed\":123456788");
    EXPECT_THROW(ParseAndDiscard(changed), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, RejectsMissingAndDuplicateFields)
{
    std::string missing = WarmupManifest();
    const auto field = missing.find("\"machine_id\":\"anonymous-machine\",");
    ASSERT_NE(field, std::string::npos);
    missing.erase(field, std::string("\"machine_id\":\"anonymous-machine\",").size());
    EXPECT_THROW(ParseAndDiscard(missing), std::invalid_argument);

    std::string duplicate = WarmupManifest();
    duplicate.insert(1U, "\"manifest_version\":1,");
    EXPECT_THROW(ParseAndDiscard(duplicate), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, RejectsRunAndPathCollisions)
{
    std::string duplicateRun = RehashAfterReplace(
        WarmupManifest(), "\"run_id\":\"run-1\"", "\"run_id\":\"run-0\"");
    EXPECT_THROW(ParseAndDiscard(duplicateRun), std::invalid_argument);

    std::string duplicatePath = RehashAfterReplace(WarmupManifest(),
        "\"output_package_path\":\"results/local/run-1\"",
        "\"output_package_path\":\"results/local/run-0\"");
    EXPECT_THROW(ParseAndDiscard(duplicatePath), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, RejectsUnsupportedIndicesAndOrdering)
{
    const std::string badIndex = RehashAfterReplace(
        WarmupManifest(), "\"process_index\":0", "\"process_index\":5");
    EXPECT_THROW(ParseAndDiscard(badIndex), std::invalid_argument);
    const std::string badOrder = RehashAfterReplace(
        WarmupManifest(), "\"backend\":\"cuda\"", "\"backend\":\"vulkan\"");
    EXPECT_THROW(ParseAndDiscard(badOrder), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, RejectsUuidAndPairingMismatch)
{
    const std::string uuid = RehashAfterReplace(WarmupManifest(),
        std::string(kUuid), "10112233-4455-6677-8899-aabbccddeeff");
    EXPECT_THROW(ParseAndDiscard(uuid), std::invalid_argument);
    const std::string mode = RehashAfterReplace(InstrumentManifest(),
        "\"instrument_mode\":\"N\"", "\"instrument_mode\":\"H\"");
    EXPECT_THROW(ParseAndDiscard(mode), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, EnforcesPhasePrerequisites)
{
    const std::string noPrerequisite = RehashAfterReplace(SampleManifest(),
        "\"prerequisite\":{", "\"prerequisite_removed\":{");
    EXPECT_THROW(ParseAndDiscard(noPrerequisite), std::invalid_argument);
    const std::string unqualifiedCount = RehashAfterReplace(InstrumentManifest(),
        "\"qualified_sample_count\":100", "\"qualified_sample_count\":null");
    EXPECT_THROW(ParseAndDiscard(unqualifiedCount), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorManifest, RejectsBudgetMismatchAndOverflow)
{
    const std::string tooSmall = RehashAfterReplace(
        SampleManifest(), "\"campaign_timeout_ms\":20000",
        "\"campaign_timeout_ms\":9999");
    EXPECT_THROW(ParseAndDiscard(tooSmall), std::invalid_argument);
    const std::string wrongOperations = RehashAfterReplace(
        WarmupManifest(), "\"declared_operation_count\":480",
        "\"declared_operation_count\":18446744073709551615");
    EXPECT_THROW(ParseAndDiscard(wrongOperations), std::invalid_argument);
    const std::string arithmeticOverflow = RehashAfterReplace(SampleManifest(),
        "\"planned_sample_count\":200",
        "\"planned_sample_count\":18446744073709551615");
    EXPECT_THROW(ParseAndDiscard(arithmeticOverflow), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorPackage, MissingEvidenceClassificationUsesExitCode)
{
    auto manifest = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    const auto success = supervisor::InspectPackage(manifest.children.front(), 0U);
    EXPECT_EQ(success.packageState, "missing");
    EXPECT_FALSE(success.integrityErrors.empty());
    EXPECT_FALSE(success.structurallyValid);
    const auto failure = supervisor::InspectPackage(manifest.children.front(), 4U);
    EXPECT_EQ(failure.packageState, "missing");
    EXPECT_TRUE(failure.integrityErrors.empty());
    EXPECT_FALSE(failure.structurallyValid);
    EXPECT_FALSE(failure.admitted);
}

TEST(Ex2Gate0SupervisorPackage, RejectsIncompleteCompletedPackageAndRetainsStaging)
{
    auto manifest = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    auto& child = manifest.children.front();
    const auto package = TemporaryPath("g005-package");
    RemovePath cleanup{package};
    child.configuration.outputPackageDirectory = package;
    std::filesystem::create_directory(package);
    {
        std::ofstream stream(package / "environment.json");
        stream << "{}\n";
    }
    const auto malformed = supervisor::InspectPackage(child, 0U);
    EXPECT_EQ(malformed.packageState, "complete");
    EXPECT_FALSE(malformed.admitted);
    EXPECT_FALSE(malformed.integrityErrors.empty());

    std::filesystem::remove_all(package);
    const std::filesystem::path incomplete{package.string() + ".incomplete"};
    std::filesystem::create_directory(incomplete);
    {
        std::ofstream stream(incomplete / "samples.csv");
        stream << gate0::SamplesCsvHeader() << "\r\n";
    }
    const auto retained = supervisor::InspectPackage(child,
        static_cast<std::uint32_t>(gate0::ExitCode::IncompleteOrTimeout));
    EXPECT_EQ(retained.packageState, "incomplete");
    EXPECT_FALSE(retained.admitted);
    ASSERT_EQ(retained.artifacts.size(), 1U);
    EXPECT_EQ(retained.artifacts.front().relativePath, "samples.csv");
}

TEST(Ex2Gate0SupervisorProcess, LaunchesFreshProcessAndValidatesProgress)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "2", "--sleep-ms", "1", "--exit-code", "0"},
        std::chrono::milliseconds(500), std::chrono::seconds(5),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    EXPECT_EQ(result.exitCode, 0U);
    EXPECT_FALSE(result.terminationReason.has_value());
    EXPECT_EQ(result.operationStarts, 2U);
    EXPECT_EQ(result.operationCompletions, 2U);
    EXPECT_EQ(result.progressValidation, "passed");
}

TEST(Ex2Gate0SupervisorProcess, TerminatesOperationAtDeadlineWithoutRetry)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "1", "--sleep-ms", "250", "--exit-code", "0"},
        std::chrono::milliseconds(50), std::chrono::seconds(5),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    ASSERT_TRUE(result.terminationReason.has_value());
    EXPECT_EQ(*result.terminationReason, "operation_timeout");
    EXPECT_EQ(result.operationStarts, 1U);
    EXPECT_EQ(result.operationCompletions, 0U);
    EXPECT_EQ(result.progressValidation, "incomplete_operation");
}

TEST(Ex2Gate0SupervisorProcess, RejectsOverdueCompletionFoundInFinalDrain)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "1", "--sleep-ms", "5", "--defer-progress-write",
            "--exit-code", "0"},
        std::chrono::milliseconds(1), std::chrono::seconds(5),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    EXPECT_TRUE(result.exitCode == 0U
        || result.exitCode
            == static_cast<std::uint32_t>(gate0::ExitCode::IncompleteOrTimeout));
    ASSERT_TRUE(result.terminationReason.has_value());
    EXPECT_EQ(*result.terminationReason, "operation_timeout");
    EXPECT_EQ(result.operationStarts, 1U);
    EXPECT_EQ(result.operationCompletions, 1U);
    EXPECT_EQ(result.progressValidation, "passed");
}

TEST(Ex2Gate0SupervisorProcess, RejectsChildDeadlineCrossedInFinalWait)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "0", "--exit-code", "0"},
        std::chrono::milliseconds(1), std::chrono::milliseconds(1),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    EXPECT_TRUE(result.exitCode == 0U
        || result.exitCode
            == static_cast<std::uint32_t>(gate0::ExitCode::IncompleteOrTimeout));
    ASSERT_TRUE(result.terminationReason.has_value());
    EXPECT_EQ(*result.terminationReason, "child_timeout");
}

TEST(Ex2Gate0SupervisorProcess, ClassifiesChildExitWithIncompleteProgress)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "1", "--omit-completion", "--exit-code", "3"},
        std::chrono::milliseconds(500), std::chrono::seconds(5),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    EXPECT_EQ(result.exitCode, 3U);
    EXPECT_EQ(result.operationStarts, 1U);
    EXPECT_EQ(result.operationCompletions, 0U);
    EXPECT_EQ(result.progressValidation, "incomplete_operation");
}

TEST(Ex2Gate0SupervisorProcess, EnforcesChildDeadlineBeforeFirstOperation)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "0", "--pre-sleep-ms", "250", "--exit-code", "0"},
        std::chrono::milliseconds(50), std::chrono::milliseconds(100),
        std::chrono::steady_clock::now() + std::chrono::seconds(10));
    ASSERT_TRUE(result.terminationReason.has_value());
    EXPECT_EQ(*result.terminationReason, "child_timeout");
    EXPECT_EQ(result.operationStarts, 0U);
    EXPECT_EQ(result.operationCompletions, 0U);
}

TEST(Ex2Gate0SupervisorProcess, EnforcesCampaignDeadlineDuringChild)
{
    const auto result = supervisor::RunSupervisedProcess(
        COMPUTELAB_EX2_GATE0_SUPERVISOR_TEST_CHILD,
        {"--operations", "0", "--pre-sleep-ms", "250", "--exit-code", "0"},
        std::chrono::milliseconds(500), std::chrono::seconds(5),
        std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
    ASSERT_TRUE(result.terminationReason.has_value());
    EXPECT_EQ(*result.terminationReason, "campaign_timeout");
    EXPECT_EQ(result.operationStarts, 0U);
    EXPECT_EQ(result.operationCompletions, 0U);
}

TEST(Ex2Gate0SupervisorRecord, IsExecutionControlEvidenceWithoutGateVerdict)
{
    supervisor::SupervisorExecutionRecord record;
    record.manifest = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    record.startTimeUtc = "2026-09-17T00:00:00.000Z";
    record.endTimeUtc = "2026-09-17T00:00:01.000Z";
    record.overallExecutionStatus = "qualification_execution_complete";
    const std::string json = supervisor::SerializeExecutionRecord(record);
    EXPECT_NE(json.find("\"manifest_sha256\":"), std::string::npos);
    EXPECT_NE(json.find("qualification_only_no_gate_verdict"), std::string::npos);
    EXPECT_NE(json.find("\"scheduled_children\":["), std::string::npos);
    EXPECT_NE(json.find("\"actual_executions\":["), std::string::npos);
    EXPECT_EQ(json.find("\"gate_0_verdict\""), std::string::npos);
}

TEST(Ex2Gate0SupervisorAdmission, SupervisorFailureRevokesStructurallyValidPackage)
{
    supervisor::PackageInspection package;
    package.packageState = "complete";
    package.structurallyValid = true;
    package.admitted = true;
    supervisor::SupervisedProcessResult execution;
    execution.exitCode = 0U;
    execution.terminationReason = "child_timeout";
    execution.operationStarts = 2U;
    execution.operationCompletions = 2U;
    execution.progressValidation = "passed";
    supervisor::ApplySupervisorAdmission(package, execution, 2U);
    EXPECT_TRUE(package.structurallyValid);
    EXPECT_TRUE(package.integrityErrors.empty());
    EXPECT_FALSE(package.admitted);
    ASSERT_EQ(package.admissionReasons.size(), 1U);
    EXPECT_EQ(package.admissionReasons.front(),
        "supervisor_termination:child_timeout");

    supervisor::SupervisorExecutionRecord record;
    record.manifest = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    supervisor::ChildExecutionRecord child;
    child.package = package;
    record.actualExecutions.push_back(child);
    const auto json = supervisor::SerializeExecutionRecord(record);
    EXPECT_NE(json.find("\"integrity_validation\":\"passed\""),
        std::string::npos);
    EXPECT_NE(json.find("\"admission_eligibility\":\"rejected\""),
        std::string::npos);
}

TEST(Ex2Gate0SupervisorPrerequisite, RecordsExternalHumanReviewBoundary)
{
    const auto warmup = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    const auto downstream = supervisor::ParseManifest(SampleManifest(), "D:/root");
    EXPECT_EQ(supervisor::PrerequisiteVerificationBoundary(warmup),
        "not_applicable");
    EXPECT_EQ(supervisor::PrerequisiteVerificationBoundary(downstream),
        "external_human_review_required_not_machine_verified");

    supervisor::SupervisorExecutionRecord record;
    record.manifest = downstream;
    const auto json = supervisor::SerializeExecutionRecord(record);
    EXPECT_NE(json.find("\"prerequisite_verification_boundary\":"
        "\"external_human_review_required_not_machine_verified\""),
        std::string::npos);

    const auto malformedHash = RehashAfterReplace(SampleManifest(),
        std::string(64U, 'd'), std::string(64U, 'g'));
    EXPECT_THROW(ParseAndDiscard(malformedHash), std::invalid_argument);
}

TEST(Ex2Gate0SupervisorRecord, PublishesIncompleteThenFinalRecordAtomically)
{
    supervisor::SupervisorExecutionRecord record;
    record.manifest = supervisor::ParseManifest(WarmupManifest(), "D:/root");
    record.startTimeUtc = "2026-09-17T00:00:00.000Z";
    record.endTimeUtc = "";
    const auto path = TemporaryPath("g005-supervisor-record");
    RemovePath cleanup{path};
    supervisor::WriteExecutionRecord(path, record, false);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::is_regular_file(path.string() + ".incomplete"));

    record.endTimeUtc = "2026-09-17T00:00:01.000Z";
    record.overallExecutionStatus = "qualification_execution_complete";
    supervisor::WriteExecutionRecord(path, record, true);
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".incomplete"));
    EXPECT_EQ(gate0::Sha256File(path).size(), 64U);
}

} // namespace
