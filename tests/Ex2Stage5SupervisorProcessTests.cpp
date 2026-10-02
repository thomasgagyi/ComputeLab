#include "ex2/Ex2Stage5Supervisor.hpp"
#include <gtest/gtest.h>
#define NOMINMAX
#include <Windows.h>
#include <thread>

namespace
{
namespace s5 = computelab::ex2::stage5;
namespace c = s5::control;
using namespace std::chrono_literals;
c::ProcessResult RunHelper(std::vector<std::string> args, c::ProgressExpectation e = {s5::Stage5Phase::A1Sentinel, 0U, {}},
    std::chrono::milliseconds op = 200ms, std::chrono::milliseconds child = 800ms)
{
    // Isolate protocol/operation/child behavior; campaign expiry has its own case.
    return c::RunSupervisedProcess(COMPUTELAB_S5_TEST_CHILD, args, e, op, child, std::chrono::steady_clock::now() + 30s);
}
c::ProgressEvent Event(const c::ProgressState& p, std::int64_t counter)
{
    const auto i = p.NextIdentity(); return {c::ProgressMagic, 1U, p.eventCount % 2U ? c::AttemptEvent::Returned : c::AttemptEvent::Started,
        static_cast<std::uint32_t>(i.phase), static_cast<std::uint32_t>(i.backend), i.kind, i.planIndex, i.attemptIndex, counter};
}
TEST(Ex2Stage5SupervisorProgress, ExactFrozenSequencesForEveryCandidateAndPhase)
{
    for (const auto phase : {s5::Stage5Phase::A1Sentinel, s5::Stage5Phase::D1Warmup, s5::Stage5Phase::D1Sample})
        for (const auto w : s5::CandidateWarmups) for (const auto plan : {0U, 1U, 2U})
        {
            c::ProgressState p({phase, plan, phase == s5::Stage5Phase::D1Sample ? std::optional{w} : std::nullopt});
            std::int64_t tick = 100;
            while (!p.Full()) { const auto e = Event(p, tick++); p.Accept(e, tick, 100); }
            EXPECT_FALSE(p.invalid); EXPECT_FALSE(p.operationTimedOut); EXPECT_FALSE(p.activeAttempt); EXPECT_TRUE(p.lastCompletedAttempt);
            EXPECT_EQ(p.eventCount, phase == s5::Stage5Phase::D1Sample ? 2U * (200U + w) : 96U);
            auto extra = c::ProgressEvent{}; p.Accept(extra, tick, 100); EXPECT_TRUE(p.invalid);
        }
}
TEST(Ex2Stage5SupervisorProgress, DelayedDrainAndReturnedAttemptUseChildStartDeadline)
{
    c::ProgressState p({s5::Stage5Phase::D1Warmup, 0U, {}}); auto e = Event(p, 100); p.Accept(e, 250, 100); EXPECT_TRUE(p.operationTimedOut);
    c::ProgressState q({s5::Stage5Phase::D1Sample, 1U, 2U}); q.Accept(Event(q, 100), 100, 100); EXPECT_TRUE(q.activeAttempt);
    EXPECT_EQ(q.activeAttempt->kind, c::AttemptKind::SelectedWarmupPreparation); q.Accept(Event(q, 200), 200, 100); EXPECT_TRUE(q.operationTimedOut);
    EXPECT_FALSE(q.outstandingStart); EXPECT_EQ(q.lastCompletedAttempt->attemptIndex, 0U);
}
class Stage5ControlWireMutation : public testing::TestWithParam<unsigned> {};
TEST_P(Stage5ControlWireMutation, RejectsWireIdentityOrderEnumAndQpcDrift)
{
    c::ProgressState p({s5::Stage5Phase::D1Sample, 2U, 2U}); auto e = Event(p, 100);
    switch (GetParam()) { case 0: e.magic = 0; break; case 1: e.version = 0; break; case 2: e.event = c::AttemptEvent::Returned; break;
        case 3: e.phase = 0; break; case 4: e.backend = 0; break; case 5: e.kind = c::AttemptKind::MeasuredObservation; break;
        case 6: e.planIndex = 0; break; case 7: e.attemptIndex = 1; break; case 8: e.performanceCounter = 0; break;
        case 9: e.performanceCounter = -1; break; case 10: e.performanceCounter = 201; break; case 11: e.event = static_cast<c::AttemptEvent>(99); break; }
    p.Accept(e, 200, 100); EXPECT_TRUE(p.invalid); EXPECT_EQ(p.eventCount, 0U);
}
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlWireMutation, testing::Range(0U, 12U));
class Stage5ControlProtocolFailure : public testing::TestWithParam<const char*> {};
TEST_P(Stage5ControlProtocolFailure, FailsClosedWithoutRetry)
{ const auto r = RunHelper({GetParam()}); EXPECT_EQ(r.progressValidation, "invalid"); EXPECT_EQ(r.terminationReason, "progress_protocol_error"); }
INSTANTIATE_TEST_SUITE_P(Ex2Stage5, Stage5ControlProtocolFailure, testing::Values("bad-magic", "bad-version", "bad-phase", "bad-backend", "bad-kind",
    "bad-plan", "bad-index", "returned-first", "future", "zero-qpc", "truncated", "duplicate-start", "duplicate-return", "backward", "extra", "empty-success"));
TEST(Ex2Stage5SupervisorProcess, CompleteAllPhasesIncludingZeroAndPositivePreparation)
{
    for (const auto phase : {s5::Stage5Phase::A1Sentinel, s5::Stage5Phase::D1Warmup, s5::Stage5Phase::D1Sample})
        for (const auto plan : {0U, 1U}) for (const auto w : {0U, 2U, 16U})
        {
            const c::ProgressExpectation e{phase, plan, phase == s5::Stage5Phase::D1Sample ? std::optional<std::uint64_t>{w} : std::nullopt};
            const auto r = RunHelper({"complete", std::to_string(static_cast<unsigned>(phase)), std::to_string(plan), std::to_string(w)}, e, 2000ms, 10000ms);
            EXPECT_EQ(r.exitCode, 0U); EXPECT_FALSE(r.terminationReason); EXPECT_EQ(r.progressValidation, "complete"); EXPECT_EQ(r.eventCount, c::ProgressState(e).ExpectedEventCount());
            EXPECT_TRUE(r.processId); EXPECT_FALSE(r.activeAttempt); ASSERT_TRUE(r.lastCompletedAttempt); EXPECT_EQ(r.lastCompletedAttempt->attemptIndex, phase == s5::Stage5Phase::D1Sample ? 199U : 47U);
        }
}
TEST(Ex2Stage5SupervisorProcess, AllExternalDeadlinesAndPreparationWatchdogTerminateOwnedChild)
{
    EXPECT_EQ(RunHelper({"no-progress"}).terminationReason, "child_timeout"); EXPECT_EQ(RunHelper({"cuda-hang"}).terminationReason, "cuda_attempt_timeout");
    EXPECT_EQ(RunHelper({"vulkan-hang"}, {s5::Stage5Phase::A1Sentinel, 1U, {}}).terminationReason, "vulkan_attempt_timeout");
    const auto prep = RunHelper({"sample-hang", "2", "0", "2"}, {s5::Stage5Phase::D1Sample, 0U, 2U});
    EXPECT_EQ(prep.terminationReason, "cuda_attempt_timeout"); ASSERT_TRUE(prep.activeAttempt); EXPECT_EQ(prep.activeAttempt->kind, c::AttemptKind::SelectedWarmupPreparation);
    EXPECT_EQ(RunHelper({"delayed-pipe"}).terminationReason, "cuda_attempt_timeout");
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S5_TEST_CHILD, {"no-progress"}, {s5::Stage5Phase::A1Sentinel, 0U, {}}, 200ms, 2000ms, std::chrono::steady_clock::now() + 500ms);
    EXPECT_EQ(r.terminationReason, "campaign_timeout");
}
TEST(Ex2Stage5SupervisorProcess, ExpiredCampaignCannotStartLaterChild)
{
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S5_TEST_CHILD, {"complete"}, {s5::Stage5Phase::A1Sentinel, 0U, {}}, 200ms, 800ms, std::chrono::steady_clock::now());
    EXPECT_EQ(r.terminationReason, "campaign_timeout"); EXPECT_FALSE(r.processId); EXPECT_EQ(r.progressValidation, "not_started");
}
TEST(Ex2Stage5SupervisorProcess, NonzeroPrefixCrashAndPipeFailureAreTruthful)
{
    const auto p = RunHelper({"prefix"}); EXPECT_EQ(p.exitCode, 3U); EXPECT_EQ(p.progressValidation, "valid_prefix"); EXPECT_EQ(p.eventCount, 2U);
    const auto full = RunHelper({"full-nonzero"}, {s5::Stage5Phase::A1Sentinel, 0U, {}}, 2000ms, 10000ms); EXPECT_EQ(full.exitCode, 3U); EXPECT_EQ(full.progressValidation, "complete");
    EXPECT_EQ(RunHelper({"abnormal"}).exitCode, 0xc0000005U); EXPECT_EQ(RunHelper({"pipe-failure"}).exitCode, 4U);
}
TEST(Ex2Stage5SupervisorProcess, QuotingKillOwnedJobAndRestrictedInheritance)
{
    EXPECT_EQ(RunHelper({"quote", "space and \"quote\"", "trailing slash\\", ""}, {s5::Stage5Phase::A1Sentinel, 0U, {}}, 2000ms, 10000ms).exitCode, 0U);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE unrelated = CreateEventW(&security, TRUE, FALSE, nullptr); ASSERT_NE(unrelated, nullptr);
    EXPECT_EQ(RunHelper({"restricted-handle", std::to_string(reinterpret_cast<std::uintptr_t>(unrelated))}, {s5::Stage5Phase::A1Sentinel, 0U, {}}, 2000ms, 10000ms).exitCode, 0U); CloseHandle(unrelated);
}
TEST(Ex2Stage5SupervisorProcess, TerminationKillsChildAndDescendantBeforeDelayedPublication)
{
    for (const auto mode : {"delayed-marker", "descendant-marker"})
    {
        const auto marker = std::filesystem::temp_directory_path() / ("s5-no-survivor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        EXPECT_EQ(RunHelper({mode, marker.string()}).terminationReason, "cuda_attempt_timeout"); std::this_thread::sleep_for(1800ms); EXPECT_FALSE(std::filesystem::exists(marker));
    }
}
TEST(Ex2Stage5SupervisorProgress, HiddenPipeOptionIsOptionalStrictAndRemovedBeforePublicParse)
{
    std::vector<std::string_view> no{"--phase", "a1-sentinel"}; auto absent = c::ExtractProgressReporter(no); EXPECT_EQ(no.size(), 2U); EXPECT_EQ(absent.Observer().callback, nullptr);
    for (const auto value : {"", "0", "-1", "1.0", "18446744073709551616"})
    { std::vector<std::string_view> a{"--supervisor-progress-handle", value}; EXPECT_THROW(c::ExtractProgressReporter(a), std::invalid_argument); }
    std::vector<std::string_view> missing{"--supervisor-progress-handle"}; EXPECT_THROW(c::ExtractProgressReporter(missing), std::invalid_argument);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE read{}, write{}; ASSERT_TRUE(CreatePipe(&read, &write, &security, 4096U));
    const auto handle = std::to_string(reinterpret_cast<std::uintptr_t>(write));
    std::vector<std::string_view> valid{"--phase", "a1-sentinel", "--supervisor-progress-handle", handle}; auto p = c::ExtractProgressReporter(valid);
    EXPECT_NE(p.Observer().callback, nullptr); EXPECT_EQ(valid, no);
    std::vector<std::string_view> dup{"--supervisor-progress-handle", handle, "--supervisor-progress-handle", handle}; EXPECT_THROW(c::ExtractProgressReporter(dup), std::invalid_argument);
    CloseHandle(read); CloseHandle(write);
}
} // namespace
