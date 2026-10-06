#include "app/Ex2Stage6Execution.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#include <process.h>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace run = s6::execution;
namespace ev = s6::evidence;
namespace env = computelab::environment;
std::string Read(const std::filesystem::path& p)
{ std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("smoke artifact missing"); return {std::istreambuf_iterator<char>(f), {}}; }
std::string Field(std::string_view json, std::string_view key)
{
    const auto marker = '"' + std::string(key) + "\":"; auto begin = json.find(marker);
    if (begin == json.npos) throw std::runtime_error("smoke JSON field missing"); begin += marker.size();
    const auto end = json[begin] == '"' ? json.find('"', begin + 1) + 1 : json.find_first_of(",}", begin);
    if (end == json.npos || end <= begin) throw std::runtime_error("smoke JSON field malformed");
    return std::string(json.substr(begin, end - begin));
}
std::string Text(std::string_view json, std::string_view key)
{ const auto s = Field(json, key); if (s.size() < 2 || s.front() != '"' || s.back() != '"') throw std::runtime_error("smoke text missing"); return s.substr(1, s.size() - 2); }
std::uint64_t Integer(std::string_view s)
{ std::uint64_t value{}; const auto r = std::from_chars(s.data(), s.data() + s.size(), value); if (s.empty() || r.ec != std::errc{} || r.ptr != s.data() + s.size()) throw std::runtime_error("smoke integer invalid"); return value; }
std::vector<std::string> Csv(std::string_view line)
{
    std::vector<std::string> fields; std::size_t begin{};
    while (true) { const auto end = line.find(',', begin); fields.emplace_back(line.substr(begin, end == line.npos ? end : end - begin)); if (end == line.npos) break; begin = end + 1; }
    return fields;
}
struct Cleanup
{
    run::SessionPaths paths;
    bool owned{};
    ~Cleanup()
    {
        if (!owned) return;
        const auto root = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT).lexically_normal() / "results" / "local";
        std::error_code e;
        for (const auto& p : {paths.finalDirectory, paths.stagingDirectory, paths.failureSidecar})
            if (p.lexically_normal().parent_path() == root && p.filename().string().starts_with("s6-i2-smoke-")) std::filesystem::remove_all(p, e);
    }
};
ev::Foundation ExpectedFoundation(const ev::FoundationRequest& r)
{
    const auto& w = s6::WorkloadForCell(r.slot.cellIndex);
    return std::visit([&](const auto& c) -> ev::Foundation {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, ex2::TransferConfiguration>)
        { const auto d = ex2::ReferenceTransfer(w.common.seed, c.byteCount, c.direction); return ev::MakeByteFoundation(r, ex2::evidence::MakeI7ByteInputIdentity(w, d.source), d.expectedDestination); }
        else if constexpr (std::is_same_v<T, ex2::IndexedConfiguration>)
        { const auto input = ex2::GenerateWordInput(w.common.seed, c.elementCount); const auto indices = c.indexPattern == ex2::IndexPattern::StructuredV1 ? ex2::GenerateStructuredPermutation(c.elementCount) : ex2::GenerateShuffledPermutation(w.common.seed, c.elementCount);
            const auto output = c.variant == ex2::IndexedVariant::B1 ? ex2::ReferenceB1Gather(input, indices) : ex2::ReferenceB2Scatter(input, indices);
            return ev::MakeWordFoundation(r, ex2::evidence::MakeI7IndexedInputIdentity(w, input, indices), output); }
        else if constexpr (std::is_same_v<T, ex2::ContentionConfiguration>)
        { const auto d = ex2::ReferenceContention(c.elementCount, c.activeCounterCount); const auto zeros = ex2::MakeZeroInitialCounterState(c.allocatedCounterCount);
            return ev::MakeWordFoundation(r, ex2::evidence::MakeI7ContentionInputIdentity(w, d.targets, zeros), d.counters); }
        else { const auto input = ex2::GenerateWordInput(w.common.seed, c.elementCount); std::vector<std::uint32_t> output;
            if constexpr (std::is_same_v<T, ex2::LinearConfiguration>) output = c.variant == ex2::LinearVariant::A1 ? ex2::ReferenceA1(input) : ex2::ReferenceA2(input);
            else output = ex2::ReferenceD1(input, c.iterationCount).finalState;
            return ev::MakeWordFoundation(r, ex2::evidence::MakeI7WordInputIdentity(w, input), output); }
    }, w.parameters);
}
void Smoke(std::size_t cell, std::size_t plan)
{
    const auto started = std::chrono::steady_clock::now(); const bool cuda = plan == 0;
    const auto expectedUuid = cuda ? run::FormatUuid(env::EnumerateCudaDeviceMetadata().at(0).uuid) : run::FormatUuid(env::EnumerateVulkanDeviceMetadata().at(0).uuid);
    const std::string session = "s6-i2-smoke-" + std::to_string(_getpid()) + "-" + std::to_string(cell) + "-" + std::to_string(plan) + "-"
        + std::to_string(started.time_since_epoch().count());
    run::Configuration c{cell, plan, 0, 0, "s6-i2-smoke-machine", session, expectedUuid};
    const run::RuntimePaths paths{COMPUTELAB_REPOSITORY_ROOT, COMPUTELAB_EX2_STAGE6_EXE,
        COMPUTELAB_EX2_A1_SPIRV_PATH, COMPUTELAB_EX2_A2_SPIRV_PATH, COMPUTELAB_EX2_B1_SPIRV_PATH, COMPUTELAB_EX2_B2_SPIRV_PATH,
        COMPUTELAB_EX2_C_SPIRV_PATH, COMPUTELAB_EX2_D1_SPIRV_PATH};
    const auto slot = run::ResolveSlot(c); const auto provenance = run::ResolveProvenance(paths, slot);
    Cleanup cleanup{run::MakeSessionPaths(paths, session)}; run::RequireUnusedSession(cleanup.paths); cleanup.owned = true;
    std::vector<std::string> args{COMPUTELAB_EX2_STAGE6_EXE, "--cell-index", std::to_string(cell), "--plan-index", std::to_string(plan),
        "--cuda-device", "0", "--vulkan-device", "0", "--machine-id", c.machineId, "--session-id", session, "--expected-gpu-uuid", expectedUuid};
    std::vector<const char*> argv; for (const auto& a : args) argv.push_back(a.c_str()); argv.push_back(nullptr);
    const auto exit = _spawnv(_P_WAIT, argv.front(), argv.data());
    std::cout << "S6-I2 smoke cell=" << cell << " plan=" << plan << " backend=" << (cuda ? "cuda" : "vulkan") << " exit=" << exit << " session=" << session << '\n';
    ASSERT_EQ(exit, 0); ASSERT_TRUE(std::filesystem::is_directory(cleanup.paths.finalDirectory));
    EXPECT_FALSE(std::filesystem::exists(cleanup.paths.stagingDirectory)); EXPECT_FALSE(std::filesystem::exists(cleanup.paths.failureSidecar));
    std::set<std::string> names; for (const auto& file : std::filesystem::directory_iterator(cleanup.paths.finalDirectory)) { ASSERT_TRUE(file.is_regular_file()); ASSERT_FALSE(file.is_symlink()); names.insert(file.path().filename().string()); }
    EXPECT_EQ(names, (std::set<std::string>{"environment.json", "initialization.csv", "samples.csv", "summary.json"}));
    const auto f = ExpectedFoundation({slot, session, c.machineId, {expectedUuid, true}, provenance.sourceRevision, provenance.executableSha256, provenance.shaderSha256});
    const auto environment = Read(cleanup.paths.finalDirectory / "environment.json");
    EXPECT_EQ(Text(environment, "run_id"), session); EXPECT_EQ(Text(environment, "machine_id"), c.machineId);
    EXPECT_EQ(Text(environment, "protocol_version"), "1.3"); EXPECT_EQ(Text(environment, "evidence_kind"), "diagnostic");
    EXPECT_EQ(Text(environment, "backend"), cuda ? "cuda" : "vulkan"); EXPECT_EQ(Text(environment, "instrument_mode"), "H");
    const std::string route = cell < 3 ? "a1" : cell < 5 ? "a2" : cell < 8 ? "b1" : cell < 11 ? "b2" : cell < 14 ? "c" : cell < 16 ? "d1" : cell < 19 ? "e1" : "e2";
    EXPECT_EQ(Text(environment, "implementation"), "ex2-" + std::string(cuda ? "cuda" : "vulkan") + "-" + route + "-native");
    EXPECT_EQ(Field(environment, "stream_flags"), cuda ? "\"nonblocking\"" : "null");
    const char* layers = std::getenv("VK_INSTANCE_LAYERS");
    const bool externalValidation = layers && std::string_view(layers).find("VK_LAYER_KHRONOS_validation") != std::string_view::npos;
    const bool routeValidation = cell >= 14 && std::getenv(cell < 16 ? "COMPUTELAB_EX2_D1_VALIDATION" : "COMPUTELAB_EX2_E_VALIDATION");
    EXPECT_EQ(Field(environment, "validation_enabled"), !cuda && (externalValidation || routeValidation) ? "true" : "false");
    EXPECT_EQ(Field(environment, "diagnostic_instrumentation"), "false");
    EXPECT_EQ(Field(environment, "warmup_count"), "0"); EXPECT_EQ(Field(environment, "planned_sample_count"), "100");
    EXPECT_EQ(Text(environment, "source_revision"), provenance.sourceRevision); EXPECT_EQ(Text(environment, "git_commit"), provenance.sourceRevision);
    EXPECT_EQ(Field(environment, "git_dirty"), provenance.dirty ? "true" : "false"); EXPECT_EQ(Text(environment, "executable_sha256"), ex2::Sha256File(paths.executable));
    EXPECT_EQ(Field(environment, "shader_sha256"), ev::detail::Json(provenance.shaderSha256)); EXPECT_EQ(Text(environment, "gpu_uuid_identity"), expectedUuid);
    EXPECT_EQ(Text(environment, "comparison_condition_id"), f.Identity().comparisonConditionId); EXPECT_EQ(Text(environment, "series_id"), f.Identity().seriesId);
    EXPECT_EQ(Text(environment, "input_sha256"), f.InputSha256()); EXPECT_EQ(Text(environment, "expected_output_sha256"), f.ExpectedOutputSha256());
    EXPECT_EQ(Integer(Field(environment, "cell_index")), cell); EXPECT_EQ(Integer(Field(environment, "slot_sequence_index")), cell * 10 + plan);
    for (const std::string key : {"timestamp_utc", "os_name", "os_version", "cpu_name", "gpu_name", "gpu_vendor", "gpu_device_id", "nvidia_driver_version", "compiler_name", "compiler_version", "cmake_version", "ninja_version", "configure_preset", "build_type"}) EXPECT_FALSE(Text(environment, key).empty()) << key;
    for (const std::string key : {"timestamp_valid_bits", "timestamp_period_ns", "native_timing_method", "native_timing_resolution_ns", "native_timing_start_stage", "native_timing_stop_stage", "native_duration_envelope_ns"}) EXPECT_EQ(Field(environment, key), "null") << key;
    EXPECT_EQ(Field(environment, "native_markers_enabled"), "false");
    const auto init = Read(cleanup.paths.finalDirectory / "initialization.csv"); EXPECT_EQ(std::count(init.begin(), init.end(), '\n'), 2); EXPECT_NE(init.find(",setup_complete,,"), std::string::npos);
    std::istringstream csv(Read(cleanup.paths.finalDirectory / "samples.csv")); std::string line; ASSERT_TRUE(static_cast<bool>(std::getline(csv, line))); if (line.ends_with('\r')) line.pop_back(); EXPECT_EQ(line, ev::SamplesCsvHeader());
    std::vector<ev::SampleRecord> rows;
    while (std::getline(csv, line))
    {
        ASSERT_TRUE(line.ends_with('\r')); line.pop_back(); const auto values = Csv(line); ASSERT_EQ(values.size(), 31);
        const auto index = rows.size(); ASSERT_LT(index, 100); EXPECT_EQ(Integer(values[22]), index);
        EXPECT_EQ(values[23], "true"); EXPECT_EQ(values[24], "ok"); EXPECT_TRUE(values[25].empty()); EXPECT_TRUE(values[26].empty()); EXPECT_TRUE(values[30].empty());
        const auto submission = Integer(values[27]), wait = Integer(values[28]), completion = Integer(values[29]);
        auto row = ev::MakeSampleRecord(f, index); row.correctness = {true, true, true, true, true}; row.status = ev::Status::Ok;
        row.hostSubmissionNanoseconds = submission; row.hostWaitNanoseconds = wait; row.hostCompletionNanoseconds = completion;
        ev::ValidateSampleRecord(row); EXPECT_EQ(ev::SerializeSampleRow(row), line + "\r\n"); rows.push_back(std::move(row));
    }
    ASSERT_EQ(rows.size(), 100); const auto summary = ev::SummarizeSamples(f.Identity(), rows, ev::Status::Ok);
    const auto diskSummary = Read(cleanup.paths.finalDirectory / "summary.json"); EXPECT_EQ(diskSummary, ev::SerializeSummaryJson(summary, rows));
    EXPECT_EQ(summary.recordedSampleCount, 100); EXPECT_EQ(summary.successfulSampleCount, 100); EXPECT_EQ(summary.validationFailures, 0); EXPECT_EQ(summary.failedSampleCount, 0);
    EXPECT_EQ(summary.hostSubmissionNanoseconds.sampleCount, 100); EXPECT_EQ(summary.hostWaitNanoseconds.sampleCount, 100); EXPECT_EQ(summary.hostCompletionNanoseconds.sampleCount, 100); EXPECT_FALSE(summary.nativeDeviceIntervalNanoseconds);
    run::RequireSameProvenance(provenance, run::ResolveProvenance(paths, slot));
    std::cout << "S6-I2 smoke artifacts=4 rows=" << rows.size() << " successful=" << summary.successfulSampleCount << " seconds="
        << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " cleanup=owned-session\n";
}
#ifndef NDEBUG
#define CASE(NAME, CELL, PLAN) TEST(Ex2Stage6DebugSmoke, NAME) { Smoke(CELL, PLAN); }
CASE(A1Cuda, 0, 0) CASE(A1Vulkan, 0, 1)
CASE(A2Cuda, 3, 0) CASE(A2Vulkan, 3, 1)
CASE(B1Cuda, 5, 0) CASE(B1Vulkan, 5, 1)
CASE(B2Cuda, 8, 0) CASE(B2Vulkan, 8, 1)
CASE(CCuda, 11, 0) CASE(CVulkan, 11, 1)
CASE(D1Cuda, 14, 0) CASE(D1Vulkan, 14, 1)
CASE(E1Cuda, 16, 0) CASE(E1Vulkan, 16, 1)
CASE(E2Cuda, 19, 0) CASE(E2Vulkan, 19, 1)
#else
#define CASE(NAME, CELL, PLAN) TEST(Ex2Stage6ReleaseSmoke, NAME) { Smoke(CELL, PLAN); }
CASE(A1Cuda, 0, 0) CASE(A1Vulkan, 0, 1)
CASE(E1Cuda, 16, 0) CASE(E1Vulkan, 16, 1)
#endif
#undef CASE
} // namespace
