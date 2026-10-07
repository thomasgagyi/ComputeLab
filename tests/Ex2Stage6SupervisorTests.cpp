#include "ex2/Ex2Stage6Supervisor.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <cstring>
#include <limits>
#include <set>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace c = s6::control;
namespace p = s6::progress;
namespace ev = s6::evidence;
namespace a = s6::analysis;
constexpr std::string_view Uuid = "00112233-4455-6677-8899-aabbccddeeff";
std::string Read(const std::filesystem::path& path)
{ std::ifstream in(path, std::ios::binary); if (!in) throw std::runtime_error("fixture read failed"); return {std::istreambuf_iterator<char>(in), {}}; }
void Write(const std::filesystem::path& path, std::string_view bytes)
{ std::ofstream out(path, std::ios::binary); out.write(bytes.data(), bytes.size()); if (!out) throw std::runtime_error("fixture write failed"); }
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
c::PreflightFacts Facts(const c::Manifest& m)
{ return {m.sourceRevision, false, m.childExecutableSha256, m.supervisorExecutableSha256, m.gpuUuid, m.gpuUuid, m.shaderSha256}; }
struct Temporary
{
    std::filesystem::path root;
    Temporary() { root = std::filesystem::temp_directory_path() / ("computelab-s6-i3-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("fixture root collision"); std::filesystem::create_directories(root / "results/local"); }
    ~Temporary() { std::error_code error; if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("computelab-s6-i3-")) std::filesystem::remove_all(root, error); }
};
struct TestJunction
{
    Temporary& owner;
    std::filesystem::path link;
    TestJunction(Temporary& t, std::string_view name, const std::filesystem::path& target) : owner(t), link(t.root / name)
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
ev::SampleRecord Good(const ev::Plan& plan, std::uint64_t n)
{ ev::SampleRecord r; r.plan = plan; r.sampleIndex = n; r.correctness = {true, true, true, true, true}; r.status = ev::Status::Ok;
    r.hostSubmissionNanoseconds = 10 + n; r.hostWaitNanoseconds = 20; r.hostCompletionNanoseconds = 30 + n; return r; }
c::ProcessResult Safe(std::uint64_t slot = 0, std::size_t rows = 100)
{
    c::ProcessResult r; r.processCreated = r.containmentAssigned = r.containmentVerified = r.processResumed = r.primaryTerminationConfirmed = r.jobEmptyConfirmed = r.cleanEof = true;
    r.processId = 42; r.exitCode = 0; r.exitKind = c::ExitKind::VoluntaryStage6; r.jobTotalProcesses = 5; r.jobActiveProcesses = 0;
    r.launchTimeUtc = r.exitTimeUtc = "2026-10-06T00:00:00.000Z";
    for (std::size_t n = 0; n < rows * 2; ++n) r.observations.push_back({{p::ProgressMagic, p::ProgressVersion, n % 2 ? p::AttemptEvent::Returned : p::AttemptEvent::Started,
        0, slot, n / 2, static_cast<std::int64_t>(n + 1)}, static_cast<std::int64_t>(n + 1)});
    r.progressForm = rows == 0 ? p::TerminalForm::Empty : rows == 100 ? p::TerminalForm::Full : p::TerminalForm::ReturnedPrefix;
    if (rows) r.lastReturnedAttempt = p::AttemptIdentity{slot, rows - 1}; return r;
}
c::ProcessResult PostCompletionCleanup(std::uint64_t slot = 0, std::size_t rows = 100)
{
    auto r = Safe(slot, rows);
    r.descendantSurvivalObserved = r.supervisorTerminationRequested = r.terminateJobSucceeded = true;
    r.exitKind = c::ExitKind::SupervisorForced; r.jobTotalProcesses = 17;
    return r;
}
std::map<std::string, std::string> Bundle(const ev::Foundation& f, int kind = 0)
{
    computelab::results::EnvironmentRecord common{2, "EX-2", f.Identity().runId, "2026-10-06T00:00:00Z", f.Identity().seriesIdentity.sourceRevision, false, "test-machine",
        "Windows", "11", "fixture CPU", 1024, "fixture GPU", "NVIDIA", "10DE:0000", 1024, "driver", "toolkit", "runtime", "capability", "sdk", "api", "MSVC", "compiler", "cmake", "ninja", "x64-release", "Release", false, false};
    ev::BackendDiagnostics diagnostics; diagnostics.implementation = "fixture-native";
    if (f.Identity().slot.process.backend == ex2::Backend::Cuda) diagnostics.streamFlags = "nonblocking";
    else { diagnostics.queueFamilyIndex = 0; diagnostics.queueFlags = 2; diagnostics.queueCount = 1; }
    const auto environment = ev::MakeEnvironmentRecord(common, f, diagnostics);
    auto init = ev::MakeSetupCompleteInitialization(f, "CPU-only fixture, comma and \"quote\""); std::vector<ev::SampleRecord> rows;
    const auto count = kind == 0 ? 100 : kind == 4 ? 0 : 3;
    for (int n = 0; n < count; ++n) rows.push_back(Good(f.Identity(), n));
    ev::Status status = ev::Status::Ok; std::optional<ev::FailurePhase> phase; std::optional<std::string> error;
    if (kind) {
        status = kind == 1 ? ev::Status::ValidationFailed : kind == 3 ? ev::Status::SubmitFailed : ev::Status::Incomplete;
        phase = kind == 1 ? ev::FailurePhase::Validation : kind == 2 ? ev::FailurePhase::Readback : kind == 3 ? ev::FailurePhase::Submission : ev::FailurePhase::BackendInitialization;
        error = kind == 1 ? "output_mismatch" : kind == 2 ? "readback_failed" : kind == 3 ? "submission_failed" : "backend_initialization_failed";
        if (rows.empty()) init.metric = "initialization_failed";
        else { auto& last = rows.back(); last.status = status; last.failurePhase = phase; last.errorCode = error;
            last.correctness = kind == 1 ? ev::CorrectnessProgress{true, true, true, true, false} : ev::CorrectnessProgress{true, kind == 2, false, false, {}};
            if (kind == 3) { last.hostSubmissionNanoseconds.reset(); last.hostWaitNanoseconds.reset(); last.hostCompletionNanoseconds.reset(); } }
    }
    const auto summary = ev::SummarizeSamples(f.Identity(), rows, status, phase, error);
    return {{"environment.json", ev::SerializeEnvironmentJson(f, environment)}, {"initialization.csv", ev::SerializeInitializationCsv(f.Identity(), {&init, 1})},
        {"samples.csv", ev::SerializeSamplesCsv(f.Identity(), rows)}, {"summary.json", ev::SerializeSummaryJson(summary, rows)}};
}
std::string Sidecar(const ev::Foundation& f, unsigned exit = 0, bool uncertain = false, std::optional<std::uint64_t> sample = {})
{
    const auto& plan = f.Identity();
    return "{\"record_version\":1,\"record_type\":\"ex2-stage6-child-failure\",\"session_id\":\"" + plan.runId
        + "\",\"slot_sequence_index\":" + std::to_string(plan.slot.sequenceIndex) + ",\"foundation_established\":true,\"run_id\":\"" + plan.runId
        + "\",\"series_id\":\"" + plan.seriesId + "\",\"source_revision\":\"" + plan.seriesIdentity.sourceRevision + "\",\"exit_category\":" + std::to_string(exit)
        + ",\"failure_phase\":\"backend-execution\",\"error_code\":\"native_failure\",\"sample_index\":" + (sample ? std::to_string(*sample) : "null")
        + ",\"completion_uncertain\":" + (uncertain ? "true" : "false") + ",\"native_phase\":null,\"native_code\":null,\"native_detail\":null}\n";
}
struct PackageFixture
{
    Temporary temp; c::Manifest manifest{c::ParseManifest(ManifestBytes())}; ev::Foundation foundation{c::ReconstructFoundation(manifest, 0)};
    std::filesystem::path final{temp.root / "results/local" / foundation.Identity().runId}, staging{final.wstring() + L".incomplete"}, side{final.wstring() + L".failure.json"};
    void Package(int kind = 0, bool staged = false) { const auto path = staged ? staging : final; std::filesystem::create_directory(path);
        for (const auto& [name, bytes] : Bundle(foundation, kind)) Write(path / name, bytes); }
    c::PackageInspection Inspect() { return c::InspectPackage(manifest, 0, temp.root); }
};

TEST(Ex2Stage6SupervisorManifest, FixedFullScheduleAndCanonicalHash)
{
    const auto bytes = ManifestBytes(); const auto m = c::ParseManifest(bytes); EXPECT_EQ(m.canonicalJson + "\n", bytes);
    EXPECT_EQ(m.sha256, c::CalculateManifestSha256(bytes)); EXPECT_EQ(m.fileSha256, ex2::Sha256(bytes));
    EXPECT_EQ(m.sha256, "084716d1795f8eb98e5dbae8f8c00a48925a85b444a74decb8dc8b99c842613e");
    for (std::size_t n = 0; n < 220; ++n) { EXPECT_EQ(m.groups[n / 10].children[n % 10].sequenceIndex, n); EXPECT_EQ(c::ChildArguments(m, n).size(), 14); }
    EXPECT_EQ(c::ParseSupervisorArguments(std::array<std::string_view, 2>{"--manifest", "results/local/s6-i3-test-stage6-manifest.json"}).generic_string(), "results/local/s6-i3-test-stage6-manifest.json");
}
TEST(Ex2Stage6SupervisorManifest, RejectsTypesKeysVersionsCountsIdentityAndTimeoutMutations)
{
    const auto raw = ManifestBytes();
    for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{
        {"\"manifest_version\":1", "\"manifest_version\":2"}, {"\"protocol_version\":\"1.4\"", "\"protocol_version\":\"1.2\""},
        {"\"protocol_version\":\"1.4\"", "\"protocol_version\":\"1.3\""},
        {"\"evidence_schema_version\":2", "\"evidence_schema_version\":1"}, {"\"instrument_mode\":\"H\"", "\"instrument_mode\":\"N\""},
        {"\"expected_git_dirty\":false", "\"expected_git_dirty\":true"}, {"\"declared_child_count\":220", "\"declared_child_count\":219"},
        {"\"declared_cell_group_count\":22", "\"declared_cell_group_count\":21"}, {"\"sequence_index\":137", "\"sequence_index\":138"},
        {"s6-i3-test-slot-137", "s6-i3-test-slot-138"}, {"\"cell_index\":3", "\"cell_index\":4"}, {"\"operation_timeout_ms\":1000", "\"operation_timeout_ms\":0"},
        {"\"operation_timeout_ms\":1000", "\"operation_timeout_ms\":60001"}, {"\"child_timeout_ms\":10000", "\"child_timeout_ms\":999"},
        {"\"child_timeout_ms\":10000", "\"child_timeout_ms\":1200001"}, {"\"campaign_timeout_ms\":86400000", "\"campaign_timeout_ms\":86400001"},
        {"\"campaign_timeout_ms\":86400000", "\"campaign_timeout_ms\":9999"}, {"\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":-1"},
        {"\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":2147483648"},
        {"\"vulkan_physical_device_index\":0", "\"vulkan_physical_device_index\":4294967296"},
        {"\"continuation_policy\":\"resolved-only-no-retry\"", "\"continuation_policy\":\"retry\""}, {"\"machine_id\":\"test-machine\"", "\"machine_id\":\"CON\""}}) {
        SCOPED_TRACE(before);
    EXPECT_THROW((void)c::ParseManifest(Rehash(Replace(raw, before, after))), std::exception);
    }
    EXPECT_THROW((void)c::ParseManifest(Rehash(Replace(raw, "\"instrument_mode\":\"H\",", ""))), std::exception);
    EXPECT_THROW((void)c::ParseManifest(Rehash(Replace(raw, "\"instrument_mode\":\"H\"", "\"instrument_mode\":\"H\",\"extra\":1"))), std::exception);
    EXPECT_THROW((void)c::ParseManifest(Replace(raw, "\"instrument_mode\":\"H\"", "\"instrument_mode\":\"H\",\"instrument_mode\":\"H\"")), std::exception);
    EXPECT_THROW((void)c::ParseManifest(Replace(raw, "\"cuda_device_ordinal\":0", "\"cuda_device_ordinal\":0.0")), std::exception);
}
TEST(Ex2Stage6SupervisorManifest, RejectsNoncanonicalBytesHashUnsafePathsAndPublicOverrides)
{
    const auto bytes = ManifestBytes(); for (const auto& text : {" " + bytes, bytes + "\n", bytes.substr(0, bytes.size() - 1), Replace(bytes, "\n", "\r\n"), Replace(bytes, "\"machine_id\":", "\"machine_id\": ")}) EXPECT_THROW((void)c::ParseManifest(text), std::exception);
    auto badHash = bytes; auto& digit = badHash[badHash.find("\"manifest_sha256\":\"") + 19]; digit = digit == 'f' ? 'e' : 'f';
    EXPECT_THROW((void)c::ParseManifest(badHash), std::exception);
    for (const auto path : {"../escape.exe", "C:/escape.exe", "/absolute.exe", "out//child.exe", "out/./child.exe", "out/../child.exe"})
        EXPECT_THROW((void)c::ParseManifest(Rehash(Replace(bytes, "out/build/x64-release/src/app/ComputeLabEx2Stage6.exe", path))), std::exception);
    for (const auto option : {"--resume", "--continue", "--help", "--sample-count", "--allow-debug", "--backend"}) EXPECT_THROW((void)c::ParseSupervisorArguments(std::array<std::string_view, 2>{option, "x"}), std::exception);
    EXPECT_THROW((void)c::ParseManifest(std::string(1024 * 1024 + 1, ' ')), std::exception);
}
TEST(Ex2Stage6SupervisorProgress, EveryValidPrefixAndOutstandingPrefix)
{
    for (std::size_t rows = 0; rows <= 100; ++rows) {
        p::State state(137); for (const auto& o : Safe(137, rows).observations) state.Accept(o);
        EXPECT_FALSE(state.invalid); EXPECT_EQ(state.Form(true), rows == 0 ? p::TerminalForm::Empty : rows == 100 ? p::TerminalForm::Full : p::TerminalForm::ReturnedPrefix);
        if (rows < 100) { state.Accept({{p::ProgressMagic, p::ProgressVersion, p::AttemptEvent::Started, 0, 137, rows, 300}, 300}); EXPECT_EQ(state.Form(true), p::TerminalForm::OutstandingStartedPrefix); }
    }
    EXPECT_EQ(p::State(0).Form(false), p::TerminalForm::NotStarted);
}
TEST(Ex2Stage6SupervisorProgress, ExactAbiWireMutationsAndQpcEquality)
{
    EXPECT_EQ(sizeof(p::ProgressEvent), 40); EXPECT_EQ(offsetof(p::ProgressEvent, slotSequenceIndex), 16); EXPECT_EQ(offsetof(p::ProgressEvent, sampleIndex), 24); EXPECT_EQ(offsetof(p::ProgressEvent, performanceCounter), 32);
    const p::Observation valid{{p::ProgressMagic, 1, p::AttemptEvent::Started, 0, 0, 0, 10}, 10};
    for (int mutation = 0; mutation < 10; ++mutation) { auto o = valid;
        switch (mutation) { case 0: o.record.magic = 0; break; case 1: o.record.version = 2; break; case 2: o.record.reserved = 1; break;
        case 3: o.record.event = p::AttemptEvent::Returned; break; case 4: o.record.event = static_cast<p::AttemptEvent>(99); break;
        case 5: o.record.slotSequenceIndex = 1; break; case 6: o.record.sampleIndex = 1; break; case 7: o.record.sampleIndex = 100; break;
        case 8: o.record.performanceCounter = 0; break; case 9: o.receiveCounter = 9; break; }
        p::State state(0); state.Accept(o); EXPECT_TRUE(state.invalid) << mutation;
    }
    p::State equal(0); equal.Accept(valid); auto returned = valid; returned.record.event = p::AttemptEvent::Returned; equal.Accept(returned); EXPECT_FALSE(equal.invalid);
    p::State backward(0); backward.Accept(valid); returned.record.performanceCounter = 9; backward.Accept(returned); EXPECT_TRUE(backward.invalid);
    p::State extra(0); for (const auto& o : Safe().observations) extra.Accept(o); extra.Accept(valid); EXPECT_TRUE(extra.invalid);
}
TEST(Ex2Stage6SupervisorProgress, ReporterOptionStrippingAndCheckedDeadlineCeilings)
{
    std::vector<std::string_view> direct{"--cell-index", "0"}; auto reporter = p::ExtractReporter(direct); EXPECT_EQ(direct.size(), 2); EXPECT_EQ(reporter.Observer().callback, nullptr);
    for (const auto value : {"0", "-1", "+1", "1x", "18446744073709551616"}) { std::vector<std::string_view> args{"--supervisor-progress-handle", value};
    EXPECT_THROW((void)p::ExtractReporter(args), std::exception); }
    std::vector<std::string_view> missing{"--supervisor-progress-handle"};
    EXPECT_THROW((void)p::ExtractReporter(missing), std::exception);
    EXPECT_EQ(c::DeadlineTicks(1, 1001), 2); EXPECT_EQ(c::DeadlineTicks(1000, 1001), 1001); EXPECT_EQ(c::DeadlineTicks(1, 1), 1);
    EXPECT_THROW((void)c::DeadlineTicks(UINT64_MAX, INT64_MAX), std::exception);
    EXPECT_THROW((void)c::AddDeadline(INT64_MAX, 1), std::exception);
    EXPECT_THROW((void)c::DeadlineTicks(1, 0), std::exception);
}
TEST(Ex2Stage6SupervisorInspection, IndependentCanonicalSuccessAndDiagnosticBundles)
{
    for (int kind = 0; kind <= 4; ++kind) { PackageFixture f; f.Package(kind); const auto i = f.Inspect(); SCOPED_TRACE(kind);
        ASSERT_TRUE(i.structurallyValid); ASSERT_TRUE(i.scientificBundleParsed); ASSERT_TRUE(i.scientificBundleValid); ASSERT_TRUE(i.scientificBytesCanonical);
        ASSERT_TRUE(i.input); EXPECT_TRUE(i.packageFinalized); EXPECT_EQ(i.topology, c::Topology::FinalOnly); EXPECT_FALSE(i.changedDuringInspection);
        const auto r = c::ReconcileSlot(Safe(0, kind == 0 ? 100 : kind == 4 ? 0 : 3), i);
        EXPECT_EQ(r.disposition, kind == 0 ? a::SlotDisposition::ResolvedSuccess : a::SlotDisposition::ResolvedDiagnosticFailure);
    }
}
TEST(Ex2Stage6SupervisorInspection, AllTopologiesAndCompleteStagingNeverPromoted)
{
    for (int mask = 0; mask < 8; ++mask) { PackageFixture f;
        if (mask & 1) f.Package(); if (mask & 2) f.Package(0, true); if (mask & 4) Write(f.side, Sidecar(f.foundation, 4));
        const auto i = f.Inspect(); const auto expected = (mask & 3) == 3 ? c::Topology::Contradictory : mask & 1 ? (mask & 4 ? c::Topology::FinalWithSidecar : c::Topology::FinalOnly)
            : mask & 2 ? (mask & 4 ? c::Topology::StagingWithSidecar : c::Topology::StagingOnly) : mask & 4 ? c::Topology::SidecarOnly : c::Topology::Missing;
        EXPECT_EQ(i.topology, expected); EXPECT_EQ(std::filesystem::exists(f.final), bool(mask & 1));
        if ((mask & 3) == 3) EXPECT_FALSE(i.structurallyValid);
    }
    PackageFixture f; std::filesystem::create_directory(f.staging); Write(f.staging / "samples.csv", "partial"); const auto partial = f.Inspect();
    EXPECT_TRUE(partial.structurallyValid); EXPECT_FALSE(partial.exactFileSet); EXPECT_FALSE(partial.input);
    Write(f.staging / "unexpected", "x"); EXPECT_FALSE(f.Inspect().structurallyValid);
}
TEST(Ex2Stage6SupervisorInspection, RejectsMalformedIdentityHArithmeticSummaryAndCanonicalBytes)
{
    for (const auto& [name, before, after] : std::vector<std::tuple<std::string, std::string, std::string>>{
        {"environment.json", "\"git_dirty\":false", "\"git_dirty\":true"}, {"environment.json", "x64-release", "x64-debug"},
        {"environment.json", "\"validation_enabled\":false", "\"validation_enabled\":true"}, {"environment.json", "\"cell_index\":0", "\"cell_index\":1"},
        {"environment.json", "\"slot_sequence_index\":0", "\"slot_sequence_index\":1"}, {"environment.json", "\"instrument_mode\":\"H\"", "\"instrument_mode\":\"N\""},
        {"environment.json", std::string(Uuid), "10112233-4455-6677-8899-aabbccddeeff"}, {"environment.json", std::string(40, 'a'), std::string(40, 'b')},
        {"environment.json", "\"input_sha256\":\"", "\"input_sha256\":\"0"}, {"environment.json", "\"shader_sha256\":null", "\"shader_sha256\":\"wrong\""},
        {"samples.csv", ",10,20,30,", ",10,20,31,"}, {"samples.csv", ",10,20,30,", ",,20,30,"}, {"samples.csv", ",true,ok,", ",false,ok,"},
        {"samples.csv", ",10,20,30,\r\n", ",10,20,30,1\r\n"}, {"samples.csv", ",0,true,ok,", ",1,true,ok,"},
        {"summary.json", "\"recorded_sample_count\":100", "\"recorded_sample_count\":99"}, {"summary.json", "\"native_device_interval_ns\":null", "\"native_device_interval_ns\":0"},
        {"initialization.csv", ",setup_complete,,", ",setup_complete,0,"}, {"summary.json", "\"schema_version\":2", "\"schema_version\":2,\"extra\":0"}}) {
        PackageFixture f; f.Package(); const auto path = f.final / name; Write(path, Replace(Read(path), before, after)); EXPECT_FALSE(f.Inspect().structurallyValid) << name << before;
    }
    PackageFixture f; f.Package(); Write(f.final / "environment.json", " " + Read(f.final / "environment.json")); EXPECT_FALSE(f.Inspect().scientificBytesCanonical);
}
TEST(Ex2Stage6SupervisorInspection, SidecarIdentityCausalityAndNarrowExitFourRecovery)
{
    for (const auto category : {0, 4, 5}) { PackageFixture f; f.Package(0, true); Write(f.side, Sidecar(f.foundation, category)); auto process = Safe(); process.exitCode = 4;
        const auto r = c::ReconcileSlot(process, f.Inspect()); EXPECT_EQ(r.disposition, category == 5 ? a::SlotDisposition::UnresolvedCampaignFatal : a::SlotDisposition::ResolvedSuccess);
        EXPECT_FALSE(std::filesystem::exists(f.final)); }
    for (const bool uncertain : {false, true}) { PackageFixture f; f.Package(3); Write(f.side, Sidecar(f.foundation, 0, uncertain, 2));
        EXPECT_EQ(c::ReconcileSlot(Safe(0, 3), f.Inspect()).disposition, uncertain ? a::SlotDisposition::UnresolvedCampaignFatal : a::SlotDisposition::ResolvedDiagnosticFailure); }
    PackageFixture success; success.Package(); Write(success.side, Sidecar(success.foundation)); EXPECT_EQ(c::ReconcileSlot(Safe(), success.Inspect()).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
    PackageFixture bad; bad.Package(3); Write(bad.side, Sidecar(bad.foundation, 0, false, 1)); EXPECT_FALSE(bad.Inspect().structurallyValid);
    Write(bad.side, Replace(Sidecar(bad.foundation), "\"foundation_established\":true", "\"foundation_established\":false")); EXPECT_FALSE(bad.Inspect().sidecarValid);
}
TEST(Ex2Stage6SupervisorReconcile, EverySafetyFactFailsClosedButLifetimeHelpersArePermitted)
{
    PackageFixture f; f.Package(); const auto inspection = f.Inspect(); ASSERT_EQ(c::ReconcileSlot(Safe(), inspection).disposition, a::SlotDisposition::ResolvedSuccess);
    for (int n = 0; n < 25; ++n) { auto process = Safe();
        switch (n) { case 0: process.processCreated = false; break; case 1: process.processResumed = false; break; case 2: process.containmentAssigned = false; break;
        case 3: process.containmentVerified = false; break; case 4: process.primaryTerminationConfirmed = false; break; case 5: process.jobEmptyConfirmed = false; break;
        case 6: process.jobActiveProcesses = 1; break; case 7: process.descendantSurvivalObserved = true; break; case 8: process.containmentVerificationFailed = true; break;
        case 9: process.supervisorTerminationRequested = true; break; case 10: process.operationTimedOut = true; break; case 11: process.childTimedOut = true; break;
        case 12: process.campaignTimedOut = true; break; case 13: process.progressInvalid = true; break; case 14: process.progressTransportFailed = true; break;
        case 15: process.cleanEof = false; break; case 16: process.trailingBytes = 1; break; case 17: process.exitKind = c::ExitKind::Abnormal; break;
        case 18: process.exitKind = c::ExitKind::ProgressTransportAbort; break; case 19: process.exitKind = c::ExitKind::SupervisorForced; break;
        case 20: process.exitKind = c::ExitKind::NotAvailable; break; case 21: process.exitCode = 2; break; case 22: process.exitCode = 3; break; case 23: process.exitCode = 5; break;
        case 24: process.observations[3].record.sampleIndex = 9; break; }
        const auto r = c::ReconcileSlot(process, inspection); EXPECT_EQ(r.disposition, a::SlotDisposition::UnresolvedCampaignFatal) << n; EXPECT_FALSE(r.process);
    }
    auto cleaned = Safe(); cleaned.descendantSurvivalObserved = true; cleaned.terminateJobSucceeded = true; EXPECT_EQ(c::ReconcileSlot(cleaned, inspection).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
    auto failedCleanup = cleaned; failedCleanup.containmentVerificationFailed = true; failedCleanup.jobEmptyConfirmed = false; EXPECT_EQ(c::ReconcileSlot(failedCleanup, inspection).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
}
TEST(Ex2Stage6SupervisorReconcile, EveryPackageGateAndProgressRowMismatchFailsClosed)
{
    PackageFixture f; f.Package(); const auto valid = f.Inspect(); ASSERT_TRUE(valid.input);
    for (int n = 0; n < 22; ++n) {
        for (const bool cleanup : {false, true}) {
        auto i = valid; auto process = cleanup ? PostCompletionCleanup() : Safe();
        switch (n) {
        case 0: i.attempted = false; break; case 1: i.structurallyValid = false; break; case 2: i.exactFileSet = false; break;
        case 3: i.scientificBundleParsed = false; break; case 4: i.scientificBundleValid = false; break;
        case 5: i.scientificBytesCanonical = false; break; case 6: i.changedDuringInspection = true; break;
        case 7: i.packageSha256.reset(); break; case 8: i.input.reset(); break; case 9: i.input->packageSha256 = std::string(64, 'f'); break;
        case 10: ++i.artifacts[0].sizeBytes; break; case 11: i.packageFinalized = false; break; case 12: i.location = c::PackageLocation::Staging; break;
        case 13: i.topology = c::Topology::Contradictory; break; case 14: i.sidecarPresent = true; break;
        case 15: process = cleanup ? PostCompletionCleanup(0, 99) : Safe(0, 99); break; case 16: process.progressForm = p::TerminalForm::NotStarted; break;
        case 17: process.lastReturnedAttempt.reset(); break; case 18: process.activeAttempt = p::AttemptIdentity{0, 99}; break;
        case 19: i.input->summary.processStatus = ev::Status::DeviceLost; break; case 20: i.input->samples.back().status = ev::Status::DeviceLost; break;
        case 21: process.exitCode = 4; break;
        }
        const auto r = c::ReconcileSlot(process, i); EXPECT_EQ(r.disposition, a::SlotDisposition::UnresolvedCampaignFatal) << n; EXPECT_FALSE(r.process);
        }
    }
}
TEST(Ex2Stage6SupervisorReconcile, OrdinarySuccessAndExactAttemptOneSlotFiveCleanup)
{
    PackageFixture f; f.Package(); const auto inspection = f.Inspect();
    const auto ordinary = c::ReconcileSlot(Safe(), inspection);
    EXPECT_EQ(ordinary.disposition, a::SlotDisposition::ResolvedSuccess); EXPECT_EQ(ordinary.reason, "resolved_success");
    // Independently inspect canonical bytes for the exact cell/backend/block/order/process of slot 5.
    const auto foundation = c::ReconstructFoundation(f.manifest, 5);
    const auto final = f.temp.root / "results/local" / foundation.Identity().runId;
    std::filesystem::create_directory(final);
    for (const auto& [name, bytes] : Bundle(foundation)) Write(final / name, bytes);
    const auto result = c::ReconcileSlot(PostCompletionCleanup(5), c::InspectPackage(f.manifest, 5, f.temp.root));
    EXPECT_EQ(result.disposition, a::SlotDisposition::ResolvedSuccess);
    EXPECT_EQ(result.reason, "resolved_success_post_completion_cleanup"); ASSERT_TRUE(result.process);
    EXPECT_EQ(result.process->TerminalState(), a::ProcessTerminalState::Success);
    EXPECT_EQ(result.process->Summary().successfulSampleCount, 100);
}
TEST(Ex2Stage6SupervisorReconcile, CleanupSafetyAndExitMutationsRemainFatal)
{
    PackageFixture f; f.Package(); const auto inspection = f.Inspect();
    for (int n = 0; n < 20; ++n) {
        auto process = PostCompletionCleanup();
        switch (n) {
        case 0: process.terminateJobSucceeded = false; break; case 1: process.jobEmptyConfirmed = false; break;
        case 2: process.jobActiveProcesses = 1; break; case 3: process.jobActiveProcesses.reset(); break;
        case 4: process.jobTotalProcesses = 0; break; case 5: process.primaryTerminationConfirmed = false; break;
        case 6: process.exitCode = 3; break; case 7: process.exitCode.reset(); break;
        case 8: process.exitKind = c::ExitKind::Abnormal; break; case 9: process.exitKind = c::ExitKind::ProgressTransportAbort; break;
        case 10: process.exitKind = c::ExitKind::NotAvailable; break; case 11: process.containmentAssigned = false; break;
        case 12: process.containmentVerified = false; break; case 13: process.containmentVerificationFailed = true; break;
        case 14: process.controlError = "injected"; break; case 15: process.processCreated = false; break;
        case 16: process.processResumed = false; break; case 17: process.descendantSurvivalObserved = false; break;
        case 18: process.supervisorTerminationRequested = false; break; case 19: process.exitCode = 4; break;
        }
        const auto result = c::ReconcileSlot(process, inspection);
        EXPECT_EQ(result.disposition, a::SlotDisposition::UnresolvedCampaignFatal) << n; EXPECT_FALSE(result.process);
    }
    auto absentTotal = PostCompletionCleanup(); absentTotal.jobTotalProcesses.reset();
    EXPECT_EQ(c::ReconcileSlot(absentTotal, inspection).disposition, a::SlotDisposition::ResolvedSuccess);
}
TEST(Ex2Stage6SupervisorReconcile, CleanupCannotRescueTimeoutOrUnsafeProgress)
{
    PackageFixture f; f.Package(); const auto inspection = f.Inspect();
    const std::array reasons{"progress_protocol_invalid", "progress_protocol_invalid", "operation_timeout", "campaign_timeout", "child_timeout",
        "unsafe_progress_terminal", "unsafe_progress_terminal", "unsafe_progress_terminal", "unsafe_progress_terminal", "unsafe_progress_terminal", "unsafe_progress_terminal", "progress_sample_mismatch"};
    for (std::size_t n = 0; n < reasons.size(); ++n) {
        auto process = PostCompletionCleanup();
        switch (n) {
        case 0: process.progressInvalid = true; break; case 1: process.progressForm = p::TerminalForm::Invalid; break;
        case 2: process.operationTimedOut = true; break; case 3: process.campaignTimedOut = true; break; case 4: process.childTimedOut = true; break;
        case 5: process.progressTransportFailed = true; break; case 6: process.cleanEof = false; break; case 7: process.trailingBytes = 1; break;
        case 8: process.activeAttempt = p::AttemptIdentity{0, 99}; break; case 9: process.progressForm = p::TerminalForm::OutstandingStartedPrefix; break;
        case 10: process.progressForm = p::TerminalForm::NotStarted; break; case 11: process.observations[3].record.sampleIndex = 9; break;
        }
        const auto result = c::ReconcileSlot(process, inspection);
        EXPECT_EQ(result.disposition, a::SlotDisposition::UnresolvedCampaignFatal) << n; EXPECT_EQ(result.reason, reasons[n]);
    }
}
TEST(Ex2Stage6SupervisorReconcile, CleanupRequiresScientificSuccessAndKnownNativeCompletion)
{
    for (int kind = 1; kind <= 4; ++kind) {
        PackageFixture f; f.Package(kind); const auto inspection = f.Inspect();
        const auto ordinary = c::ReconcileSlot(Safe(0, kind == 4 ? 0 : 3), inspection);
        EXPECT_EQ(ordinary.disposition, a::SlotDisposition::ResolvedDiagnosticFailure); EXPECT_EQ(ordinary.reason, "resolved_diagnostic_failure");
        const auto result = c::ReconcileSlot(PostCompletionCleanup(0, kind == 4 ? 0 : 3), inspection);
        EXPECT_EQ(result.disposition, a::SlotDisposition::UnresolvedCampaignFatal); EXPECT_EQ(result.reason, "unsafe_process_control");
    }
    PackageFixture f; f.Package(3); Write(f.side, Sidecar(f.foundation, 0, true, 2));
    const auto uncertain = c::ReconcileSlot(PostCompletionCleanup(0, 3), f.Inspect());
    EXPECT_EQ(uncertain.disposition, a::SlotDisposition::UnresolvedCampaignFatal); EXPECT_EQ(uncertain.reason, "native_completion_uncertain");
}
TEST(Ex2Stage6SupervisorReconcile, CompleteStagingRecoveryPreservesEveryDiagnosticTemplate)
{
    for (int kind = 1; kind <= 4; ++kind) for (const auto category : {0, 4}) {
        PackageFixture f; f.Package(kind, true); Write(f.side, Sidecar(f.foundation, category));
        auto process = Safe(0, kind == 4 ? 0 : 3); process.exitCode = 4;
        const auto r = c::ReconcileSlot(process, f.Inspect()); EXPECT_EQ(r.disposition, a::SlotDisposition::ResolvedDiagnosticFailure);
        ASSERT_TRUE(r.process); EXPECT_EQ(r.process->TerminalState(), a::ProcessTerminalState::DiagnosticFailure); EXPECT_FALSE(std::filesystem::exists(f.final));
    }
}
struct Simulation
{
    Temporary temp; c::Manifest manifest{c::ParseManifest(ManifestBytes())}; c::RuntimePaths paths{temp.root};
    std::map<std::uint64_t, c::PackageInspection> packages;
    std::vector<std::uint64_t> launched;
    std::optional<std::uint64_t> fatalAt, driftAt;
    std::set<std::uint64_t> diagnosticAt, cleanupAt;
    std::function<void(const c::Ledger&)> onUpdate;
    std::function<void(std::string_view, const c::Ledger&)> fault;
    std::optional<std::uint64_t> failCell;
    bool failCampaign{}, terminalDrift{};
    std::uint64_t entered{}, lastRevision{};
    std::unique_ptr<c::Ledger> terminal;
    Simulation() { Write(temp.root / "results/local" / (manifest.id + "-stage6-manifest.json"), manifest.canonicalJson + "\n"); }
    ev::Plan Plan(std::uint64_t sequence)
    {
        const auto slot = s6::FrozenCampaignPlan()[sequence]; const auto& process = slot.process;
        ex2::SeriesIdentityContext identity{{"1.4", manifest.machineId, {manifest.gpuUuid, true}, s6::WorkloadForCell(slot.cellIndex), ex2::InstrumentMode::H},
            process.backend, process.processIndex, process.blockIndex, process.orderSlot, 0, 100, manifest.sourceRevision, manifest.childExecutableSha256, {}};
        if (process.backend == ex2::Backend::Vulkan && slot.cellIndex < 16) identity.shaderSha256 = manifest.shaderSha256[0];
        ev::Plan plan{slot, manifest.groups[sequence / 10].children[sequence % 10].sessionId, identity, ex2::ComparisonConditionId(identity.condition), ex2::SeriesId(identity)};
        ev::ValidatePlan(plan); return plan;
    }
    void Package(std::uint64_t sequence)
    {
        const auto plan = Plan(sequence); const auto directory = temp.root / "results/local" / plan.runId; std::filesystem::create_directory(directory);
        c::PackageInspection i; i.attempted = i.structurallyValid = i.packageFinalized = i.exactFileSet = i.scientificBundleParsed = i.scientificBundleValid = i.scientificBytesCanonical = true;
        i.topology = c::Topology::FinalOnly; i.location = c::PackageLocation::Final;
        for (const auto name : {"environment.json", "initialization.csv", "samples.csv", "summary.json"}) {
            // Deterministic inspection-service fixture; these are intentionally
            // not presented to the real semantic inspector as GPU evidence.
            const std::string bytes = std::string(name) + ":simulated:" + std::to_string(sequence); Write(directory / name, bytes);
            i.artifacts.push_back({name, (directory / name).lexically_relative(temp.root).generic_string(), bytes.size(), ex2::Sha256(bytes)});
        }
        i.packageSha256 = c::PackageHash(i.artifacts); std::vector<ev::SampleRecord> rows;
        for (std::uint64_t n = 0; n < (diagnosticAt.contains(sequence) ? 3 : 100); ++n) rows.push_back(Good(plan, n));
        if (diagnosticAt.contains(sequence)) { auto& row = rows.back(); row.status = ev::Status::ValidationFailed; row.correctness.validationPassed = false;
            row.failurePhase = ev::FailurePhase::Validation; row.errorCode = "output_mismatch"; }
        const auto& last = rows.back(); auto summary = ev::SummarizeSamples(plan, rows, last.status, last.failurePhase, last.errorCode);
        i.input = a::ProcessInput{*i.packageSha256, plan, std::move(rows), std::move(summary)}; packages.emplace(sequence, std::move(i));
    }
    c::RuntimeServices Services()
    {
        c::RuntimeServices services;
        services.collectFacts = [&](const auto&, const auto&) { auto f = Facts(manifest);
            if ((driftAt && entered == *driftAt + 1) || (terminalDrift && launched.size() == 220)) f.childSha256 = std::string(64, 'e'); return f; };
        services.runProcess = [&](const auto&, const auto&, std::uint64_t n, auto, auto) {
            EXPECT_EQ(entered, n + 1); EXPECT_EQ(launched.size(), n); launched.push_back(n); Package(n);
            auto r = cleanupAt.contains(n) ? PostCompletionCleanup(n, diagnosticAt.contains(n) ? 3 : 100) : Safe(n, diagnosticAt.contains(n) ? 3 : 100);
            if (fatalAt == n) { r.exitCode = 3; } return r;
        };
        services.inspectPackage = [&](const auto&, std::uint64_t n, const auto&) { return packages.at(n); };
        services.publishAnalysis = [&](const auto& root, const auto& path, auto bytes) {
            const auto name = path.filename().string();
            if ((failCampaign && name == "campaign-analysis.json") || (failCell && name == "cell-" + std::string(*failCell < 10 ? "0" : "") + std::to_string(*failCell) + "-analysis.json")) throw std::runtime_error("injected analysis publication fault");
            return c::PublishAnalysis(root, path, bytes);
        };
        services.afterDurableUpdate = [&](const c::Ledger& l) {
            EXPECT_EQ(l.revision, l.revision == 0 ? 0 : lastRevision + 1); lastRevision = l.revision;
            for (std::size_t n = 0; n < l.slots.size(); ++n) if (l.slots[n].phase == "launch_intent") {
                entered = n + 1; if (n) EXPECT_TRUE(l.slots[n - 1].disposition);
                if (n && n % 10 == 0 && !failCell) EXPECT_TRUE(l.cellAnalyses[n / 10 - 1]);
            }
            if (l.state == "completed" || l.state == "incomplete") terminal = std::make_unique<c::Ledger>(l);
            if (onUpdate) onUpdate(l);
        };
        services.publicationFault = [&](auto stage, const auto& l) { if (fault) fault(stage, l); }; return services;
    }
    c::ExitCode Run() { return c::ExecuteManifestWithServices(manifest, paths, Services()); }
};
TEST(Ex2Stage6SupervisorPreflight, EveryProvenanceMismatchLaunchesZeroAndCreatesNothing)
{
    for (int n = 0; n < 12; ++n) { Simulation f; auto services = f.Services(); services.collectFacts = [&](const auto&, const auto&) {
        auto facts = Facts(f.manifest); switch (n) { case 0: facts.sourceRevision = std::string(40, 'b'); break; case 1: facts.gitDirty = true; break;
        case 2: facts.childSha256 = "wrong"; break; case 3: facts.supervisorSha256 = "wrong"; break; case 4: facts.cudaUuid = "wrong"; break; case 5: facts.vulkanUuid = "wrong"; break;
        default: facts.shaderSha256[n - 6] = "wrong"; break; } return facts; };
        EXPECT_EQ(c::ExecuteManifestWithServices(f.manifest, f.paths, services), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_TRUE(f.launched.empty());
        EXPECT_FALSE(std::filesystem::exists(c::ControlPath(f.manifest, f.temp.root))); EXPECT_FALSE(std::filesystem::exists(c::AnalysisPath(f.manifest, f.temp.root)));
    }
}
TEST(Ex2Stage6SupervisorPreflight, EveryOutputNamespaceAndExistingLedgerRefusesAdoption)
{
    for (const auto suffix : {"", ".incomplete", ".failure.json"}) { Simulation f;
        Write(f.temp.root / "results/local" / (f.manifest.groups[21].children[9].sessionId + suffix), "collision");
        EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_TRUE(f.launched.empty()); }
    for (const auto suffix : {L"", L".incomplete", L".incomplete.tmp"}) { Simulation f; const auto path = std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + suffix);
        Write(path, "interrupted previous supervisor"); EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_TRUE(f.launched.empty()); EXPECT_EQ(Read(path), "interrupted previous supervisor"); }
    Simulation f; std::filesystem::create_directory(c::AnalysisPath(f.manifest, f.temp.root)); EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected);
}
TEST(Ex2Stage6SupervisorPreflight, RejectsManifestAndOutputAncestorReparseTraversalBeforeOpeningBytes)
{
    Simulation f; Temporary outside;
    TestJunction link(f.temp, "redirect", outside.root);
    Write(outside.root / "manifest.json", "deliberately malformed and must not be parsed");
    try { (void)c::LoadManifestFromRepository(f.temp.root, "redirect/manifest.json"); FAIL() << "reparse accepted"; }
    catch (const std::invalid_argument& error) { EXPECT_STREQ(error.what(), "reparse path is not supported"); }
    TestJunction output(f.temp, "results/local/" + f.manifest.groups[21].children[9].sessionId, outside.root);
    EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_TRUE(f.launched.empty());
}
TEST(Ex2Stage6SupervisorInspection, RejectsPackageDirectoryAndNestedFileReparseTraversal)
{
    PackageFixture f; Temporary outside;
    {
        TestJunction package(f.temp, "results/local/" + f.foundation.Identity().runId, outside.root);
        const auto i = f.Inspect(); EXPECT_FALSE(i.structurallyValid); EXPECT_FALSE(i.input);
    }
    f.Package(); std::filesystem::remove(f.final / "samples.csv");
    TestJunction file(f.temp, "results/local/" + f.foundation.Identity().runId + "/samples.csv", outside.root);
    const auto i = f.Inspect(); EXPECT_FALSE(i.structurallyValid); EXPECT_FALSE(i.input);
}
TEST(Ex2Stage6SupervisorSimulation, AllTwoHundredTwentySuccessesAndDurableAnalyses)
{
    Simulation f; EXPECT_EQ(f.Run(), c::ExitCode::Completed); ASSERT_EQ(f.launched.size(), 220); ASSERT_TRUE(f.terminal);
    const auto& l = *f.terminal; EXPECT_EQ(l.state, "completed"); EXPECT_TRUE(l.campaignAnalysis); EXPECT_FALSE(l.fatalReason);
    for (const auto& slot : l.slots) EXPECT_EQ(slot.disposition, a::SlotDisposition::ResolvedSuccess);
    for (const auto& anchor : l.cellAnalyses) EXPECT_TRUE(anchor);
    const auto bytes = Read(c::ControlPath(f.manifest, f.temp.root)); EXPECT_NO_THROW(c::ValidateLedgerBytes(bytes)); EXPECT_LT(bytes.size(), 32U * 1024U * 1024U);
    EXPECT_EQ(std::count(bytes.begin(), bytes.end(), '\n'), 1); EXPECT_EQ(bytes.find("host_completion_ns"), bytes.npos); EXPECT_EQ(bytes.find("median"), bytes.npos);
    EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_EQ(f.launched.size(), 220);
}
TEST(Ex2Stage6SupervisorSimulation, DiagnosticFailuresAtZeroAndOneThirtySevenContinue)
{
    Simulation f; f.diagnosticAt = {0, 9, 10, 137, 219}; EXPECT_EQ(f.Run(), c::ExitCode::Completed); ASSERT_TRUE(f.terminal); EXPECT_EQ(f.launched.size(), 220);
    for (const auto n : f.diagnosticAt) EXPECT_EQ(f.terminal->slots[n].disposition, a::SlotDisposition::ResolvedDiagnosticFailure);
}
TEST(Ex2Stage6SupervisorSimulation, SlotFiveCleanupLaunchesNextPredeclaredSlotWithoutRetry)
{
    Simulation f; f.cleanupAt = {5}; f.fatalAt = 6;
    EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal);
    EXPECT_EQ(f.launched, (std::vector<std::uint64_t>{0, 1, 2, 3, 4, 5, 6}));
    const auto& slot = f.terminal->slots[5];
    EXPECT_EQ(slot.disposition, a::SlotDisposition::ResolvedSuccess);
    EXPECT_EQ(slot.reason, "resolved_success_post_completion_cleanup"); EXPECT_EQ(slot.continuation, "launch_next");
    EXPECT_EQ(f.terminal->fatalSequence, 6);
}
TEST(Ex2Stage6SupervisorSimulation, FullCampaignWithCleanupRetainsFactsAndSuccessfulAnalyses)
{
    Simulation f; f.cleanupAt = {5, 137, 219};
    EXPECT_EQ(f.Run(), c::ExitCode::Completed); ASSERT_TRUE(f.terminal); ASSERT_EQ(f.launched.size(), 220);
    const auto& ledger = *f.terminal; EXPECT_EQ(ledger.state, "completed"); EXPECT_FALSE(ledger.fatalReason);
    for (std::size_t n = 0; n < 220; ++n) {
        EXPECT_EQ(f.launched[n], n); EXPECT_EQ(ledger.slots[n].disposition, a::SlotDisposition::ResolvedSuccess);
        EXPECT_EQ(ledger.slots[n].reason, f.cleanupAt.contains(n) ? "resolved_success_post_completion_cleanup" : "resolved_success");
    }
    for (const auto n : f.cleanupAt) {
        const auto& slot = ledger.slots[n]; const auto& process = slot.process;
        EXPECT_TRUE(process.descendantSurvivalObserved); EXPECT_TRUE(process.supervisorTerminationRequested);
        EXPECT_TRUE(process.terminateJobSucceeded); EXPECT_TRUE(process.jobEmptyConfirmed); EXPECT_EQ(process.jobActiveProcesses, 0);
        EXPECT_EQ(process.jobTotalProcesses, 17); EXPECT_EQ(process.exitKind, c::ExitKind::SupervisorForced); EXPECT_EQ(process.exitCode, 0);
        EXPECT_EQ(slot.continuation, n == 219 ? "schedule_complete" : "launch_next");
    }
    const auto bytes = Read(c::ControlPath(f.manifest, f.temp.root)); EXPECT_NO_THROW(c::ValidateLedgerBytes(bytes));
    EXPECT_NE(bytes.find("\"protocol_version\":\"1.4\""), bytes.npos);
    a::CampaignInput campaign;
    for (std::size_t cell = 0; cell < 22; ++cell) {
        ASSERT_TRUE(ledger.cellAnalyses[cell]); a::CellInput input; input.cellIndex = cell;
        for (std::size_t pos = 0; pos < 10; ++pos) input.slots[pos] = {a::SlotDisposition::ResolvedSuccess, a::DescribeProcess(*f.packages.at(cell * 10 + pos).input)};
        const auto description = a::DescribeCell(input); EXPECT_EQ(description.SuccessfulSlotCount(), 10);
        const auto cellBytes = Read(f.temp.root / ledger.cellAnalyses[cell]->relativePath);
        EXPECT_EQ(cellBytes, a::SerializeCellJson(description)); campaign.cellAnalysisSha256[cell] = ex2::Sha256(cellBytes);
        for (std::size_t pos = 0; pos < 10; ++pos) campaign.slots[cell * 10 + pos] = a::SlotDisposition::ResolvedSuccess;
    }
    ASSERT_TRUE(ledger.campaignAnalysis); const auto analysis = Read(f.temp.root / ledger.campaignAnalysis->relativePath);
    EXPECT_EQ(analysis, a::SerializeCampaignJson(a::DescribeCampaign(campaign)));
    EXPECT_NE(analysis.find("\"successful_slots\":220"), analysis.npos);
    for (const auto forbidden : {"speedup", "winner", "loser", "paired_ratio", "backend_rank", "cuda_over_vulkan", "vulkan_over_cuda"})
        EXPECT_EQ(analysis.find(forbidden), analysis.npos);
    EXPECT_NE(analysis.find("\"cross_backend_performance_admitted\":false"), analysis.npos);
    EXPECT_NE(analysis.find("\"stage2_authorized\":false"), analysis.npos);
    EXPECT_NE(analysis.find("\"production_backend_selected\":false"), analysis.npos);
}
TEST(Ex2Stage6SupervisorSimulation, FatalBoundariesHaveOneFatalAndAtomicUnlaunchedSuffix)
{
    for (const auto n : {0U, 9U, 10U, 137U, 219U}) { Simulation f; f.fatalAt = n; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete) << n;
        ASSERT_TRUE(f.terminal); EXPECT_EQ(f.launched.size(), n + 1); EXPECT_EQ(f.terminal->fatalSequence, n); const auto revision = f.terminal->slots[n].terminalRevision;
        for (std::size_t j = 0; j < 220; ++j) { const auto& s = f.terminal->slots[j];
            EXPECT_EQ(s.disposition, j < n ? a::SlotDisposition::ResolvedSuccess : j == n ? a::SlotDisposition::UnresolvedCampaignFatal : a::SlotDisposition::NotLaunched);
            if (j > n) { EXPECT_FALSE(s.enteredRevision); EXPECT_EQ(s.terminalRevision, revision); } }
        EXPECT_FALSE(f.terminal->cellAnalyses[n / 10]);
    }
}
TEST(Ex2Stage6SupervisorSimulation, DriftAtOneThirtySevenIsEnteredButNeverResumed)
{
    Simulation f; f.driftAt = 137; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal); EXPECT_EQ(f.launched.size(), 137);
    const auto& slot = f.terminal->slots[137]; EXPECT_TRUE(slot.enteredRevision); EXPECT_FALSE(slot.process.processResumed); EXPECT_EQ(slot.reason, "prelaunch_drift");
}
TEST(Ex2Stage6SupervisorSimulation, LedgerFailureAfterSlotFiftyStopsWithoutSecondWrite)
{
    Simulation f; unsigned failures{}; f.fault = [&](auto phase, const auto& l) {
        if (phase == "after_flush" && l.slots[50].phase == "resolved") { ++failures; throw std::runtime_error("injected ledger failure"); } };
    EXPECT_EQ(f.Run(), c::ExitCode::ControlPublicationFailure); EXPECT_EQ(f.launched.size(), 51); EXPECT_EQ(failures, 1);
    const auto final = c::ControlPath(f.manifest, f.temp.root); EXPECT_FALSE(std::filesystem::exists(final)); EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(final.wstring() + L".incomplete.tmp")));
    EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected);
}
TEST(Ex2Stage6SupervisorSimulation, CellAnalysisFailureEntersNextCellAndSkipsInvalidCampaignAnalysis)
{
    Simulation f; f.failCell = 0; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal); EXPECT_EQ(f.launched.size(), 10);
    EXPECT_EQ(f.terminal->fatalSequence, 10); EXPECT_EQ(f.terminal->fatalReason, "cell_analysis_publication_failure"); EXPECT_TRUE(f.terminal->slots[10].enteredRevision);
    EXPECT_FALSE(f.terminal->slots[10].process.processResumed); EXPECT_FALSE(f.terminal->campaignAnalysis);
}
TEST(Ex2Stage6SupervisorSimulation, CampaignAnalysisFailurePreservesAllResolvedSlots)
{
    Simulation f; f.failCampaign = true; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal); EXPECT_EQ(f.launched.size(), 220);
    EXPECT_FALSE(f.terminal->fatalSequence); EXPECT_EQ(f.terminal->fatalReason, "campaign_analysis_publication_failure"); EXPECT_FALSE(f.terminal->campaignAnalysis);
    for (const auto& slot : f.terminal->slots) EXPECT_EQ(slot.disposition, a::SlotDisposition::ResolvedSuccess);
}
TEST(Ex2Stage6SupervisorSimulation, OptionalIncompleteAnalysisFailurePreservesOriginalFatal)
{
    Simulation f; f.fatalAt = 0; f.failCampaign = true; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal);
    EXPECT_EQ(f.terminal->fatalSequence, 0); EXPECT_EQ(f.terminal->fatalReason, "unsafe_child_exit"); EXPECT_FALSE(f.terminal->campaignAnalysis);
}
TEST(Ex2Stage6SupervisorSimulation, LastCellPublicationFailureHasNoInventedSlot)
{
    Simulation f; f.failCell = 21; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal);
    EXPECT_EQ(f.launched.size(), 220); EXPECT_FALSE(f.terminal->fatalSequence); EXPECT_FALSE(f.terminal->cellAnalyses[21]);
    EXPECT_EQ(f.terminal->fatalReason, "cell_analysis_publication_failure");
    for (const auto& slot : f.terminal->slots) EXPECT_EQ(slot.disposition, a::SlotDisposition::ResolvedSuccess);
}
TEST(Ex2Stage6SupervisorSimulation, TerminalProvenanceDriftPreservesAllResolvedSlots)
{
    Simulation f; f.terminalDrift = true; EXPECT_EQ(f.Run(), c::ExitCode::CampaignIncomplete); ASSERT_TRUE(f.terminal);
    EXPECT_EQ(f.launched.size(), 220); EXPECT_FALSE(f.terminal->fatalSequence); EXPECT_EQ(f.terminal->fatalReason, "terminal_integrity_failure");
    for (const auto& slot : f.terminal->slots) EXPECT_EQ(slot.disposition, a::SlotDisposition::ResolvedSuccess);
}
TEST(Ex2Stage6SupervisorSimulation, FinalLedgerFailureAfterAllSlotsPreservesOwnedStaging)
{
    Simulation f; unsigned failures{};
    f.fault = [&](auto stage, const auto&) { if (stage == "before_final_rename") { ++failures; throw std::runtime_error("injected final publication failure"); } };
    EXPECT_EQ(f.Run(), c::ExitCode::ControlPublicationFailure); EXPECT_EQ(f.launched.size(), 220); EXPECT_EQ(failures, 1);
    EXPECT_FALSE(std::filesystem::exists(c::ControlPath(f.manifest, f.temp.root)));
    const auto bytes = Read(std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + L".incomplete"));
    EXPECT_NO_THROW(c::ValidateLedgerBytes(bytes)); EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected);
}
TEST(Ex2Stage6SupervisorSimulation, UnexpectedOwnerDeathLeavesDurableIncompleteAndForbidsResume)
{
    Simulation f; LARGE_INTEGER frequency{}, now{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&now);
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S6_TEST_CHILD,
        {"ledger-owner-death", "0", f.temp.root.string(), "results/local/" + f.manifest.id + "-stage6-manifest.json"},
        0, 1000, 10000, frequency.QuadPart, c::AddDeadline(now.QuadPart, c::DeadlineTicks(120000, frequency.QuadPart)));
    ASSERT_EQ(r.exitKind, c::ExitKind::Abnormal); ASSERT_TRUE(r.jobEmptyConfirmed);
    const auto retained = std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + L".incomplete");
    const auto bytes = Read(retained); EXPECT_NO_THROW(c::ValidateLedgerBytes(bytes));
    EXPECT_FALSE(std::filesystem::exists(c::ControlPath(f.manifest, f.temp.root)));
    EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected); EXPECT_TRUE(f.launched.empty()); EXPECT_EQ(Read(retained), bytes);
}
TEST(Ex2Stage6SupervisorLedger, PublicationFaultsNeverRetryOrAdopt)
{
    for (const auto stage : {"before_create", "after_flush", "before_replace", "after_replace", "after_reopen", "before_final_rename"}) { Simulation f; f.fatalAt = 0; unsigned faults{};
        f.fault = [&](auto phase, const auto& l) { if (phase == stage && (std::string_view(stage) == "before_final_rename" || l.revision == 0)) { ++faults; throw std::runtime_error("injected"); } };
        EXPECT_EQ(f.Run(), c::ExitCode::ControlPublicationFailure) << stage; EXPECT_EQ(faults, 1); EXPECT_FALSE(std::filesystem::exists(c::ControlPath(f.manifest, f.temp.root)));
        if (std::string_view(stage) != "before_create") EXPECT_EQ(f.Run(), c::ExitCode::ConfigurationOrPreflightRejected);
    }
}
TEST(Ex2Stage6SupervisorLedger, OwnedStagingMutationPreventsAnotherAuthoritativeWrite)
{
    Simulation f; f.onUpdate = [&](const auto& l) { if (l.revision == 0) Write(std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + L".incomplete"), "unowned"); };
    EXPECT_EQ(f.Run(), c::ExitCode::ControlPublicationFailure); EXPECT_TRUE(f.launched.empty());
    EXPECT_EQ(Read(std::filesystem::path(c::ControlPath(f.manifest, f.temp.root).wstring() + L".incomplete")), "unowned");
}
TEST(Ex2Stage6SupervisorLedger, RejectsSchemaRevisionIdentityAndOversizeMutations)
{
    Simulation f; std::string zero; f.fault = [&](auto phase, const auto& l) { if (phase == "before_create" && l.revision == 0) { zero = c::SerializeLedger(l); throw std::runtime_error("retain fixture"); } };
    EXPECT_EQ(f.Run(), c::ExitCode::ControlPublicationFailure); ASSERT_FALSE(zero.empty()); EXPECT_NO_THROW(c::ValidateLedgerBytes(zero));
    for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{{"\"ledger_revision\":0", "\"ledger_revision\":1"}, {"\"resume_policy\":\"forbidden\"", "\"resume_policy\":\"allowed\""},
        {"\"sequence_index\":219", "\"sequence_index\":218"}, {"\"control_state\":\"owned\"", "\"control_state\":\"completed\""}, {"\"entered_revision\":null", "\"entered_revision\":2"}})
        EXPECT_THROW(c::ValidateLedgerBytes(Replace(zero, before, after)), std::exception);
    EXPECT_THROW(c::ValidateLedgerBytes(std::string(32U * 1024U * 1024U + 1, ' ')), std::exception);
}
TEST(Ex2Stage6SupervisorLedger, RejectsMalformedNumericTranscriptAndNestedControlFacts)
{
    Simulation f; f.fatalAt = 0; ASSERT_EQ(f.Run(), c::ExitCode::CampaignIncomplete);
    const auto bytes = Read(c::ControlPath(f.manifest, f.temp.root));
    EXPECT_NE(bytes.find("{\"child_qpc\":1,\"event\":1,\"magic\":1396068419,\"receive_qpc\":1,\"reserved\":0,\"sample_index\":0,\"slot_sequence_index\":0,\"version\":1}"), bytes.npos);
    for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{
        {"\"child_qpc\":1,", "\"child_qpc\":1.0,"}, {"\"child_qpc\":1,", "\"child_qpc\": 1,"},
        {"\"event\":1,", "\"event\":-1,"}, {"\"event\":1,", "\"event\":4294967296,"},
        {"\"receive_qpc\":1,", "\"receive_qpc\":9223372036854775808,"},
        {"\"version\":1}", "\"version\":1,\"extra\":0}"}, {"\"job_active_processes\":0", "\"job_active_processes\":false"},
        {"\"trailing_bytes\":0", "\"trailing_bytes\":40"}, {"\"progress_form\":\"Full\"", "\"progress_form\":\"unknown\""},
        {"\"recorded_sample_count\":100", "\"recorded_sample_count\":101"}}) {
        SCOPED_TRACE(before); EXPECT_THROW(c::ValidateLedgerBytes(Replace(bytes, before, after)), std::exception);
    }
}
TEST(Ex2Stage6SupervisorInspection, PackageHashIgnoresDirectoryAndSidecarButNotRawBytes)
{
    PackageFixture f; f.Package(); const auto final = f.Inspect(); ASSERT_TRUE(final.packageSha256);
    std::filesystem::rename(f.final, f.staging); Write(f.side, Sidecar(f.foundation, 4)); const auto staging = f.Inspect(); EXPECT_EQ(final.packageSha256, staging.packageSha256);
    auto changed = final.artifacts; changed[0].sizeBytes += 1; EXPECT_NE(c::PackageHash(changed), final.packageSha256);
    changed = final.artifacts; changed[0].relativePath = "ignored/path"; EXPECT_EQ(c::PackageHash(changed), final.packageSha256);
}
TEST(Ex2Stage6SupervisorInspection, IndependentPackageHashGolden)
{
    std::vector<c::Artifact> inventory; unsigned index{};
    for (const auto name : {"environment.json", "initialization.csv", "samples.csv", "summary.json"}) {
        inventory.push_back({name, "irrelevant", index + 1, std::string(64, static_cast<char>('0' + index))}); ++index;
    }
    EXPECT_EQ(c::PackageHash(inventory), "15d7d612ebd699ec74aa81ed6f67ffd4f69743341919749fc65d3cf80850ef15");
}
TEST(Ex2Stage6SupervisorInspection, MutationBetweenIndependentPassesInvalidatesInput)
{
    PackageFixture f; f.Package(); const auto result = c::InspectPackage(f.manifest, 0, f.temp.root, c::InspectionPurpose::SupervisedCampaign,
        {[&] { Write(f.final / "samples.csv", Read(f.final / "samples.csv") + "\n"); }});
    EXPECT_TRUE(result.changedDuringInspection); EXPECT_FALSE(result.structurallyValid); EXPECT_FALSE(result.input);
}
TEST(Ex2Stage6SupervisorInspection, BoundedFileReadsAndImmutableAnalysisPublication)
{
    PackageFixture f; f.Package(); Write(f.final / "summary.json", std::string(16U * 1024U * 1024U + 1, 'x')); EXPECT_FALSE(f.Inspect().structurallyValid);
    const auto path = f.temp.root / "results/local" / "analysis.json"; const auto a = c::PublishAnalysis(f.temp.root, path, "fixture\n");
    EXPECT_EQ(a.sizeBytes, 8); EXPECT_EQ(a.sha256, ex2::Sha256("fixture\n"));
    EXPECT_THROW((void)c::PublishAnalysis(f.temp.root, path, "replacement\n"), std::exception); EXPECT_EQ(Read(path), "fixture\n");
}
TEST(Ex2Stage6SupervisorReconcile, CurrentI2SidecarCompatibilityTable)
{
    PackageFixture f; f.Package(3); Write(f.side, Sidecar(f.foundation)); const auto good = f.Inspect(); ASSERT_TRUE(good.sidecar);
    for (const auto exit : {0U, 2U, 3U, 4U, 5U}) for (const auto historical : {0U, 2U, 3U, 4U, 5U}) {
        auto process = Safe(0, 3); process.exitCode = exit; auto inspection = good; inspection.sidecar->exitCategory = historical;
        const bool compatible = exit == 0 ? historical == 0 : exit == 3 ? historical == 3 : exit == 4 ? historical == 0 || historical == 4 || historical == 5 : exit == 5 ? historical == 0 || historical == 5 : false;
        const auto result = c::ReconcileSlot(process, inspection); EXPECT_EQ(result.reason == "sidecar_exit_incompatible", !compatible) << exit << ":" << historical;
        if (exit != 0 || historical != 0) EXPECT_EQ(result.disposition, a::SlotDisposition::UnresolvedCampaignFatal);
    }
}
} // namespace
