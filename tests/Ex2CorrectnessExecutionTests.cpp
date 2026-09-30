#include "app/Ex2CorrectnessExecution.hpp"

#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

namespace ex2 = computelab::ex2;
namespace correctness = computelab::ex2::correctness;
namespace evidence = computelab::ex2::evidence;

constexpr std::string_view kUuid =
    "00112233-4455-6677-8899-aabbccddeeff";
const std::string kSource(40U, 'a');
const std::string kExecutable(64U, 'b');
const std::string kShader(64U, 'c');

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto tick = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("computelab-i7-prompt2-" + std::to_string(tick));
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

computelab::results::EnvironmentRecord Common(
    const evidence::CorrectnessPlan& plan)
{
    const auto& series = plan.seriesIdentity;
    return {
        evidence::SchemaVersion, std::string(evidence::ExperimentId), plan.runId,
        "2026-09-30T00:00:00Z", series.sourceRevision, true,
        series.condition.machineId, "Windows", "11", "CPU fixture",
        std::numeric_limits<std::uint64_t>::max(), "GPU fixture", "NVIDIA",
        "10DE:0000", 8'589'934'592ULL, "driver", "toolkit", "runtime",
        "capability", "sdk", "api", "MSVC", "compiler", "cmake",
        "ninja", "x64-debug", "Debug", true, true};
}

correctness::PreparedFoundationPair Foundation(std::size_t index)
{
    const auto selection = correctness::SelectApprovedCoreCell(index);
    const bool transfer = std::holds_alternative<ex2::TransferConfiguration>(
        selection.configuration.parameters);
    return correctness::PrepareFoundationPair(selection, "i7-test-machine",
        std::string(kUuid), std::string(kSource), std::string(kExecutable),
        transfer ? std::nullopt
                 : std::optional<std::string>(std::string(kShader)),
        "i7-test-session-cuda", "i7-test-session-vulkan");
}

correctness::SerializedPair PassingPair(std::size_t index = 0U)
{
    auto foundation = Foundation(index);
    evidence::SampleRecord cuda;
    evidence::SampleRecord vulkan;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, correctness::ByteLogicalData>)
        {
            cuda = evidence::MakeI7ComparedByteSample(
                foundation.cuda, 0U, data.expected, data.expected);
            vulkan = evidence::MakeI7ComparedByteSample(
                foundation.vulkan, 0U, data.expected, data.expected);
        }
        else
        {
            cuda = evidence::MakeI7ComparedWordSample(
                foundation.cuda, 0U, data.expected, data.expected);
            vulkan = evidence::MakeI7ComparedWordSample(
                foundation.vulkan, 0U, data.expected, data.expected);
        }
    }, foundation.logicalData);
    return {
        correctness::BuildSerializedSeries(foundation.cuda,
            Common(foundation.cuda.Plan()), std::move(cuda), true,
            {.implementation = "test-cuda"}, "cpu-only setup fixture"),
        correctness::BuildSerializedSeries(foundation.vulkan,
            Common(foundation.vulkan.Plan()), std::move(vulkan), true,
            {.implementation = "test-vulkan"}, "cpu-only setup fixture")};
}

void Rewrite(const std::filesystem::path& path,
    std::string_view before, std::string_view after)
{
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), {});
    const auto position = text.find(before);
    ASSERT_NE(position, std::string::npos) << path.string();
    text.replace(position, before.size(), after);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

TEST(Ex2I7CorrectnessDispatch, ExactlyMatchesAllApprovedCoreCells)
{
    const auto& approved = ex2::ApprovedCoreCells();
    ASSERT_EQ(approved.size(), 22U);
    std::set<std::string> identities;
    for (std::size_t index = 0U; index < approved.size(); ++index)
    {
        const auto selected = correctness::SelectApprovedCoreCell(index);
        EXPECT_EQ(selected.index, index);
        EXPECT_EQ(selected.configuration, approved[index]);
        EXPECT_NO_THROW(correctness::RequireApprovedCoreCell(
            selected.configuration));
        const ex2::ComparisonConditionContext condition{
            "1.1", "test-machine", {std::string(kUuid), true},
            selected.configuration, ex2::InstrumentMode::P};
        EXPECT_TRUE(identities.insert(
            ex2::ComparisonConditionId(condition)).second);
    }
    EXPECT_THROW(static_cast<void>(correctness::SelectApprovedCoreCell(22U)),
        std::invalid_argument);
}

TEST(Ex2I7CorrectnessDispatch, RejectsD2AndCorrectnessOnlyCells)
{
    EXPECT_THROW(correctness::RequireApprovedCoreCell(ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D2, 256U, 16U})),
        std::invalid_argument);
    EXPECT_THROW(correctness::RequireApprovedCoreCell(ex2::MakeConfiguration(
        ex2::LinearConfiguration{ex2::LinearVariant::A1, 0U})),
        std::invalid_argument);
}

TEST(Ex2I7CorrectnessIdentity, UsesFrozenCorrectnessSeriesShape)
{
    const auto foundation = Foundation(0U);
    const auto& cuda = foundation.cuda.Plan();
    const auto& vulkan = foundation.vulkan.Plan();
    EXPECT_EQ(cuda.seriesIdentity.condition.protocolVersion, "1.1");
    EXPECT_EQ(cuda.seriesIdentity.condition.instrumentMode, ex2::InstrumentMode::P);
    EXPECT_EQ(cuda.seriesIdentity.warmupCount, 0U);
    EXPECT_EQ(cuda.seriesIdentity.plannedSampleCount, 1U);
    EXPECT_EQ(cuda.seriesIdentity.processIndex, 0U);
    EXPECT_EQ(cuda.seriesIdentity.blockIndex, 0U);
    EXPECT_EQ(cuda.seriesIdentity.orderSlot, 0U);
    EXPECT_EQ(vulkan.seriesIdentity.orderSlot, 1U);
    EXPECT_EQ(cuda.comparisonConditionId, vulkan.comparisonConditionId);
    EXPECT_NE(cuda.seriesId, vulkan.seriesId);
    EXPECT_NE(cuda.runId, vulkan.runId);
    EXPECT_FALSE(cuda.seriesIdentity.shaderSha256.has_value());
    EXPECT_EQ(vulkan.seriesIdentity.shaderSha256, kShader);
}

TEST(Ex2I7CorrectnessFoundation, PreservesCompositeAndByteIdentities)
{
    for (const std::size_t index : {0U, 3U, 5U, 8U, 11U, 14U, 16U, 19U})
    {
        auto foundation = Foundation(index);
        std::visit([&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, correctness::IndexedLogicalData>)
            {
                EXPECT_EQ(foundation.cuda.InputIdentity().Sha256(),
                    ex2::IndexedLogicalInputSha256(data.primary, data.permutation));
                auto changed = data.permutation;
                ASSERT_GT(changed.size(), 1U);
                std::swap(changed[0], changed[1]);
                EXPECT_NE(foundation.cuda.InputIdentity().Sha256(),
                    ex2::IndexedLogicalInputSha256(data.primary, changed));
            }
            else if constexpr (std::is_same_v<T, correctness::ContentionLogicalData>)
            {
                EXPECT_TRUE(std::ranges::all_of(data.initialCounters,
                    [](std::uint32_t value) { return value == 0U; }));
                EXPECT_EQ(foundation.cuda.InputIdentity().Sha256(),
                    ex2::ContentionLogicalInputSha256(
                        data.targets, data.initialCounters));
                EXPECT_NE(foundation.cuda.InputIdentity().Sha256(),
                    ex2::WordInputSha256(data.targets));
                auto nonzero = data.initialCounters;
                ASSERT_FALSE(nonzero.empty());
                nonzero[0] = 1U;
                EXPECT_THROW(static_cast<void>(
                    evidence::MakeI7ContentionInputIdentity(
                        foundation.cell.configuration, data.targets, nonzero)),
                    std::invalid_argument);
            }
            else if constexpr (std::is_same_v<T, correctness::ByteLogicalData>)
            {
                EXPECT_EQ(foundation.cuda.InputIdentity().Sha256(),
                    ex2::ByteInputSha256(data.source));
                EXPECT_FALSE(foundation.vulkan.Plan().seriesIdentity
                    .shaderSha256.has_value());
            }
            else
                EXPECT_EQ(foundation.cuda.InputIdentity().Sha256(),
                    ex2::WordInputSha256(data.input));
        }, foundation.logicalData);
    }
}

TEST(Ex2I7CorrectnessPackageAdapter, BuildsUntimedRepresentativeBCEBundles)
{
    for (const std::size_t index : {5U, 11U, 16U})
    {
        const auto foundation = Foundation(index);
        const auto pair = PassingPair(index);
        EXPECT_EQ(pair.cuda.environment.inputSha256,
            foundation.cuda.InputIdentity().Sha256());
        EXPECT_EQ(pair.vulkan.environment.inputSha256,
            foundation.vulkan.InputIdentity().Sha256());
        EXPECT_EQ(pair.cuda.environment.expectedOutputSha256,
            foundation.cuda.ExpectedOutputSha256());
        EXPECT_EQ(pair.vulkan.environment.expectedOutputSha256,
            foundation.vulkan.ExpectedOutputSha256());
        for (const auto* series : {&pair.cuda, &pair.vulkan})
        {
            ASSERT_EQ(series->samples.size(), 1U);
            const auto& sample = series->samples.front();
            EXPECT_FALSE(sample.hostSubmissionNanoseconds.has_value());
            EXPECT_FALSE(sample.hostWaitNanoseconds.has_value());
            EXPECT_FALSE(sample.hostCompletionNanoseconds.has_value());
            EXPECT_FALSE(sample.nativeDeviceIntervalNanoseconds.has_value());
        }
        if (index == 16U)
            EXPECT_FALSE(pair.vulkan.environment.plan.seriesIdentity
                .shaderSha256.has_value());
    }
}

TEST(Ex2I7CorrectnessFailureRecord, IsExternalBoundedAndNullable)
{
    const correctness::ExternalFailureRecord record{
        1U, "i7-failure-test", correctness::ExternalFailurePhase::Provenance,
        "git_state_unavailable", false, std::nullopt, std::nullopt,
        std::nullopt, false, "bounded diagnostic"};
    const auto json = correctness::SerializeExternalFailureRecord(record);
    EXPECT_NE(json.find("ex2-i7-correctness-child-failure"), std::string::npos);
    EXPECT_NE(json.find("\"foundation_established\":false"), std::string::npos);
    EXPECT_NE(json.find("\"cuda_run_id\":null"), std::string::npos);
    auto unsafe = record;
    unsafe.detail = "C:\\Users\\person\\secret";
    EXPECT_THROW(static_cast<void>(
        correctness::SerializeExternalFailureRecord(unsafe)),
        std::invalid_argument);
}

TEST(Ex2I7CorrectnessFailureMapping, PreservesPhaseTimeoutDeviceLossAndProgress)
{
    using Stage = correctness::PostFoundationFailureStage;
    using Kind = evidence::I7FailureKind;
    const auto map = [](Stage stage, bool timeout = false,
        bool deviceLost = false, bool completed = false,
        bool outputObserved = false) {
        return correctness::MapPostFoundationFailure(
            {stage, timeout, deviceLost, completed, outputObserved});
    };
    EXPECT_EQ(map(Stage::BackendInitialization),
        Kind::BackendInitializationFailed);
    EXPECT_EQ(map(Stage::ResourceAllocation), Kind::ResourceAllocationFailed);
    EXPECT_EQ(map(Stage::Submission), Kind::SubmissionFailed);
    EXPECT_EQ(map(Stage::Completion), Kind::CompletionFailed);
    EXPECT_EQ(map(Stage::Readback), Kind::ReadbackFailed);
    EXPECT_EQ(map(Stage::Submission, true), Kind::SubmissionTimeout);
    EXPECT_EQ(map(Stage::Completion, true), Kind::CompletionTimeout);
    EXPECT_EQ(map(Stage::BackendInitialization, false, true),
        Kind::DeviceLostDuringInitialization);
    EXPECT_EQ(map(Stage::Submission, false, true),
        Kind::DeviceLostDuringSubmission);
    EXPECT_EQ(map(Stage::Completion, false, true),
        Kind::DeviceLostDuringCompletion);
    EXPECT_EQ(map(Stage::Readback, false, true),
        Kind::DeviceLostDuringReadback);
    EXPECT_EQ(map(Stage::Interrupted), Kind::InterruptedBeforeCompletion);
    EXPECT_EQ(map(Stage::Interrupted, false, false, true),
        Kind::InterruptedAfterCompletion);
    EXPECT_EQ(map(Stage::Interrupted, false, false, true, true),
        Kind::InterruptedAfterOutput);
}

TEST(Ex2I7CorrectnessSetupBoundary, RejectsEarlyAndUntouchedBackendEvidence)
{
    ex2::a1::BackendObservation linear;
    linear.failurePhase = evidence::FailurePhase::BackendInitialization;
    EXPECT_FALSE(correctness::HasEstablishedSetup(linear));
    linear.operationCompleted = true;
    EXPECT_FALSE(correctness::HasEstablishedSetup(linear));
    ex2::b::BackendObservation indexed;
    indexed.failurePhase = ex2::b::IntegrationFailurePhase::InputUpload;
    EXPECT_FALSE(correctness::HasEstablishedSetup(indexed));
    indexed.failurePhase = ex2::b::IntegrationFailurePhase::InterruptedSecondBackend;
    EXPECT_FALSE(correctness::HasEstablishedSetup(indexed));
    indexed.nativeOperationCompleted = true;
    EXPECT_FALSE(correctness::HasEstablishedSetup(indexed));
    ex2::c::BackendObservation contention;
    contention.failurePhase = ex2::c::IntegrationFailurePhase::ResetCompletion;
    EXPECT_FALSE(correctness::HasEstablishedSetup(contention));
    ex2::d1::BackendObservation iterative;
    iterative.failurePhase = ex2::d1::IntegrationFailurePhase::CommandRecording;
    EXPECT_FALSE(correctness::HasEstablishedSetup(iterative));
    ex2::e::BackendObservation transfer;
    transfer.failurePhase = ex2::e::IntegrationFailurePhase::PreparationCompletion;
    EXPECT_FALSE(correctness::HasEstablishedSetup(transfer));

    const auto foundation = Foundation(0U);
    EXPECT_THROW(static_cast<void>(correctness::BuildSerializedSeries(
        foundation.cuda, Common(foundation.cuda.Plan()), {}, false,
        {.implementation = "test-cuda"}, "setup_complete")),
        std::invalid_argument);
}

TEST(Ex2I7CorrectnessSetupBoundary, RepresentsPostSetupFailures)
{
    ex2::a1::BackendObservation linear;
    linear.failurePhase = evidence::FailurePhase::Submission;
    EXPECT_TRUE(correctness::HasEstablishedSetup(linear));
    ex2::b::BackendObservation indexed;
    indexed.failurePhase = ex2::b::IntegrationFailurePhase::CompletionWait;
    EXPECT_TRUE(correctness::HasEstablishedSetup(indexed));
    ex2::c::BackendObservation contention;
    contention.failurePhase = ex2::c::IntegrationFailurePhase::CounterReadback;
    EXPECT_TRUE(correctness::HasEstablishedSetup(contention));
    ex2::d1::BackendObservation iterative;
    iterative.failurePhase = ex2::d1::IntegrationFailurePhase::SequenceSubmission;
    EXPECT_TRUE(correctness::HasEstablishedSetup(iterative));
    ex2::e::BackendObservation transfer;
    transfer.failurePhase = ex2::e::IntegrationFailurePhase::ValidationReadback;
    EXPECT_TRUE(correctness::HasEstablishedSetup(transfer));

    const auto foundation = Foundation(0U);
    for (const auto kind : {evidence::I7FailureKind::SubmissionFailed,
            evidence::I7FailureKind::CompletionFailed,
            evidence::I7FailureKind::ReadbackFailed})
    {
        const auto failure = evidence::MakeI7FailureObservation(
            foundation.cuda, 0U, kind);
        const auto series = correctness::BuildSerializedSeries(
            foundation.cuda, Common(foundation.cuda.Plan()), failure.sample, true,
            {.implementation = "test-cuda"}, "setup_complete");
        ASSERT_EQ(series.initialization.size(), 1U);
        ASSERT_EQ(series.samples.size(), 1U);
        EXPECT_EQ(series.samples.front().status, failure.sample.status);
        EXPECT_EQ(series.samples.front().failurePhase, failure.sample.failurePhase);
    }
}

TEST(Ex2I7CorrectnessFailureRecord, ClassifiesCompletedValidationAndStaging)
{
    const auto foundation = Foundation(0U);
    const auto& data = std::get<correctness::WordLogicalData>(foundation.logicalData);
    auto wrong = data.expected;
    wrong.front() ^= 1U;
    const auto sample = evidence::MakeI7ComparedWordSample(
        foundation.cuda, 0U, data.expected, wrong);
    EXPECT_EQ(sample.status, evidence::OperationStatus::ValidationFailed);
    const auto series = correctness::BuildSerializedSeries(
        foundation.cuda, Common(foundation.cuda.Plan()), sample, true,
        {.implementation = "test-cuda"}, "setup_complete");
    EXPECT_EQ(series.samples.front().status,
        evidence::OperationStatus::ValidationFailed);
    const auto validation = correctness::ClassifyExecutionFailure(true);
    EXPECT_EQ(validation.phase, correctness::ExternalFailurePhase::Validation);
    EXPECT_EQ(validation.errorCode, "completed_validation_failed");
    EXPECT_EQ(correctness::ClassifyExecutionFailure(false).phase,
        correctness::ExternalFailurePhase::BackendExecution);
    const auto staging = correctness::StagingCreationFailure();
    EXPECT_EQ(staging.phase, correctness::ExternalFailurePhase::EvidencePublication);
    EXPECT_EQ(staging.errorCode, "staging_creation_failed");
    EXPECT_EQ(staging.exitCode, correctness::ExitCode::PackageFailure);
}

TEST(Ex2I7CorrectnessFailureRecord, RetentionChecksActualIncompleteDirectory)
{
    TemporaryDirectory temporary;
    const auto plan = correctness::MakeSessionPlan(temporary.path, "i7-partial");
    EXPECT_FALSE(correctness::IsStagingRetained(plan));
    std::filesystem::create_directory(plan.stagingDirectory);
    EXPECT_TRUE(correctness::IsStagingRetained(plan));
}

TEST(Ex2I7CorrectnessPackagePlan, RejectsEveryExistingDestination)
{
    TemporaryDirectory temporary;
    for (int target = 0; target < 3; ++target)
    {
        const auto plan = correctness::MakeSessionPlan(
            temporary.path, "i7-plan-" + std::to_string(target));
        const auto path = target == 0 ? plan.finalDirectory
            : target == 1 ? plan.stagingDirectory : plan.failureSidecar;
        if (target == 2)
            std::ofstream(path) << "owned";
        else
            std::filesystem::create_directory(path);
        EXPECT_THROW(correctness::RequireUnusedSessionPlan(plan),
            std::invalid_argument);
    }
    EXPECT_THROW(static_cast<void>(
        correctness::MakeSessionPlan(temporary.path, "../escape")),
        std::invalid_argument);
}

TEST(Ex2I7CorrectnessPackage, WritesVerifiesAndAtomicallyFinalizesPair)
{
    TemporaryDirectory temporary;
    const auto plan = correctness::MakeSessionPlan(temporary.path, "i7-package-ok");
    const auto pair = PassingPair();
    correctness::CreateStagingSession(plan);
    correctness::WriteStagedSession(plan, pair);
    const auto verification = correctness::VerifyStagedSession(plan, pair);
    EXPECT_EQ(verification.artifacts.size(), 8U);
    for (const auto& artifact : verification.artifacts)
    {
        EXPECT_GT(artifact.sizeBytes, 0U);
        EXPECT_EQ(artifact.sha256.size(), 64U);
    }
    correctness::FinalizeStagedSession(plan);
    EXPECT_TRUE(std::filesystem::is_directory(plan.finalDirectory));
    EXPECT_FALSE(std::filesystem::exists(plan.stagingDirectory));
}

TEST(Ex2I7CorrectnessPackage, FailedFinalRenameRetainsIncompleteWithoutOverwrite)
{
    TemporaryDirectory temporary;
    const auto plan = correctness::MakeSessionPlan(
        temporary.path, "i7-package-rename-failure");
    const auto pair = PassingPair();
    correctness::CreateStagingSession(plan);
    correctness::WriteStagedSession(plan, pair);
    static_cast<void>(correctness::VerifyStagedSession(plan, pair));
    std::filesystem::create_directory(plan.finalDirectory);
    EXPECT_THROW(correctness::FinalizeStagedSession(plan), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_directory(plan.stagingDirectory));
    EXPECT_TRUE(std::filesystem::is_directory(plan.finalDirectory));
}

TEST(Ex2I7CorrectnessPackage, RejectsDiskCorruptionMatrix)
{
    using Mutation = std::function<void(const correctness::SessionPlan&)>;
    const std::vector<Mutation> mutations{
        [](const auto& p) { std::filesystem::remove(p.cudaDirectory / "environment.json"); },
        [](const auto& p) { std::filesystem::remove(p.cudaDirectory / "samples.csv"); },
        [](const auto& p) { std::ofstream(p.cudaDirectory / "extra.txt") << "extra"; },
        [](const auto& p) { Rewrite(p.cudaDirectory / "samples.csv", "schema_version", "schema_broken"); },
        [](const auto& p) { std::ifstream in(p.cudaDirectory / "samples.csv", std::ios::binary); std::string s((std::istreambuf_iterator<char>(in)), {}); const auto n=s.find('\n'); std::ofstream(p.cudaDirectory / "samples.csv", std::ios::binary|std::ios::app) << s.substr(n+1); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "samples.csv", "i7-test-session-cuda", "i7-test-session-other"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"comparison_condition_id\":\"", "\"comparison_condition_id\":\"0"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"series_id\":\"", "\"series_id\":\"0"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"input_sha256\":\"", "\"input_sha256\":\"0"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"expected_output_sha256\":\"", "\"expected_output_sha256\":\"0"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", std::string(kUuid), "10112233-4455-6677-8899-aabbccddeeff"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"backend\":\"cuda\"", "\"backend\":\"vulkan\""); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"instrument_mode\":\"P\"", "\"instrument_mode\":\"H\""); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "samples.csv", ",ok,,,,,,\r\n", ",ok,,,,,1,\r\n"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "summary.json", "\"recorded_sample_count\":1", "\"recorded_sample_count\":2"); },
        [](const auto& p) { Rewrite(p.cudaDirectory / "environment.json", "\"shader_sha256\":null", "\"shader_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\""); },
        [](const auto& p) { Rewrite(p.vulkanDirectory / "environment.json", "\"comparison_condition_id\":\"", "\"comparison_condition_id\":\"0"); },
    };
    for (std::size_t index = 0U; index < mutations.size(); ++index)
    {
        TemporaryDirectory temporary;
        const auto plan = correctness::MakeSessionPlan(
            temporary.path, "i7-corrupt-" + std::to_string(index));
        const auto pair = PassingPair();
        correctness::CreateStagingSession(plan);
        correctness::WriteStagedSession(plan, pair);
        mutations[index](plan);
        EXPECT_ANY_THROW(static_cast<void>(
            correctness::VerifyStagedSession(plan, pair))) << index;
        EXPECT_TRUE(std::filesystem::is_directory(plan.stagingDirectory));
    }
}

} // namespace
