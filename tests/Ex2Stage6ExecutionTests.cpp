#include "app/Ex2Stage6Execution.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Sha256.hpp"
#include <gtest/gtest.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>
#include <chrono>
#include <fstream>
#include <ranges>
#include <set>

namespace
{
namespace ex2 = computelab::ex2;
namespace s6 = ex2::stage6;
namespace run = s6::execution;
namespace ev = s6::evidence;
constexpr std::string_view uuid = "00112233-4455-6677-8899-aabbccddeeff";
run::Configuration Config()
{ return {0, 0, 0, 0, "test-machine", "s6-i2-test", std::string(uuid)}; }
std::vector<std::string> Args()
{ return {"--cell-index", "0", "--plan-index", "0", "--cuda-device", "0", "--vulkan-device", "0", "--machine-id", "test-machine", "--session-id", "s6-i2-test", "--expected-gpu-uuid", std::string(uuid)}; }
run::Configuration Parse(const std::vector<std::string>& strings)
{ std::vector<std::string_view> views; for (const auto& s : strings) views.push_back(s); return run::ParseConfiguration(views); }
ev::Foundation Foundation()
{
    const auto slot = run::ResolveSlot(Config()); const auto& w = s6::WorkloadForCell(0);
    const auto input = ex2::GenerateWordInput(w.common.seed, 256);
    return ev::MakeWordFoundation({slot, "s6-i2-test", "test-machine", {std::string(uuid), true}, std::string(40, 'a'), std::string(64, 'b'), {}},
        ex2::evidence::MakeI7WordInputIdentity(w, input), ex2::ReferenceA1(input));
}
ex2::HostTimingIntervals H()
{ return {ex2::HostTimingStatus::Ok, 10, 20, 30}; }
ev::SampleRecord Good(const ev::Foundation& f, std::uint64_t index = 0)
{ return run::MapAttempt(f, index, {true, true, true, true, true}, H(), {}); }
computelab::results::EnvironmentRecord Common(const ev::Foundation& f)
{
    return {2, "EX-2", f.Identity().runId, "2026-10-05T00:00:00Z", std::string(40, 'a'), true, "test-machine", "Windows", "11", "fixture CPU", 1024,
        "fixture GPU", "NVIDIA", "10DE:0000", 1024, "driver", "toolkit", "runtime", "capability", {}, {}, "MSVC", "compiler", "cmake", "ninja", "x64-debug", "Debug", true, false};
}
ev::EnvironmentRecord Environment(const ev::Foundation& f)
{ return ev::MakeEnvironmentRecord(Common(f), f, {.implementation = "test-cuda", .streamFlags = "nonblocking"}); }
run::Artifacts Package(const ev::Foundation& f, std::vector<ev::SampleRecord> rows, bool failedSetup = false)
{
    auto init = ev::MakeSetupCompleteInitialization(f, "CPU-only fixture ready");
    ev::Status status = ev::Status::Ok; std::optional<ev::FailurePhase> phase; std::optional<std::string> code;
    if (failedSetup) { init.metric = "initialization_failed"; status = ev::Status::Incomplete; phase = ev::FailurePhase::BackendInitialization; code = "backend_initialization_failed"; }
    else if (!rows.empty()) { status = rows.back().status; phase = rows.back().failurePhase; code = rows.back().errorCode; }
    const auto summary = ev::SummarizeSamples(f.Identity(), rows, status, phase, code);
    return run::SerializePackage(f, Environment(f), {&init, 1}, rows, summary);
}
std::vector<ev::SampleRecord> Hundred(const ev::Foundation& f)
{ std::vector<ev::SampleRecord> rows; for (std::uint64_t i = 0; i < 100; ++i) rows.push_back(Good(f, i)); return rows; }
class Temporary
{
public:
    Temporary()
    {
        root = std::filesystem::temp_directory_path() / ("computelab-s6-i2-" + std::to_string(GetCurrentProcessId()) + "-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("unique test root unavailable");
        paths = run::MakeSessionPaths({root}, "s6-i2-test");
    }
    ~Temporary() { std::error_code e; std::filesystem::remove_all(root, e); }
    std::filesystem::path root;
    run::SessionPaths paths;
};
run::FailureSidecar Sidecar(const ev::Foundation& f)
{ return {"s6-i2-test", 0, true, f.Identity().runId, f.Identity().seriesId, std::string(40, 'a'), run::ExitCode::Completed,
    "backend-execution", "native_failure", 0, false, "completion wait", "-2", "bounded native failure"}; }

TEST(Ex2Stage6ExecutionCli, AcceptsOnlyTheExactSevenOptions)
{
    const auto c = Parse(Args());
    EXPECT_EQ(c.sessionId, "s6-i2-test");
    EXPECT_EQ(c.machineId, "test-machine");
    EXPECT_EQ(c.expectedGpuUuid, uuid);
    EXPECT_EQ(c.cellIndex, 0);
    EXPECT_EQ(c.planIndex, 0);
    auto a = Args(); std::rotate(a.begin(), a.begin() + 4, a.end());
    EXPECT_NO_THROW((void)Parse(a));
}
TEST(Ex2Stage6ExecutionCli, RejectsMissingDuplicateUnknownAndScientificOverrides)
{
    for (const std::string option : {"--backend", "--run-id", "--sample-count", "--warmup-count", "--instrument-mode", "--seed", "--shader", "--output", "--help"})
    { auto a = Args(); a[0] = option;
    EXPECT_THROW((void)Parse(a), std::invalid_argument) << option; }
    auto a = Args(); a[2] = a[0];
    EXPECT_THROW((void)Parse(a), std::invalid_argument);
    a = Args(); a.pop_back();
    EXPECT_THROW((void)Parse(a), std::invalid_argument);
    a = Args(); a.push_back("--help");
    EXPECT_THROW((void)Parse(a), std::invalid_argument);
}
TEST(Ex2Stage6ExecutionCli, RejectsMalformedOverflowAndOutOfRangeNumbers)
{
    for (const auto field : {1U, 3U, 5U, 7U}) for (const std::string value : {"", "-1", "+1", "1x", "1.0", " 1", "18446744073709551616"})
    { auto a = Args(); a[field] = value;
    EXPECT_THROW((void)Parse(a), std::invalid_argument) << field << value; }
    for (const auto [field, value] : std::array<std::pair<unsigned, const char*>, 4>{{{1, "22"}, {3, "10"}, {5, "2147483648"}, {7, "4294967296"}}})
    { auto a = Args(); a[field] = value;
    EXPECT_THROW((void)Parse(a), std::invalid_argument); }
}
TEST(Ex2Stage6ExecutionCli, RejectsUnsafeIdentifiersAndUuid)
{
    for (const auto field : {9U, 11U}) for (const std::string value : {"", "../escape", "a/b", "C:\\secret", "..", ".hidden", "CON", "AUX", "NUL", "LPT1", "CON.txt", "com1.data", "a\nb"})
    { auto a = Args(); a[field] = value;
    EXPECT_THROW((void)Parse(a), std::invalid_argument) << value; }
    for (const std::string value : {"", "00000000-0000-0000-0000-000000000000", "00112233-4455-6677-8899-AABBCCDDEEFF", "00112233445566778899aabbccddeeff", "00112233-4455-6677-8899-aabbccddeefg"})
    { auto a = Args(); a[13] = value;
    EXPECT_THROW((void)Parse(a), std::invalid_argument); }
}
TEST(Ex2Stage6ExecutionSlot, ExhaustsAll220FrozenSlotsWithoutExecutingThem)
{
    const auto campaign = s6::FrozenCampaignPlan(); std::set<std::uint64_t> sequences;
    for (std::size_t cell = 0; cell < 22; ++cell) for (std::size_t plan = 0; plan < 10; ++plan)
    { auto c = Config(); c.cellIndex = cell; c.planIndex = plan; const auto slot = run::ResolveSlot(c);
        EXPECT_EQ(slot, campaign[cell * 10 + plan]);
    EXPECT_EQ(slot.process, s6::FrozenCellProcessPlan()[plan]); sequences.insert(slot.sequenceIndex); }
    EXPECT_EQ(sequences.size(), 220);
    EXPECT_EQ(campaign.front().sequenceIndex, 0);
    EXPECT_EQ(campaign.back().sequenceIndex, 219);
}
TEST(Ex2Stage6ExecutionProvenance, ShaderApplicabilityAcrossEveryFrozenRoute)
{
    const run::RuntimePaths p{{}, {}, "a1", "a2", "b1", "b2", "c", "d1"};
    for (std::size_t cell = 0; cell < 22; ++cell)
    { auto cfg = Config(); cfg.cellIndex = cell; cfg.planIndex = 0;
    EXPECT_FALSE(run::ApplicableShader(run::ResolveSlot(cfg), p));
        cfg.planIndex = 1; const auto shader = run::ApplicableShader(run::ResolveSlot(cfg), p);
        if (cell >= 16) EXPECT_FALSE(shader);
        else { ASSERT_TRUE(shader); const std::string expected = cell < 3 ? "a1" : cell < 5 ? "a2" : cell < 8 ? "b1" : cell < 11 ? "b2" : cell < 14 ? "c" : "d1";
    EXPECT_EQ(shader->string(), expected); } }
}
TEST(Ex2Stage6ExecutionProvenance, RejectsEveryIdentityAndProvenanceMismatch)
{
    EXPECT_NO_THROW(run::RequireGpuIdentity(uuid, uuid, uuid));
    EXPECT_THROW(run::RequireGpuIdentity(uuid, "10112233-4455-6677-8899-aabbccddeeff", uuid), std::invalid_argument);
    EXPECT_THROW(run::RequireGpuIdentity(uuid, uuid, "10112233-4455-6677-8899-aabbccddeeff"), std::invalid_argument);
    EXPECT_THROW((void)run::FormatUuid({}), std::invalid_argument);
    const run::Provenance a{std::string(40, 'a'), true, std::string(64, 'b'), {}};
    EXPECT_NO_THROW(run::RequireSameProvenance(a, a));
    for (unsigned i = 0; i < 4; ++i) { auto b = a; if (i == 0) b.sourceRevision[0] = 'b'; if (i == 1) b.dirty = false;
        if (i == 2) b.executableSha256[0] = 'c'; if (i == 3) b.shaderSha256 = std::string(64, 'd');
    EXPECT_THROW(run::RequireSameProvenance(a, b), std::invalid_argument); }
}
TEST(Ex2Stage6ExecutionProvenance, RejectsLoadedShaderDriftAndWrongNames)
{
    const std::array<std::uint32_t, 2> bytes{0x07230203, 1}; auto cfg = Config(); cfg.planIndex = 1;
    const auto& w = s6::WorkloadForCell(0); const auto input = ex2::GenerateWordInput(w.common.seed, 256);
    const auto f = ev::MakeWordFoundation({run::ResolveSlot(cfg), cfg.sessionId, cfg.machineId, {std::string(uuid), true}, std::string(40, 'a'), std::string(64, 'b'), ex2::Sha256(std::as_bytes(std::span(bytes)))},
        ex2::evidence::MakeI7WordInputIdentity(w, input), ex2::ReferenceA1(input));
    EXPECT_NO_THROW(run::RequireLoadedShader(f, bytes, "Ex2A1.comp.spv"));
    EXPECT_THROW(run::RequireLoadedShader(f, {}, "Ex2A1.comp.spv"), std::invalid_argument);
    EXPECT_THROW(run::RequireLoadedShader(f, bytes, "Ex2A2.comp.spv"), std::invalid_argument);
    auto changed = bytes; changed[1]++;
    EXPECT_THROW(run::RequireLoadedShader(f, changed), std::invalid_argument);
    EXPECT_EQ(f.Identity().runId, cfg.sessionId);
}
TEST(Ex2Stage6ExecutionMapping, SuccessMismatchAndCompletedReadbackFailure)
{
    const auto f = Foundation();
    EXPECT_EQ(Good(f).status, ev::Status::Ok);
    const auto mismatch = run::MapAttempt(f, 0, {true, true, true, true, false}, H(), {});
    EXPECT_EQ(mismatch.status, ev::Status::ValidationFailed);
    EXPECT_EQ(mismatch.errorCode, "output_mismatch");
    EXPECT_EQ(mismatch.hostCompletionNanoseconds, 30);
    for (const bool lost : {false, true})
    { const auto r = run::MapAttempt(f, 0, {true, true, false, false, {}}, H(), run::FailureContext{ev::FailurePhase::Readback, false, lost});
        EXPECT_EQ(r.status, lost ? ev::Status::DeviceLost : ev::Status::Incomplete);
    EXPECT_EQ(r.hostCompletionNanoseconds, 30);
    EXPECT_FALSE(r.correctness.outputObserved); }
}
TEST(Ex2Stage6ExecutionMapping, PreCompletionFailuresDiscardAllPartialH)
{
    const auto f = Foundation();
    for (const auto phase : {ev::FailurePhase::Submission, ev::FailurePhase::CompletionWait}) for (unsigned kind = 0; kind < 3; ++kind)
    { const auto r = run::MapAttempt(f, 0, {true, false, false, false, {}}, H(), run::FailureContext{phase, kind == 1, kind == 2});
        EXPECT_EQ(r.status, kind == 1 ? ev::Status::Timeout : kind == 2 ? ev::Status::DeviceLost : phase == ev::FailurePhase::Submission ? ev::Status::SubmitFailed : ev::Status::WaitFailed);
        EXPECT_FALSE(r.hostSubmissionNanoseconds);
    EXPECT_FALSE(r.hostWaitNanoseconds);
    EXPECT_FALSE(r.hostCompletionNanoseconds);
    EXPECT_FALSE(r.nativeDeviceIntervalNanoseconds); }
}
TEST(Ex2Stage6ExecutionMapping, PreparationIsNotMeasuredOperationTimeout)
{
    const auto f = Foundation();
    for (const bool allocation : {false, true})
    { const auto r = run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, run::FailureContext{ev::FailurePhase::BackendInitialization, true, false, allocation});
        EXPECT_EQ(r.status, ev::Status::Incomplete);
    EXPECT_EQ(r.failurePhase, allocation ? ev::FailurePhase::ResourceAllocation : ev::FailurePhase::BackendInitialization);
    EXPECT_FALSE(r.hostCompletionNanoseconds); }
}
TEST(Ex2Stage6ExecutionMapping, InvalidCompletedHAndImpossibleProgressRemainInternal)
{
    const auto f = Foundation();
    for (unsigned i = 0; i < 5; ++i)
    { auto h = H(); if (i == 0) h.status = ex2::HostTimingStatus::TimestampInvalid; if (i == 1) h.hostSubmissionNanoseconds.reset();
        if (i == 2) h.hostWaitNanoseconds.reset(); if (i == 3) h.hostCompletionNanoseconds = 31; if (i == 4) { h.hostSubmissionNanoseconds = UINT64_MAX; h.hostWaitNanoseconds = 1; h.hostCompletionNanoseconds = 0; }
        EXPECT_THROW((void)run::MapAttempt(f, 0, {true, true, true, true, true}, h, {}), std::invalid_argument); }
    EXPECT_THROW((void)run::MapAttempt(f, 0, {true, true, false, false, {}}, H(), {}), std::invalid_argument);
    EXPECT_THROW((void)run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, run::FailureContext{ev::FailurePhase::EvidencePublication}), std::invalid_argument);
}
struct ObserverState
{
    std::vector<ev::SampleRecord>* rows{}; bool prepared{}; unsigned started{}, returned{};
    static void Call(void* context, const run::AttemptIdentity& i, run::AttemptEvent e) noexcept
    {
        auto& s = *static_cast<ObserverState*>(context);
    EXPECT_EQ(i.slotSequenceIndex, 0);
    EXPECT_EQ(i.sampleIndex, 0);
        if (e == run::AttemptEvent::Started) { EXPECT_FALSE(s.prepared);
    EXPECT_TRUE(s.rows->empty()); ++s.started; }
        else { EXPECT_TRUE(s.prepared);
    EXPECT_EQ(s.rows->size(), 1); ++s.returned; }
    }
};
TEST(Ex2Stage6ExecutionObserver, ExactOrderIdentityAndTerminalRowRetention)
{
    const auto f = Foundation(); std::vector<ev::SampleRecord> rows; ObserverState s{&rows};
    run::RetainObservedAttempt({0, 0}, {&s, ObserverState::Call}, [&] { s.prepared = true; return run::MapAttempt(f, 0, {true, true, true, true, false}, H(), {}); }, rows);
    EXPECT_EQ(s.started, 1);
    EXPECT_EQ(s.returned, 1);
    EXPECT_EQ(rows.front().status, ev::Status::ValidationFailed);
    EXPECT_THROW(run::RetainObservedAttempt({0, 1}, {}, [&] { return Good(f, 1); }, rows), std::invalid_argument);
}
TEST(Ex2Stage6ExecutionObserver, UnrepresentableAttemptHasNoReturnedOrRow)
{
    std::vector<ev::SampleRecord> rows; ObserverState s{&rows}; const auto f = Foundation();
    EXPECT_THROW(run::RetainObservedAttempt({0, 0}, {&s, ObserverState::Call}, [&]() -> ev::SampleRecord { s.prepared = true; throw std::logic_error("unrepresentable"); }, rows), std::logic_error);
    EXPECT_EQ(s.started, 1);
    EXPECT_EQ(s.returned, 0);
    EXPECT_TRUE(rows.empty());
    run::AttemptObserver{}.Report({0, 0}, run::AttemptEvent::Started);
    run::RetainObservedAttempt({0, 0}, {}, [&] { return Good(f); }, rows);
    EXPECT_EQ(rows.size(), 1);
}
TEST(Ex2Stage6ExecutionObserver, RepresentableUncertainWaitReturnsAndStops)
{
    const auto f = Foundation(); std::vector<ev::SampleRecord> rows; ObserverState s{&rows};
    run::RetainObservedAttempt({0, 0}, {&s, ObserverState::Call}, [&] { s.prepared = true; return run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, run::FailureContext{ev::FailurePhase::CompletionWait, true, false, false, true}); }, rows);
    EXPECT_EQ(s.returned, 1);
    EXPECT_EQ(rows.front().status, ev::Status::Timeout);
}
TEST(Ex2Stage6ExecutionExit, ExactCodesAndExhaustiveSafetyPrecedence)
{
    const std::array codes{run::ExitCode::Completed, run::ExitCode::PreFoundationFailure, run::ExitCode::InternalFailure, run::ExitCode::PublicationFailure, run::ExitCode::NativeStateUncertain};
    EXPECT_EQ(static_cast<int>(codes[0]), 0);
    EXPECT_EQ(static_cast<int>(codes[1]), 2);
    EXPECT_EQ(static_cast<int>(codes[2]), 5);
    EXPECT_EQ(static_cast<int>(codes[3]), 4);
    EXPECT_EQ(static_cast<int>(codes[4]), 3);
    for (std::size_t a = 0; a < codes.size(); ++a) for (std::size_t b = 0; b < codes.size(); ++b) EXPECT_EQ(run::DominantExit(codes[a], codes[b]), codes[std::max(a, b)]);
}
TEST(Ex2Stage6ExecutionPackage, PathsCollisionAndTraversalFirewall)
{
    Temporary t;
    EXPECT_EQ(t.paths.finalDirectory, t.root / "results/local/s6-i2-test");
    EXPECT_EQ(t.paths.stagingDirectory, t.root / "results/local/s6-i2-test.incomplete");
    EXPECT_EQ(t.paths.failureSidecar, t.root / "results/local/s6-i2-test.failure.json");
    EXPECT_THROW((void)run::MakeSessionPaths({t.root}, "../escape"), std::invalid_argument);
    for (unsigned i = 0; i < 3; ++i) { Temporary one; std::filesystem::create_directories(one.paths.finalDirectory.parent_path());
        const auto p = i == 0 ? one.paths.finalDirectory : i == 1 ? one.paths.stagingDirectory : one.paths.failureSidecar;
        if (i < 2) std::filesystem::create_directory(p); else std::ofstream(p) << "preexisting";
        EXPECT_THROW(run::RequireUnusedSession(one.paths), std::invalid_argument);
    EXPECT_THROW(run::CreateStaging(one.paths), std::invalid_argument); }
}
TEST(Ex2Stage6ExecutionPackage, StagingRemainsEmptyThroughoutMemoryRetention)
{
    Temporary t; run::CreateStaging(t.paths); const auto f = Foundation(); std::vector<ev::SampleRecord> rows;
    for (std::uint64_t j = 0; j < 100; ++j) { run::RetainObservedAttempt({0, j}, {}, [&] { EXPECT_TRUE(std::filesystem::is_empty(t.paths.stagingDirectory)); return Good(f, j); }, rows); }
    EXPECT_EQ(rows.size(), 100);
    EXPECT_TRUE(std::filesystem::is_empty(t.paths.stagingDirectory));
}
TEST(Ex2Stage6ExecutionPackage, ExactFourFilesSuccessFinalRenameAndNoOverwrite)
{
    Temporary t; const auto f = Foundation(); const auto a = Package(f, Hundred(f)); run::CreateStaging(t.paths);
    run::WriteStagedPackage(t.paths, a);
    EXPECT_NO_THROW(run::VerifyStagedPackage(t.paths, a));
    EXPECT_THROW(run::WriteStagedPackage(t.paths, a), std::invalid_argument); run::FinalizePackage(t.paths);
    EXPECT_TRUE(std::filesystem::is_directory(t.paths.finalDirectory));
    EXPECT_FALSE(std::filesystem::exists(t.paths.stagingDirectory));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(t.paths.finalDirectory), {}), 4);
}
TEST(Ex2Stage6ExecutionPackage, FinalizesShortTerminalDiagnosticAndTruthfulZeroSampleSetupFailure)
{
    const auto f = Foundation();
    for (const bool zero : {false, true}) { Temporary t; std::vector<ev::SampleRecord> rows;
        if (!zero) rows.push_back(run::MapAttempt(f, 0, {true, true, true, true, false}, H(), {}));
        const auto a = Package(f, rows, zero); run::CreateStaging(t.paths); run::WriteStagedPackage(t.paths, a); run::VerifyStagedPackage(t.paths, a); run::FinalizePackage(t.paths);
        EXPECT_TRUE(std::filesystem::is_directory(t.paths.finalDirectory));
    EXPECT_NE(a.at("summary.json").find(zero ? "\"recorded_sample_count\":0" : "\"recorded_sample_count\":1"), std::string::npos); }
}
TEST(Ex2Stage6ExecutionPackage, NeverFabricatesConstructorOrIncompleteSetupEvidence)
{
    const auto f = Foundation(); const auto e = Environment(f); auto init = ev::MakeSetupCompleteInitialization(f, "ready");
    const auto summary = ev::SummarizeSamples(f.Identity(), {}, ev::Status::Incomplete, ev::FailurePhase::BackendInitialization, "backend_initialization_failed");
    EXPECT_THROW((void)run::SerializePackage(f, e, {}, {}, summary), std::invalid_argument);
    EXPECT_THROW((void)run::SerializePackage(f, e, {&init, 1}, {}, summary), std::invalid_argument);
    init.metric = "initialization_failed"; const auto rows = Hundred(f); const auto ok = ev::SummarizeSamples(f.Identity(), rows, ev::Status::Ok);
    EXPECT_THROW((void)run::SerializePackage(f, e, {&init, 1}, rows, ok), std::invalid_argument);
}
TEST(Ex2Stage6ExecutionPackage, MutationTruncationMissingExtraAndNestedAreRejected)
{
    const auto f = Foundation(); const auto a = Package(f, Hundred(f));
    for (unsigned i = 0; i < 5; ++i) { Temporary t; run::CreateStaging(t.paths); run::WriteStagedPackage(t.paths, a);
        const auto samples = t.paths.stagingDirectory / "samples.csv";
        if (i == 0) std::ofstream(samples, std::ios::binary | std::ios::app) << "mutated";
        if (i == 1) std::ofstream(samples, std::ios::binary | std::ios::trunc) << "truncated";
        if (i == 2) std::filesystem::remove(samples);
        if (i == 3) std::ofstream(t.paths.stagingDirectory / "extra.txt") << "extra";
        if (i == 4) std::filesystem::create_directory(t.paths.stagingDirectory / "nested");
        EXPECT_ANY_THROW(run::VerifyStagedPackage(t.paths, a));
    EXPECT_TRUE(std::filesystem::is_directory(t.paths.stagingDirectory)); }
}
TEST(Ex2Stage6ExecutionPackage, RenameCollisionRetainsIncompleteAndExistingDestination)
{
    Temporary t; const auto a = Package(Foundation(), Hundred(Foundation())); run::CreateStaging(t.paths); run::WriteStagedPackage(t.paths, a); run::VerifyStagedPackage(t.paths, a);
    std::filesystem::create_directory(t.paths.finalDirectory); std::ofstream(t.paths.finalDirectory / "owned.txt") << "preexisting";
    EXPECT_THROW(run::FinalizePackage(t.paths), std::invalid_argument);
    EXPECT_TRUE(std::filesystem::exists(t.paths.finalDirectory / "owned.txt"));
    EXPECT_TRUE(std::filesystem::exists(t.paths.stagingDirectory));
}
TEST(Ex2Stage6ExecutionSidecar, ControlOnlyStaticCoordinateAndCreateOnce)
{
    Temporary t; const auto f = Foundation(); const auto failure = Sidecar(f); run::CreateStaging(t.paths); const auto json = run::SerializeFailureSidecar(failure);
    for (const std::string key : {"host_submission_ns", "host_wait_ns", "host_completion_ns", "native_device_interval_ns", "validation_passed", "process_status", "backend", "cell_index", "process_index", "block_index", "order_slot", "plan_index", "output_sha256", "median"})
        EXPECT_EQ(json.find('"' + key + '"'), std::string::npos) << key;
    EXPECT_NE(json.find("\"slot_sequence_index\":0"), std::string::npos);
    run::WriteFailureSidecar(t.paths, failure); std::ifstream stream(t.paths.failureSidecar, std::ios::binary); const std::string disk{std::istreambuf_iterator<char>(stream), {}};
    EXPECT_EQ(disk, json);
    EXPECT_THROW(run::WriteFailureSidecar(t.paths, failure), std::runtime_error);
}
TEST(Ex2Stage6ExecutionSidecar, RejectsUnsafeDetailsAndFoundationContradictions)
{
    auto f = Sidecar(Foundation()); f.nativeDetail = "C:\\Users\\secret";
    EXPECT_THROW((void)run::SerializeFailureSidecar(f), std::invalid_argument);
    f = Sidecar(Foundation()); f.foundationEstablished = false;
    EXPECT_THROW((void)run::SerializeFailureSidecar(f), std::invalid_argument);
    f = Sidecar(Foundation()); f.runId.reset();
    EXPECT_THROW((void)run::SerializeFailureSidecar(f), std::invalid_argument);
}
TEST(Ex2Stage6ExecutionSidecar, CoexistsWithFourFileFinalPackage)
{
    Temporary t; const auto f = Foundation(); const auto a = Package(f, Hundred(f)); run::CreateStaging(t.paths); run::WriteFailureSidecar(t.paths, Sidecar(f));
    run::WriteStagedPackage(t.paths, a); run::VerifyStagedPackage(t.paths, a); run::FinalizePackage(t.paths);
    EXPECT_TRUE(std::filesystem::exists(t.paths.failureSidecar));
    EXPECT_TRUE(std::filesystem::exists(t.paths.finalDirectory));
    EXPECT_FALSE(std::filesystem::exists(t.paths.stagingDirectory));
}
TEST(Ex2Stage6ExecutionMapping, NativeFailureSafetyDoesNotInventScientificDeviceLoss)
{
    const auto f = Foundation();
    const auto cuda = run::CudaFailureContext(ev::FailurePhase::CompletionWait, "completion wait", 700, "cudaErrorIllegalAddress");
    EXPECT_TRUE(cuda.completionUncertain);
    EXPECT_FALSE(cuda.deviceLost);
    const auto row = run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, cuda);
    EXPECT_EQ(row.status, ev::Status::WaitFailed);
    EXPECT_EQ(row.errorCode, "completion_failed");
    const auto safe = run::VulkanFailureContext(ev::FailurePhase::Submission, "submission", -2, "vkQueueSubmit2");
    EXPECT_TRUE(safe.allocation);
    EXPECT_FALSE(safe.completionUncertain);
    const auto wait = run::VulkanFailureContext(ev::FailurePhase::CompletionWait, "completion wait", -2, "vkWaitForFences");
    EXPECT_TRUE(wait.completionUncertain);
    const auto recording = run::VulkanFailureContext(ev::FailurePhase::Submission, "command recording", -2, "vkEndCommandBuffer");
    EXPECT_FALSE(recording.completionUncertain);
    const auto recordingRow = run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, recording);
    EXPECT_EQ(recordingRow.status, ev::Status::SubmitFailed);
    const auto lost = run::VulkanFailureContext(ev::FailurePhase::Readback, "output readback", -4, "vkQueueSubmit2");
    EXPECT_TRUE(lost.deviceLost);
    EXPECT_TRUE(lost.completionUncertain);
    const auto prep = run::VulkanFailureContext(ev::FailurePhase::BackendInitialization, "preparation completion", 2, "vkWaitForFences");
    EXPECT_TRUE(prep.timeout);
    const auto prepRow = run::MapAttempt(f, 0, {true, false, false, false, {}}, {}, prep);
    EXPECT_EQ(prepRow.status, ev::Status::Incomplete);
    EXPECT_EQ(prepRow.errorCode, "backend_initialization_failed");
}
TEST(Ex2Stage6ExecutionSidecar, FailureStillPublishesValidPackageAndUncertaintyDominates)
{
    const auto f = Foundation(); const auto a = Package(f, Hundred(f));
    for (const auto initial : {run::ExitCode::Completed, run::ExitCode::NativeStateUncertain})
    {
        Temporary t; run::CreateStaging(t.paths);
        std::ofstream(t.paths.failureSidecar) << "preexisting control context";
        const auto exit = run::PublishPreparedPackage(t.paths, a, Sidecar(f), true, initial, [] {});
        EXPECT_EQ(exit, initial == run::ExitCode::Completed ? run::ExitCode::PublicationFailure : run::ExitCode::NativeStateUncertain);
        EXPECT_TRUE(std::filesystem::is_directory(t.paths.finalDirectory));
        EXPECT_FALSE(std::filesystem::exists(t.paths.stagingDirectory));
        std::ifstream in(t.paths.failureSidecar); std::string original{std::istreambuf_iterator<char>(in), {}};
        EXPECT_EQ(original, "preexisting control context");
    }
}
TEST(Ex2Stage6ExecutionPublication, PostSamplingPublicationAndProvenanceFailuresHaveNoSampleAttribution)
{
    const auto f = Foundation(); const auto rows = Hundred(f); const auto a = Package(f, rows);
    std::optional<std::uint64_t> currentSample = rows.back().sampleIndex;
    run::CompleteSampleSequence(currentSample, {});
    auto failure = Sidecar(f); failure.sampleIndex = currentSample;
    for (const bool provenanceFailure : {false, true})
    {
        Temporary t; run::CreateStaging(t.paths);
        if (!provenanceFailure) std::ofstream(t.paths.stagingDirectory / "environment.json") << "existing file";
        const auto exit = run::PublishPreparedPackage(t.paths, a, failure, false, run::ExitCode::Completed,
            [=] { if (provenanceFailure) throw std::logic_error("provenance drift"); });
        EXPECT_EQ(exit, provenanceFailure ? run::ExitCode::InternalFailure : run::ExitCode::PublicationFailure);
        EXPECT_TRUE(std::filesystem::is_directory(t.paths.stagingDirectory));
        EXPECT_FALSE(std::filesystem::exists(t.paths.finalDirectory));
        EXPECT_TRUE(std::filesystem::exists(t.paths.failureSidecar));
        std::ifstream in(t.paths.failureSidecar); const std::string json{std::istreambuf_iterator<char>(in), {}};
        EXPECT_NE(json.find("\"sample_index\":null"), std::string::npos);
    }
}
TEST(Ex2Stage6ExecutionPublication, SafeDiagnosticFailureCanFinalizeWithExitZeroAndSidecar)
{
    Temporary t; const auto f = Foundation(); const std::optional<run::FailureContext> nativeFailure{run::FailureContext{ev::FailurePhase::Submission}};
    const auto row = run::MapAttempt(f, 2, {true, false, false, false, {}}, {}, nativeFailure);
    const auto a = Package(f, {Good(f, 0), Good(f, 1), row}); run::CreateStaging(t.paths);
    std::optional<std::uint64_t> currentSample = row.sampleIndex;
    run::CompleteSampleSequence(currentSample, nativeFailure);
    auto failure = Sidecar(f); failure.sampleIndex = currentSample;
    const auto exit = run::PublishPreparedPackage(t.paths, a, failure, true, run::ExitCode::Completed, [] {});
    EXPECT_EQ(exit, run::ExitCode::Completed);
    EXPECT_TRUE(std::filesystem::is_directory(t.paths.finalDirectory));
    EXPECT_TRUE(std::filesystem::exists(t.paths.failureSidecar));
    std::ifstream in(t.paths.failureSidecar); const std::string json{std::istreambuf_iterator<char>(in), {}};
    EXPECT_EQ(currentSample, row.sampleIndex);
    EXPECT_NE(json.find("\"sample_index\":2,"), std::string::npos);
}
TEST(Ex2Stage6ExecutionPackage, RejectsNativeDirectoryJunctionWithoutFollowingIt)
{
    Temporary t; const auto f = Foundation(); const auto a = Package(f, Hundred(f)); run::CreateStaging(t.paths);
    run::WriteStagedPackage(t.paths, a);
    const auto target = t.root / "junction-target"; std::filesystem::create_directory(target);
    const auto junction = t.paths.stagingDirectory / "nested"; std::filesystem::create_directory(junction);
    const std::wstring substitute = L"\\??\\" + target.wstring();
    const std::wstring printable = target.wstring();
    struct MountPoint { DWORD tag; WORD dataLength, reserved; WORD subOffset, subLength, printOffset, printLength; wchar_t text[2048]; };
    MountPoint buffer{}; buffer.tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer.subLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    buffer.printOffset = buffer.subLength + sizeof(wchar_t); buffer.printLength = static_cast<WORD>(printable.size() * sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), buffer.text);
    std::copy(printable.begin(), printable.end(), buffer.text + substitute.size() + 1);
    buffer.dataLength = static_cast<WORD>(8 + buffer.printOffset + buffer.printLength + sizeof(wchar_t));
    HANDLE handle = CreateFileW(junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    DWORD returned{}; const bool set = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, buffer.dataLength + 8, nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle); ASSERT_TRUE(set) << GetLastError();
    EXPECT_THROW(run::VerifyStagedPackage(t.paths, a), std::invalid_argument);
    EXPECT_TRUE(RemoveDirectoryW(junction.c_str()));
    EXPECT_TRUE(std::filesystem::is_directory(target));
}
} // namespace
