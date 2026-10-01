#include "ex2/Ex2CorrectnessSupervisor.hpp"
#include <gtest/gtest.h>
#define NOMINMAX
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <thread>

namespace
{
namespace c = computelab::ex2::correctness::control;
using namespace std::chrono_literals;

c::ProcessResult RunHelper(std::vector<std::string> args, std::chrono::milliseconds op = 200ms,
    std::chrono::milliseconds child = 800ms)
{
    // This helper isolates protocol/backend/child behavior; campaign timeout is
    // tested independently below. Child modes sleep at most 10s, so this remote
    // test-only guard remains bounded if a local timeout breaks.
    return c::RunSupervisedProcess(COMPUTELAB_I7_TEST_CHILD, args, op, child,
        std::chrono::steady_clock::now() + 30s);
}

TEST(Ex2I7SupervisorProgress, ExactOrderAndChildTimestampDeadline)
{
    c::ProgressState p;
    p.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Started, c::AttemptBackend::Cuda, 100}, 110, 100);
    p.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Returned, c::AttemptBackend::Cuda, 120}, 130, 100);
    p.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Started, c::AttemptBackend::Vulkan, 130}, 140, 100);
    p.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Returned, c::AttemptBackend::Vulkan, 150}, 160, 100);
    EXPECT_TRUE(p.Full());
    EXPECT_FALSE(p.timedOutBackend);
    c::ProgressState delayed;
    delayed.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Started, c::AttemptBackend::Cuda, 100}, 250, 100);
    EXPECT_EQ(delayed.timedOutBackend, c::AttemptBackend::Cuda);
    c::ProgressState overtime;
    overtime.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Started, c::AttemptBackend::Cuda, 100}, 100, 100);
    overtime.Accept({c::ProgressMagic, 1U, c::AttemptEvent::Returned, c::AttemptBackend::Cuda, 200}, 200, 100);
    EXPECT_EQ(overtime.timedOutBackend, c::AttemptBackend::Cuda);
}

class ProtocolFailure : public testing::TestWithParam<const char*> {};
TEST_P(ProtocolFailure, FailsClosedWithoutRetry)
{
    const auto result = RunHelper({GetParam()});
    EXPECT_EQ(result.progressValidation, "invalid");
    EXPECT_EQ(result.terminationReason, "progress_protocol_error");
}
INSTANTIATE_TEST_SUITE_P(Ex2I7Supervisor, ProtocolFailure, testing::Values("bad-magic", "bad-version", "truncated",
    "returned-first", "duplicate-start", "duplicate-return", "backward", "vulkan-first", "extra", "empty-success"));

TEST(Ex2I7SupervisorProcess, ChildAndBothBackendDeadlinesTerminateOwnedJob)
{
    EXPECT_EQ(RunHelper({"no-progress"}).terminationReason, "child_timeout");
    EXPECT_EQ(RunHelper({"cuda-hang"}).terminationReason, "cuda_attempt_timeout");
    EXPECT_EQ(RunHelper({"vulkan-hang"}).terminationReason, "vulkan_attempt_timeout");
}
TEST(Ex2I7SupervisorProcess, NormalExitPrefixAndCrashAreDistinct)
{
    const auto complete = RunHelper({"complete"});
    EXPECT_EQ(complete.exitCode, 0U);
    EXPECT_EQ(complete.progressValidation, "complete");
    EXPECT_EQ(complete.eventCount, 4U);
    EXPECT_FALSE(complete.terminationReason);
    const auto prefix = RunHelper({"prefix"});
    EXPECT_EQ(prefix.exitCode, 3U);
    EXPECT_EQ(prefix.eventCount, 2U);
    EXPECT_EQ(prefix.progressValidation, "valid_prefix");
    const auto crash = RunHelper({"abnormal"});
    EXPECT_EQ(crash.exitCode, 0xc0000005U);
    EXPECT_EQ(crash.eventCount, 0U);
}
TEST(Ex2I7SupervisorProcess, WindowsQuotingJobBeforeResumeAndRestrictedInheritance)
{
    EXPECT_EQ(RunHelper({"quote", "space and \"quote\"", "trailing slash\\", ""}).exitCode, 0U);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE unrelated = CreateEventW(&security, TRUE, FALSE, nullptr);
    ASSERT_NE(unrelated, nullptr);
    const auto result = RunHelper({"restricted-handle", std::to_string(reinterpret_cast<std::uintptr_t>(unrelated))});
    CloseHandle(unrelated);
    EXPECT_EQ(result.exitCode, 0U);
}
TEST(Ex2I7SupervisorProcess, TerminatedChildCannotWriteDelayedMarker)
{
    const auto marker = std::filesystem::temp_directory_path()
        / ("i7-no-survivor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto result = RunHelper({"delayed-marker", marker.string()});
    EXPECT_EQ(result.terminationReason, "cuda_attempt_timeout");
    std::this_thread::sleep_for(1600ms);
    EXPECT_FALSE(std::filesystem::exists(marker));
    if (std::filesystem::exists(marker)) std::filesystem::remove(marker);
}
TEST(Ex2I7SupervisorProcess, ExhaustedCampaignNeverStartsLaterChild)
{
    const auto deadline = std::chrono::steady_clock::now() + 300ms;
    const auto first = c::RunSupervisedProcess(COMPUTELAB_I7_TEST_CHILD, {"no-progress"}, 200ms, 1000ms, deadline);
    EXPECT_EQ(first.terminationReason, "campaign_timeout");
    const auto next = c::RunSupervisedProcess(COMPUTELAB_I7_TEST_CHILD, {"complete"}, 200ms, 1000ms, deadline);
    EXPECT_EQ(next.terminationReason, "campaign_timeout");
    EXPECT_FALSE(next.exitCode);
    EXPECT_TRUE(next.launchTimeUtc.empty());
}
TEST(Ex2I7SupervisorProgress, HiddenOptionRejectsInvalidHandlesAndDuplicates)
{
    std::vector<std::string_view> direct{"--core-cell-index", "0"};
    auto disabled = c::ExtractProgressReporter(direct);
    EXPECT_EQ(disabled.Observer().callback, nullptr);
    for (auto args : {std::vector<std::string_view>{"--supervisor-progress-handle"},
            std::vector<std::string_view>{"--supervisor-progress-handle", "0"},
            std::vector<std::string_view>{"--supervisor-progress-handle", "18446744073709551616"},
            std::vector<std::string_view>{"--supervisor-progress-handle", "1", "--supervisor-progress-handle", "1"}})
        EXPECT_THROW(static_cast<void>(c::ExtractProgressReporter(args)), std::exception);
}
} // namespace
