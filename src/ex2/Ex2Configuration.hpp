#pragma once

#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2SemanticTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace computelab::ex2
{

enum class LinearVariant
{
    A1,
    A2,
};

enum class IndexedVariant
{
    B1,
    B2,
};

enum class IndexPattern
{
    StructuredV1,
    ShuffledV1,
};

enum class IterativeVariant
{
    D1,
    D2,
};

enum class TransferVariant
{
    E1,
    E2,
};

enum class LogicalExecutionMode
{
    Ordinary,
    Prepared,
};

enum class OperationBoundary
{
    SingleDispatchCompletion,
    AtomicDispatchCompletion,
    OrdinaryIterationSequenceCompletion,
    PreparedIterationReplayCompletion,
    PreparedSingleCopyCompletion,
};

enum class InstrumentMode
{
    H,
    N,
    P,
};

enum class Backend
{
    Cuda,
    Vulkan,
};

struct CommonConfiguration
{
    std::uint64_t seed{};
    std::string generatorRevision;
    LogicalExecutionMode executionMode{};
    OperationBoundary operationBoundary{};

    bool operator==(const CommonConfiguration&) const = default;
};

struct LinearConfiguration
{
    LinearVariant variant{};
    std::uint64_t elementCount{};

    bool operator==(const LinearConfiguration&) const = default;
};

struct IndexedConfiguration
{
    IndexedVariant variant{};
    std::uint64_t elementCount{};
    IndexPattern indexPattern{};

    bool operator==(const IndexedConfiguration&) const = default;
};

struct ContentionConfiguration
{
    std::uint64_t elementCount{};
    // Serialized as counter_count: the active B counters in [0, B).
    std::uint64_t activeCounterCount{};
    // C allocates exactly N counters; this field makes that invariant explicit.
    std::uint64_t allocatedCounterCount{};

    bool operator==(const ContentionConfiguration&) const = default;
};

struct IterativeConfiguration
{
    IterativeVariant variant{};
    std::uint64_t elementCount{};
    std::uint64_t iterationCount{};

    bool operator==(const IterativeConfiguration&) const = default;
};

struct TransferConfiguration
{
    TransferVariant variant{};
    std::uint64_t byteCount{};
    TransferDirection direction{};

    bool operator==(const TransferConfiguration&) const = default;
};

using WorkloadParameters = std::variant<
    LinearConfiguration,
    IndexedConfiguration,
    ContentionConfiguration,
    IterativeConfiguration,
    TransferConfiguration>;

struct WorkloadConfiguration
{
    CommonConfiguration common;
    WorkloadParameters parameters;

    bool operator==(const WorkloadConfiguration&) const = default;
};

enum class ConfigurationError
{
    UnsupportedGeneratorRevision,
    InvalidVariant,
    UnrepresentableElementCount,
    UnrepresentableByteCount,
    InvalidIndexPattern,
    StructuredPatternNotCoprime,
    InvalidActiveCounterCount,
    InvalidAllocatedCounterCount,
    ContentionCountNotDivisible,
    ContentionPermutationNotCoprime,
    UnrepresentableIterationCount,
    InvalidTransferDirection,
    VariantDirectionMismatch,
    InvalidExecutionMode,
    InvalidOperationBoundary,
};

struct ConfigurationValidation
{
    std::vector<ConfigurationError> errors;

    [[nodiscard]] bool IsValid() const noexcept { return errors.empty(); }
};

enum class CellEligibility
{
    InvalidSemanticConfiguration,
    CorrectnessOnly,
    ApprovedCoreCell,
    ConditionalExtension,
};

// Cell eligibility describes the frozen specification inventory only. It is
// never execution authorization or evidence of measurement qualification.

[[nodiscard]] std::string_view ToString(LinearVariant value) noexcept;
[[nodiscard]] std::string_view ToString(IndexedVariant value) noexcept;
[[nodiscard]] std::string_view ToString(IndexPattern value) noexcept;
[[nodiscard]] std::string_view ToString(IterativeVariant value) noexcept;
[[nodiscard]] std::string_view ToString(TransferVariant value) noexcept;
[[nodiscard]] std::string_view ToString(TransferDirection value) noexcept;
[[nodiscard]] std::string_view ToString(LogicalExecutionMode value) noexcept;
[[nodiscard]] std::string_view ToString(OperationBoundary value) noexcept;
[[nodiscard]] std::string_view ToString(InstrumentMode value) noexcept;
[[nodiscard]] std::string_view ToString(Backend value) noexcept;

[[nodiscard]] WorkloadConfiguration MakeConfiguration(
    WorkloadParameters parameters,
    std::uint64_t seed = CoreInputSeed);
[[nodiscard]] ConfigurationValidation ValidateSemanticConfiguration(
    const WorkloadConfiguration& configuration);
[[nodiscard]] bool IsCorrectnessTestEligible(
    const WorkloadConfiguration& configuration);
[[nodiscard]] CellEligibility ClassifyCellEligibility(
    const WorkloadConfiguration& configuration);
[[nodiscard]] const std::vector<WorkloadConfiguration>& ApprovedCoreCells();

using CanonicalValue = std::variant<std::nullptr_t, std::string, std::uint64_t>;
using CanonicalField = std::pair<std::string, CanonicalValue>;

struct SuppliedGpuIdentity
{
    std::string uuid;
    // Set only after an external CUDA/Vulkan runtime has established physical
    // identity. UUID syntax validation alone never sets or implies this flag.
    bool externallyVerified{};
};

struct ComparisonConditionContext
{
    std::string protocolVersion;
    std::string machineId;
    SuppliedGpuIdentity gpuIdentity;
    WorkloadConfiguration workload;
    InstrumentMode instrumentMode{};
};

struct SeriesIdentityContext
{
    ComparisonConditionContext condition;
    Backend backend{};
    std::uint64_t processIndex{};
    std::uint64_t blockIndex{};
    std::uint64_t orderSlot{};
    std::uint64_t warmupCount{};
    std::uint64_t plannedSampleCount{};
    std::string sourceRevision;
    std::string executableSha256;
    std::optional<std::string> shaderSha256;
};

[[nodiscard]] std::string CanonicalJson(std::vector<CanonicalField> fields);
[[nodiscard]] std::string ComparisonConditionCanonicalJson(
    const ComparisonConditionContext& context);
[[nodiscard]] std::string ComparisonConditionId(
    const ComparisonConditionContext& context);
[[nodiscard]] std::string SeriesCanonicalJson(
    const SeriesIdentityContext& context);
[[nodiscard]] std::string SeriesId(const SeriesIdentityContext& context);

enum class RunPlanError
{
    InvalidRunId,
    DuplicateRunId,
    InvalidDestination,
    DestinationOutsideLocalResults,
    DestinationLeafMismatch,
    DestinationCollision,
};

struct PlannedRun
{
    std::string runId;
    std::filesystem::path destination;
};

struct RunPlanValidation
{
    std::vector<RunPlanError> errors;

    [[nodiscard]] bool IsValid() const noexcept { return errors.empty(); }
};

[[nodiscard]] bool IsValidAnonymousIdentifier(std::string_view value) noexcept;
// Pure plan validation only: it performs no directory creation and cannot
// eliminate later filesystem races at evidence-package publication time.
[[nodiscard]] RunPlanValidation ValidateRunPlan(
    std::span<const PlannedRun> runs,
    const std::filesystem::path& localResultsRoot);

} // namespace computelab::ex2
