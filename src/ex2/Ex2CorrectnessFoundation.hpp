#pragma once

#include "ex2/Ex2Evidence.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace computelab::ex2::evidence
{

inline constexpr std::string_view I7CorrectnessProtocolVersion = "1.1";

// This value proves canonical hashing of the supplied host bytes and their
// agreement with the typed workload configuration. It does not prove that a
// GPU consumed those bytes; Prompt 2 must supply that execution observation.
class I7CorrectnessInputIdentity final
{
public:
    [[nodiscard]] const WorkloadConfiguration& Workload() const noexcept;
    [[nodiscard]] const std::string& Sha256() const noexcept;

private:
    I7CorrectnessInputIdentity(
        WorkloadConfiguration workload,
        std::string sha256);

    WorkloadConfiguration workload_;
    std::string sha256_;

    friend I7CorrectnessInputIdentity MakeI7WordInputIdentity(
        const WorkloadConfiguration&, std::span<const std::uint32_t>);
    friend I7CorrectnessInputIdentity MakeI7ByteInputIdentity(
        const WorkloadConfiguration&, std::span<const std::uint8_t>);
    friend I7CorrectnessInputIdentity MakeI7IndexedInputIdentity(
        const WorkloadConfiguration&, std::span<const std::uint32_t>,
        std::span<const std::uint32_t>);
    friend I7CorrectnessInputIdentity MakeI7ContentionInputIdentity(
        const WorkloadConfiguration&, std::span<const std::uint32_t>,
        std::span<const std::uint32_t>);
};

[[nodiscard]] I7CorrectnessInputIdentity MakeI7WordInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> input);
[[nodiscard]] I7CorrectnessInputIdentity MakeI7ByteInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint8_t> input);
[[nodiscard]] I7CorrectnessInputIdentity MakeI7IndexedInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> primary,
    std::span<const std::uint32_t> permutation);
[[nodiscard]] I7CorrectnessInputIdentity MakeI7ContentionInputIdentity(
    const WorkloadConfiguration& workload,
    std::span<const std::uint32_t> targets,
    std::span<const std::uint32_t> initialCounters);

class I7CorrectnessFoundation final
{
public:
    [[nodiscard]] const CorrectnessPlan& Plan() const noexcept;
    [[nodiscard]] const I7CorrectnessInputIdentity& InputIdentity() const noexcept;
    [[nodiscard]] const std::string& ExpectedOutputSha256() const noexcept;
    [[nodiscard]] bool ExpectedOutputUsesWords() const noexcept;

private:
    I7CorrectnessFoundation(
        CorrectnessPlan plan,
        I7CorrectnessInputIdentity inputIdentity,
        std::string expectedOutputSha256,
        bool expectedOutputUsesWords);

    CorrectnessPlan plan_;
    I7CorrectnessInputIdentity inputIdentity_;
    std::string expectedOutputSha256_;
    bool expectedOutputUsesWords_{};

    friend I7CorrectnessFoundation MakeI7WordCorrectnessFoundation(
        std::string, SeriesIdentityContext,
        I7CorrectnessInputIdentity, std::span<const std::uint32_t>);
    friend I7CorrectnessFoundation MakeI7ByteCorrectnessFoundation(
        std::string, SeriesIdentityContext,
        I7CorrectnessInputIdentity, std::span<const std::uint8_t>);
};

// Strict future-I7 seam: protocol 1.1, one of the 22 approved core cells,
// and an input identity constructed from the exact same typed configuration.
[[nodiscard]] I7CorrectnessFoundation MakeI7WordCorrectnessFoundation(
    std::string runId,
    SeriesIdentityContext seriesIdentity,
    I7CorrectnessInputIdentity inputIdentity,
    std::span<const std::uint32_t> expectedOutput);
[[nodiscard]] I7CorrectnessFoundation MakeI7ByteCorrectnessFoundation(
    std::string runId,
    SeriesIdentityContext seriesIdentity,
    I7CorrectnessInputIdentity inputIdentity,
    std::span<const std::uint8_t> expectedOutput);

[[nodiscard]] EnvironmentRecord MakeI7EnvironmentRecord(
    results::EnvironmentRecord common,
    const I7CorrectnessFoundation& foundation,
    BackendDiagnostics diagnostics);

[[nodiscard]] InitializationRecord MakeI7SetupCompleteInitialization(
    const I7CorrectnessFoundation& foundation,
    std::string observation);

// The passing/failing status is derived from a full exact comparison rather
// than accepted as a caller-set status flag.
[[nodiscard]] SampleRecord MakeI7ComparedWordSample(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    std::span<const std::uint32_t> expected,
    std::span<const std::uint32_t> observed);
[[nodiscard]] SampleRecord MakeI7ComparedByteSample(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    std::span<const std::uint8_t> expected,
    std::span<const std::uint8_t> observed);

// Only failures observed after a valid I7 correctness foundation exists are
// representable as plan-bound samples. Configuration or input-generation
// failures before foundation construction belong to Prompt 2's external
// execution/package failure mechanism; it must not fabricate a plan, expected
// output, GPU identity, or sample row for them.
enum class I7FailureKind
{
    BackendInitializationFailed,
    ResourceAllocationFailed,
    SubmissionFailed,
    CompletionFailed,
    SubmissionTimeout,
    CompletionTimeout,
    DeviceLostDuringInitialization,
    DeviceLostDuringSubmission,
    DeviceLostDuringCompletion,
    DeviceLostDuringReadback,
    ReadbackFailed,
    InterruptedBeforeCompletion,
    InterruptedAfterCompletion,
    InterruptedAfterOutput,
};

struct I7FailureObservation
{
    SampleRecord sample;
    // Kept at the execution-observation boundary only. Schema v2 has no field
    // for native diagnostic text, so it is not silently serialized.
    std::optional<std::string> nativeDetail;
};

[[nodiscard]] I7FailureObservation MakeI7FailureObservation(
    const I7CorrectnessFoundation& foundation,
    std::uint64_t sampleIndex,
    I7FailureKind failure,
    std::optional<std::string> nativeDetail = std::nullopt);

} // namespace computelab::ex2::evidence
