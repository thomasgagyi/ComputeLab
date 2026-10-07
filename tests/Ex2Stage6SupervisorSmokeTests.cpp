#include "ex2/Ex2Stage6Supervisor.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <chrono>
#include <iostream>
namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace c = s6::control;
namespace p = s6::progress;
namespace a = s6::analysis;
constexpr std::string_view Uuid = "00112233-4455-6677-8899-aabbccddeeff";
std::string Replace(std::string text, std::string_view before, std::string_view after)
{ const auto n = text.find(before); if (n == text.npos) throw std::runtime_error("fixture replacement missing"); text.replace(n, before.size(), after); return text; }
std::string Rehash(std::string_view raw)
{
    auto payload = c::CanonicalManifestPayload(raw); const auto hash = ex2::Sha256(payload);
    payload.insert(payload.find("\"manifest_type\""), "\"manifest_sha256\":\"" + hash + "\","); return payload + "\n";
}
std::string ManifestBytes(std::string id = "s6-i3-test")
{
    std::string raw = "{\"manifest_version\":1,\"manifest_type\":\"ex2-stage6-diagnostic\",\"manifest_id\":\"" + id
        + "\",\"manifest_sha256\":\"" + std::string(64, '0') + "\",\"protocol_version\":\"1.4\",\"evidence_schema_version\":2,\"evidence_kind\":\"diagnostic\",\"instrument_mode\":\"H\","
        "\"machine_id\":\"test-machine\",\"child_executable_path\":\"out/build/x64-release/src/app/ComputeLabEx2Stage6.exe\",\"expected_source_revision\":\"" + std::string(40, 'a')
        + "\",\"expected_git_dirty\":false,\"expected_child_executable_sha256\":\"" + std::string(64, 'b') + "\",\"expected_supervisor_executable_sha256\":\"" + std::string(64, 'c')
        + "\",\"expected_gpu_uuid\":\"" + std::string(Uuid) + "\",\"cuda_device_ordinal\":0,\"vulkan_physical_device_index\":0,\"expected_vulkan_shader_sha256\":{";
    bool first = true; for (const auto name : {"a1", "a2", "b1", "b2", "c", "d1"}) { if (!first) raw += ','; first = false; raw += '"' + std::string(name) + "\":\"" + std::string(64, 'd') + '"'; }
    raw += "},\"operation_timeout_ms\":1000,\"child_timeout_ms\":10000,\"campaign_timeout_ms\":86400000,\"continuation_policy\":\"resolved-only-no-retry\",\"declared_cell_group_count\":22,\"declared_child_count\":220,\"groups\":[";
    for (std::size_t g = 0; g < 22; ++g) {
        if (g) raw += ','; raw += "{\"cell_index\":" + std::to_string(g) + ",\"declared_child_count\":10,\"children\":[";
        for (std::size_t n = 0; n < 10; ++n) { if (n) raw += ','; const auto number = std::to_string(g * 10 + n);
            raw += "{\"sequence_index\":" + number + ",\"session_id\":\"" + id + "-slot-" + std::string(3 - number.size(), '0') + number + "\"}"; }
        raw += "]}";
    }
    return Rehash(raw + "]}");
}
void Smoke(std::uint64_t sequence)
{
#ifndef NDEBUG
    FAIL() << "Supervised S6-I3 GPU smoke requires the x64-release target";
#else
    const auto start = std::chrono::steady_clock::now();
    std::wstring module(32768, L'\0'); const auto n = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())); ASSERT_GT(n, 0); ASSERT_LT(n, module.size()); module.resize(n);
    const c::RuntimePaths paths{COMPUTELAB_REPOSITORY_ROOT, module,
        {COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH, COMPUTELAB_EX2_B1_SPIRV_PATH,
         COMPUTELAB_EX2_B2_SPIRV_PATH, COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}};
    const auto id = "s6-i3-smoke-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(sequence) + "-" + std::to_string(start.time_since_epoch().count());
    auto bytes = ManifestBytes(id); auto m = c::ParseManifest(bytes);
    const auto relative = std::filesystem::path(COMPUTELAB_EX2_STAGE6_EXE).lexically_relative(paths.repositoryRoot).generic_string();
    bytes = Replace(bytes, m.childExecutablePath, relative); m = c::ParseManifest(Rehash(bytes));
    const auto facts = c::CollectPreflightFacts(m, paths); ASSERT_EQ(facts.cudaUuid, facts.vulkanUuid);
    bytes = Replace(bytes, m.sourceRevision, facts.sourceRevision); bytes = Replace(bytes, m.childExecutableSha256, facts.childSha256);
    bytes = Replace(bytes, m.supervisorExecutableSha256, facts.supervisorSha256); bytes = Replace(bytes, m.gpuUuid, facts.cudaUuid);
    std::size_t shader{}; for (const auto key : {"a1", "a2", "b1", "b2", "c", "d1"}) {
        bytes = Replace(bytes, '"' + std::string(key) + "\":\"" + m.shaderSha256[shader] + '"', '"' + std::string(key) + "\":\"" + facts.shaderSha256[shader] + '"');
        ++shader;
    }
    bytes = Replace(bytes, "\"operation_timeout_ms\":1000", "\"operation_timeout_ms\":60000");
    bytes = Replace(bytes, "\"child_timeout_ms\":10000", "\"child_timeout_ms\":120000"); m = c::ParseManifest(Rehash(bytes));
    c::RejectOutputCollisions(m, paths.repositoryRoot);
    LARGE_INTEGER frequency{}, now{}; ASSERT_TRUE(QueryPerformanceFrequency(&frequency)); ASSERT_TRUE(QueryPerformanceCounter(&now));
    const auto result = c::RunSupervisedProcess(COMPUTELAB_EX2_STAGE6_EXE, c::ChildArguments(m, sequence), sequence,
        m.operationTimeoutMs, m.childTimeoutMs, frequency.QuadPart, c::AddDeadline(now.QuadPart, c::DeadlineTicks(240000, frequency.QuadPart)));
    std::cout << "S6-I3 supervised Release smoke slot=" << sequence << " exit=" << result.exitCode.value_or(999)
        << " job_total=" << result.jobTotalProcesses.value_or(0) << " job_active=" << result.jobActiveProcesses.value_or(999) << '\n';
    ASSERT_TRUE(result.processCreated); ASSERT_TRUE(result.containmentAssigned); ASSERT_TRUE(result.containmentVerified); ASSERT_TRUE(result.processResumed);
    ASSERT_TRUE(result.primaryTerminationConfirmed); ASSERT_TRUE(result.jobEmptyConfirmed); ASSERT_EQ(result.jobActiveProcesses, 0);
    ASSERT_GE(result.jobTotalProcesses.value_or(0), 1); ASSERT_FALSE(result.descendantSurvivalObserved); ASSERT_FALSE(result.containmentVerificationFailed);
    ASSERT_FALSE(result.supervisorTerminationRequested); ASSERT_FALSE(result.operationTimedOut); ASSERT_FALSE(result.childTimedOut); ASSERT_FALSE(result.campaignTimedOut);
    ASSERT_EQ(result.exitKind, c::ExitKind::VoluntaryStage6); ASSERT_EQ(result.exitCode, 0); ASSERT_EQ(result.progressForm, p::TerminalForm::Full);
    ASSERT_EQ(result.observations.size(), 200); ASSERT_TRUE(result.cleanEof); ASSERT_EQ(result.trailingBytes, 0); ASSERT_FALSE(result.progressTransportFailed);
    const auto inspection = c::InspectPackage(m, sequence, paths.repositoryRoot, c::InspectionPurpose::DisposableSmoke);
    ASSERT_EQ(inspection.topology, c::Topology::FinalOnly); ASSERT_TRUE(inspection.structurallyValid); ASSERT_TRUE(inspection.packageFinalized);
    ASSERT_TRUE(inspection.scientificBundleParsed); ASSERT_TRUE(inspection.scientificBundleValid); ASSERT_TRUE(inspection.scientificBytesCanonical);
    ASSERT_FALSE(inspection.sidecarPresent); ASSERT_FALSE(inspection.changedDuringInspection); ASSERT_TRUE(inspection.input); ASSERT_EQ(inspection.input->samples.size(), 100);
    for (std::size_t sample = 0; sample < 100; ++sample) EXPECT_EQ(inspection.input->samples[sample].sampleIndex, sample);
    const auto fresh = c::InspectPackage(m, sequence, paths.repositoryRoot, c::InspectionPurpose::DisposableSmoke);
    ASSERT_EQ(fresh.artifacts, inspection.artifacts); ASSERT_EQ(fresh.packageSha256, inspection.packageSha256);
    const auto reconciliation = c::ReconcileSlot(result, inspection); ASSERT_EQ(reconciliation.disposition, a::SlotDisposition::ResolvedSuccess);
    ASSERT_TRUE(reconciliation.process); ASSERT_EQ(reconciliation.process->TerminalState(), a::ProcessTerminalState::Success);
    const auto after = c::CollectPreflightFacts(m, paths); ASSERT_EQ(after.sourceRevision, facts.sourceRevision); ASSERT_EQ(after.gitDirty, facts.gitDirty);
    ASSERT_EQ(after.childSha256, facts.childSha256); ASSERT_EQ(after.shaderSha256, facts.shaderSha256); ASSERT_EQ(after.cudaUuid, facts.cudaUuid); ASSERT_EQ(after.vulkanUuid, facts.vulkanUuid);
    const auto session = m.groups[sequence / 10].children[sequence % 10].sessionId;
    const auto final = (paths.repositoryRoot / "results/local" / session).lexically_normal();
    ASSERT_EQ(final.parent_path(), (paths.repositoryRoot / "results/local").lexically_normal()); ASSERT_TRUE(session.starts_with("s6-i3-smoke-"));
    ASSERT_EQ(std::filesystem::remove_all(final), 5);
    std::cout << "S6-I3 supervised smoke artifacts=4 samples=100 progress=200 seconds="
        << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " cleanup=success-owned-local-session\n";
#endif
}
TEST(Ex2Stage6SupervisorReleaseSmoke, A1Cuda) { Smoke(0); }
TEST(Ex2Stage6SupervisorReleaseSmoke, A1Vulkan) { Smoke(1); }
TEST(Ex2Stage6SupervisorReleaseSmoke, E1Cuda) { Smoke(160); }
TEST(Ex2Stage6SupervisorReleaseSmoke, E1Vulkan) { Smoke(161); }
} // namespace
