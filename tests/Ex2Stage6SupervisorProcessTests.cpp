#include "ex2/Ex2Stage6Supervisor.hpp"
#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <chrono>

namespace
{
namespace c = computelab::ex2::stage6::control;
namespace p = computelab::ex2::stage6::progress;
namespace a = computelab::ex2::stage6::analysis;
c::ProcessResult RunSynthetic(std::vector<std::string> args, std::uint64_t operation = 1000, std::uint64_t child = 4000, std::uint64_t campaign = 120000, c::ProcessTestFaults faults = {})
{
    LARGE_INTEGER f{}, now{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    return c::RunSupervisedProcess(COMPUTELAB_S6_TEST_CHILD, args, 0, operation, child, f.QuadPart, c::AddDeadline(now.QuadPart, c::DeadlineTicks(campaign, f.QuadPart)), std::move(faults));
}
void Quiescent(const c::ProcessResult& r)
{
    EXPECT_TRUE(r.processCreated); EXPECT_TRUE(r.containmentAssigned); EXPECT_TRUE(r.containmentVerified); EXPECT_TRUE(r.processResumed);
    EXPECT_TRUE(r.primaryTerminationConfirmed); EXPECT_EQ(r.jobActiveProcesses, 0); EXPECT_TRUE(r.jobEmptyConfirmed); EXPECT_FALSE(r.containmentVerificationFailed);
    EXPECT_TRUE(r.jobTotalProcesses); EXPECT_GE(r.jobTotalProcesses.value_or(0), 1);
}
TEST(Ex2Stage6SupervisorProcess, FullFreshJobAndTwoHundredObservations)
{
    const auto r = RunSynthetic({"full", "0"}); Quiescent(r); ASSERT_EQ(r.exitCode, 0); EXPECT_EQ(r.exitKind, c::ExitKind::VoluntaryStage6);
    EXPECT_EQ(r.progressForm, p::TerminalForm::Full); EXPECT_EQ(r.observations.size(), 200); EXPECT_TRUE(r.cleanEof); EXPECT_EQ(r.trailingBytes, 0);
    EXPECT_FALSE(r.descendantSurvivalObserved) << "Job lifetime total=" << r.jobTotalProcesses.value_or(0)
        << ", final active=" << r.jobActiveProcesses.value_or(999) << ", primary PID=" << r.processId.value_or(0);
    EXPECT_FALSE(r.operationTimedOut); EXPECT_FALSE(r.childTimedOut); EXPECT_FALSE(r.campaignTimedOut);
    EXPECT_FALSE(r.supervisorTerminationRequested); EXPECT_FALSE(r.progressTransportFailed);
}
TEST(Ex2Stage6SupervisorProcess, ContainedHelperCreationIsPermitted)
{
    unsigned samples{}; c::ProcessTestFaults faults;
    faults.naturalAccountingSample = [&](std::uint32_t&, std::uint32_t& active) { if (++samples <= 3) active = 1; };
    const auto r = RunSynthetic({"helper-clean", "0"}, 1000, 4000, 120000, faults); Quiescent(r); EXPECT_GT(r.jobTotalProcesses.value_or(0), 1);
    EXPECT_GT(samples, 3);
    EXPECT_FALSE(r.descendantSurvivalObserved); EXPECT_FALSE(r.supervisorTerminationRequested); EXPECT_EQ(r.exitCode, 0); EXPECT_EQ(r.progressForm, p::TerminalForm::Full);
}
TEST(Ex2Stage6SupervisorProcess, PrimaryAccountingLagSettlesNaturally)
{
    unsigned samples{}; c::ProcessTestFaults faults;
    faults.naturalAccountingSample = [&](std::uint32_t& total, std::uint32_t& active) {
        if (++samples <= 3) { total = 1; active = 1; }
    };
    const auto r = RunSynthetic({"full", "0"}, 1000, 4000, 120000, faults); Quiescent(r);
    EXPECT_GT(samples, 3); EXPECT_FALSE(r.descendantSurvivalObserved); EXPECT_FALSE(r.supervisorTerminationRequested);
    EXPECT_EQ(r.exitKind, c::ExitKind::VoluntaryStage6); EXPECT_EQ(r.exitCode, 0); EXPECT_EQ(r.progressForm, p::TerminalForm::Full);
}
TEST(Ex2Stage6SupervisorProcess, UnresolvedAccountingIsUncertaintyNotSurvival)
{
    unsigned samples{}; c::ProcessTestFaults faults;
    faults.naturalAccountingSample = [&](std::uint32_t& total, std::uint32_t& active) { ++samples; total = 1; active = 1; };
    const auto start = GetTickCount64();
    const auto r = RunSynthetic({"full", "0"}, 1000, 4000, 120000, faults);
    EXPECT_GT(samples, 1); EXPECT_GE(GetTickCount64() - start, c::ContainmentGraceMs);
    EXPECT_TRUE(r.primaryTerminationConfirmed); EXPECT_FALSE(r.descendantSurvivalObserved);
    EXPECT_TRUE(r.containmentVerificationFailed); EXPECT_TRUE(r.supervisorTerminationRequested); EXPECT_TRUE(r.terminateJobSucceeded);
    EXPECT_TRUE(r.jobEmptyConfirmed); EXPECT_EQ(r.jobActiveProcesses, 0); EXPECT_EQ(r.exitKind, c::ExitKind::SupervisorForced);
    EXPECT_EQ(c::ReconcileSlot(r, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
}
TEST(Ex2Stage6SupervisorProcess, SurvivingDescendantIsKilledAndRemainsFatal)
{
    const auto marker = std::filesystem::temp_directory_path() / ("computelab-s6-descendant-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    ASSERT_FALSE(std::filesystem::exists(marker)); const auto r = RunSynthetic({"descendant-survive", "0", marker.string()});
    Quiescent(r); EXPECT_GT(r.jobTotalProcesses.value_or(0), 1); EXPECT_TRUE(r.descendantSurvivalObserved); EXPECT_TRUE(r.supervisorTerminationRequested);
    EXPECT_TRUE(r.terminateJobSucceeded); EXPECT_EQ(c::ReconcileSlot(r, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
    Sleep(2000); EXPECT_FALSE(std::filesystem::exists(marker));
}
TEST(Ex2Stage6SupervisorProcess, WholeJobTimeoutPreventsDescendantMarker)
{
    const auto marker = std::filesystem::temp_directory_path() / ("computelab-s6-timeout-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    ASSERT_FALSE(std::filesystem::exists(marker)); const auto r = RunSynthetic({"descendant-hang", "0", marker.string()}, 100, 3000);
    Quiescent(r); EXPECT_TRUE(r.operationTimedOut); EXPECT_TRUE(r.supervisorTerminationRequested); EXPECT_TRUE(r.terminateJobSucceeded);
    EXPECT_EQ(r.exitCode, c::SupervisorForcedExitCode); EXPECT_EQ(r.exitKind, c::ExitKind::SupervisorForced); Sleep(2000); EXPECT_FALSE(std::filesystem::exists(marker));
}
TEST(Ex2Stage6SupervisorProcess, UnavailableFinalAccountingCannotEraseSurvivalFailure)
{
    LARGE_INTEGER f{}, now{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S6_TEST_CHILD, {"descendant-survive", "0"}, 0, 1000, 4000,
        f.QuadPart, c::AddDeadline(now.QuadPart, c::DeadlineTicks(10000, f.QuadPart)), {true});
    EXPECT_TRUE(r.primaryTerminationConfirmed); EXPECT_TRUE(r.descendantSurvivalObserved); EXPECT_TRUE(r.terminateJobSucceeded);
    EXPECT_TRUE(r.containmentVerificationFailed); EXPECT_FALSE(r.jobEmptyConfirmed);
    EXPECT_EQ(c::ReconcileSlot(r, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
}
TEST(Ex2Stage6SupervisorProcess, CompletePrefixesAndOutstandingBoundaries)
{
    for (const auto n : {1, 25, 99}) { const auto r = RunSynthetic({"prefix", "0", std::to_string(n)}); Quiescent(r);
        EXPECT_EQ(r.progressForm, p::TerminalForm::ReturnedPrefix); EXPECT_EQ(r.observations.size(), n * 2); EXPECT_TRUE(r.cleanEof); }
    for (const auto n : {0, 25, 99}) { const auto r = RunSynthetic({"outstanding", "0", std::to_string(n)}); Quiescent(r);
        EXPECT_EQ(r.progressForm, p::TerminalForm::OutstandingStartedPrefix); EXPECT_EQ(r.observations.size(), n * 2 + 1);
        EXPECT_EQ(c::ReconcileSlot(r, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal); }
}
TEST(Ex2Stage6SupervisorProcess, SilentPrimaryUsesChildOrCampaignDeadline)
{
    const auto child = RunSynthetic({"no-progress", "0"}, 50, 150); Quiescent(child); EXPECT_TRUE(child.childTimedOut); EXPECT_FALSE(child.operationTimedOut);
    const auto campaign = RunSynthetic({"no-progress", "0"}, 50, 3000, 200); Quiescent(campaign); EXPECT_TRUE(campaign.campaignTimedOut); EXPECT_FALSE(campaign.operationTimedOut);
}
TEST(Ex2Stage6SupervisorProcess, OutstandingAttemptAndCompletedPairBothEnforceOperationDeadline)
{
    const auto hung = RunSynthetic({"operation-hang", "0", "3"}, 100, 3000); Quiescent(hung); EXPECT_TRUE(hung.operationTimedOut); EXPECT_EQ(hung.timedOutSampleIndex, 3);
    const auto returned = RunSynthetic({"pair-timeout", "0"}, 100, 3000); Quiescent(returned); EXPECT_TRUE(returned.operationTimedOut); EXPECT_EQ(returned.timedOutSampleIndex, 0);
}
TEST(Ex2Stage6SupervisorProcess, RejectsWireAndGrammarCorruption)
{
    for (const auto mode : {"bad-magic", "bad-version", "bad-reserved", "bad-enum", "wrong-slot", "bad-sample", "returned-first", "future-qpc", "zero-qpc",
        "duplicate-start", "duplicate-return", "backward-qpc", "extra-event", "truncated", "trailing-byte"}) {
        SCOPED_TRACE(mode); const auto r = RunSynthetic({mode, "0"}); Quiescent(r); EXPECT_TRUE(r.progressInvalid); EXPECT_EQ(r.progressForm, p::TerminalForm::Invalid);
        EXPECT_EQ(c::ReconcileSlot(r, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
    }
}
TEST(Ex2Stage6SupervisorProcess, CompletedPairAtExactThresholdTimesOutButOneTickBelowDoesNot)
{
    const auto equal = RunSynthetic({"pair-exact", "0"}, 100, 3000); Quiescent(equal);
    EXPECT_TRUE(equal.operationTimedOut); EXPECT_EQ(equal.timedOutSampleIndex, 0);
    const auto below = RunSynthetic({"pair-below", "0"}, 100, 3000); Quiescent(below);
    EXPECT_FALSE(below.operationTimedOut); EXPECT_EQ(below.progressForm, p::TerminalForm::ReturnedPrefix);
    EXPECT_FALSE(below.supervisorTerminationRequested);
}
TEST(Ex2Stage6SupervisorProcess, BlockingReaderFramesPartialWritesAndCleanEof)
{ const auto r = RunSynthetic({"split-frame", "0"}); Quiescent(r); EXPECT_EQ(r.progressForm, p::TerminalForm::Full); EXPECT_TRUE(r.cleanEof); EXPECT_FALSE(r.progressTransportFailed); }
TEST(Ex2Stage6SupervisorProcess, EmptyAbnormalAndTransportAbortRemainDistinct)
{
    const auto empty = RunSynthetic({"empty", "0"}); EXPECT_EQ(empty.progressForm, p::TerminalForm::Empty); EXPECT_EQ(empty.exitCode, 0);
    const auto abnormal = RunSynthetic({"abnormal", "0"}); EXPECT_EQ(abnormal.exitKind, c::ExitKind::Abnormal); EXPECT_EQ(abnormal.exitCode, 0xC0000005U);
    const auto transport = RunSynthetic({"transport-abort", "0"}); EXPECT_EQ(transport.exitKind, c::ExitKind::ProgressTransportAbort); EXPECT_EQ(transport.exitCode, p::ProgressTransportAbortExitCode);
    EXPECT_EQ(c::ReconcileSlot(empty, {}).disposition, a::SlotDisposition::UnresolvedCampaignFatal);
}
TEST(Ex2Stage6SupervisorProcess, RestrictedInheritanceAndWindowsArgumentQuoting)
{
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; const auto event = CreateEventW(&security, TRUE, FALSE, nullptr); ASSERT_NE(event, nullptr);
    const auto r = RunSynthetic({"restricted-handle", "0", std::to_string(reinterpret_cast<std::uintptr_t>(event))}); EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT); CloseHandle(event);
    const auto quote = RunSynthetic({"quote", "0", "space and \"quote\"", "trailing slash\\", ""}); EXPECT_EQ(quote.exitCode, 0); EXPECT_EQ(quote.progressForm, p::TerminalForm::Full);
}
TEST(Ex2Stage6SupervisorProcess, ExpiredCampaignCannotResumeOrCreatePrimary)
{
    LARGE_INTEGER f{}, now{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&now);
    const auto r = c::RunSupervisedProcess(COMPUTELAB_S6_TEST_CHILD, {"full", "0"}, 0, 100, 1000, f.QuadPart, now.QuadPart);
    EXPECT_FALSE(r.processCreated); EXPECT_FALSE(r.processResumed); EXPECT_TRUE(r.campaignTimedOut); EXPECT_EQ(r.progressForm, p::TerminalForm::NotStarted);
}
} // namespace
