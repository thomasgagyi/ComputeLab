#include "ex2/Ex2CorrectnessSupervisor.hpp"
#include "app/Ex2CorrectnessExecution.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <limits>
#include <type_traits>

namespace
{
namespace ex2 = computelab::ex2;
namespace control = ex2::correctness::control;
namespace child = ex2::correctness;
namespace evidence = ex2::evidence;

std::string Replace(std::string text, std::string_view before, std::string_view after)
{
    const auto at = text.find(before);
    if (at == std::string::npos) throw std::runtime_error("fixture replacement absent");
    text.replace(at, before.size(), after);
    return text;
}
std::string ManifestJson(std::size_t cell = 0U)
{
    const bool transfer = std::holds_alternative<ex2::TransferConfiguration>(ex2::ApprovedCoreCells().at(cell).parameters);
    return "{\"manifest_version\":1,\"manifest_type\":\"i7-correctness\",\"manifest_id\":\"control-test\","
        "\"manifest_sha256\":\"HASH\",\"protocol_version\":\"1.1\",\"machine_id\":\"test-machine\","
        "\"child_executable_path\":\"out/build/x64-debug/src/app/ComputeLabEx2Correctness.exe\","
        "\"supervisor_record_path\":\"results/local/control-test.json\","
        "\"expected_source_revision\":\"" + std::string(40U, 'a') + "\",\"expected_git_dirty\":true,"
        "\"expected_child_executable_sha256\":\"" + std::string(64U, 'b') + "\","
        "\"expected_supervisor_executable_sha256\":\"" + std::string(64U, 'd') + "\","
        "\"expected_gpu_uuid\":\"00112233-4455-6677-8899-aabbccddeeff\","
        "\"cuda_device_ordinal\":0,\"vulkan_physical_device_index\":0,"
        "\"operation_timeout_ms\":1000,\"child_timeout_ms\":5000,\"campaign_timeout_ms\":10000,"
        "\"continuation_policy\":\"completed-validation-only\",\"declared_child_count\":1,"
        "\"children\":[{\"sequence_index\":0,\"core_cell_index\":" + std::to_string(cell)
        + ",\"session_id\":\"control-session\",\"expected_vulkan_shader_sha256\":"
        + (transfer ? "null" : "\"" + std::string(64U, 'c') + "\"") + "}]}";
}
std::string Seal(std::string json)
{
    return Replace(json, "HASH", control::CalculateManifestSha256(json));
}
control::Manifest Manifest(std::size_t cell = 0U) { return control::ParseManifest(Seal(ManifestJson(cell))); }
std::string Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void Write(const std::filesystem::path& path, const std::string& value)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << value;
    if (!out) throw std::runtime_error("fixture write failed");
}
struct Temp
{
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("computelab-i7-control-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directories(root / "results/local"); }
    ~Temp() { std::error_code error; std::filesystem::remove_all(root, error); }
};

computelab::results::EnvironmentRecord Common(const evidence::CorrectnessPlan& plan)
{
    return {2U, "EX-2", plan.runId, "2026-09-30T00:00:00Z", plan.seriesIdentity.sourceRevision, true,
        "test-machine", "Windows", "11", "CPU fixture", 1024U,
        "GPU fixture", "NVIDIA", "10DE:0000", 1024U, "driver", "toolkit", "runtime", "capability",
        "sdk", "api", "MSVC", "compiler", "cmake", "ninja", "x64-debug", "Debug", true, true};
}

// The test producer may use Prompt 2. The production inspector does not link
// Prompt 2 and derives all expectations from the manifest and semantic APIs.
child::SessionPlan Produce(const control::Manifest& m, const Temp& temp, bool validation = false,
    std::optional<evidence::I7FailureKind> failure = std::nullopt)
{
    const auto& c = m.children.front();
    auto pair = child::PrepareFoundationPair(child::SelectApprovedCoreCell(c.coreCellIndex),
        m.machineId, m.gpuUuid, m.sourceRevision, m.childExecutableSha256, c.vulkanShaderSha256,
        c.sessionId + "-cuda", c.sessionId + "-vulkan");
    evidence::SampleRecord cuda, vulkan;
    std::visit([&](const auto& data) {
        auto observed = data.expected;
        if (validation) observed[0] ^= 1U;
        if constexpr (std::is_same_v<std::decay_t<decltype(data)>, child::ByteLogicalData>)
        {
            cuda = evidence::MakeI7ComparedByteSample(pair.cuda, 0U, data.expected, observed);
            vulkan = evidence::MakeI7ComparedByteSample(pair.vulkan, 0U, data.expected, data.expected);
        }
        else
        {
            cuda = evidence::MakeI7ComparedWordSample(pair.cuda, 0U, data.expected, observed);
            vulkan = evidence::MakeI7ComparedWordSample(pair.vulkan, 0U, data.expected, data.expected);
        }
    }, pair.logicalData);
    if (failure) cuda = evidence::MakeI7FailureObservation(pair.cuda, 0U, *failure).sample;
    const child::SerializedPair serialized{
        child::BuildSerializedSeries(pair.cuda, Common(pair.cuda.Plan()), cuda, true,
            {.implementation = "test-cuda"}, "fixture setup"),
        child::BuildSerializedSeries(pair.vulkan, Common(pair.vulkan.Plan()), vulkan, true,
            {.implementation = "test-vulkan"}, "fixture setup")};
    const auto plan = child::MakeSessionPlan(temp.root / "results/local", c.sessionId);
    child::CreateStagingSession(plan);
    child::WriteStagedSession(plan, serialized);
    if (validation || failure)
        child::WriteExternalFailureRecord(plan, {1U, c.sessionId,
            validation ? child::ExternalFailurePhase::Validation : child::ExternalFailurePhase::BackendExecution,
            validation ? "validation_failed" : "backend_failed", true, m.sourceRevision,
            plan.cudaRunId, plan.vulkanRunId, true, std::nullopt});
    else child::FinalizeStagedSession(plan);
    return plan;
}

TEST(Ex2I7SupervisorManifest, CanonicalHashSortsKeysAndPreservesArrayOrder)
{
    const std::string json = "{\"z\":[2,1],\"manifest_sha256\":\"ignored\",\"a\":true}";
    const std::string expected = "{\"a\":true,\"z\":[2,1]}";
    EXPECT_EQ(control::CanonicalManifestPayload(json), expected);
    EXPECT_EQ(control::CalculateManifestSha256(json), ex2::Sha256(std::as_bytes(std::span(expected.data(), expected.size()))));
    EXPECT_NE(control::CalculateManifestSha256(json), control::CalculateManifestSha256(Replace(json, "[2,1]", "[1,2]")));
    EXPECT_NO_THROW(static_cast<void>(Manifest()));
}

class ManifestMutation : public testing::TestWithParam<std::pair<std::string, std::string>> {};
TEST_P(ManifestMutation, RejectsMalformedContract)
{
    auto json = Replace(ManifestJson(), GetParam().first, GetParam().second);
    EXPECT_THROW({ const auto sealed = Seal(json); static_cast<void>(control::ParseManifest(sealed)); }, std::exception);
}
INSTANTIATE_TEST_SUITE_P(Ex2I7Supervisor, ManifestMutation, testing::Values(
    std::pair{"\"manifest_version\":1", "\"manifest_version\":2"},
    std::pair{"\"manifest_version\":1", "\"manifest_version\":1,\"manifest_version\":1"},
    std::pair{"\"manifest_version\":1", "\"manifest_version\":1,\"unknown\":1"},
    std::pair{"\"manifest_version\":1,", ""},
    std::pair{"i7-correctness", "g0-qualification"}, std::pair{"\"1.1\"", "\"1.0\""},
    std::pair{"\"core_cell_index\":0", "\"core_cell_index\":22"},
    std::pair{"\"core_cell_index\":0", "\"core_cell_index\":-1"},
    std::pair{"\"core_cell_index\":0", "\"core_cell_index\":0.5"},
    std::pair{"\"core_cell_index\":0", "\"core_cell_index\":18446744073709551616"},
    std::pair{"\"sequence_index\":0", "\"sequence_index\":1"},
    std::pair{"control-session", "../escape"},
    std::pair{"out/build", "../build"}, std::pair{"out/build", "D:/build"},
    std::pair{"results/local/control-test.json", "results/control-test.json"},
    std::pair{"\"operation_timeout_ms\":1000", "\"operation_timeout_ms\":0"},
    std::pair{"\"operation_timeout_ms\":1000", "\"operation_timeout_ms\":60001"},
    std::pair{"\"child_timeout_ms\":5000", "\"child_timeout_ms\":999"},
    std::pair{"\"campaign_timeout_ms\":10000", "\"campaign_timeout_ms\":4999"},
    std::pair{"\"expected_git_dirty\":true", "\"expected_git_dirty\":1"},
    std::pair{"completed-validation-only", "continue-anything"},
    std::pair{"\"declared_child_count\":1", "\"declared_child_count\":2"}));

TEST(Ex2I7SupervisorManifest, RejectsHashShaderAndCaseCollisions)
{
    EXPECT_THROW(static_cast<void>(control::ParseManifest(ManifestJson())), std::exception);
    auto json = Replace(ManifestJson(), "\"" + std::string(64U, 'c') + "\"", "null");
    EXPECT_THROW(static_cast<void>(control::ParseManifest(Seal(json))), std::exception);
    json = ManifestJson(21U);
    json = Replace(json, "\"expected_vulkan_shader_sha256\":null", "\"expected_vulkan_shader_sha256\":\"" + std::string(64U, 'c') + "\"");
    EXPECT_THROW(static_cast<void>(control::ParseManifest(Seal(json))), std::exception);
    json = Replace(ManifestJson(), "control-test.json", "CONTROL-SESSION");
    EXPECT_THROW(static_cast<void>(control::ParseManifest(Seal(json))), std::exception);
}

TEST(Ex2I7SupervisorPreflight, RejectsEachProvenanceMismatch)
{
    const auto m = Manifest();
    control::PreflightFacts good{m.sourceRevision, m.gitDirty, m.childExecutableSha256,
        m.supervisorExecutableSha256, m.gpuUuid, m.gpuUuid, {m.children[0].vulkanShaderSha256}};
    EXPECT_NO_THROW(control::ValidatePreflightFacts(m, good));
    for (int i = 0; i < 7; ++i)
    {
        auto bad = good;
        if (i == 0) bad.sourceRevision[0] = '0';
        if (i == 1) bad.gitDirty = false;
        if (i == 2) bad.childSha256[0] = '0';
        if (i == 3) bad.supervisorSha256[0] = '0';
        if (i == 4) bad.cudaUuid[0] = 'f';
        if (i == 5) bad.vulkanUuid[0] = 'f';
        if (i == 6) bad.shaderSha256[0] = std::string(64U, '0');
        EXPECT_THROW(control::ValidatePreflightFacts(m, bad), std::exception) << i;
    }
}

TEST(Ex2I7SupervisorPreflight, RejectsEveryOutputCollisionWithoutLaunch)
{
    const auto m = Manifest();
    for (const auto& name : {"control-session", "control-session.incomplete", "control-session.failure.json",
            "control-test.json", "control-test.json.incomplete", "control-test.json.incomplete.tmp"})
    {
        Temp temp;
        Write(temp.root / "results/local" / name, "owned elsewhere");
        EXPECT_THROW(control::RejectOutputCollisions(m, temp.root), std::exception);
        EXPECT_EQ(control::ExecuteManifest(m, {temp.root}), control::ExitCode::ConfigurationOrPreflightError);
        EXPECT_EQ(Read(temp.root / "results/local" / name), "owned elsewhere");
    }
}

class InspectorCell : public testing::TestWithParam<std::size_t> {};
TEST_P(InspectorCell, IndependentlyChecksAllCoreSemanticFamiliesWithoutGpu)
{
    Temp temp;
    const auto m = Manifest(GetParam());
    Produce(m, temp);
    const auto result = control::InspectPackage(m, m.children[0], temp.root, 0U);
    EXPECT_EQ(result.state, "complete-pass") << (result.errors.empty() ? "" : result.errors[0]);
    EXPECT_TRUE(result.structurallyValid);
    ASSERT_EQ(result.artifacts.size(), 8U);
    for (const auto& a : result.artifacts)
        EXPECT_EQ(a.sha256, ex2::Sha256File(temp.root / a.relativePath));
}
INSTANTIATE_TEST_SUITE_P(Ex2I7Supervisor, InspectorCell, testing::Values(0U, 3U, 6U, 8U, 12U, 15U, 16U, 19U));

class PackageMutation : public testing::TestWithParam<std::tuple<std::string, std::string, std::string>> {};
TEST_P(PackageMutation, DetectsActualDiskCorruption)
{
    Temp temp;
    const auto m = Manifest();
    const auto plan = Produce(m, temp);
    const auto path = plan.finalDirectory / plan.cudaRunId / std::get<0>(GetParam());
    Write(path, Replace(Read(path), std::get<1>(GetParam()), std::get<2>(GetParam())));
    const auto result = control::InspectPackage(m, m.children[0], temp.root, 0U);
    EXPECT_EQ(result.state, "contradictory");
    EXPECT_FALSE(result.structurallyValid);
}
INSTANTIATE_TEST_SUITE_P(Ex2I7Supervisor, PackageMutation, testing::Values(
    std::tuple{"environment.json", "\"schema_version\":2", "\"schema_version\":1"},
    std::tuple{"environment.json", "\"protocol_version\":\"1.1\"", "\"protocol_version\":\"1.0\""},
    std::tuple{"environment.json", "\"instrument_mode\":\"P\"", "\"instrument_mode\":\"N\""},
    std::tuple{"environment.json", "control-session-cuda", "other-run"},
    std::tuple{"environment.json", "\"comparison_condition_id\":\"", "\"comparison_condition_id\":\"0"},
    std::tuple{"environment.json", "\"series_id\":\"", "\"series_id\":\"0"},
    std::tuple{"environment.json", "\"input_sha256\":\"", "\"input_sha256\":\"0"},
    std::tuple{"environment.json", "\"expected_output_sha256\":\"", "\"expected_output_sha256\":\"0"},
    std::tuple{"environment.json", "00112233-4455-6677-8899-aabbccddeeff", "10112233-4455-6677-8899-aabbccddeeff"},
    std::tuple{"environment.json", "\"source_revision\":\"", "\"source_revision\":\"0"},
    std::tuple{"environment.json", "\"executable_sha256\":\"", "\"executable_sha256\":\"0"},
    std::tuple{"environment.json", "\"shader_sha256\":null", "\"shader_sha256\":\"bad\""},
    std::tuple{"environment.json", "\"native_markers_enabled\":false", "\"native_markers_enabled\":true"},
    std::tuple{"initialization.csv", "setup_complete", "fabricated_setup"},
    std::tuple{"samples.csv", ",true,ok,", ",false,ok,"},
    std::tuple{"samples.csv", ",,,\r\n", ",,42,\r\n"},
    std::tuple{"summary.json", "\"recorded_sample_count\":1", "\"recorded_sample_count\":2"},
    std::tuple{"summary.json", "\"validation_failures\":0", "\"validation_failures\":1"}));

TEST(Ex2I7SupervisorInspector, ReconcilesSiblingTopologyAndMembership)
{
    for (int i = 0; i < 7; ++i)
    {
        Temp temp;
        const auto m = Manifest();
        const auto plan = Produce(m, temp);
        if (i == 0) std::filesystem::create_directory(plan.stagingDirectory);
        if (i == 1) std::filesystem::rename(plan.finalDirectory, plan.stagingDirectory);
        if (i == 2) std::filesystem::remove_all(plan.finalDirectory);
        if (i == 3) Write(plan.failureSidecar, "{}");
        if (i == 4) std::filesystem::remove_all(plan.finalDirectory / plan.vulkanRunId);
        if (i == 5) Write(plan.finalDirectory / plan.cudaRunId / "extra.txt", "extra");
        if (i == 6) std::filesystem::remove(plan.finalDirectory / plan.cudaRunId / "summary.json");
        const auto result = control::InspectPackage(m, m.children[0], temp.root, 0U);
        EXPECT_FALSE(result.structurallyValid) << i;
        EXPECT_TRUE(result.state == "collision" || result.state == "contradictory") << i;
    }
}

TEST(Ex2I7SupervisorInspector, CompletedValidationMayContinueButNativeFailuresMayNot)
{
    Temp temp;
    const auto m = Manifest();
    Produce(m, temp, true);
    const auto result = control::InspectPackage(m, m.children[0], temp.root, 3U);
    ASSERT_EQ(result.state, "validation-failure") << (result.errors.empty() ? "" : result.errors[0]);
    control::ProcessResult process{"start", "end", 3U, std::nullopt, "complete", 4U};
    EXPECT_EQ(control::ContinuationDecision(process, result), "continue_validation_failure");
    for (int i = 0; i < 4; ++i)
    {
        auto bad = process;
        if (i == 0) bad.terminationReason = "cuda_attempt_timeout";
        if (i == 1) bad.progressValidation = "invalid";
        if (i == 2) { bad.progressValidation = "valid_prefix"; bad.eventCount = 2U; }
        if (i == 3) bad.exitCode = 0xc0000005U;
        EXPECT_EQ(control::ContinuationDecision(bad, result), "stop_incomplete_or_unsafe");
    }
    for (const auto kind : {evidence::I7FailureKind::SubmissionFailed, evidence::I7FailureKind::CompletionFailed,
            evidence::I7FailureKind::ReadbackFailed, evidence::I7FailureKind::DeviceLostDuringReadback})
    {
        Temp nativeTemp;
        Produce(m, nativeTemp, false, kind);
        const auto native = control::InspectPackage(m, m.children[0], nativeTemp.root, 3U);
        EXPECT_EQ(native.state, "incomplete") << (native.errors.empty() ? "" : native.errors[0]);
        EXPECT_EQ(control::ContinuationDecision(process, native), "stop_incomplete_or_unsafe");
    }
}

class SidecarMutation : public testing::TestWithParam<std::pair<std::string, std::string>> {};
TEST_P(SidecarMutation, RejectsInvalidRetainedTruth)
{
    Temp temp;
    const auto m = Manifest();
    const auto plan = Produce(m, temp, true);
    Write(plan.failureSidecar, Replace(Read(plan.failureSidecar), GetParam().first, GetParam().second));
    const auto result = control::InspectPackage(m, m.children[0], temp.root, 3U);
    EXPECT_EQ(result.state, "contradictory");
}
INSTANTIATE_TEST_SUITE_P(Ex2I7Supervisor, SidecarMutation, testing::Values(
    std::pair{"\"record_version\":1", "\"record_version\":2"},
    std::pair{"ex2-i7-correctness-child-failure", "other-record"},
    std::pair{"\"session_id\":\"control-session\"", "\"session_id\":\"wrong\""},
    std::pair{"\"source_revision\":\"", "\"source_revision\":\"0"},
    std::pair{"control-session-cuda", "wrong-cuda"},
    std::pair{"\"staging_retained\":true", "\"staging_retained\":false"},
    std::pair{"\"record_version\":1", "\"record_version\":1,\"unexpected\":1"},
    std::pair{"\"record_version\":1", "\"record_version\":1,\"record_version\":1"}));

TEST(Ex2I7SupervisorLedger, DurableSnapshotsFinalizationAndNoPerformanceData)
{
    Temp temp;
    const auto m = Manifest();
    control::ExecutionRecord r{m};
    r.startTimeUtc = "2026-09-30T00:00:00Z";
    const auto path = temp.root / m.supervisorRecordPath;
    control::WriteExecutionRecord(path, r, false);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(path.string() + ".incomplete"));
    r.status = "stopped_incomplete_or_unsafe";
    r.endTimeUtc = "2026-09-30T00:00:01Z";
    control::WriteExecutionRecord(path, r, true, true);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".incomplete"));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".incomplete.tmp"));
    const auto content = Read(path);
    EXPECT_NE(content.find("correctness_control_only_no_stage4_verdict"), std::string::npos);
    for (const auto* excluded : {"performanceCounter", "duration_ns", "throughput", "stage4_pass"})
        EXPECT_EQ(content.find(excluded), std::string::npos);
    EXPECT_THROW(control::WriteExecutionRecord(path, r, true), std::exception);
    EXPECT_EQ(Read(path), content);
}
TEST(Ex2I7SupervisorInspector, EarlyFailureHasNoStandardRowsAndCannotContinue)
{
    Temp temp;
    const auto m = Manifest();
    const auto& c = m.children[0];
    const auto plan = child::MakeSessionPlan(temp.root / "results/local", c.sessionId);
    child::CreateStagingSession(plan);
    child::WriteExternalFailureRecord(plan, {1U, c.sessionId, child::ExternalFailurePhase::BackendExecution,
        "backend_setup_not_established", true, m.sourceRevision, plan.cudaRunId, plan.vulkanRunId, true, std::nullopt});
    const auto result = control::InspectPackage(m, c, temp.root, 3U);
    EXPECT_EQ(result.state, "incomplete");
    EXPECT_FALSE(result.bothCompleted);
    EXPECT_EQ(result.artifacts.size(), 1U);
    EXPECT_EQ(control::ContinuationDecision({"start", "end", 3U, {}, "valid_prefix", 2U}, result),
        "stop_incomplete_or_unsafe");
}

TEST(Ex2I7SupervisorInspector, NonValidationSidecarsAndUnfinishedPairNeverContinue)
{
    for (const auto* phase : {"backend_execution", "provenance", "evidence_publication", "interrupted"})
    {
        Temp temp;
        const auto m = Manifest();
        const auto plan = Produce(m, temp, true);
        Write(plan.failureSidecar, Replace(Read(plan.failureSidecar), "\"failure_phase\":\"validation\"",
            "\"failure_phase\":\"" + std::string(phase) + "\""));
        const auto result = control::InspectPackage(m, m.children[0], temp.root, 3U);
        EXPECT_EQ(result.state, "incomplete");
        EXPECT_FALSE(control::ContinuationDecision({"start", "end", 3U, {}, "complete", 4U}, result).starts_with("continue"));
    }
    Temp temp;
    const auto m = Manifest();
    const auto plan = Produce(m, temp, true);
    std::filesystem::remove_all(plan.vulkanDirectory);
    const auto result = control::InspectPackage(m, m.children[0], temp.root, 3U);
    EXPECT_EQ(result.state, "incomplete");
    EXPECT_FALSE(result.bothCompleted);
}

TEST(Ex2I7SupervisorInspector, VulkanShaderCorruptionAndPairMismatchFailClosed)
{
    for (std::size_t cell : {0U, 19U})
    {
        Temp temp;
        const auto m = Manifest(cell);
        const auto plan = Produce(m, temp);
        const auto path = plan.finalDirectory / plan.vulkanRunId / "environment.json";
        Write(path, Replace(Read(path), cell == 0U ? "\"" + std::string(64U, 'c') + "\"" : "\"shader_sha256\":null",
            cell == 0U ? "null" : "\"shader_sha256\":\"" + std::string(64U, 'c') + "\""));
        EXPECT_EQ(control::InspectPackage(m, m.children[0], temp.root, 0U).state, "contradictory");
    }
}

TEST(Ex2I7SupervisorManifest, RejectsDuplicateCellsCaseSessionsAndSequenceGaps)
{
    const auto base = ManifestJson();
    const auto start = base.find("{\"sequence_index\"");
    const auto entry = base.substr(start, base.size() - start - 2U);
    for (int i = 0; i < 3; ++i)
    {
        auto second = Replace(entry, "\"sequence_index\":0", i == 2 ? "\"sequence_index\":2" : "\"sequence_index\":1");
        if (i != 0) second = Replace(second, "\"core_cell_index\":0", "\"core_cell_index\":3");
        second = Replace(second, "control-session", i == 1 ? "CONTROL-SESSION" : "other-session");
        auto json = Replace(base, "\"declared_child_count\":1", "\"declared_child_count\":2");
        json.insert(json.size() - 2U, "," + second);
        EXPECT_THROW(static_cast<void>(control::ParseManifest(Seal(json))), std::exception);
    }
}

TEST(Ex2I7SupervisorLedger, ExistingStagingAndTemporaryFilesAreNotOverwritten)
{
    const auto m = Manifest();
    for (const auto* suffix : {".incomplete", ".incomplete.tmp"})
    {
        Temp temp;
        const auto path = temp.root / m.supervisorRecordPath;
        Write(path.string() + suffix, "not owned");
        EXPECT_THROW(control::WriteExecutionRecord(path, {m}, false), std::exception);
        EXPECT_EQ(Read(path.string() + suffix), "not owned");
    }
}
} // namespace
