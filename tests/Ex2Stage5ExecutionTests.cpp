#include "app/Ex2Stage5Execution.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <fstream>
#include <locale>

namespace
{
namespace s5 = computelab::ex2::stage5;
namespace ex = s5::execution;
namespace ev = s5::evidence;
namespace core = computelab::ex2;
using Phase = s5::Stage5Phase;
using Status = ev::Status;
using Failure = ev::FailurePhase;
constexpr std::string_view Uuid = "00112233-4455-6677-8899-aabbccddeeff";
std::array<std::uint8_t, 16> RawUuid{0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
ex::Configuration Config(Phase phase = Phase::A1Sentinel, std::size_t index = 0)
{
    return {phase, index, phase == Phase::D1Sample ? std::optional<std::uint64_t>{2} : std::nullopt,
        0, 0, "anonymous-machine", "synthetic-stage5", std::string(Uuid)};
}
std::vector<std::string_view> Arguments()
{
    return {"--phase", "a1-sentinel", "--plan-index", "0", "--selected-w", "none",
        "--cuda-device", "0", "--vulkan-device", "0", "--machine-id", "anonymous-machine",
        "--session-id", "synthetic-stage5", "--expected-gpu-uuid", Uuid};
}
const std::vector<std::uint32_t>& Expected(Phase phase)
{
    if (phase == Phase::A1Sentinel)
    {
        static const auto a = core::ReferenceA1(core::GenerateWordInput(core::CoreInputSeed, 256));
        return a;
    }
    static const auto d = core::ReferenceD1(core::CoreInputSeed, 1048576, 64).finalState;
    return d;
}
ev::Foundation Foundation(Phase phase = Phase::A1Sentinel, std::size_t index = 0)
{
    const auto route = ex::ResolveRoute(Config(phase, index));
    const auto input = core::GenerateWordInput(core::CoreInputSeed, route.condition.elementCount);
    return ev::MakeWordFoundation({route.condition, route.process, "synthetic-stage5", "anonymous-machine",
        {std::string(Uuid), true}, std::string(40, 'a'), std::string(64, 'b'),
        route.process.backend == s5::Stage5Backend::Cuda ? std::nullopt : std::optional{std::string(64, 'c')}},
        input, Expected(phase));
}
computelab::timing::HostTimePoint Time(std::int64_t ns)
{
    return computelab::timing::HostTimePoint{std::chrono::duration_cast<computelab::timing::HostClock::duration>(
        std::chrono::nanoseconds(ns))};
}
ex::Attempt Success(Phase phase = Phase::A1Sentinel)
{
    ex::Attempt a; a.t0 = Time(100); a.t1 = Time(200); a.t2 = Time(500);
    a.waitSucceeded = true; a.output = Expected(phase); a.executed = true;
    a.dispatchCount = 64; a.barrierCount = 63; a.finalBuffer = core::IterativeFinalBuffer::StateA;
    return a;
}
ex::Bundle Bundle(Phase phase = Phase::A1Sentinel, std::size_t index = 0)
{
    auto f = Foundation(phase, index);
    computelab::results::EnvironmentRecord common{2, "EX-2", "synthetic-stage5", "2026-10-01T00:00:00Z",
        std::string(40, 'a'), false, "anonymous-machine", "Windows", "11", "Synthetic CPU", 16,
        "Synthetic GPU", "NVIDIA", "10DE:1234", 8, "driver", "toolkit", "runtime", "7.5", "sdk", "1.4",
        "MSVC", "19", "4", "1", "x64-debug", "Debug", false, false};
    ev::BackendDiagnostics diag;
    if (index == 0) { diag.implementation = "synthetic-cuda"; diag.streamFlags = "nonblocking"; }
    else { diag.implementation = "synthetic-vulkan"; diag.queueFamilyIndex = 0; diag.queueFlags = 2; diag.queueCount = 1; }
    ex::Bundle b{f, ev::MakeEnvironmentRecord(common, f, diag),
        {ev::MakeSetupCompleteInitialization(f, "synthetic setup")}, {}, {}, {}};
    for (unsigned i = 0; i < 4095; ++i) b.clock.push_back(ev::MakeHostClockRecord(f, i, i % 3 == 0 ? 0 : 1));
    auto a = Success(phase);
    const unsigned count = phase == Phase::D1Sample ? 200 : 48;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto mapped = ex::MapAttempt(f, i, a, Expected(phase));
        if (phase == Phase::D1Sample)
        { ev::SampleRecord row; static_cast<ev::OperationRecord&>(row) = *mapped.record; b.samples.push_back(row); }
        else { ev::WarmupRecord row; static_cast<ev::OperationRecord&>(row) = *mapped.record; b.warmup.push_back(row); }
    }
    return b;
}
class Temp
{
public:
    Temp()
    {
        static std::atomic<unsigned> sequence{};
        root = std::filesystem::temp_directory_path() / ("computelab-stage5-test-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory collision");
    }
    ~Temp() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    std::filesystem::path root;
    ex::SessionPaths Paths() const { return ex::MakeSessionPaths(root, "synthetic-stage5"); }
};
void Write(const std::filesystem::path& path, std::string_view bytes)
{ std::ofstream file(path, std::ios::binary); file << bytes; }
std::string Read(const std::filesystem::path& path)
{ std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
ex::FailureRecord FailureRecord()
{
    ex::FailureRecord r; r.sessionId = "synthetic-stage5"; r.failurePhase = "preflight";
    r.errorCode = "preflight_failed"; r.detail = "synthetic failure"; return r;
}

TEST(Ex2Stage5ExecutionRoute, ExactThreeFrozenConditions)
{
    for (auto phase : {Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample})
    {
        const auto r = ex::ResolveRoute(Config(phase));
        EXPECT_TRUE(s5::ValidateCondition(r.condition)); EXPECT_EQ(r.condition.instrumentMode, "H");
        EXPECT_EQ(r.condition.elementCount, phase == Phase::A1Sentinel ? 256 : 1048576);
        EXPECT_EQ(r.condition.diagnosticCount, phase == Phase::D1Sample ? 0 : 48);
        EXPECT_EQ(r.condition.measuredSampleCount, phase == Phase::D1Sample ? 200 : 0);
        EXPECT_EQ(r.preparationCount, phase == Phase::D1Sample ? 2 : 0);
    }
}
TEST(Ex2Stage5ExecutionRoute, AllTenPlanEntriesAreDerived)
{
    for (std::size_t i = 0; i < 10; ++i) EXPECT_EQ(ex::ResolveRoute(Config(Phase::A1Sentinel, i)).process, s5::FrozenProcessPlan()[i]);
    EXPECT_THROW(ex::ResolveRoute(Config(Phase::A1Sentinel, 10)), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionRoute, EverySelectedWAndPhaseMismatch)
{
    auto c = Config(Phase::D1Sample);
    for (auto w : s5::CandidateWarmups) { c.selectedW = w; EXPECT_EQ(ex::ResolveRoute(c).preparationCount, w); }
    c.selectedW = 3; EXPECT_THROW(ex::ResolveRoute(c), std::invalid_argument);
    c.selectedW.reset(); EXPECT_THROW(ex::ResolveRoute(c), std::invalid_argument);
    c = Config(); c.selectedW = 0; EXPECT_THROW(ex::ResolveRoute(c), std::invalid_argument);
    c.phase = Phase::D1Warmup; EXPECT_THROW(ex::ResolveRoute(c), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionCli, ExactOptionsParse)
{ const auto c = ex::ParseArguments(Arguments()); EXPECT_EQ(c.phase, Phase::A1Sentinel); EXPECT_EQ(c.expectedGpuUuid, Uuid); }
TEST(Ex2Stage5ExecutionCli, D1WarmupAndEverySamplePreparationParse)
{
    auto a = Arguments(); a[1] = "d1-warmup";
    EXPECT_EQ(ex::ParseArguments(a).phase, Phase::D1Warmup);
    a[1] = "d1-sample";
    for (auto w : {"0", "1", "2", "4", "8", "16"})
    { a[5] = w; EXPECT_TRUE(ex::ParseArguments(a).selectedW); }
}
TEST(Ex2Stage5ExecutionCli, UnknownDuplicateMissingPositionalAndNumericRejected)
{
    auto a = Arguments(); a.pop_back(); EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument);
    a = Arguments(); a[0] = "--unknown"; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument);
    a = Arguments(); a[0] = "--plan-index"; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument);
    a = Arguments(); a[0] = "a1-sentinel"; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument);
    for (auto bad : {"-1", "+1", "1x", "999999999999999999999999999"})
    { a = Arguments(); a[3] = bad; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument); }
}
TEST(Ex2Stage5ExecutionCli, RejectsPrivatePathTokensAndUuidAliases)
{
    for (auto bad : {"user@host", "C:\\Users\\name", "../escape", "two words", "", "CON", "nul", "LPT1"})
    { auto a = Arguments(); a[11] = bad; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument); }
    for (auto bad : {"00112233445566778899aabbccddeeff", "00112233-4455-6677-8899-AABBCCDDEEFF",
        "00000000-0000-0000-0000-000000000000", "bad"})
    { auto a = Arguments(); a[15] = bad; EXPECT_THROW(ex::ParseArguments(a), std::invalid_argument); }
}
TEST(Ex2Stage5ExecutionProvenance, ExactRawUuidAndNoOrdinalSubstitute)
{
    EXPECT_EQ(ex::FormatUuid(RawUuid), Uuid); EXPECT_NO_THROW(ex::RequireExpectedUuid(Uuid, RawUuid));
    auto changed = RawUuid; ++changed[0]; EXPECT_THROW(ex::RequireExpectedUuid(Uuid, changed), std::invalid_argument);
    EXPECT_THROW(ex::FormatUuid({}), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionProvenance, CleanSourceFullHashesAndBackendShaderRules)
{
    const auto cuda = ex::ResolveRoute(Config()), vulkan = ex::ResolveRoute(Config(Phase::D1Warmup, 1));
    const auto source = std::string(40, 'a'), exe = std::string(64, 'b');
    const auto shader = std::optional{std::string(64, 'c')};
    EXPECT_NO_THROW(ex::ValidateProvenance(cuda, source, false, exe, std::nullopt));
    EXPECT_NO_THROW(ex::ValidateProvenance(vulkan, source, false, exe, shader));
    EXPECT_THROW(ex::ValidateProvenance(cuda, source, true, exe, std::nullopt), std::invalid_argument);
    EXPECT_THROW(ex::ValidateProvenance(cuda, "short", false, exe, std::nullopt), std::invalid_argument);
    EXPECT_THROW(ex::ValidateProvenance(cuda, source, false, "bad", std::nullopt), std::invalid_argument);
    EXPECT_THROW(ex::ValidateProvenance(cuda, source, false, exe, shader), std::invalid_argument);
    EXPECT_THROW(ex::ValidateProvenance(vulkan, source, false, exe, std::nullopt), std::invalid_argument);
    EXPECT_THROW(ex::ValidateProvenance(vulkan, source, false, exe, std::optional<std::string>{"bad"}), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionClock, ExactCaptureDeltaCountAndZeroPreservation)
{
    const auto f = Foundation(); std::vector<std::optional<computelab::timing::HostTimePoint>> captures(4096, Time(100));
    const auto rows = ex::ClockRecords(f, captures); ASSERT_EQ(rows.size(), 4095);
    for (std::size_t i = 0; i < rows.size(); ++i) { EXPECT_EQ(rows[i].sequenceIndex, i); EXPECT_EQ(rows[i].deltaNanoseconds, 0); }
    const auto csv = ev::SerializeHostClockCsv(f, rows);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 4096);
    captures.pop_back(); EXPECT_THROW(ex::ClockRecords(f, captures), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionClock, DecreasingMissingAndOverflowRetainNull)
{
    const auto f = Foundation(); std::vector<std::optional<computelab::timing::HostTimePoint>> captures(4096, Time(100));
    captures[1] = Time(99); captures[3].reset();
    captures[6] = computelab::timing::HostTimePoint::min(); captures[7] = computelab::timing::HostTimePoint::max();
    const auto rows = ex::ClockRecords(f, captures);
    EXPECT_FALSE(rows[0].deltaNanoseconds); EXPECT_FALSE(rows[2].deltaNanoseconds); EXPECT_FALSE(rows[3].deltaNanoseconds);
    EXPECT_FALSE(rows[6].deltaNanoseconds); EXPECT_EQ(rows[1].deltaNanoseconds, 1);
    EXPECT_NO_THROW(ev::SerializeHostClockCsv(f, rows));
}
TEST(Ex2Stage5ExecutionTiming, ExistingArithmeticAndNativeNull)
{
    const auto f = Foundation(); const auto a = Success(); const auto mapped = ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel));
    ASSERT_TRUE(mapped.record); EXPECT_TRUE(mapped.successful);
    EXPECT_EQ(mapped.record->hostSubmissionNanoseconds, 100); EXPECT_EQ(mapped.record->hostWaitNanoseconds, 300);
    EXPECT_EQ(mapped.record->hostCompletionNanoseconds, 400); EXPECT_FALSE(mapped.record->nativeDeviceIntervalNanoseconds);
}
TEST(Ex2Stage5ExecutionTiming, SuccessfulWaitInvalidClockCannotInventCompletedRow)
{
    auto a = Success(); a.t2 = Time(199);
    const auto result = ex::MapAttempt(Foundation(), 0, a, Expected(Phase::A1Sentinel));
    EXPECT_FALSE(result.record); EXPECT_FALSE(result.successful); EXPECT_EQ(result.errorCode, "timestamp_invalid");
}
class WaitFailure : public ::testing::TestWithParam<Status> {};
TEST_P(WaitFailure, SuccessfulSubmissionFailedWaitHasNoT2Timing)
{
    ex::Attempt a; a.t0 = Time(100); a.t1 = Time(200); a.status = GetParam(); a.failurePhase = Failure::CompletionWait;
    a.errorCode = a.status == Status::Timeout ? "operation_timeout" : a.status == Status::DeviceLost ? "device_lost" : "completion_failed";
    a.completionUncertain = true;
    const auto f = Foundation(); const auto mapped = ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel));
    ASSERT_TRUE(mapped.record); EXPECT_FALSE(mapped.successful); EXPECT_EQ(mapped.record->hostSubmissionNanoseconds, 100);
    EXPECT_FALSE(mapped.record->hostWaitNanoseconds); EXPECT_FALSE(mapped.record->hostCompletionNanoseconds);
    EXPECT_FALSE(mapped.record->correctness.operationCompleted); EXPECT_FALSE(mapped.record->correctness.validationPassed);
    a.t2 = Time(500); EXPECT_THROW(ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel)), std::invalid_argument);
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5Execution, WaitFailure,
    ::testing::Values(Status::WaitFailed, Status::Timeout, Status::DeviceLost, Status::Incomplete));
TEST(Ex2Stage5ExecutionCorrectness, SubmissionAndReadbackFailuresRetainOnlyEstablishedFacts)
{
    const auto f = Foundation(); ex::Attempt a; a.t0 = Time(100); a.status = Status::SubmitFailed;
    a.failurePhase = Failure::Submission; a.errorCode = "submission_failed";
    auto mapped = ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel)); ASSERT_TRUE(mapped.record);
    EXPECT_FALSE(mapped.record->hostCompletionNanoseconds); EXPECT_FALSE(mapped.record->correctness.outputObserved);
    a = Success(); a.output.reset(); a.status = Status::Incomplete; a.failurePhase = Failure::Readback; a.errorCode = "readback_failed";
    mapped = ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel)); ASSERT_TRUE(mapped.record);
    EXPECT_TRUE(mapped.record->correctness.operationCompleted); EXPECT_FALSE(mapped.record->correctness.outputObserved);
    EXPECT_EQ(mapped.record->hostCompletionNanoseconds, 400);
}
TEST(Ex2Stage5ExecutionCorrectness, FullOutputMismatchAndExecutionTruth)
{
    const auto f = Foundation(); auto a = Success(); (*a.output)[255] ^= 1;
    auto mapped = ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel)); ASSERT_TRUE(mapped.record);
    EXPECT_EQ(mapped.record->status, Status::ValidationFailed); EXPECT_EQ(mapped.record->correctness.validationPassed, false);
    EXPECT_EQ(mapped.record->hostCompletionNanoseconds, 400);
    a = Success(); a.executed = false; EXPECT_FALSE(ex::MapAttempt(f, 0, a, Expected(Phase::A1Sentinel)).successful);
}
TEST(Ex2Stage5ExecutionCorrectness, D1DispatchParityAndVulkanBarriersAreRequired)
{
    const auto f = Foundation(Phase::D1Sample, 1); auto a = Success(Phase::D1Sample);
    EXPECT_TRUE(ex::MapAttempt(f, 0, a, Expected(Phase::D1Sample)).successful);
    --a.dispatchCount; EXPECT_FALSE(ex::MapAttempt(f, 0, a, Expected(Phase::D1Sample)).successful);
    a = Success(Phase::D1Sample); --a.barrierCount; EXPECT_FALSE(ex::MapAttempt(f, 0, a, Expected(Phase::D1Sample)).successful);
    a = Success(Phase::D1Sample); a.finalBuffer = core::IterativeFinalBuffer::StateB;
    EXPECT_FALSE(ex::MapAttempt(f, 0, a, Expected(Phase::D1Sample)).successful);
}
class PhasePackage : public ::testing::TestWithParam<Phase> {};
TEST_P(PhasePackage, ExactTopologyRereadAndFinalRename)
{
    Temp temp; const auto p = temp.Paths(); const auto b = Bundle(GetParam());
    ex::CreateStaging(p); ex::WriteCompleteBundle(p, b); EXPECT_NO_THROW(ex::VerifyStagedBundle(p, b));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(p.staging), std::filesystem::directory_iterator{}), 6);
    const auto warmup = Read(p.staging / "warmup.csv"), samples = Read(p.staging / "samples.csv");
    EXPECT_EQ(std::count(warmup.begin(), warmup.end(), '\n'), GetParam() == Phase::D1Sample ? 1 : 49);
    EXPECT_EQ(std::count(samples.begin(), samples.end(), '\n'), GetParam() == Phase::D1Sample ? 201 : 1);
    EXPECT_NE(Read(p.staging / "environment.json").find("\"protocol_version\":\"1.2\""), std::string::npos);
    EXPECT_NE(Read(p.staging / "summary.json").find("\"evidence_kind\":\"qualification\""), std::string::npos);
    ex::FinalizeBundle(p, b); EXPECT_TRUE(std::filesystem::exists(p.final));
    EXPECT_FALSE(std::filesystem::exists(p.staging)); EXPECT_FALSE(std::filesystem::exists(p.failure));
    EXPECT_THROW(ex::RequireUnused(p), std::invalid_argument);
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5Execution, PhasePackage,
    ::testing::Values(Phase::A1Sentinel, Phase::D1Warmup, Phase::D1Sample));
TEST(Ex2Stage5ExecutionPackage, AllNamespaceCollisionsAndTraversalRejected)
{
    Temp temp; const auto p = temp.Paths();
    for (const auto& path : {p.staging, p.final, p.failure})
    { Write(path, "existing"); EXPECT_THROW(ex::CreateStaging(p), std::invalid_argument); EXPECT_EQ(Read(path), "existing"); std::filesystem::remove(path); }
    EXPECT_THROW(ex::MakeSessionPaths(temp.root, "../escape"), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionPackage, MutationTruncationExtraMissingAndNestedEntriesBlockRename)
{
    Temp temp; const auto p = temp.Paths(); const auto b = Bundle(); ex::CreateStaging(p); ex::WriteCompleteBundle(p, b);
    const auto path = p.staging / "summary.json";
    const auto original = Read(path);
    Write(path, original + " "); EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument);
    Write(path, original.substr(0, original.size() / 2)); EXPECT_THROW(ex::VerifyStagedBundle(p, b), std::invalid_argument);
    std::filesystem::remove(path); EXPECT_THROW(ex::VerifyStagedBundle(p, b), std::invalid_argument); Write(path, original);
    Write(p.staging / "extra.txt", "extra"); EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument);
    std::filesystem::remove(p.staging / "extra.txt"); std::filesystem::create_directory(p.staging / "unexpected-directory");
    EXPECT_THROW(ex::VerifyStagedBundle(p, b), std::invalid_argument); EXPECT_FALSE(std::filesystem::exists(p.final));
}
TEST(Ex2Stage5ExecutionPackage, DirtyEnvironmentWrongProvenanceIncompleteAndFailedRowsCannotFinalize)
{
    Temp temp; const auto p = temp.Paths(); auto b = Bundle(); ex::CreateStaging(p);
    b.environment.common.gitDirty = true; EXPECT_THROW(ex::WriteCompleteBundle(p, b), std::invalid_argument);
    b.environment.common.gitDirty = false; b.environment.common.gitCommit[0] = 'c';
    EXPECT_THROW(ex::WriteCompleteBundle(p, b), std::invalid_argument); b.environment.common.gitCommit[0] = 'a';
    b.warmup.pop_back(); EXPECT_THROW(ex::WriteCompleteBundle(p, b), std::invalid_argument);
    b = Bundle(); b.warmup[0].status = Status::ValidationFailed;
    EXPECT_THROW(ex::WriteCompleteBundle(p, b), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionPackage, NoOverwriteOfExistingArtifactOrSidecar)
{
    Temp temp; const auto p = temp.Paths(); const auto b = Bundle(); ex::CreateStaging(p); ex::WriteCompleteBundle(p, b);
    EXPECT_THROW(ex::WriteCompleteBundle(p, b), std::runtime_error);
    auto r = FailureRecord(); ex::WriteFailure(p, r); EXPECT_THROW(ex::WriteFailure(p, r), std::runtime_error);
    EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionPackage, EveryArtifactMutationBlocksFinalization)
{
    Temp temp; const auto p = temp.Paths(); const auto b = Bundle(); ex::CreateStaging(p); ex::WriteCompleteBundle(p, b);
    for (auto name : {"environment.json", "initialization.csv", "host-clock.csv", "warmup.csv", "samples.csv", "summary.json"})
    {
        const auto path = p.staging / name; const auto original = Read(path);
        auto changed = original; changed[0] ^= 1; Write(path, changed);
        EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument); EXPECT_FALSE(std::filesystem::exists(p.final));
        Write(path, original);
    }
    EXPECT_NO_THROW(ex::VerifyStagedBundle(p, b));
}
TEST(Ex2Stage5ExecutionIncomplete, MidSequenceFailureRetainsPrefixAndNativeControlContext)
{
    Temp temp; const auto p = temp.Paths(); auto b = Bundle(); b.warmup.clear(); ex::CreateStaging(p);
    Write(p.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n");
    auto r = FailureRecord(); r.foundationEstablished = true; r.runId = b.foundation.Identity().runId;
    r.seriesId = b.foundation.Identity().seriesId; r.stagingRetained = true;
    EXPECT_TRUE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), Success(), 0, false));
    ex::Attempt failed; failed.t0 = Time(100); failed.t1 = Time(200); failed.status = Status::WaitFailed;
    failed.failurePhase = Failure::CompletionWait; failed.errorCode = "completion_failed"; failed.completionUncertain = true;
    failed.nativePhase = "completion_wait"; failed.nativeCode = -4; failed.nativeDetail = "vkWaitForFences";
    EXPECT_FALSE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), failed, 1, false));
    ASSERT_EQ(b.warmup.size(), 2); EXPECT_FALSE(b.warmup.back().hostCompletionNanoseconds);
    ex::WriteFailure(p, r); const auto csv = Read(p.staging / "warmup.csv");
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'), 3); EXPECT_FALSE(std::filesystem::exists(p.final));
    const auto sidecar = Read(p.failure); EXPECT_NE(sidecar.find("\"native_code\":-4"), std::string::npos);
    EXPECT_NE(sidecar.find("\"completion_uncertain\":true"), std::string::npos);
    EXPECT_NE(sidecar.find("\"attempt_index\":1"), std::string::npos); EXPECT_EQ(r.category, ex::ExitCode::ExecutionFailure);
}
TEST(Ex2Stage5ExecutionIncomplete, SelectedWPreparationsDoNotBecomeDiagnosticOrSampleRows)
{
    Temp temp; const auto p = temp.Paths(); auto b = Bundle(Phase::D1Sample); b.samples.clear(); ex::CreateStaging(p);
    Write(p.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n"); Write(p.staging / "samples.csv", ev::SamplesCsvHeader() + "\r\n");
    auto r = FailureRecord(); const auto a = Success(Phase::D1Sample);
    for (unsigned i = 0; i < 2; ++i) EXPECT_TRUE(ex::RetainAttempt(b, p, r, Expected(Phase::D1Sample), a, i, true));
    EXPECT_TRUE(b.samples.empty()); EXPECT_TRUE(b.warmup.empty());
    EXPECT_EQ(Read(p.staging / "warmup.csv"), ev::WarmupCsvHeader() + "\r\n");
    EXPECT_EQ(Read(p.staging / "samples.csv"), ev::SamplesCsvHeader() + "\r\n");
    EXPECT_THROW(ex::RetainAttempt(b, p, r, Expected(Phase::D1Sample), a, 2, true), std::invalid_argument);
    auto failed = a; (*failed.output)[0] ^= 1;
    EXPECT_FALSE(ex::RetainAttempt(b, p, r, Expected(Phase::D1Sample), failed, 0, true)); EXPECT_TRUE(b.samples.empty());
    EXPECT_EQ(r.validationPassed, false); EXPECT_EQ(r.hostCompletionNanoseconds, 400);
}
TEST(Ex2Stage5ExecutionIncomplete, PreFoundationSidecarHasExplicitNullsAndNoEvidenceIdentity)
{
    Temp temp; const auto p = temp.Paths(); auto r = FailureRecord();
    ex::WriteFailure(p, r); const auto json = Read(p.failure);
    EXPECT_NE(json.find("\"run_id\":null,\"series_id\":null"), std::string::npos);
    EXPECT_EQ(json.find("input_sha256"), std::string::npos); EXPECT_EQ(json.find("gpu_uuid"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(p.staging)); r.runId = "fabricated";
    EXPECT_THROW(ex::SerializeFailure(r), std::invalid_argument);
}
TEST(Ex2Stage5ExecutionIncomplete, InvalidTimestampControlContextRetainsNoInventedRow)
{
    Temp temp; const auto p = temp.Paths(); auto b = Bundle(); b.warmup.clear(); ex::CreateStaging(p);
    Write(p.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n"); auto a = Success(); a.t2.reset(); auto r = FailureRecord();
    EXPECT_FALSE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), a, 0, false));
    EXPECT_TRUE(b.warmup.empty()); EXPECT_EQ(r.errorCode, "timestamp_invalid"); EXPECT_EQ(r.successfulWait, true);
}
TEST(Ex2Stage5ExecutionControl, A1AttemptContextIsSetBeforePreparationAndClearsPreviousSuccess)
{
    Temp temp; const auto p = temp.Paths(); auto b = Bundle(); b.warmup.clear(); ex::CreateStaging(p);
    Write(p.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n");
    auto r = FailureRecord(); r.foundationEstablished = true; r.phase = Phase::A1Sentinel;
    r.runId = b.foundation.Identity().runId; r.seriesId = b.foundation.Identity().seriesId;
    ASSERT_TRUE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), Success(), 0, false));
    ASSERT_TRUE(r.hostCompletionNanoseconds); ASSERT_TRUE(r.errorCode.empty());
    r.nativePhase = "stale"; r.nativeCode = -99; r.nativeDetail = "stale";
    r.completionUncertain = true; r.preparationAttempt = true;
    ex::BeginA1Observation(r, 1);
    EXPECT_EQ(r.attemptIndex, 1); EXPECT_FALSE(r.preparationAttempt); EXPECT_FALSE(r.completionUncertain);
    EXPECT_FALSE(r.successfulWait); EXPECT_FALSE(r.validationPassed);
    EXPECT_FALSE(r.hostSubmissionNanoseconds); EXPECT_FALSE(r.hostWaitNanoseconds); EXPECT_FALSE(r.hostCompletionNanoseconds);
    EXPECT_FALSE(r.nativePhase); EXPECT_FALSE(r.nativeCode); EXPECT_FALSE(r.nativeDetail);
    EXPECT_EQ(r.category, ex::ExitCode::ExecutionFailure); EXPECT_EQ(r.failurePhase, "a1_preparation");
    // Unexpected preparation rejection still has serializable current context.
    r.stagingRetained = true; ex::WriteFailure(p, r);
    const auto json = Read(p.failure);
    EXPECT_NE(json.find("\"attempt_index\":1"), std::string::npos);
    EXPECT_NE(json.find("\"successful_wait\":null"), std::string::npos);
    EXPECT_NE(json.find("lifecycle guard"), std::string::npos);
    EXPECT_EQ(b.warmup.size(), 1); EXPECT_FALSE(std::filesystem::exists(p.final));
    EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument);
}

TEST(Ex2Stage5ExecutionControl, A1PreparationContextRequiresFoundationAndBoundedIndex)
{
    auto r = FailureRecord();
    EXPECT_THROW(ex::BeginA1Observation(r, 0), std::invalid_argument);
    r.foundationEstablished = true; r.phase = Phase::D1Warmup;
    EXPECT_THROW(ex::BeginA1Observation(r, 0), std::invalid_argument);
    r.phase = Phase::A1Sentinel;
    EXPECT_THROW(ex::BeginA1Observation(r, 48), std::invalid_argument);
    EXPECT_FALSE(r.attemptIndex);
}

TEST(Ex2Stage5ExecutionIncomplete, PreT0A1PreparationFailuresRetainNativeFactsOrStableLifecycleContext)
{
    for (const unsigned kind : {0U, 1U, 2U})
    {
        SCOPED_TRACE(kind);
        Temp temp; const auto p = temp.Paths(); auto b = Bundle(Phase::A1Sentinel, kind == 1 ? 1 : 0);
        b.warmup.clear(); ex::CreateStaging(p);
        Write(p.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n");
        auto r = FailureRecord(); r.foundationEstablished = true; r.phase = Phase::A1Sentinel;
        r.runId = b.foundation.Identity().runId; r.seriesId = b.foundation.Identity().seriesId;
        ASSERT_TRUE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), Success(), 0, false));
        ex::BeginA1Observation(r, 1);
        ex::Attempt a; a.status = Status::Incomplete; a.failurePhase = Failure::BackendInitialization;
        a.errorCode = "backend_initialization_failed";
        if (kind != 2)
        {
            a.nativePhase = kind == 0 ? "submission" : "preparation";
            a.nativeCode = kind == 0 ? 1 : -1;
            a.nativeDetail = kind == 0 ? "cudaErrorInvalidValue" : "vkResetFences for EX-2 Vulkan A1 retained-input compute";
        }
        EXPECT_FALSE(ex::RetainAttempt(b, p, r, Expected(Phase::A1Sentinel), a, 1, false));
        ASSERT_EQ(b.warmup.size(), 2); EXPECT_EQ(b.warmup[0].status, Status::Ok);
        EXPECT_EQ(b.warmup[1].status, Status::Incomplete); EXPECT_TRUE(b.samples.empty());
        EXPECT_EQ(r.attemptIndex, 1); EXPECT_EQ(r.successfulWait, false);
        EXPECT_FALSE(r.hostSubmissionNanoseconds); EXPECT_FALSE(r.hostWaitNanoseconds); EXPECT_FALSE(r.hostCompletionNanoseconds);
        EXPECT_EQ(r.nativePhase, a.nativePhase); EXPECT_EQ(r.nativeCode, a.nativeCode); EXPECT_EQ(r.nativeDetail, a.nativeDetail);
        r.stagingRetained = true; ex::WriteFailure(p, r);
        const auto json = Read(p.failure);
        EXPECT_NE(json.find("\"host_submission_ns\":null,\"host_wait_ns\":null,\"host_completion_ns\":null"), std::string::npos);
        EXPECT_EQ(json.find(temp.root.string()), std::string::npos);
        EXPECT_THROW(ex::FinalizeBundle(p, b), std::invalid_argument);
        EXPECT_TRUE(std::filesystem::is_directory(p.staging)); EXPECT_FALSE(std::filesystem::exists(p.final));
    }
}

TEST(Ex2Stage5ExecutionControl, ExactExitCategoriesAndLocaleIndependentJson)
{
    EXPECT_EQ(static_cast<int>(ex::ExitCode::Completed), 0); EXPECT_EQ(static_cast<int>(ex::ExitCode::PreflightFailure), 2);
    EXPECT_EQ(static_cast<int>(ex::ExitCode::ExecutionFailure), 3); EXPECT_EQ(static_cast<int>(ex::ExitCode::PublicationFailure), 4);
    const auto r = FailureRecord(); const auto before = ex::SerializeFailure(r);
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto previous = std::locale(); std::locale::global(std::locale(previous, new Comma));
    EXPECT_EQ(ex::SerializeFailure(r), before); std::locale::global(previous);
    for (auto forbidden : {"verdict", "winner", "speedup", "d1ScopeQualified"}) EXPECT_EQ(before.find(forbidden), std::string::npos);
}
TEST(Ex2Stage5ExecutionControl, LiteralPreFoundationGoldenJson)
{
    EXPECT_EQ(ex::SerializeFailure(FailureRecord()),
        "{\"record_version\":1,\"record_type\":\"ex2-stage5-execution-failure\",\"session_id\":\"synthetic-stage5\","
        "\"phase\":null,\"plan_index\":null,\"backend\":null,\"process_index\":null,\"block_index\":null,\"order_slot\":null,"
        "\"exit_category\":2,\"failure_phase\":\"preflight\",\"error_code\":\"preflight_failed\",\"foundation_established\":false,"
        "\"source_revision\":null,\"run_id\":null,\"series_id\":null,\"staging_retained\":false,\"completion_uncertain\":false,"
        "\"native_phase\":null,\"native_code\":null,\"native_detail\":null,\"attempt_index\":null,\"preparation_attempt\":false,"
        "\"successful_wait\":null,\"validation_passed\":null,\"host_submission_ns\":null,\"host_wait_ns\":null,\"host_completion_ns\":null,"
        "\"detail\":\"synthetic failure\"}\n");
}
} // namespace
