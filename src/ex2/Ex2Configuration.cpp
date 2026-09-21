#include "ex2/Ex2Configuration.hpp"

#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace computelab::ex2
{
namespace
{

constexpr std::uint64_t kA1ConfirmationElementCount = 33'554'432ULL;

template <typename T>
void AppendInteger(std::string& output, T value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
    {
        throw std::runtime_error("integer serialization failed");
    }
    output.append(buffer.data(), end);
}

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

bool IsValidEnum(LinearVariant value) noexcept
{
    return value == LinearVariant::A1 || value == LinearVariant::A2;
}

bool IsValidEnum(IndexedVariant value) noexcept
{
    return value == IndexedVariant::B1 || value == IndexedVariant::B2;
}

bool IsValidEnum(IndexPattern value) noexcept
{
    return value == IndexPattern::StructuredV1
        || value == IndexPattern::ShuffledV1;
}

bool IsValidEnum(IterativeVariant value) noexcept
{
    return value == IterativeVariant::D1 || value == IterativeVariant::D2;
}

bool IsValidEnum(TransferVariant value) noexcept
{
    return value == TransferVariant::E1 || value == TransferVariant::E2;
}

bool IsValidEnum(TransferDirection value) noexcept
{
    return value == TransferDirection::HostToDevice
        || value == TransferDirection::DeviceToHost;
}

bool IsValidEnum(InstrumentMode value) noexcept
{
    return value == InstrumentMode::H || value == InstrumentMode::N
        || value == InstrumentMode::P;
}

bool IsValidEnum(Backend value) noexcept
{
    return value == Backend::Cuda || value == Backend::Vulkan;
}

OperationBoundary RequiredBoundary(const WorkloadParameters& parameters)
{
    return std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ContentionConfiguration>)
            return OperationBoundary::AtomicDispatchCompletion;
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
            return value.variant == IterativeVariant::D2
                ? OperationBoundary::PreparedIterationReplayCompletion
                : OperationBoundary::OrdinaryIterationSequenceCompletion;
        else if constexpr (std::is_same_v<T, TransferConfiguration>)
            return OperationBoundary::PreparedSingleCopyCompletion;
        else
            return OperationBoundary::SingleDispatchCompletion;
    }, parameters);
}

LogicalExecutionMode RequiredExecutionMode(const WorkloadParameters& parameters)
{
    if (std::holds_alternative<TransferConfiguration>(parameters))
        return LogicalExecutionMode::Prepared;
    if (const auto* iterative = std::get_if<IterativeConfiguration>(&parameters);
        iterative != nullptr && iterative->variant == IterativeVariant::D2)
    {
        return LogicalExecutionMode::Prepared;
    }
    return LogicalExecutionMode::Ordinary;
}

void AddError(ConfigurationValidation& result, ConfigurationError error)
{
    if (std::find(result.errors.begin(), result.errors.end(), error)
        == result.errors.end())
    {
        result.errors.push_back(error);
    }
}

bool IsCoreCommon(const CommonConfiguration& common) noexcept
{
    return common.seed == CoreInputSeed
        && common.generatorRevision == InputGeneratorRevision;
}

std::string_view WorkloadId(const WorkloadParameters& parameters) noexcept
{
    if (std::holds_alternative<LinearConfiguration>(parameters)) return "A";
    if (std::holds_alternative<IndexedConfiguration>(parameters)) return "B";
    if (std::holds_alternative<ContentionConfiguration>(parameters)) return "C";
    if (std::holds_alternative<IterativeConfiguration>(parameters)) return "D";
    return "E";
}

std::string_view VariantId(const WorkloadParameters& parameters) noexcept
{
    return std::visit([](const auto& value) -> std::string_view {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
            return ToString(value.variant);
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            return ToString(value.variant);
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
            return "C";
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
            return ToString(value.variant);
        else
            return ToString(value.variant);
    }, parameters);
}

std::vector<CanonicalField> ConditionFields(
    const ComparisonConditionContext& context)
{
    std::optional<std::uint64_t> elementCount;
    std::optional<std::uint64_t> byteCount;
    std::optional<std::uint64_t> counterCount;
    std::optional<std::uint64_t> iterationCount;
    std::optional<std::string> indexPattern;
    std::optional<std::string> transferDirection;

    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
            elementCount = value.elementCount;
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
        {
            elementCount = value.elementCount;
            indexPattern = std::string(ToString(value.indexPattern));
        }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        {
            elementCount = value.elementCount;
            counterCount = value.activeCounterCount;
        }
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        {
            elementCount = value.elementCount;
            iterationCount = value.iterationCount;
        }
        else
        {
            byteCount = value.byteCount;
            transferDirection = std::string(ToString(value.direction));
        }
    }, context.workload.parameters);

    auto optionalInteger = [](const std::optional<std::uint64_t>& value) {
        return value.has_value() ? CanonicalValue{*value} : CanonicalValue{nullptr};
    };
    auto optionalString = [](const std::optional<std::string>& value) {
        return value.has_value() ? CanonicalValue{*value} : CanonicalValue{nullptr};
    };
    return {
        {"byte_count", optionalInteger(byteCount)},
        {"counter_count", optionalInteger(counterCount)},
        {"element_count", optionalInteger(elementCount)},
        {"execution_mode", std::string(ToString(context.workload.common.executionMode))},
        {"generator_revision", context.workload.common.generatorRevision},
        {"gpu_uuid_identity", context.gpuIdentity.uuid},
        {"index_pattern", optionalString(indexPattern)},
        {"instrument_mode", std::string(ToString(context.instrumentMode))},
        {"iteration_count", optionalInteger(iterationCount)},
        {"machine_id", context.machineId},
        {"operation_boundary", std::string(ToString(context.workload.common.operationBoundary))},
        {"protocol_version", context.protocolVersion},
        {"seed", context.workload.common.seed},
        {"transfer_direction", optionalString(transferDirection)},
        {"variant", std::string(VariantId(context.workload.parameters))},
        {"workload", std::string(WorkloadId(context.workload.parameters))},
    };
}

bool IsLowerHex(std::string_view value, std::size_t length) noexcept
{
    return value.size() == length
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

bool IsHex(std::string_view value, std::size_t length) noexcept
{
    return value.size() == length
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f')
                || (character >= 'A' && character <= 'F');
        });
}

bool IsCanonicalUuid(std::string_view value) noexcept
{
    if (value.size() != 36U || value[8] != '-' || value[13] != '-'
        || value[18] != '-' || value[23] != '-')
    {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
            continue;
        const char character = value[index];
        if (!((character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

void ValidateConditionContext(const ComparisonConditionContext& context)
{
    if (context.protocolVersion != "1.0")
        throw std::invalid_argument("EX-2 protocol version must be exactly 1.0");
    if (!IsValidAnonymousIdentifier(context.machineId))
        throw std::invalid_argument("EX-2 machine_id is not an anonymous identifier");
    if (!context.gpuIdentity.externallyVerified)
        throw std::invalid_argument("EX-2 GPU UUID identity was not externally verified");
    if (!IsCanonicalUuid(context.gpuIdentity.uuid))
        throw std::invalid_argument("EX-2 GPU UUID identity is not canonical lowercase UUID text");
    if (!ValidateSemanticConfiguration(context.workload).IsValid())
        throw std::invalid_argument("EX-2 workload configuration is not semantically valid");
    if (!IsValidEnum(context.instrumentMode))
        throw std::invalid_argument("EX-2 instrument mode is invalid");
}

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](char character) {
        if (character >= 'A' && character <= 'Z')
            return static_cast<char>(character - 'A' + 'a');
        return character;
    });
    return value;
}

} // namespace

std::string_view ToString(LinearVariant value) noexcept
{
    if (value == LinearVariant::A1) return "A1";
    if (value == LinearVariant::A2) return "A2";
    return "invalid";
}

std::string_view ToString(IndexedVariant value) noexcept
{
    if (value == IndexedVariant::B1) return "B1";
    if (value == IndexedVariant::B2) return "B2";
    return "invalid";
}

std::string_view ToString(IndexPattern value) noexcept
{
    if (value == IndexPattern::StructuredV1) return "structured-v1";
    if (value == IndexPattern::ShuffledV1) return "shuffled-v1";
    return "invalid";
}

std::string_view ToString(IterativeVariant value) noexcept
{
    if (value == IterativeVariant::D1) return "D1";
    if (value == IterativeVariant::D2) return "D2";
    return "invalid";
}

std::string_view ToString(TransferVariant value) noexcept
{
    if (value == TransferVariant::E1) return "E1";
    if (value == TransferVariant::E2) return "E2";
    return "invalid";
}

std::string_view ToString(TransferDirection value) noexcept
{
    if (value == TransferDirection::HostToDevice) return "H2D";
    if (value == TransferDirection::DeviceToHost) return "D2H";
    return "invalid";
}

std::string_view ToString(LogicalExecutionMode value) noexcept
{
    if (value == LogicalExecutionMode::Ordinary) return "ordinary";
    if (value == LogicalExecutionMode::Prepared) return "prepared";
    return "invalid";
}

std::string_view ToString(OperationBoundary value) noexcept
{
    if (value == OperationBoundary::SingleDispatchCompletion)
        return "single-dispatch-completion";
    if (value == OperationBoundary::AtomicDispatchCompletion)
        return "atomic-dispatch-completion";
    if (value == OperationBoundary::OrdinaryIterationSequenceCompletion)
        return "ordinary-iteration-sequence-completion";
    if (value == OperationBoundary::PreparedIterationReplayCompletion)
        return "prepared-iteration-replay-completion";
    if (value == OperationBoundary::PreparedSingleCopyCompletion)
        return "prepared-single-copy-completion";
    return "invalid";
}

std::string_view ToString(InstrumentMode value) noexcept
{
    if (value == InstrumentMode::H) return "H";
    if (value == InstrumentMode::N) return "N";
    if (value == InstrumentMode::P) return "P";
    return "invalid";
}

std::string_view ToString(Backend value) noexcept
{
    if (value == Backend::Cuda) return "cuda";
    if (value == Backend::Vulkan) return "vulkan";
    return "invalid";
}

WorkloadConfiguration MakeConfiguration(
    WorkloadParameters parameters,
    std::uint64_t seed)
{
    return {
        {seed, std::string(InputGeneratorRevision),
            RequiredExecutionMode(parameters), RequiredBoundary(parameters)},
        std::move(parameters)};
}

ConfigurationValidation ValidateSemanticConfiguration(
    const WorkloadConfiguration& configuration)
{
    ConfigurationValidation result;
    if (configuration.common.generatorRevision != InputGeneratorRevision)
        AddError(result, ConfigurationError::UnsupportedGeneratorRevision);
    if (configuration.common.executionMode
        != RequiredExecutionMode(configuration.parameters))
        AddError(result, ConfigurationError::InvalidExecutionMode);
    if (configuration.common.operationBoundary
        != RequiredBoundary(configuration.parameters))
        AddError(result, ConfigurationError::InvalidOperationBoundary);

    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
        {
            if (!IsValidEnum(value.variant))
                AddError(result, ConfigurationError::InvalidVariant);
            if (value.elementCount > std::numeric_limits<std::uint32_t>::max())
                AddError(result, ConfigurationError::UnrepresentableElementCount);
        }
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
        {
            if (!IsValidEnum(value.variant))
                AddError(result, ConfigurationError::InvalidVariant);
            if (value.elementCount > std::numeric_limits<std::uint32_t>::max())
                AddError(result, ConfigurationError::UnrepresentableElementCount);
            if (!IsValidEnum(value.indexPattern))
                AddError(result, ConfigurationError::InvalidIndexPattern);
            else if (value.indexPattern == IndexPattern::StructuredV1
                && value.elementCount != 0U
                && std::gcd(8191ULL, value.elementCount) != 1U)
                AddError(result, ConfigurationError::StructuredPatternNotCoprime);
        }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        {
            if (value.elementCount > std::numeric_limits<std::uint32_t>::max())
                AddError(result, ConfigurationError::UnrepresentableElementCount);
            if (value.allocatedCounterCount != value.elementCount)
                AddError(result, ConfigurationError::InvalidAllocatedCounterCount);
            if (value.elementCount == 0U)
            {
                if (value.activeCounterCount != 0U)
                    AddError(result, ConfigurationError::InvalidActiveCounterCount);
            }
            else
            {
                if (value.activeCounterCount == 0U
                    || value.activeCounterCount > value.elementCount
                    || value.activeCounterCount
                        > std::numeric_limits<std::uint32_t>::max())
                    AddError(result, ConfigurationError::InvalidActiveCounterCount);
                else if (value.elementCount % value.activeCounterCount != 0U)
                    AddError(result, ConfigurationError::ContentionCountNotDivisible);
                if (std::gcd(8191ULL, value.elementCount) != 1U)
                    AddError(result, ConfigurationError::ContentionPermutationNotCoprime);
            }
        }
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        {
            if (!IsValidEnum(value.variant))
                AddError(result, ConfigurationError::InvalidVariant);
            if (value.elementCount > std::numeric_limits<std::uint32_t>::max())
                AddError(result, ConfigurationError::UnrepresentableElementCount);
            if (value.iterationCount > std::numeric_limits<std::uint32_t>::max())
                AddError(result, ConfigurationError::UnrepresentableIterationCount);
        }
        else
        {
            if (!IsValidEnum(value.variant))
                AddError(result, ConfigurationError::InvalidVariant);
            if (!IsValidEnum(value.direction))
                AddError(result, ConfigurationError::InvalidTransferDirection);
            else if ((value.variant == TransferVariant::E1
                    && value.direction != TransferDirection::HostToDevice)
                || (value.variant == TransferVariant::E2
                    && value.direction != TransferDirection::DeviceToHost))
                AddError(result, ConfigurationError::VariantDirectionMismatch);
            if (value.byteCount > std::vector<std::uint8_t>{}.max_size())
                AddError(result, ConfigurationError::UnrepresentableByteCount);
        }
    }, configuration.parameters);
    return result;
}

bool IsCorrectnessTestEligible(const WorkloadConfiguration& configuration)
{
    if (!ValidateSemanticConfiguration(configuration).IsValid()) return false;
    const auto* iterative = std::get_if<IterativeConfiguration>(
        &configuration.parameters);
    return iterative == nullptr || iterative->variant != IterativeVariant::D2;
}

const std::vector<WorkloadConfiguration>& ApprovedCoreCells()
{
    static const std::vector<WorkloadConfiguration> cells = {
        MakeConfiguration(LinearConfiguration{LinearVariant::A1, 256U}),
        MakeConfiguration(LinearConfiguration{LinearVariant::A1, 262'144U}),
        MakeConfiguration(LinearConfiguration{LinearVariant::A1, 16'777'216U}),
        MakeConfiguration(LinearConfiguration{LinearVariant::A2, 262'144U}),
        MakeConfiguration(LinearConfiguration{LinearVariant::A2, 16'777'216U}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B1, 262'144U, IndexPattern::StructuredV1}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B1, 262'144U, IndexPattern::ShuffledV1}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B1, 16'777'216U, IndexPattern::ShuffledV1}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B2, 262'144U, IndexPattern::StructuredV1}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B2, 262'144U, IndexPattern::ShuffledV1}),
        MakeConfiguration(IndexedConfiguration{IndexedVariant::B2, 16'777'216U, IndexPattern::ShuffledV1}),
        MakeConfiguration(ContentionConfiguration{1'048'576U, 1'048'576U, 1'048'576U}),
        MakeConfiguration(ContentionConfiguration{1'048'576U, 32'768U, 1'048'576U}),
        MakeConfiguration(ContentionConfiguration{1'048'576U, 64U, 1'048'576U}),
        MakeConfiguration(IterativeConfiguration{IterativeVariant::D1, 262'144U, 16U}),
        MakeConfiguration(IterativeConfiguration{IterativeVariant::D1, 1'048'576U, 64U}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E1, 1'024U, TransferDirection::HostToDevice}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E1, 1'048'576U, TransferDirection::HostToDevice}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E1, 67'108'864U, TransferDirection::HostToDevice}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E2, 1'024U, TransferDirection::DeviceToHost}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E2, 1'048'576U, TransferDirection::DeviceToHost}),
        MakeConfiguration(TransferConfiguration{TransferVariant::E2, 67'108'864U, TransferDirection::DeviceToHost}),
    };
    return cells;
}

CellEligibility ClassifyCellEligibility(
    const WorkloadConfiguration& configuration)
{
    if (!ValidateSemanticConfiguration(configuration).IsValid())
        return CellEligibility::InvalidSemanticConfiguration;
    if (const auto* iterative = std::get_if<IterativeConfiguration>(
            &configuration.parameters);
        iterative != nullptr && iterative->variant == IterativeVariant::D2)
    {
        return CellEligibility::ConditionalExtension;
    }
    const auto& cells = ApprovedCoreCells();
    if (std::find(cells.begin(), cells.end(), configuration) != cells.end())
        return CellEligibility::ApprovedCoreCell;
    if (IsCoreCommon(configuration.common))
    {
        if (const auto* linear = std::get_if<LinearConfiguration>(
                &configuration.parameters);
            linear != nullptr && linear->variant == LinearVariant::A1
            && linear->elementCount == kA1ConfirmationElementCount)
        {
            return CellEligibility::ConditionalExtension;
        }
    }
    return CellEligibility::CorrectnessOnly;
}

std::string CanonicalJson(std::vector<CanonicalField> fields)
{
    for (const auto& [key, unused] : fields)
    {
        static_cast<void>(unused);
        if (key.empty() || !std::all_of(key.begin(), key.end(), [](char character) {
                return (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9')
                    || character == '_';
            }))
        {
            throw std::invalid_argument(
                "canonical identity keys must use lowercase ASCII names");
        }
    }
    std::sort(fields.begin(), fields.end(),
        [](const CanonicalField& left, const CanonicalField& right) {
            return left.first < right.first;
        });
    for (std::size_t index = 1U; index < fields.size(); ++index)
    {
        if (fields[index - 1U].first == fields[index].first)
            throw std::invalid_argument("canonical identity contains a duplicate key");
    }

    std::string output{"{"};
    for (std::size_t index = 0U; index < fields.size(); ++index)
    {
        if (index != 0U) output.push_back(',');
        AppendJsonString(output, fields[index].first);
        output.push_back(':');
        std::visit([&output](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
            else if constexpr (std::is_same_v<T, std::string>)
                AppendJsonString(output, value);
            else
                AppendInteger(output, value);
        }, fields[index].second);
    }
    output.push_back('}');
    return output;
}

std::string ComparisonConditionCanonicalJson(
    const ComparisonConditionContext& context)
{
    ValidateConditionContext(context);
    return CanonicalJson(ConditionFields(context));
}

std::string ComparisonConditionId(const ComparisonConditionContext& context)
{
    return Sha256(ComparisonConditionCanonicalJson(context));
}

std::string SeriesCanonicalJson(const SeriesIdentityContext& context)
{
    ValidateConditionContext(context.condition);
    if (!IsValidEnum(context.backend))
        throw std::invalid_argument("EX-2 series backend is invalid");
    if (context.orderSlot > 1U)
        throw std::invalid_argument("EX-2 series order_slot must be 0 or 1");
    if (!(IsLowerHex(context.sourceRevision, 40U)
        || IsLowerHex(context.sourceRevision, 64U)))
        throw std::invalid_argument("EX-2 source revision must be a full lowercase Git object ID");
    if (!IsLowerHex(context.executableSha256, 64U))
        throw std::invalid_argument("EX-2 executable SHA-256 must be 64 lowercase hexadecimal characters");
    if (context.backend == Backend::Cuda && context.shaderSha256.has_value())
        throw std::invalid_argument("EX-2 CUDA series must represent shader_sha256 as inapplicable");
    if (context.backend == Backend::Vulkan
        && (!context.shaderSha256.has_value()
            || !IsLowerHex(*context.shaderSha256, 64U)))
        throw std::invalid_argument("EX-2 Vulkan series requires an exact shader SHA-256");

    auto fields = ConditionFields(context.condition);
    fields.emplace_back("backend", std::string(ToString(context.backend)));
    fields.emplace_back("block_index", context.blockIndex);
    fields.emplace_back("executable_sha256", context.executableSha256);
    fields.emplace_back("order_slot", context.orderSlot);
    fields.emplace_back("planned_sample_count", context.plannedSampleCount);
    fields.emplace_back("process_index", context.processIndex);
    fields.emplace_back("shader_sha256", context.shaderSha256.has_value()
        ? CanonicalValue{*context.shaderSha256} : CanonicalValue{nullptr});
    fields.emplace_back("source_revision", context.sourceRevision);
    fields.emplace_back("warmup_count", context.warmupCount);
    return CanonicalJson(std::move(fields));
}

std::string SeriesId(const SeriesIdentityContext& context)
{
    return Sha256(SeriesCanonicalJson(context));
}

bool IsValidAnonymousIdentifier(std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 128U && value != "."
        && value != ".."
        && !IsHex(value, 64U)
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '.' || character == '_'
                || character == '-';
        });
}

RunPlanValidation ValidateRunPlan(
    std::span<const PlannedRun> runs,
    const std::filesystem::path& localResultsRoot)
{
    RunPlanValidation result;
    std::set<std::string> runIds;
    std::set<std::string> destinations;
    std::filesystem::path normalizedRoot;
    try
    {
        normalizedRoot = std::filesystem::absolute(localResultsRoot).lexically_normal();
    }
    catch (const std::filesystem::filesystem_error&)
    {
        result.errors.push_back(RunPlanError::InvalidDestination);
        return result;
    }

    for (const auto& run : runs)
    {
        if (!IsValidAnonymousIdentifier(run.runId))
            result.errors.push_back(RunPlanError::InvalidRunId);
        if (!runIds.insert(run.runId).second)
            result.errors.push_back(RunPlanError::DuplicateRunId);

        std::filesystem::path destination;
        try
        {
            destination = std::filesystem::absolute(run.destination).lexically_normal();
        }
        catch (const std::filesystem::filesystem_error&)
        {
            result.errors.push_back(RunPlanError::InvalidDestination);
            continue;
        }
        if (destination.parent_path() != normalizedRoot)
            result.errors.push_back(RunPlanError::DestinationOutsideLocalResults);
        if (destination.filename().string() != run.runId)
            result.errors.push_back(RunPlanError::DestinationLeafMismatch);
        const std::string collisionKey = LowerAscii(destination.generic_string());
        if (!destinations.insert(collisionKey).second)
            result.errors.push_back(RunPlanError::DestinationCollision);
    }
    return result;
}

} // namespace computelab::ex2
