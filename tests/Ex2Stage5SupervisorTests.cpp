#include "ex2/Ex2Stage5Supervisor.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"
#include "app/Ex2Stage5Execution.hpp"
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#include <cstring>
#include <limits>
#include <gtest/gtest.h>
#include <atomic>
#include <fstream>
#include <locale>

namespace
{
namespace s5 = computelab::ex2::stage5;
namespace c = s5::control;
namespace ev = s5::evidence;
namespace core = computelab::ex2;
using Phase = s5::Stage5Phase;
// Independent Python json(sort_keys=True)+hashlib fixture, not the implementation hasher.
constexpr std::string_view GoldenHash = "e17bb7fdf1cfa06d3cb4fdb6528601a68a1e8ac9fa51822a8adc972d987aa7f0";
std::string Golden() { return R"json({"campaign_timeout_ms":30000,"child_executable_path":"out/build/x64-debug/src/app/ComputeLabEx2Stage5.exe","child_timeout_ms":800,"cuda_device_ordinal":0,"declared_max_child_count":30,"evidence_kind":"qualification","expected_a1_vulkan_shader_sha256":"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd","expected_child_executable_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","expected_d1_vulkan_shader_sha256":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee","expected_git_dirty":false,"expected_gpu_uuid":"00112233-4455-6677-8899-aabbccddeeff","expected_source_revision":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","expected_supervisor_executable_sha256":"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc","groups":[{"children":[{"plan_index":0,"sequence_index":0,"session_id":"synthetic-g0-p0"},{"plan_index":1,"sequence_index":1,"session_id":"synthetic-g0-p1"},{"plan_index":2,"sequence_index":2,"session_id":"synthetic-g0-p2"},{"plan_index":3,"sequence_index":3,"session_id":"synthetic-g0-p3"},{"plan_index":4,"sequence_index":4,"session_id":"synthetic-g0-p4"},{"plan_index":5,"sequence_index":5,"session_id":"synthetic-g0-p5"},{"plan_index":6,"sequence_index":6,"session_id":"synthetic-g0-p6"},{"plan_index":7,"sequence_index":7,"session_id":"synthetic-g0-p7"},{"plan_index":8,"sequence_index":8,"session_id":"synthetic-g0-p8"},{"plan_index":9,"sequence_index":9,"session_id":"synthetic-g0-p9"}],"declared_child_count":10,"group_index":0,"launch_rule":"after-campaign-preflight","phase":"a1-sentinel","selected_w_policy":"none"},{"children":[{"plan_index":0,"sequence_index":10,"session_id":"synthetic-g1-p0"},{"plan_index":1,"sequence_index":11,"session_id":"synthetic-g1-p1"},{"plan_index":2,"sequence_index":12,"session_id":"synthetic-g1-p2"},{"plan_index":3,"sequence_index":13,"session_id":"synthetic-g1-p3"},{"plan_index":4,"sequence_index":14,"session_id":"synthetic-g1-p4"},{"plan_index":5,"sequence_index":15,"session_id":"synthetic-g1-p5"},{"plan_index":6,"sequence_index":16,"session_id":"synthetic-g1-p6"},{"plan_index":7,"sequence_index":17,"session_id":"synthetic-g1-p7"},{"plan_index":8,"sequence_index":18,"session_id":"synthetic-g1-p8"},{"plan_index":9,"sequence_index":19,"session_id":"synthetic-g1-p9"}],"declared_child_count":10,"group_index":1,"launch_rule":"after-a1-integrity-complete","phase":"d1-warmup","selected_w_policy":"none"},{"children":[{"plan_index":0,"sequence_index":20,"session_id":"synthetic-g2-p0"},{"plan_index":1,"sequence_index":21,"session_id":"synthetic-g2-p1"},{"plan_index":2,"sequence_index":22,"session_id":"synthetic-g2-p2"},{"plan_index":3,"sequence_index":23,"session_id":"synthetic-g2-p3"},{"plan_index":4,"sequence_index":24,"session_id":"synthetic-g2-p4"},{"plan_index":5,"sequence_index":25,"session_id":"synthetic-g2-p5"},{"plan_index":6,"sequence_index":26,"session_id":"synthetic-g2-p6"},{"plan_index":7,"sequence_index":27,"session_id":"synthetic-g2-p7"},{"plan_index":8,"sequence_index":28,"session_id":"synthetic-g2-p8"},{"plan_index":9,"sequence_index":29,"session_id":"synthetic-g2-p9"}],"declared_child_count":10,"group_index":2,"launch_rule":"after-d1-warmup-qualified","phase":"d1-sample","selected_w_policy":"max-qualified-d1-warmup-w"}],"machine_id":"anonymous-machine","manifest_id":"synthetic-campaign","manifest_sha256":"e17bb7fdf1cfa06d3cb4fdb6528601a68a1e8ac9fa51822a8adc972d987aa7f0","manifest_type":"ex2-stage5-qualification","manifest_version":1,"operation_timeout_ms":200,"protocol_version":"1.2","vulkan_physical_device_index":0})json"; }
std::string Replace(std::string text, std::string_view from, std::string_view to)
{ const auto pos = text.find(from); if (pos == text.npos) throw std::logic_error("fixture token missing"); text.replace(pos, from.size(), to); return text; }
std::string Sign(std::string text)
{
    const auto key = text.find("\"manifest_sha256\":\"") + 19U;
    text.replace(key, 64U, c::CalculateManifestSha256(text)); return text;
}
void Write(const std::filesystem::path& path, std::string_view bytes)
{ std::filesystem::create_directories(path.parent_path()); std::ofstream out(path, std::ios::binary); out << bytes; if (!out) throw std::runtime_error("fixture write"); }
std::string Read(const std::filesystem::path& path)
{ std::ifstream in(path, std::ios::binary); if (!in) throw std::runtime_error("fixture read"); return {std::istreambuf_iterator<char>(in), {}}; }
struct Temp
{
    std::filesystem::path root;
    Temp()
    {
        static std::atomic<unsigned> n{};
        root = std::filesystem::temp_directory_path() / ("computelab-s5-control-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(n++));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test root collision");
    }
    ~Temp()
    {
        // Cleanup only this independently created direct child of TEMP.
        if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("computelab-s5-control-"))
        { std::error_code e; std::filesystem::remove_all(root, e); }
    }
};
// A test-owned mount-point junction needs no symbolic-link privilege. Unlink it
// nonrecursively before Temp cleanup so the external fixture is never traversed.
struct TestJunction
{
    Temp& owner;
    std::filesystem::path link;
    TestJunction(Temp& t, std::string_view name, const std::filesystem::path& target) : owner(t), link(t.root / name)
    {
        const auto print = std::filesystem::absolute(target).native();
        const auto substitute = L"\\??\\" + print;
        struct Header { DWORD tag; WORD length, reserved, substituteOffset, substituteLength, printOffset, printLength; };
        static_assert(sizeof(Header) == 16U);
        const auto subBytes = substitute.size() * sizeof(wchar_t), printBytes = print.size() * sizeof(wchar_t);
        const auto total = sizeof(Header) + subBytes + printBytes + 2U * sizeof(wchar_t);
        if (total - 8U > std::numeric_limits<WORD>::max()) throw std::runtime_error("junction fixture too large");
        const Header header{IO_REPARSE_TAG_MOUNT_POINT, static_cast<WORD>(total - 8U), 0, 0,
            static_cast<WORD>(subBytes), static_cast<WORD>(subBytes + sizeof(wchar_t)), static_cast<WORD>(printBytes)};
        std::vector<unsigned char> bytes(total);
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + sizeof(header), substitute.c_str(), subBytes + sizeof(wchar_t));
        std::memcpy(bytes.data() + sizeof(header) + subBytes + sizeof(wchar_t), print.c_str(), printBytes + sizeof(wchar_t));
        if (!CreateDirectoryW(link.c_str(), nullptr)) throw std::runtime_error("junction fixture directory creation failed");
        const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        DWORD returned{};
        const bool made = handle != INVALID_HANDLE_VALUE && DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT,
            bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, 0, &returned, nullptr);
        const auto error = GetLastError();
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!made)
        {
            if (!RemoveDirectoryW(link.c_str())) owner.root.clear();
            throw std::runtime_error("junction fixture failed: " + std::to_string(error));
        }
    }
    ~TestJunction()
    {
        if (!RemoveDirectoryW(link.c_str()))
        { ADD_FAILURE() << "junction unlink failed; preserving test root"; owner.root.clear(); }
    }
};
c::PreflightFacts Facts(const c::Manifest& m)
{ return {m.sourceRevision, "main", false, m.childExecutableSha256, m.supervisorExecutableSha256, m.gpuUuid, m.gpuUuid, m.a1ShaderSha256, m.d1ShaderSha256}; }
c::RuntimePaths Paths(const Temp& t) { return {t.root, t.root / "supervisor.exe", t.root / "a1.spv", t.root / "d1.spv"}; }
void Package(const c::Manifest& m, Phase phase, const c::ManifestChild& child, const std::filesystem::path& root,
    std::optional<std::uint64_t> w = {}, std::function<std::uint64_t(std::size_t)> duration = {}, std::optional<std::uint64_t> delta = 1U)
{
    const auto condition = c::FrozenCondition(phase, w); const auto process = s5::FrozenProcessPlan()[child.planIndex];
    const auto input = core::GenerateWordInput(core::CoreInputSeed, condition.elementCount);
    static const auto a1 = core::ReferenceA1(core::GenerateWordInput(core::CoreInputSeed, 256));
    static const auto d1 = core::ReferenceD1(core::CoreInputSeed, 1048576, 64).finalState;
    const auto f = ev::MakeWordFoundation({condition, process, child.sessionId, m.machineId, {m.gpuUuid, true}, m.sourceRevision,
        m.childExecutableSha256, process.backend == s5::Stage5Backend::Cuda ? std::nullopt : std::optional{phase == Phase::A1Sentinel ? m.a1ShaderSha256 : m.d1ShaderSha256}}, input, phase == Phase::A1Sentinel ? a1 : d1);
    computelab::results::EnvironmentRecord common{2, "EX-2", child.sessionId, "2026-10-02T00:00:00Z", m.sourceRevision, false, m.machineId,
        "Windows", "11", "Synthetic CPU", 16, "Synthetic GPU", "NVIDIA", "10DE:1234", 8, "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", false, false};
    ev::BackendDiagnostics diag;
    if (process.backend == s5::Stage5Backend::Cuda) { diag.implementation = phase == Phase::A1Sentinel ? "ex2-cuda-a1-native" : "ex2-cuda-d1-native"; diag.streamFlags = "nonblocking"; }
    else { diag.implementation = phase == Phase::A1Sentinel ? "ex2-vulkan-a1-native" : "ex2-vulkan-d1-native"; diag.queueFamilyIndex = 0; diag.queueFlags = 2; diag.queueCount = 1; }
    const auto env = ev::MakeEnvironmentRecord(common, f, diag);
    std::vector<ev::InitializationRecord> init{ev::MakeSetupCompleteInitialization(f, "synthetic setup, quoted \"diagnostic\"")};
    std::vector<ev::HostClockRecord> clock; std::vector<ev::WarmupRecord> warm; std::vector<ev::SampleRecord> samples;
    for (unsigned i = 0; i < 4095U; ++i) clock.push_back(ev::MakeHostClockRecord(f, i, i % 3U == 0U && delta ? std::optional<std::uint64_t>{0U} : delta));
    for (std::uint64_t i = 0; i < condition.diagnosticCount + condition.measuredSampleCount; ++i)
    {
        ev::OperationRecord row; row.plan = f.Identity(); row.sequenceIndex = i; row.correctness = {true, true, true, true, true}; row.status = ev::Status::Ok;
        row.hostSubmissionNanoseconds = 100U; row.hostCompletionNanoseconds = duration ? duration(static_cast<std::size_t>(i)) : 1000U;
        row.hostWaitNanoseconds = *row.hostCompletionNanoseconds - 100U;
        if (phase == Phase::D1Sample) { ev::SampleRecord r; static_cast<ev::OperationRecord&>(r) = row; samples.push_back(r); }
        else { ev::WarmupRecord r; static_cast<ev::OperationRecord&>(r) = row; warm.push_back(r); }
    }
    const auto summary = ev::SummarizeSamples(f, samples, ev::Status::Ok);
    const auto path = root / "results/local" / child.sessionId;
    Write(path / "environment.json", ev::SerializeEnvironmentJson(f, env)); Write(path / "initialization.csv", ev::SerializeInitializationCsv(f, init));
    Write(path / "host-clock.csv", ev::SerializeHostClockCsv(f, clock)); Write(path / "warmup.csv", ev::SerializeWarmupCsv(f, warm));
    Write(path / "samples.csv", ev::SerializeSamplesCsv(f, samples)); Write(path / "summary.json", ev::SerializeSummaryJson(f, summary, samples));
}
c::ProcessResult Complete(c::ProgressExpectation e)
{ c::ProcessResult r; r.launchTimeUtc = "2026-10-02T00:00:00Z"; r.exitTimeUtc = "2026-10-02T00:00:01Z"; r.processId = 123U; r.exitCode = 0U; r.progressValidation = "complete"; r.eventCount = c::ProgressState(e).ExpectedEventCount(); return r; }
struct CampaignFixture
{
    // Full CPU-oracle reconstruction is intentionally outside operation timing.
    // Give these control fixtures a remote guard; dedicated deadline tests stay short.
    Temp temp; c::Manifest manifest{c::ParseManifest(Sign(Replace(Golden(), "\"campaign_timeout_ms\":30000", "\"campaign_timeout_ms\":300000")))};
    unsigned launched{}, updates{}, preparations{};
    std::vector<c::ProgressExpectation> schedule;
    std::function<void(Phase, const c::ManifestChild&, std::optional<std::uint64_t>)> writePackage;
    std::function<void(const c::ExecutionRecord&)> onUpdate;
    c::RuntimeServices Services()
    {
        return {[&](const c::Manifest& m, const c::RuntimePaths&) { return Facts(m); },
            [&](const std::filesystem::path&, const std::vector<std::string>& args, const c::ProgressExpectation& e,
                std::chrono::milliseconds, std::chrono::milliseconds, std::chrono::steady_clock::time_point) {
                ++launched; schedule.push_back(e); preparations += static_cast<unsigned>(e.selectedW.value_or(0));
                const auto& child = manifest.groups[static_cast<std::size_t>(e.phase)].children[e.planIndex];
                EXPECT_EQ(args, c::ChildArguments(manifest, e.phase, child, e.selectedW));
                if (writePackage) writePackage(e.phase, child, e.selectedW); else Package(manifest, e.phase, child, temp.root, e.selectedW);
                return Complete(e);
            }, [&](const c::ExecutionRecord& r) {
                ++updates; const auto path = std::filesystem::path(c::ControlPath(manifest, temp.root).wstring() + L".incomplete");
                EXPECT_EQ(Read(path), c::SerializeExecutionRecord(r)); if (onUpdate) onUpdate(r);
            }};
    }
    c::ExitCode Run() { return c::ExecuteManifestWithServices(manifest, Paths(temp), Services()); }
};

TEST(Ex2Stage5SupervisorManifestLoading, ConfinedRegularFileUsesExistingGoldenParser)
{
    Temp t; Write(t.root / "manifests/control.json", Golden());
    const auto m = c::LoadManifestFromRepository(t.root, "manifests/control.json");
    EXPECT_EQ(m.sha256, GoldenHash); EXPECT_EQ(m.canonicalJson, c::ParseManifest(Golden()).canonicalJson);
}
TEST(Ex2Stage5SupervisorManifestLoading, MissingFileRejected)
{ Temp t; EXPECT_THROW(c::LoadManifestFromRepository(t.root, "missing.json"), std::invalid_argument); }
TEST(Ex2Stage5SupervisorManifestLoading, NonregularDirectoryRejected)
{
    Temp t; std::filesystem::create_directory(t.root / "directory.json");
    EXPECT_THROW(c::LoadManifestFromRepository(t.root, "directory.json"), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorManifestLoading, OversizedFileRejected)
{
    Temp t; auto bytes = Golden(); bytes.resize(1024U * 1024U + 1U, ' '); Write(t.root / "control.json", bytes);
    EXPECT_THROW(c::LoadManifestFromRepository(t.root, "control.json"), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorManifestLoading, ExactOneMiBBoundAccepted)
{
    Temp t; auto bytes = Golden(); bytes.resize(1024U * 1024U, ' '); Write(t.root / "control.json", bytes);
    EXPECT_EQ(c::LoadManifestFromRepository(t.root, "control.json").sha256, GoldenHash);
}
TEST(Ex2Stage5SupervisorManifestLoading, UnsafeLexicalPathsRejectedWithoutNormalization)
{
    Temp t;
    for (const auto path : {"../control.json", "folder/../control.json", "folder\\control.json", "folder//control.json", "control.data", "C:/control.json", "/control.json"})
    { SCOPED_TRACE(path); EXPECT_THROW(c::LoadManifestFromRepository(t.root, path), std::invalid_argument); }
}
TEST(Ex2Stage5SupervisorManifestLoading, AncestorJunctionRejectedBeforeValidExternalBytes)
{
    Temp repository, external; Write(external.root / "control.json", Golden());
    TestJunction junction(repository, "redirect", external.root);
    ASSERT_NE(GetFileAttributesW(junction.link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT, 0U);
    EXPECT_EQ(c::ParseManifest(Read(external.root / "control.json")).sha256, GoldenHash);
    try { static_cast<void>(c::LoadManifestFromRepository(repository.root, "redirect/control.json")); FAIL() << "junction accepted"; }
    catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "reparse path is not supported"); }
    EXPECT_EQ(Read(external.root / "control.json"), Golden());
}
TEST(Ex2Stage5SupervisorManifestLoading, FinalObjectJunctionRejectedBeforeFileTypeOrRead)
{
    Temp repository, external; TestJunction junction(repository, "control.json", external.root);
    try { static_cast<void>(c::LoadManifestFromRepository(repository.root, "control.json")); FAIL() << "junction accepted"; }
    catch (const std::invalid_argument& e) { EXPECT_STREQ(e.what(), "reparse path is not supported"); }
    EXPECT_TRUE(std::filesystem::is_empty(external.root));
}
TEST(Ex2Stage5SupervisorManifestLoading, ConfinedBytesStillRejectHashAndSchemaDrift)
{
    Temp t; const auto path = t.root / "control.json";
    Write(path, Replace(Golden(), "anonymous-machine", "changed-machine"));
    EXPECT_THROW(c::LoadManifestFromRepository(t.root, "control.json"), std::invalid_argument);
    Write(path, Sign(Replace(Golden(), "\"manifest_version\":1", "\"manifest_version\":1,\"unknown\":true")));
    EXPECT_THROW(c::LoadManifestFromRepository(t.root, "control.json"), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorManifest, IndependentGoldenHashAndCanonicalWhitespace)
{
    EXPECT_EQ(c::CalculateManifestSha256(Golden()), GoldenHash); const auto m = c::ParseManifest(" \n" + Golden() + "\t\r\n");
    EXPECT_EQ(m.sha256, GoldenHash); EXPECT_EQ(m.groups.size(), 3U);
    for (unsigned g = 0; g < 3U; ++g) for (unsigned p = 0; p < 10U; ++p)
    { EXPECT_EQ(m.groups[g].children[p].sequenceIndex, g * 10U + p); EXPECT_EQ(m.groups[g].children[p].planIndex, p); }
    EXPECT_EQ(c::ParseManifest(m.canonicalJson).canonicalJson, m.canonicalJson);
}
struct Drift { const char* from; const char* to; };
void PrintTo(const Drift& d, std::ostream* out) { *out << d.from << " -> " << d.to; }
class Stage5ControlManifestDrift : public testing::TestWithParam<Drift> {};
TEST_P(Stage5ControlManifestDrift, RejectsResignedContractDrift)
{ const auto d = GetParam(); const auto text = Sign(Replace(Golden(), d.from, d.to)); EXPECT_THROW(c::ParseManifest(text), std::invalid_argument); }
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlManifestDrift, testing::Values(
    Drift{"\"manifest_version\":1", "\"manifest_version\":2"}, Drift{"ex2-stage5-qualification", "other-type"},
    Drift{"\"protocol_version\":\"1.2\"", "\"protocol_version\":\"1.1\""}, Drift{"\"evidence_kind\":\"qualification\"", "\"evidence_kind\":\"candidate-performance\""},
    Drift{"\"expected_git_dirty\":false", "\"expected_git_dirty\":true"}, Drift{"\"declared_max_child_count\":30", "\"declared_max_child_count\":20"},
    Drift{"\"manifest_id\":\"synthetic-campaign\"", "\"manifest_id\":\"CON\""}, Drift{"anonymous-machine", "../escape"},
    Drift{"out/build/x64-debug/src/app/ComputeLabEx2Stage5.exe", "../ComputeLabEx2Stage5.exe"}, Drift{"out/build/x64-debug/src/app/ComputeLabEx2Stage5.exe", "D:/ComputeLabEx2Stage5.exe"},
    Drift{"ComputeLabEx2Stage5.exe", "wrong.exe"}, Drift{"\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":-1"},
    Drift{"\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":2147483648"}, Drift{"\"vulkan_physical_device_index\":0", "\"vulkan_physical_device_index\":4294967296"},
    Drift{"\"operation_timeout_ms\":200", "\"operation_timeout_ms\":0"}, Drift{"\"operation_timeout_ms\":200", "\"operation_timeout_ms\":60001"},
    Drift{"\"child_timeout_ms\":800", "\"child_timeout_ms\":199"}, Drift{"\"child_timeout_ms\":800", "\"child_timeout_ms\":1200001"},
    Drift{"\"campaign_timeout_ms\":30000", "\"campaign_timeout_ms\":799"}, Drift{"\"campaign_timeout_ms\":30000", "\"campaign_timeout_ms\":86400001"},
    Drift{"\"plan_index\":0", "\"plan_index\":1"}, Drift{"\"sequence_index\":0", "\"sequence_index\":1"},
    Drift{"\"declared_child_count\":10", "\"declared_child_count\":9"}, Drift{"\"group_index\":0", "\"group_index\":1"},
    Drift{"after-a1-integrity-complete", "always"}, Drift{"after-d1-warmup-qualified", "always"},
    Drift{"max-qualified-d1-warmup-w", "2"}, Drift{"a1-sentinel", "d1-warmup"},
    Drift{"synthetic-g0-p1", "SYNTHETIC-g0-p0"}, Drift{"synthetic-g0-p0", "synthetic-campaign-stage5-analysis"},
    Drift{"\"operation_timeout_ms\":200", "\"operation_timeout_ms\":200.0"}, Drift{"\"operation_timeout_ms\":200", "\"operation_timeout_ms\":\"200\""},
    Drift{"\"manifest_version\":1", "\"manifest_version\":true"},
    Drift{"\"manifest_version\":1", "\"manifest_version\":1,\"selected_w\":2"}, Drift{"\"plan_index\":0", "\"plan_index\":0,\"backend\":\"cuda\""},
    Drift{"\"manifest_version\":1", "\"manifest_version\":1,\"historical_package_exception\":true"},
    Drift{"\"manifest_version\":1", "\"manifest_version\":1,\"sample_count\":100"}, Drift{"\"manifest_version\":1", "\"manifest_version\":1,\"tolerance\":0.05"}));
TEST(Ex2Stage5SupervisorManifest, RejectsDuplicateMissingMalformedAndUnsignedHash)
{
    EXPECT_THROW(c::ParseManifest(Replace(Golden(), "\"manifest_version\":1", "\"manifest_version\":1,\"manifest_version\":1")), std::invalid_argument);
    EXPECT_THROW(c::ParseManifest(Replace(Golden(), "\"manifest_version\":1,", "")), std::invalid_argument);
    EXPECT_THROW(c::ParseManifest(Golden().substr(0, Golden().size() - 1)), std::invalid_argument);
    EXPECT_THROW(c::ParseManifest(Replace(Golden(), "\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":1")), std::invalid_argument);
    EXPECT_THROW(c::ParseManifest("[]"), std::invalid_argument);
    EXPECT_THROW(c::ParseManifest(Replace(Golden(), "\"manifest_version\":1", "\"manifest_version\":18446744073709551616")), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorManifest, ExactPublicArgumentsAndNoScientificOptions)
{
    const std::array<std::string_view, 2> args{"--manifest", "results/local/synthetic.json"}; EXPECT_EQ(c::ParseSupervisorArguments(args), "results/local/synthetic.json");
    for (const auto& name : {"--selected-w", "--resume", "--phase", "--backend", "--print-manifest-hash"})
    { const std::array<std::string_view, 2> a{name, "x.json"}; EXPECT_THROW(c::ParseSupervisorArguments(a), std::invalid_argument); }
    for (const auto& path : {"../x.json", "D:/x.json", "results\\x.json", "results/./x.json", "results/x.txt"})
    { const std::array<std::string_view, 2> a{"--manifest", path}; EXPECT_THROW(c::ParseSupervisorArguments(a), std::invalid_argument); }
    EXPECT_THROW(c::FrozenCondition(Phase::D1Sample, {}), std::invalid_argument);
    EXPECT_THROW(c::FrozenCondition(Phase::D1Sample, 32), std::invalid_argument);
    EXPECT_THROW(c::FrozenCondition(Phase::A1Sentinel, 2), std::invalid_argument);
}
class Stage5ControlFactsDrift : public testing::TestWithParam<unsigned> {};
TEST_P(Stage5ControlFactsDrift, FailsBeforeChildZero)
{
    CampaignFixture f; auto s = f.Services(); auto facts = Facts(f.manifest);
    switch (GetParam()) { case 0: facts.sourceRevision[0] = 'f'; break; case 1: facts.gitDirty = true; break; case 2: facts.branch.clear(); break;
        case 3: facts.childSha256[0] = 'f'; break; case 4: facts.supervisorSha256[0] = 'f'; break; case 5: facts.cudaUuid[0] = 'f'; break;
        case 6: facts.vulkanUuid[0] = 'f'; break; case 7: facts.a1ShaderSha256[0] = 'f'; break; case 8: facts.d1ShaderSha256[0] = 'f'; break; }
    s.collectFacts = [facts](const auto&, const auto&) { return facts; };
    EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, Paths(f.temp), s), c::ExitCode::PreflightOrControlIncomplete); EXPECT_EQ(f.launched, 0U);
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlFactsDrift, testing::Range(0U, 9U));
class Stage5ControlCollision : public testing::TestWithParam<unsigned> {};
TEST_P(Stage5ControlCollision, AllThirtySlotsAndControlPathsAreReservedBeforeChildZero)
{
    CampaignFixture f; const auto n = GetParam(); std::filesystem::path path;
    if (n < 90U) { const auto& child = f.manifest.groups[n / 30U].children[(n % 30U) / 3U];
        path = f.temp.root / "results/local" / (child.sessionId + std::array{"", ".incomplete", ".failure.json"}[n % 3U]); }
    else if (n < 93U) path = std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + std::array{L"", L".incomplete", L".incomplete.tmp"}[n - 90U]);
    else path = c::AnalysisPath(f.manifest, f.temp.root);
    Write(path, "protected collision"); EXPECT_EQ(f.Run(), c::ExitCode::PreflightOrControlIncomplete); EXPECT_EQ(f.launched, 0U); EXPECT_EQ(Read(path), "protected collision");
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlCollision, testing::Range(0U, 94U));

class Stage5ControlPhasePackage : public testing::TestWithParam<unsigned> {};
TEST_P(Stage5ControlPhasePackage, IndependentReopenPreservesRawOrderAndLegalPreparationSeparation)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto g = GetParam() / 2U, p = GetParam() % 2U;
    const auto phase = m.groups[g].phase; const auto& child = m.groups[g].children[p];
    const auto w = g == 2U ? std::optional<std::uint64_t>{2U} : std::nullopt; Package(m, phase, child, t.root, w);
    const auto result = c::InspectPackage(m, phase, child, t.root, w, 0U);
    ASSERT_TRUE(result.structurallyValid) << (result.errors.empty() ? "" : result.errors[0]); ASSERT_TRUE(result.input);
    EXPECT_EQ(result.artifacts.size(), 6U); EXPECT_EQ(result.input->observations.size(), g == 2U ? 200U : 48U);
    EXPECT_EQ(result.input->clockCalibration.deltasNanoseconds.size(), 4095U); EXPECT_EQ(result.input->clockCalibration.deltasNanoseconds[0], 0U);
    for (std::size_t i = 0; i < result.input->observations.size(); ++i) EXPECT_EQ(result.input->observations[i].sequenceIndex, i);
    for (const auto& a : result.artifacts) EXPECT_EQ(a.sha256, core::Sha256File(t.root / a.relativePath));
    EXPECT_EQ(Read(t.root / "results/local" / child.sessionId / (g == 2U ? "warmup.csv" : "samples.csv")),
        (g == 2U ? ev::WarmupCsvHeader() : ev::SamplesCsvHeader()) + "\r\n");
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlPhasePackage, testing::Range(0U, 6U));
struct Tamper { const char* file; const char* from; const char* to; };
void PrintTo(const Tamper& d, std::ostream* out) { *out << d.file << ": " << d.from << " -> " << d.to; }
class Stage5ControlPackageTamper : public testing::TestWithParam<Tamper> {};
TEST_P(Stage5ControlPackageTamper, IndependentInspectorRejectsMalformedOrIncoherentEvidence)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0]; Package(m, Phase::A1Sentinel, child, t.root);
    const auto d = GetParam(); const auto path = t.root / "results/local" / child.sessionId / d.file; Write(path, Replace(Read(path), d.from, d.to));
    const auto r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 0U); EXPECT_FALSE(r.structurallyValid); EXPECT_FALSE(r.errors.empty()); EXPECT_FALSE(r.input);
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlPackageTamper, testing::Values(
    Tamper{"environment.json", "\"schema_version\":2", "\"schema_version\":1"}, Tamper{"environment.json", "\"protocol_version\":\"1.2\"", "\"protocol_version\":\"1.1\""},
    Tamper{"environment.json", "qualification", "candidate-performance"}, Tamper{"environment.json", "\"git_dirty\":false", "\"git_dirty\":true"},
    Tamper{"environment.json", "anonymous-machine", "another-machine"}, Tamper{"environment.json", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "faaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
    Tamper{"environment.json", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "fbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
    Tamper{"environment.json", "00112233-4455-6677-8899-aabbccddeeff", "10112233-4455-6677-8899-aabbccddeeff"},
    Tamper{"environment.json", "\"shader_sha256\":null", "\"shader_sha256\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\""},
    Tamper{"environment.json", "\"schema_version\":2", "\"schema_version\":2,\"unrecognized\":0"},
    Tamper{"environment.json", "\"schema_version\":2", "\"schema_version\":2,\"schema_version\":2"},
    Tamper{"environment.json", "\"native_markers_enabled\":false", "\"native_markers_enabled\":true"},
    Tamper{"host-clock.csv", "delta_ns", "delta_ms"}, Tamper{"host-clock.csv", ",0,0\r\n", ",1,0\r\n"},
    Tamper{"host-clock.csv", ",0,0\r\n", ",0,-1\r\n"}, Tamper{"host-clock.csv", ",0,0\r\n", ",0,1.5\r\n"},
    Tamper{"host-clock.csv", ",0,0\r\n", ",0,18446744073709551616\r\n"},
    Tamper{"warmup.csv", ",100,900,1000,ok", ",100,900,1001,ok"}, Tamper{"warmup.csv", ",100,900,1000,ok", ",100,,1000,ok"},
    Tamper{"warmup.csv", ",100,900,1000,ok", ",100,900,1000,wait_failed"},
    Tamper{"warmup.csv", ",0,100,900,1000,ok", ",1,100,900,1000,ok"},
    Tamper{"initialization.csv", "setup_complete", "not_setup"}, Tamper{"initialization.csv", "EX-2,cuda", "EX-1,cuda"},
    Tamper{"initialization.csv", "synthetic setup", "\"bad\"suffix"},
    Tamper{"summary.json", "\"recorded_sample_count\":0", "\"recorded_sample_count\":1"},
    Tamper{"summary.json", "not_admitted", "pending_gate0_review"}, Tamper{"summary.json", "\"host_completion_ns\":null", "\"host_completion_ns\":0"}));
TEST(Ex2Stage5SupervisorInspection, RejectsSampleNativeValidationParametersOrderAndFalseSummary)
{
    for (unsigned i = 0; i < 6U; ++i)
    {
        Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[2].children[1]; Package(m, Phase::D1Sample, child, t.root, 2U);
        const auto path = t.root / "results/local" / child.sessionId / (i == 5U ? "summary.json" : "samples.csv"); auto b = Read(path);
        if (i == 0U) b = Replace(b, ",100,900,1000,\r\n", ",100,900,1000,12\r\n");
        if (i == 1U) b = Replace(b, ",true,ok,", ",false,ok,");
        if (i == 2U) b = Replace(b, ",H,2,200,", ",H,4,200,");
        if (i == 3U) b = Replace(b, ",0,true,ok,", ",1,true,ok,");
        if (i == 4U) b = Replace(b, ",1048576,", ",1048577,");
        if (i == 5U) b = Replace(b, "\"median\":1000", "\"median\":1001");
        Write(path, b); EXPECT_FALSE(c::InspectPackage(m, Phase::D1Sample, child, t.root, 2U, 0U).structurallyValid) << i;
    }
}
TEST(Ex2Stage5SupervisorInspection, ExactTopologyMissingExtraNestedContradictoryAndWrongExit)
{
    for (unsigned mode = 0; mode < 7U; ++mode)
    {
        Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0]; Package(m, Phase::A1Sentinel, child, t.root);
        const auto path = t.root / "results/local" / child.sessionId;
        if (mode == 0U) std::filesystem::remove(path / "host-clock.csv");
        if (mode == 1U) Write(path / "extra.txt", "extra");
        if (mode == 2U) std::filesystem::create_directory(path / "nested");
        if (mode == 3U) std::filesystem::create_directory(t.root / "results/local" / (child.sessionId + ".incomplete"));
        if (mode == 4U) Write(path / "samples.csv", ev::SamplesCsvHeader() + "\r\nextra\r\n");
        if (mode == 5U) Write(path / "host-clock.csv", ev::HostClockHeader() + "\r\n");
        const auto result = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, mode == 6U ? 3U : 0U);
        EXPECT_FALSE(result.structurallyValid) << mode;
        if (mode == 2U) EXPECT_EQ(result.artifacts.size(), 6U); // Inventory every safe file despite the rejected nested directory.
    }
}
TEST(Ex2Stage5SupervisorInspection, ValidCoarseClockAndStateSwitchRemainScientificFacts)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[1].children[0];
    Package(m, Phase::D1Warmup, child, t.root, {}, [](auto i) { return i >= 12U && i < 24U ? 1200U : 1000U; }, 11U);
    const auto r = c::InspectPackage(m, Phase::D1Warmup, child, t.root, {}, 0U); ASSERT_TRUE(r.structurallyValid); ASSERT_TRUE(r.input);
    const auto a = s5::AssessWarmupProcess(*r.input); EXPECT_TRUE(a.InputValid()); EXPECT_FALSE(a.Qualified());
    EXPECT_FALSE(a.Candidates()[0].clockAdequacy.Adequate()); EXPECT_TRUE(a.Candidates()[0].orderedState.AbruptStateSwitch());
    EXPECT_EQ(r.input->observations.size(), 48U);
}
TEST(Ex2Stage5SupervisorInspection, RetainsExtremeRawObservationAndNullClockWithoutDropping)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0];
    Package(m, Phase::A1Sentinel, child, t.root, {}, [](auto i) { return i == 3U ? 999999U : 1000U; }, std::nullopt);
    const auto r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 0U); ASSERT_TRUE(r.structurallyValid); ASSERT_TRUE(r.input);
    EXPECT_EQ(r.input->observations[3].hostCompletionNanoseconds, 999999U); EXPECT_EQ(r.input->clockCalibration.deltasNanoseconds.size(), 4095U);
    EXPECT_FALSE(r.input->clockCalibration.deltasNanoseconds[0]); EXPECT_FALSE(s5::AssessA1Process(*r.input).InputValid());
}
TEST(Ex2Stage5SupervisorInspection, MissingPartialAndNonzeroFinalAreNeverAdopted)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0];
    EXPECT_EQ(c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 3U).state, "missing");
    Write(t.root / "results/local" / (child.sessionId + ".incomplete") / "environment.json", "unfinished");
    const auto r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 3U); EXPECT_EQ(r.state, "partial-retained"); EXPECT_EQ(r.artifacts.size(), 1U); EXPECT_FALSE(r.input);
}
TEST(Ex2Stage5SupervisorControl, ThirtyFreshProcessesAndDurableAnchorsBeforeSample)
{
    CampaignFixture f; const auto before = f.manifest.canonicalJson; unsigned samplePending{}, a1Barrier{}, warmBarrier{};
    f.onUpdate = [&](const c::ExecutionRecord& r) {
        EXPECT_EQ(r.manifest.canonicalJson, before);
        if (r.analyses[0]) { ++a1Barrier; EXPECT_GE(r.executions.size(), 10U); }
        if (r.analyses[1]) { ++warmBarrier; EXPECT_GE(r.executions.size(), 20U); }
        if (!r.executions.empty() && r.executions.back().groupIndex == 2U && r.executions.back().continuation == "launch_pending")
        { ++samplePending; EXPECT_NO_THROW(c::RequireWarmupAnchor(r, f.temp.root)); EXPECT_EQ(r.selectedCommonW, 0U); }
    };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U); EXPECT_EQ(samplePending, 10U); EXPECT_GT(a1Barrier, 0U); EXPECT_GT(warmBarrier, 0U);
    const auto ledger = Read(c::ControlPath(f.manifest, f.temp.root)); EXPECT_NE(ledger.find("\"campaign_complete\":true"), ledger.npos);
    const auto analysis = Read(c::AnalysisPath(f.manifest, f.temp.root) / "campaign-analysis.json");
    EXPECT_NE(analysis.find("\"d1_scope_qualified\":true"), analysis.npos); EXPECT_NE(analysis.find("pending_human_review"), analysis.npos);
    for (const auto& word : {"performanceCounter", "performance_counter", "qpc", "winner", "speedup", "gate0_verdict", "stage2_authorization", "median"}) EXPECT_EQ(ledger.find(word), ledger.npos);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + L".incomplete")));
}
TEST(Ex2Stage5SupervisorControl, A1StateAndSpreadAreDescriptiveAndDoNotGateD1)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) {
        Package(f.manifest, phase, child, f.temp.root, w,
            phase == Phase::A1Sentinel ? std::function<std::uint64_t(std::size_t)>{[p = child.planIndex](auto i) { return (i >= 12U && i < 24U ? 1500U : 1000U) + p * 100U; }} : std::function<std::uint64_t(std::size_t)>{}, phase == Phase::A1Sentinel ? 1000U : 1U);
    };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U);
    const auto a = Read(c::AnalysisPath(f.manifest, f.temp.root) / "a1-analysis.json"); EXPECT_NE(a.find("\"abrupt_state_switch\":true"), a.npos);
    EXPECT_NE(a.find("\"exceeds_one_point_ten\":true"), a.npos);
    for (const auto& word : {"winner", "speedup", "admission", "gate0_verdict"}) EXPECT_EQ(a.find(word), a.npos);
}
TEST(Ex2Stage5SupervisorControl, ValidWarmupNonqualificationFinishesTenThenSkipsConditionalGroup)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) { Package(f.manifest, phase, child, f.temp.root, w, {}, phase == Phase::D1Warmup ? 11U : 1U); };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 20U);
    EXPECT_FALSE(std::filesystem::exists(c::AnalysisPath(f.manifest, f.temp.root) / "d1-sample-analysis.json"));
    const auto ledger = Read(c::ControlPath(f.manifest, f.temp.root)); EXPECT_NE(ledger.find("skipped_by_protocol"), ledger.npos); EXPECT_NE(ledger.find("\"campaign_complete\":true"), ledger.npos);
    const auto a = Read(c::AnalysisPath(f.manifest, f.temp.root) / "campaign-analysis.json"); EXPECT_NE(a.find("\"d1_scope_qualified\":false"), a.npos);
}
TEST(Ex2Stage5SupervisorControl, ValidSampleNonqualificationFinishesTenAndFinalizesControl)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) { Package(f.manifest, phase, child, f.temp.root, w, {}, phase == Phase::D1Sample && child.planIndex == 0U ? 11U : 1U); };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U);
    const auto a = Read(c::AnalysisPath(f.manifest, f.temp.root) / "campaign-analysis.json"); EXPECT_NE(a.find("\"d1_scope_qualified\":false"), a.npos);
    EXPECT_TRUE(std::filesystem::exists(c::AnalysisPath(f.manifest, f.temp.root) / "d1-sample-analysis.json"));
}
TEST(Ex2Stage5SupervisorControl, CommonWComesOnlyFromCurrentWarmupAndPreparationsAreControlOnly)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) {
        Package(f.manifest, phase, child, f.temp.root, w, phase == Phase::D1Warmup ? std::function<std::uint64_t(std::size_t)>{[p = child.planIndex](auto i) { const auto cut = p % 3U == 0U ? 8U : p % 3U == 1U ? 4U : 0U; return i < cut ? 1500U : 1000U; }} : std::function<std::uint64_t(std::size_t)>{});
    };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U); EXPECT_EQ(f.preparations, 80U);
    for (const auto& e : f.schedule) if (e.phase == Phase::D1Sample) EXPECT_EQ(e.selectedW, 8U);
}
TEST(Ex2Stage5SupervisorControl, InvalidClockStopsImmediatelyAsIncompleteWithoutScientificDisposition)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) { Package(f.manifest, phase, child, f.temp.root, w, {}, 0U); };
    EXPECT_EQ(f.Run(), c::ExitCode::ChildOrEvidenceIncomplete); EXPECT_EQ(f.launched, 1U);
    const auto ledger = Read(c::ControlPath(f.manifest, f.temp.root)); EXPECT_NE(ledger.find("operational analysis input invalid"), ledger.npos);
    EXPECT_NE(ledger.find("\"campaign_complete\":false"), ledger.npos);
    const auto a = Read(c::AnalysisPath(f.manifest, f.temp.root) / "campaign-analysis.json"); EXPECT_NE(a.find("\"d1_scope_qualified\":null"), a.npos);
}
TEST(Ex2Stage5SupervisorControl, CorruptedWarmupAnchorBlocksEverySampleLaunch)
{
    CampaignFixture f; bool tampered = false;
    f.onUpdate = [&](const c::ExecutionRecord& r) {
        if (!tampered && !r.executions.empty() && r.executions.back().groupIndex == 2U
            && r.executions.back().continuation == "launch_pending")
        { tampered = true; Write(f.temp.root / r.analyses[1]->relativePath, "changed"); }
    };
    EXPECT_EQ(f.Run(), c::ExitCode::PreflightOrControlIncomplete); EXPECT_EQ(f.launched, 20U);
    EXPECT_NE(Read(c::ControlPath(f.manifest, f.temp.root)).find("incomplete_control"), std::string::npos);
}
TEST(Ex2Stage5SupervisorControl, ProcessFailureStopsAndRetainedPackageIsNotAdopted)
{
    CampaignFixture f; auto services = f.Services(); services.runProcess = [&](const auto&, const auto&, const auto& e, auto, auto, auto) {
        ++f.launched; const auto& child = f.manifest.groups[0].children[e.planIndex]; Package(f.manifest, e.phase, child, f.temp.root);
        auto r = Complete(e); r.exitCode = 3U; r.progressValidation = "valid_prefix"; r.eventCount = 2U; return r;
    };
    EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, Paths(f.temp), services), c::ExitCode::ChildOrEvidenceIncomplete); EXPECT_EQ(f.launched, 1U);
    EXPECT_NE(Read(c::ControlPath(f.manifest, f.temp.root)).find("completed package requires successful child exit"), std::string::npos);
}
TEST(Ex2Stage5SupervisorControl, ProcessControlExceptionStopsAsExitTwoWithoutScientificResult)
{
    CampaignFixture f; auto services = f.Services(); c::ExecutionRecord last;
    const auto partial = f.temp.root / "results/local" / (f.manifest.groups[0].children[0].sessionId + ".incomplete") / "partial.txt";
    f.onUpdate = [&](const c::ExecutionRecord& r) { last = r; };
    services.runProcess = [&](const auto&, const auto&, const c::ProgressExpectation& e, auto, auto, auto) -> c::ProcessResult {
        ++f.launched; EXPECT_EQ(e.phase, Phase::A1Sentinel); EXPECT_EQ(e.planIndex, 0U);
        Write(partial, "retained partial bytes"); throw std::runtime_error("synthetic process control failure");
    };
    EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, Paths(f.temp), services), c::ExitCode::PreflightOrControlIncomplete);
    EXPECT_EQ(f.launched, 1U); EXPECT_FALSE(last.campaignComplete); EXPECT_EQ(last.status, "incomplete_control");
    EXPECT_EQ(last.failureReason, "process_control_failure"); ASSERT_EQ(last.executions.size(), 1U);
    EXPECT_EQ(last.executions[0].process.terminationReason, "process_control_failure");
    EXPECT_FALSE(last.executions[0].process.exitCode); EXPECT_EQ(last.executions[0].continuation, "stop_incomplete");
    EXPECT_EQ(last.groupStates[0], "incomplete"); EXPECT_EQ(last.groupStates[1], "not_reached");
    EXPECT_EQ(last.groupStates[2], "conditional_not_reached"); EXPECT_FALSE(last.selectedCommonW);
    for (const auto& a : last.analyses) EXPECT_FALSE(a);
    const auto ledger = c::ControlPath(f.manifest, f.temp.root);
    EXPECT_EQ(Read(ledger), c::SerializeExecutionRecord(last));
    EXPECT_EQ(Read(ledger).find("synthetic process control failure"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(ledger.wstring() + L".incomplete")));
    ASSERT_TRUE(last.campaignAnalysis);
    const auto analysis = Read(f.temp.root / last.campaignAnalysis->relativePath);
    EXPECT_NE(analysis.find("\"d1_scope_qualified\":null"), std::string::npos);
    EXPECT_EQ(Read(partial), "retained partial bytes");
}
TEST(Ex2Stage5SupervisorDurability, ImmutableFinalAnalysisAndOwnedLedgerOnly)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto dir = c::AnalysisPath(m, t.root); std::filesystem::create_directories(dir);
    const auto a = c::PublishAnalysis(t.root, dir / "test-analysis.json", "{\"x\":1}\n"); EXPECT_EQ(a.sha256, core::Sha256("{\"x\":1}\n"));
    EXPECT_THROW(c::PublishAnalysis(t.root, dir / "test-analysis.json", "replacement"), std::invalid_argument); EXPECT_EQ(Read(dir / "test-analysis.json"), "{\"x\":1}\n");
    c::ExecutionRecord r; r.manifest = m; r.observed = Facts(m); r.startTimeUtc = "2026-10-02T00:00:00Z";
    const auto path = c::ControlPath(m, t.root); c::WriteExecutionRecord(path, r, false);
    EXPECT_FALSE(std::filesystem::exists(path)); EXPECT_EQ(Read(std::filesystem::path(path.wstring() + L".incomplete")), c::SerializeExecutionRecord(r));
    EXPECT_THROW(c::WriteExecutionRecord(path, r, false), std::invalid_argument);
    auto other = r; other.startTimeUtc = "different-owner"; EXPECT_THROW(c::WriteExecutionRecord(path, other, false, true), std::invalid_argument);
    c::WriteExecutionRecord(path, r, true, true); EXPECT_EQ(Read(path), c::SerializeExecutionRecord(r));
    EXPECT_THROW(c::WriteExecutionRecord(path, r, false, true), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorDurability, MissingWarmupAnchorAndMissingDurableLedgerAreRejected)
{
    Temp t; c::ExecutionRecord r; r.manifest = c::ParseManifest(Golden()); EXPECT_THROW(c::RequireWarmupAnchor(r, t.root), std::invalid_argument);
    r.selectedCommonW = 2U; EXPECT_THROW(c::RequireWarmupAnchor(r, t.root), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorDurability, LocaleIndependentLedgerAndExplicitNulls)
{
    c::ExecutionRecord r; r.manifest = c::ParseManifest(Golden()); r.observed = Facts(r.manifest); const auto before = c::SerializeExecutionRecord(r);
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto previous = std::locale(); std::locale::global(std::locale(previous, new Comma)); EXPECT_EQ(c::SerializeExecutionRecord(r), before); std::locale::global(previous);
    EXPECT_NE(before.find("\"campaign_analysis\":null"), before.npos); EXPECT_NE(before.find("\"selected_common_w\":null"), before.npos);
}
TEST(Ex2Stage5SupervisorDurability, IndependentGoldenStartupLedgerByteHash)
{
    c::ExecutionRecord r; r.manifest = c::ParseManifest(Golden()); r.observed = Facts(r.manifest); r.startTimeUtc = "2026-10-02T00:00:00Z";
    const auto bytes = c::SerializeExecutionRecord(r);
    // Independently specified Python json+hashlib ledger, including all thirty
    // explicit schedule identities and null startup/analysis fields.
    EXPECT_EQ(bytes.size(), 10046U);
    EXPECT_EQ(core::Sha256(bytes), "05a296200ddf6c2e0abd105d87f96ae51f142d9b71914a00e20822ef41dd1701");
}
const std::array<std::string_view, 6> Historical{"s5-smoke-86864a8f-a1-cuda", "s5-smoke-86864a8f-a1-vulkan", "s5-smoke-86864a8f-d1w-cuda", "s5-smoke-86864a8f-d1w-vulkan", "s5-smoke-86864a8f-d1s-w2-cuda", "s5-smoke-86864a8f-d1s-w2-vulkan"};
TEST(Ex2Stage5SupervisorHistorical, UnrelatedHistoricalPackagesArePreservedNeverAdoptedAndNeverSeedW)
{
    CampaignFixture f; for (const auto& id : Historical) Write(f.temp.root / "results/local" / id / "marker", "protected historical w2");
    EXPECT_NO_THROW(c::RejectOutputCollisions(f.manifest, f.temp.root)); EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U);
    for (const auto& id : Historical) EXPECT_EQ(Read(f.temp.root / "results/local" / id / "marker"), "protected historical w2");
    for (const auto& e : f.schedule) if (e.phase == Phase::D1Sample) EXPECT_EQ(e.selectedW, 0U);
}
TEST(Ex2Stage5SupervisorHistorical, KnownHistoricalNamesStillRejectEveryProductionStage5ControlCollisionForm)
{
    for (const auto& id : Historical) for (const auto& suffix : {"", ".incomplete", ".failure.json"})
    {
        CampaignFixture f; f.manifest = c::ParseManifest(Sign(Replace(Golden(), "synthetic-g0-p0", id)));
        const auto path = f.temp.root / "results/local" / (std::string(id) + suffix); Write(path, "immutable historical");
        EXPECT_EQ(f.Run(), c::ExitCode::PreflightOrControlIncomplete); EXPECT_EQ(f.launched, 0U); EXPECT_EQ(Read(path), "immutable historical");
    }
}
TEST(Ex2Stage5SupervisorInspection, FailureSidecarContextIsRetainedWithoutInventedCompletionTiming)
{
    namespace ex = s5::execution;
    for (const auto phase : {"submission", "completion_wait", "readback"})
    {
        Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0];
        const auto input = core::GenerateWordInput(core::CoreInputSeed, 256);
        const auto foundation = ev::MakeWordFoundation({c::FrozenCondition(Phase::A1Sentinel), s5::FrozenProcessPlan()[0], child.sessionId,
            m.machineId, {m.gpuUuid, true}, m.sourceRevision, m.childExecutableSha256, {}}, input, core::ReferenceA1(input));
        ex::FailureRecord failure; failure.sessionId = child.sessionId; failure.phase = Phase::A1Sentinel; failure.planIndex = 0U;
        failure.process = s5::FrozenProcessPlan()[0]; failure.category = ex::ExitCode::ExecutionFailure; failure.failurePhase = phase;
        failure.errorCode = "synthetic_failed"; failure.foundationEstablished = true; failure.sourceRevision = m.sourceRevision;
        failure.runId = child.sessionId; failure.seriesId = foundation.Identity().seriesId; failure.stagingRetained = true;
        failure.attemptIndex = 0; failure.nativePhase = phase; failure.nativeCode = std::numeric_limits<std::int64_t>::min();
        failure.nativeDetail = "synthetic native error"; failure.successfulWait = std::string_view(phase) == "readback";
        failure.hostSubmissionNanoseconds = 100U;
        if (*failure.successfulWait) { failure.hostWaitNanoseconds = 900U; failure.hostCompletionNanoseconds = 1000U; }
        const auto path = t.root / "results/local" / (child.sessionId + ".failure.json");
        std::filesystem::create_directories(t.root / "results/local" / (child.sessionId + ".incomplete"));
        Write(path, ex::SerializeFailure(failure)); auto r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 3U);
        EXPECT_EQ(r.state, "incomplete-retained"); EXPECT_TRUE(r.errors.empty()); EXPECT_FALSE(r.input); ASSERT_TRUE(r.failureContextJson);
        EXPECT_NE(r.failureContextJson->find("-9223372036854775808"), r.failureContextJson->npos);
        if (!*failure.successfulWait)
        {
            failure.hostWaitNanoseconds = 900U; failure.hostCompletionNanoseconds = 1000U; Write(path, ex::SerializeFailure(failure));
            r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 3U); EXPECT_FALSE(r.errors.empty());
        }
    }
}
TEST(Ex2Stage5SupervisorInspection, PreFoundationSidecarOnlyAndContradictoryStatesAreTruthful)
{
    namespace ex = s5::execution; Temp t; const auto m = c::ParseManifest(Golden()); const auto& child = m.groups[0].children[0];
    ex::FailureRecord f; f.sessionId = child.sessionId; f.failurePhase = "preflight"; f.errorCode = "preflight_failed"; f.detail = "synthetic failure";
    const auto path = t.root / "results/local" / (child.sessionId + ".failure.json"); Write(path, ex::SerializeFailure(f));
    const auto r = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 2U); EXPECT_EQ(r.state, "sidecar-only"); EXPECT_TRUE(r.errors.empty()); EXPECT_EQ(r.artifacts.size(), 1U);
    EXPECT_FALSE(c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 0U).errors.empty());
    Write(path, Replace(Read(path), "\"record_version\":1", "\"record_version\":2")); EXPECT_FALSE(c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 2U).errors.empty());
    Write(path, ex::SerializeFailure(f)); Package(m, Phase::A1Sentinel, child, t.root);
    const auto both = c::InspectPackage(m, Phase::A1Sentinel, child, t.root, {}, 2U); EXPECT_EQ(both.state, "contradictory"); EXPECT_FALSE(both.structurallyValid); EXPECT_EQ(both.artifacts.size(), 7U);
}
TEST(Ex2Stage5SupervisorDurability, UnexpectedSupervisorDeathLeavesOnlyIncompleteLedger)
{
    Temp t; const auto m = c::ParseManifest(Golden()); const auto manifestPath = t.root / "manifest.json"; Write(manifestPath, Golden());
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S5_TEST_CHILD, {"ledger-death", t.root.string(), manifestPath.string()},
        {Phase::A1Sentinel, 0U, {}}, std::chrono::milliseconds(2000), std::chrono::milliseconds(10000), std::chrono::steady_clock::now() + std::chrono::seconds(30));
    EXPECT_EQ(r.exitCode, 99U); const auto path = c::ControlPath(m, t.root); EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(path.wstring() + L".incomplete"))); EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(path.wstring() + L".incomplete.tmp")));
    EXPECT_THROW(c::RejectOutputCollisions(m, t.root), std::invalid_argument);
}
TEST(Ex2Stage5SupervisorDurability, PublicationFailureStopsBeforeLaterChildAndKeepsOwnedState)
{
    CampaignFixture f; bool injected = false; f.onUpdate = [&](const c::ExecutionRecord& r) {
        if (!injected && r.groupStates[0] == "integrity_complete") { injected = true; Write(c::AnalysisPath(f.manifest, f.temp.root) / "a1-analysis.json.tmp", "collision"); }
    };
    EXPECT_EQ(f.Run(), c::ExitCode::SupervisorPublicationFailure); EXPECT_EQ(f.launched, 10U);
    EXPECT_EQ(Read(c::AnalysisPath(f.manifest, f.temp.root) / "a1-analysis.json.tmp"), "collision");
    EXPECT_NE(Read(c::ControlPath(f.manifest, f.temp.root)).find("\"campaign_complete\":false"), std::string::npos);
}
TEST(Ex2Stage5SupervisorControl, MutationOfPublicManifestCannotReplaceSignedSchedule)
{
    CampaignFixture f; auto s = f.Services(); f.manifest.groups[0].children.clear();
    s.runProcess = [&](const auto&, const auto&, const auto&, auto, auto, auto) { ++f.launched; c::ProcessResult r; r.exitCode = 3U; return r; };
    EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, Paths(f.temp), s), c::ExitCode::ChildOrEvidenceIncomplete); EXPECT_EQ(f.launched, 1U);
    EXPECT_NE(Read(c::ControlPath(f.manifest, f.temp.root)).find("synthetic-g2-p9"), std::string::npos);
}
TEST(Ex2Stage5SupervisorControl, CurrentProvenanceDriftAndFutureNamespaceRaceStopBeforeNextChild)
{
    for (const bool collide : {false, true})
    {
        CampaignFixture f; auto s = f.Services(); unsigned factsCalls{};
        s.collectFacts = [&](const auto& m, const auto&) { auto facts = Facts(m); if (!collide && ++factsCalls >= 3U) facts.gitDirty = true; return facts; };
        if (collide) f.onUpdate = [&](const c::ExecutionRecord& r) {
            if (r.executions.size() == 1U && r.executions.back().continuation == "continue_group") Write(f.temp.root / "results/local" / "synthetic-g0-p1", "race collision");
        };
        EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, Paths(f.temp), s), c::ExitCode::PreflightOrControlIncomplete); EXPECT_EQ(f.launched, 1U);
    }
}
TEST(Ex2Stage5SupervisorControl, ExistingWithinBackendProcessStabilityCanNonqualifyCompleteSampleGroup)
{
    CampaignFixture f; f.writePackage = [&](auto phase, const auto& child, auto w) {
        Package(f.manifest, phase, child, f.temp.root, w, [value = phase == Phase::D1Sample && child.planIndex == 0U ? 1200U : 1000U](auto) { return value; });
    };
    EXPECT_EQ(f.Run(), c::ExitCode::CompletedQualificationAnalysis); EXPECT_EQ(f.launched, 30U);
    const auto sample = Read(c::AnalysisPath(f.manifest, f.temp.root) / "d1-sample-analysis.json");
    EXPECT_NE(sample.find("\"d1_sample_count_qualified\":true"), sample.npos);
    EXPECT_NE(sample.find("\"cuda_process_stability_qualified\":false"), sample.npos);
    EXPECT_NE(Read(c::AnalysisPath(f.manifest, f.temp.root) / "campaign-analysis.json").find("\"d1_scope_qualified\":false"), std::string::npos);
}
} // namespace
