#include "ex2/Ex2CorrectnessSupervisor.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#include <process.h>
#include <chrono>
#include <fstream>
#include <iostream>

namespace
{
namespace c = computelab::ex2::correctness::control;
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

TEST(Ex2I7SupervisorSmoke, OneRealA1PairThroughSupervisorAndIndependentInspector)
{
    const std::filesystem::path root(COMPUTELAB_REPOSITORY_ROOT);
    const std::string id = "i7-prompt3-a1-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto local = root / "results/local";
    const auto manifestPath = local / (id + "-manifest.json");
    const auto ledger = local / (id + "-control.json");
    c::Manifest provisional;
    provisional.childExecutablePath = std::filesystem::path(COMPUTELAB_EX2_CORRECTNESS_EXE).lexically_relative(root).generic_string();
    provisional.children.push_back({0U, 0U, id, std::nullopt});
    const c::RuntimePaths paths{root, COMPUTELAB_I7_SUPERVISOR_EXE,
        {COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH,
         COMPUTELAB_EX2_B1_SPIRV_PATH, COMPUTELAB_EX2_B2_SPIRV_PATH,
         COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH}};
    const auto facts = c::CollectPreflightFacts(provisional, paths);
    ASSERT_EQ(facts.sourceRevision, "3642edd1301dded0b44c6fef89643f14352b2722");
    ASSERT_TRUE(facts.gitDirty);
    ASSERT_EQ(facts.cudaUuid, facts.vulkanUuid);
    ASSERT_TRUE(facts.shaderSha256[0]);
    std::string json = "{\"manifest_version\":1,\"manifest_type\":\"i7-correctness\","
        "\"manifest_id\":\"" + id + "\",\"manifest_sha256\":\"HASH\",\"protocol_version\":\"1.1\","
        "\"machine_id\":\"i7-smoke-machine\",\"child_executable_path\":\"" + provisional.childExecutablePath + "\","
        "\"supervisor_record_path\":\"results/local/" + id + "-control.json\","
        "\"expected_source_revision\":\"" + facts.sourceRevision + "\",\"expected_git_dirty\":true,"
        "\"expected_child_executable_sha256\":\"" + facts.childSha256 + "\","
        "\"expected_supervisor_executable_sha256\":\"" + facts.supervisorSha256 + "\","
        "\"expected_gpu_uuid\":\"" + facts.cudaUuid + "\",\"cuda_device_ordinal\":0,"
        "\"vulkan_physical_device_index\":0,\"operation_timeout_ms\":60000,\"child_timeout_ms\":120000,"
        "\"campaign_timeout_ms\":180000,\"continuation_policy\":\"completed-validation-only\","
        "\"declared_child_count\":1,\"children\":[{\"sequence_index\":0,\"core_cell_index\":0,"
        "\"session_id\":\"" + id + "\",\"expected_vulkan_shader_sha256\":\"" + *facts.shaderSha256[0] + "\"}]}";
    const auto hash = c::CalculateManifestSha256(json);
    json.replace(json.find("HASH"), 4U, hash);
    const auto manifest = c::ParseManifest(json);
    c::RejectOutputCollisions(manifest, root);
    ASSERT_FALSE(std::filesystem::exists(manifestPath));
    // Ownership is acquired only after all exact paths are proved absent.
    struct Cleanup
    {
        std::vector<std::filesystem::path> paths;
        ~Cleanup() { for (const auto& p : paths) { std::error_code e; std::filesystem::remove_all(p, e); } }
    } cleanup{{manifestPath, ledger, ledger.string() + ".incomplete", ledger.string() + ".incomplete.tmp",
        local / id, local / (id + ".incomplete"), local / (id + ".failure.json")}};
    { std::ofstream out(manifestPath, std::ios::binary); out << json; ASSERT_TRUE(out.good()); }
    const std::string executable(COMPUTELAB_I7_SUPERVISOR_EXE), path = manifestPath.string();
    const char* args[]{executable.c_str(), "--manifest", path.c_str(), nullptr};
    const auto exit = _spawnv(_P_WAIT, executable.c_str(), args);
    ASSERT_EQ(exit, 0) << (std::filesystem::exists(ledger) ? Read(ledger) : "no final ledger");
    const auto inspection = c::InspectPackage(manifest, manifest.children[0], root, 0U);
    ASSERT_EQ(inspection.state, "complete-pass");
    ASSERT_EQ(inspection.artifacts.size(), 8U);
    const auto record = Read(ledger);
    for (const auto* fact : {"\"overall_execution_status\":\"execution_complete\"",
            "\"cuda_attempt_started\":true", "\"cuda_attempt_returned\":true",
            "\"vulkan_attempt_started\":true", "\"vulkan_attempt_returned\":true",
            "\"progress_validation\":\"complete\"", "\"package_state\":\"complete-pass\""})
        EXPECT_NE(record.find(fact), std::string::npos) << fact;
    EXPECT_FALSE(std::filesystem::exists(local / (id + ".incomplete")));
    EXPECT_FALSE(std::filesystem::exists(local / (id + ".failure.json")));
    EXPECT_FALSE(std::filesystem::exists(ledger.string() + ".incomplete"));
    std::cout << "SMOKE_SESSION=" << id << "\nMANIFEST_SHA256=" << hash
        << "\nCHILD_SHA256=" << facts.childSha256 << "\nSUPERVISOR_SHA256=" << facts.supervisorSha256
        << "\nGPU_UUID=" << facts.cudaUuid << "\nSHADER_SHA256=" << *facts.shaderSha256[0]
        << "\nLEDGER_SHA256=" << computelab::ex2::Sha256File(ledger) << '\n';
    for (const auto& artifact : inspection.artifacts)
        std::cout << artifact.relativePath << " " << artifact.sizeBytes << " " << artifact.sha256 << '\n';
}
} // namespace
