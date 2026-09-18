#include "ex2/Ex2Gate0Supervisor.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace computelab::ex2::gate0::supervisor
{
namespace
{

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue
{
    using Storage = std::variant<std::nullptr_t, bool, std::uint64_t, double,
        std::string, JsonArray, JsonObject>;
    Storage storage;
};

[[noreturn]] void InvalidManifest(std::string_view detail)
{
    throw std::invalid_argument(
        "invalid EX-2 Gate-0 qualification manifest: " + std::string(detail));
}

bool IsIdentifierCharacter(char value) noexcept
{
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '.' || value == '_' || value == '-';
}

void RequireIdentifier(std::string_view value, std::string_view field)
{
    if (value.empty() || value.size() > 128U || value == "." || value == ".."
        || !std::all_of(value.begin(), value.end(), IsIdentifierCharacter))
    {
        InvalidManifest(std::string(field)
            + " must be a nonempty anonymous ASCII identifier");
    }
}

bool IsLowerHex(std::string_view value, std::size_t size) noexcept
{
    return value.size() == size
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

bool IsUuid(std::string_view value) noexcept
{
    if (value.size() != 36U) return false;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            if (value[index] != '-') return false;
        }
        else if (!((value[index] >= '0' && value[index] <= '9')
            || (value[index] >= 'a' && value[index] <= 'f')))
        {
            return false;
        }
    }
    return true;
}

class JsonParser final
{
public:
    explicit JsonParser(std::string_view input) : input_{input} {}

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (position_ != input_.size()) Fail("trailing content");
        return value;
    }

private:
    [[noreturn]] void Fail(std::string_view detail) const
    {
        InvalidManifest("JSON parse error at byte " + std::to_string(position_)
            + ": " + std::string(detail));
    }

    void SkipWhitespace()
    {
        while (position_ < input_.size()
            && (input_[position_] == ' ' || input_[position_] == '\t'
                || input_[position_] == '\r' || input_[position_] == '\n'))
        {
            ++position_;
        }
    }

    bool Consume(char value)
    {
        SkipWhitespace();
        if (position_ < input_.size() && input_[position_] == value)
        {
            ++position_;
            return true;
        }
        return false;
    }

    void RequireLiteral(std::string_view literal)
    {
        if (input_.substr(position_, literal.size()) != literal)
            Fail("invalid literal");
        position_ += literal.size();
    }

    static unsigned HexDigit(char value)
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10U;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10U;
        return 16U;
    }

    void AppendCodePoint(std::string& output, std::uint32_t value)
    {
        if (value <= 0x7FU) output.push_back(static_cast<char>(value));
        else if (value <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    std::string ParseString()
    {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"')
            Fail("expected string");
        ++position_;
        std::string output;
        while (position_ < input_.size())
        {
            const unsigned char character = input_[position_++];
            if (character == '"') return output;
            if (character < 0x20U) Fail("unescaped control character");
            if (character != '\\')
            {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) Fail("truncated escape");
            const char escaped = input_[position_++];
            switch (escaped)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                if (position_ + 4U > input_.size()) Fail("truncated Unicode escape");
                std::uint32_t value{};
                for (unsigned index = 0; index < 4U; ++index)
                {
                    const unsigned digit = HexDigit(input_[position_++]);
                    if (digit > 15U) Fail("invalid Unicode escape");
                    value = (value << 4U) | digit;
                }
                if (value >= 0xD800U && value <= 0xDFFFU)
                    Fail("surrogate Unicode escapes are not accepted");
                AppendCodePoint(output, value);
                break;
            }
            default: Fail("invalid escape");
            }
        }
        Fail("unterminated string");
    }

    JsonValue ParseNumber()
    {
        SkipWhitespace();
        const std::size_t start = position_;
        bool negative{};
        if (position_ < input_.size() && input_[position_] == '-')
        {
            negative = true;
            ++position_;
        }
        if (position_ >= input_.size() || input_[position_] < '0'
            || input_[position_] > '9')
            Fail("expected number");
        if (input_[position_] == '0' && position_ + 1U < input_.size()
            && input_[position_ + 1U] >= '0' && input_[position_ + 1U] <= '9')
            Fail("leading zero in number");
        while (position_ < input_.size()
            && input_[position_] >= '0' && input_[position_] <= '9')
            ++position_;
        bool floating{};
        if (position_ < input_.size() && input_[position_] == '.')
        {
            floating = true;
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < input_.size()
                && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (position_ == fractionStart) Fail("fraction has no digits");
        }
        if (position_ < input_.size()
            && (input_[position_] == 'e' || input_[position_] == 'E'))
        {
            floating = true;
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < input_.size()
                && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (position_ == exponentStart) Fail("exponent has no digits");
        }
        if (!negative && !floating)
        {
            std::uint64_t value{};
            const auto [end, error] = std::from_chars(
                input_.data() + start, input_.data() + position_, value);
            if (error != std::errc{} || end != input_.data() + position_)
                Fail("integer is outside uint64 range");
            return {{value}};
        }
        double value{};
        const auto [end, error] = std::from_chars(
            input_.data() + start, input_.data() + position_, value,
            std::chars_format::general);
        if (error != std::errc{} || end != input_.data() + position_
            || !std::isfinite(value))
            Fail("number is outside finite binary64 range");
        return {{value}};
    }

    JsonArray ParseArray()
    {
        if (!Consume('[')) Fail("expected array");
        JsonArray output;
        if (Consume(']')) return output;
        for (;;)
        {
            output.push_back(ParseValue());
            if (Consume(']')) return output;
            if (!Consume(',')) Fail("expected comma in array");
        }
    }

    JsonObject ParseObject()
    {
        if (!Consume('{')) Fail("expected object");
        JsonObject output;
        if (Consume('}')) return output;
        for (;;)
        {
            const std::string key = ParseString();
            if (!Consume(':')) Fail("expected colon in object");
            if (!output.emplace(key, ParseValue()).second)
                Fail("duplicate object member");
            if (Consume('}')) return output;
            if (!Consume(',')) Fail("expected comma in object");
        }
    }

    JsonValue ParseValue()
    {
        SkipWhitespace();
        if (position_ >= input_.size()) Fail("expected value");
        switch (input_[position_])
        {
        case 'n': RequireLiteral("null"); return {{nullptr}};
        case 't': RequireLiteral("true"); return {{true}};
        case 'f': RequireLiteral("false"); return {{false}};
        case '"': return {{ParseString()}};
        case '[': return {{ParseArray()}};
        case '{': return {{ParseObject()}};
        default: return ParseNumber();
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

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
            else output.push_back(static_cast<char>(character));
            break;
        }
    }
    output.push_back('"');
}

void AppendUnsigned(std::string& output, std::uint64_t value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) throw std::runtime_error("integer serialization failed");
    output.append(buffer.data(), end);
}

void AppendCanonicalJson(std::string& output, const JsonValue& value)
{
    std::visit([&output](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
        else if constexpr (std::is_same_v<T, bool>) output += item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::uint64_t>) AppendUnsigned(output, item);
        else if constexpr (std::is_same_v<T, double>)
        {
            std::array<char, 32> buffer{};
            const auto [end, error] = std::to_chars(
                buffer.data(), buffer.data() + buffer.size(), item);
            if (error != std::errc{})
                throw std::runtime_error("binary64 serialization failed");
            output.append(buffer.data(), end);
        }
        else if constexpr (std::is_same_v<T, std::string>) AppendJsonString(output, item);
        else if constexpr (std::is_same_v<T, JsonArray>)
        {
            output.push_back('[');
            for (std::size_t index = 0; index < item.size(); ++index)
            {
                if (index != 0U) output.push_back(',');
                AppendCanonicalJson(output, item[index]);
            }
            output.push_back(']');
        }
        else
        {
            output.push_back('{');
            std::size_t index{};
            for (const auto& [key, child] : item)
            {
                if (index++ != 0U) output.push_back(',');
                AppendJsonString(output, key);
                output.push_back(':');
                AppendCanonicalJson(output, child);
            }
            output.push_back('}');
        }
    }, value.storage);
}

template <typename T>
const T& RequireType(
    const JsonValue& value, std::string_view field)
{
    const T* result = std::get_if<T>(&value.storage);
    if (result == nullptr)
        InvalidManifest(std::string(field) + " has the wrong JSON type");
    return *result;
}

const JsonValue& RequireMember(
    const JsonObject& object, std::string_view field)
{
    const auto found = object.find(field);
    if (found == object.end())
        InvalidManifest("missing required field " + std::string(field));
    return found->second;
}

std::string RequireString(const JsonObject& object, std::string_view field)
{
    return RequireType<std::string>(RequireMember(object, field), field);
}

std::uint64_t RequireUnsigned(const JsonObject& object, std::string_view field)
{
    return RequireType<std::uint64_t>(RequireMember(object, field), field);
}

bool RequireBoolean(const JsonObject& object, std::string_view field)
{
    return RequireType<bool>(RequireMember(object, field), field);
}

void RequireExactKeys(
    const JsonObject& object,
    std::initializer_list<std::string_view> keys,
    std::string_view context)
{
    std::set<std::string, std::less<>> expected;
    for (const auto key : keys) expected.emplace(key);
    if (object.size() != expected.size())
        InvalidManifest(std::string(context) + " contains missing or unknown fields");
    for (const auto& [key, unused] : object)
    {
        static_cast<void>(unused);
        if (!expected.contains(key))
            InvalidManifest(std::string(context) + " contains unknown field " + key);
    }
}

ManifestType ParseManifestType(std::string_view value)
{
    if (value == "warmup-characterization")
        return ManifestType::WarmupCharacterization;
    if (value == "sample-count-qualification")
        return ManifestType::SampleCountQualification;
    if (value == "instrumentation-control")
        return ManifestType::InstrumentationControl;
    InvalidManifest("manifest_type is unsupported");
}

QualificationPhase PhaseFor(ManifestType value)
{
    switch (value)
    {
    case ManifestType::WarmupCharacterization:
        return QualificationPhase::WarmupCharacterization;
    case ManifestType::SampleCountQualification:
        return QualificationPhase::SampleCountQualification;
    case ManifestType::InstrumentationControl:
        return QualificationPhase::InstrumentationControl;
    }
    throw std::logic_error("unknown manifest type");
}

Backend ParseBackendValue(std::string_view value)
{
    if (value == "cuda") return Backend::Cuda;
    if (value == "vulkan") return Backend::Vulkan;
    InvalidManifest("child backend must be cuda or vulkan");
}

InstrumentMode ParseModeValue(std::string_view value)
{
    if (value == "H") return InstrumentMode::H;
    if (value == "N") return InstrumentMode::N;
    InvalidManifest("child instrument_mode must be H or N");
}

std::string NormalizeDeclaredPath(std::string value, std::string_view field)
{
    if (value.empty() || value.find('\\') != std::string::npos)
        InvalidManifest(std::string(field)
            + " must be a nonempty repository-relative path using forward slashes");
    const std::filesystem::path path = std::filesystem::u8path(value);
    if (path.is_absolute() || path.has_root_name() || path.lexically_normal() != path)
        InvalidManifest(std::string(field) + " must be a normalized repository-relative path");
    for (const auto& part : path)
    {
        if (part == "..") InvalidManifest(std::string(field) + " must not traverse upward");
    }
    return value;
}

std::filesystem::path ResolvePath(
    const std::filesystem::path& root,
    const std::string& declared)
{
    return std::filesystem::absolute(root / std::filesystem::u8path(declared))
        .lexically_normal();
}

bool SameLogicalCondition(
    const ManifestChild& left,
    const ManifestChild& right,
    bool includeMode) noexcept
{
    const auto& a = left.configuration;
    const auto& b = right.configuration;
    return (!includeMode || a.instrumentMode == b.instrumentMode)
        && a.elementCount == b.elementCount
        && a.seed == b.seed
        && a.warmupCount == b.warmupCount
        && a.plannedSampleCount == b.plannedSampleCount
        && a.protocolVersion == b.protocolVersion
        && left.expectedGpuUuid == right.expectedGpuUuid
        && left.expectedSourceRevision == right.expectedSourceRevision
        && left.expectedExecutableSha256 == right.expectedExecutableSha256;
}

std::uint64_t CheckedAdd(std::uint64_t left, std::uint64_t right)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        InvalidManifest("execution budget arithmetic overflow");
    return left + right;
}

std::uint64_t CheckedMultiply(std::uint64_t left, std::uint64_t right)
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        InvalidManifest("execution budget arithmetic overflow");
    return left * right;
}

void ValidateCommonChildren(const QualificationManifest& manifest)
{
    std::set<std::string, std::less<>> runIds;
    std::set<std::string, std::less<>> paths;
    std::uint64_t operationCount{};
    std::uint64_t worstCaseBudget{};
    std::optional<std::string> source;
    std::optional<std::string> executable;
    std::optional<std::string> uuid;
    std::optional<std::string> vulkanShader;
    std::optional<std::uint64_t> elementCount;
    std::optional<std::uint64_t> seed;
    std::optional<std::uint64_t> warmupCount;
    std::optional<std::uint64_t> plannedSampleCount;

    for (std::size_t index = 0; index < manifest.children.size(); ++index)
    {
        const auto& child = manifest.children[index];
        const auto& configuration = child.configuration;
        if (child.sequenceIndex != index)
            InvalidManifest("child sequence_index must equal its array position");
        RequireIdentifier(child.pairId, "child pair_id");
        RequireIdentifier(configuration.runId, "child run_id");
        if (!runIds.emplace(configuration.runId).second)
            InvalidManifest("duplicate child run_id");
        std::string foldedPath = child.declaredOutputPath;
        std::transform(foldedPath.begin(), foldedPath.end(), foldedPath.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (!paths.emplace(std::move(foldedPath)).second)
            InvalidManifest("duplicate child output_package_path");
        if (configuration.phase != PhaseFor(manifest.type))
            InvalidManifest("child phase disagrees with manifest_type");
        if (configuration.protocolVersion != manifest.protocolVersion)
            InvalidManifest("child protocol_version disagrees with manifest");
        if (configuration.machineId != manifest.machineId)
            InvalidManifest("child machine identity disagrees with manifest");
        if (configuration.processIndex > 4U || configuration.blockIndex > 4U)
            InvalidManifest("child process_index and block_index must be in [0,4]");
        if (configuration.orderSlot > 1U)
            InvalidManifest("child order_slot must be 0 or 1");
        if (configuration.elementCount == 0U
            || configuration.elementCount > std::numeric_limits<std::uint32_t>::max())
            InvalidManifest("child element_count is outside G0-04 limits");
        if (configuration.backend == Backend::Cuda
            && configuration.deviceIndex > static_cast<std::uint32_t>(
                std::numeric_limits<int>::max()))
            InvalidManifest("CUDA child device_index exceeds the native int range");
        if (!IsUuid(child.expectedGpuUuid))
            InvalidManifest("child expected_gpu_uuid must be a lowercase canonical UUID");
        if (!IsLowerHex(child.expectedSourceRevision, 40U))
            InvalidManifest("child expected_source_revision must be a full lowercase SHA-1");
        if (!IsLowerHex(child.expectedExecutableSha256, 64U))
            InvalidManifest("child expected_executable_sha256 must be lowercase SHA-256");
        if (configuration.backend == Backend::Cuda)
        {
            if (child.expectedShaderSha256.has_value())
                InvalidManifest("CUDA child expected_shader_sha256 must be null");
        }
        else if (!child.expectedShaderSha256.has_value()
            || !IsLowerHex(*child.expectedShaderSha256, 64U))
        {
            InvalidManifest("Vulkan child expected_shader_sha256 must be lowercase SHA-256");
        }
        const auto declaredPath = std::filesystem::u8path(child.declaredOutputPath);
        if (declaredPath.parent_path() != std::filesystem::path("results/local")
            || declaredPath.filename() != configuration.runId)
            InvalidManifest("child output package must be results/local/<run_id>");
        if (source.has_value() && *source != child.expectedSourceRevision)
            InvalidManifest("children disagree on expected source revision");
        if (executable.has_value() && *executable != child.expectedExecutableSha256)
            InvalidManifest("children disagree on expected executable identity");
        if (uuid.has_value() && *uuid != child.expectedGpuUuid)
            InvalidManifest("children do not target one expected physical GPU UUID");
        if (configuration.backend == Backend::Vulkan
            && vulkanShader.has_value()
            && *vulkanShader != *child.expectedShaderSha256)
            InvalidManifest("Vulkan children disagree on expected shader identity");
        if ((elementCount.has_value() && *elementCount != configuration.elementCount)
            || (seed.has_value() && *seed != configuration.seed)
            || (warmupCount.has_value() && *warmupCount != configuration.warmupCount)
            || (plannedSampleCount.has_value()
                && *plannedSampleCount != configuration.plannedSampleCount))
            InvalidManifest("children disagree on the representative logical condition");
        source = child.expectedSourceRevision;
        executable = child.expectedExecutableSha256;
        uuid = child.expectedGpuUuid;
        if (configuration.backend == Backend::Vulkan)
            vulkanShader = child.expectedShaderSha256;
        elementCount = configuration.elementCount;
        seed = configuration.seed;
        warmupCount = configuration.warmupCount;
        plannedSampleCount = configuration.plannedSampleCount;

        const std::uint64_t operations = ExpectedOperationCount(child);
        operationCount = CheckedAdd(operationCount, operations);
        const std::uint64_t operationBudget = CheckedMultiply(
            operations, manifest.operationTimeoutMilliseconds);
        worstCaseBudget = CheckedAdd(worstCaseBudget,
            std::min(operationBudget, manifest.childTimeoutMilliseconds));
    }
    if (manifest.declaredChildCount != manifest.children.size())
        InvalidManifest("declared_child_count disagrees with children array");
    if (manifest.declaredOperationCount != operationCount)
        InvalidManifest("declared_operation_count disagrees with child configurations");
    if (worstCaseBudget > manifest.campaignTimeoutMilliseconds)
        InvalidManifest("declared child execution budgets exceed campaign_timeout_ms");
}

void ValidateCudaVulkanPairs(const QualificationManifest& manifest)
{
    if (manifest.children.size() != 10U)
        InvalidManifest("warm-up and sample-count manifests require exactly ten children");
    const std::array expectedFirst{
        Backend::Cuda, Backend::Vulkan, Backend::Cuda,
        Backend::Vulkan, Backend::Cuda};
    std::set<std::string, std::less<>> pairIds;
    for (std::uint64_t block = 0; block < 5U; ++block)
    {
        const auto& first = manifest.children[static_cast<std::size_t>(block * 2U)];
        const auto& second = manifest.children[static_cast<std::size_t>(block * 2U + 1U)];
        if (first.pairId != second.pairId || !pairIds.emplace(first.pairId).second)
            InvalidManifest("each CUDA/Vulkan block requires one unique pair_id");
        if (first.configuration.blockIndex != block
            || second.configuration.blockIndex != block
            || first.configuration.processIndex != block
            || second.configuration.processIndex != block
            || first.configuration.orderSlot != 0U
            || second.configuration.orderSlot != 1U)
            InvalidManifest("CUDA/Vulkan pair indices or order slots are inconsistent");
        if (first.configuration.backend != expectedFirst[block]
            || second.configuration.backend == expectedFirst[block])
            InvalidManifest("CUDA/Vulkan block order must be C,V;V,C;C,V;V,C;C,V");
        if (!SameLogicalCondition(first, second, true))
            InvalidManifest("direct CUDA/Vulkan pair has incompatible logical conditions");
        if (first.configuration.instrumentMode != InstrumentMode::H
            || second.configuration.instrumentMode != InstrumentMode::H)
            InvalidManifest("warm-up and sample-count qualification require instrument mode H");
    }
}

void ValidateInstrumentationPairs(const QualificationManifest& manifest)
{
    if (manifest.children.size() != 20U)
        InvalidManifest("instrumentation-control requires exactly twenty children");
    std::set<std::string, std::less<>> pairIds;
    std::map<Backend, std::array<std::optional<InstrumentMode>, 5>> firstModes;
    std::map<Backend, std::array<bool, 5>> seen;
    for (std::size_t index = 0; index < manifest.children.size(); index += 2U)
    {
        const auto& first = manifest.children[index];
        const auto& second = manifest.children[index + 1U];
        if (first.pairId != second.pairId || !pairIds.emplace(first.pairId).second)
            InvalidManifest("each H/N block requires one unique pair_id");
        const auto block = first.configuration.blockIndex;
        const auto backend = first.configuration.backend;
        if (block > 4U || seen[backend][block])
            InvalidManifest("duplicate instrumentation backend/block pair");
        seen[backend][block] = true;
        firstModes[backend][block] = first.configuration.instrumentMode;
        if (second.configuration.backend != backend
            || second.configuration.blockIndex != block
            || first.configuration.processIndex != block
            || second.configuration.processIndex != block
            || first.configuration.orderSlot != 0U
            || second.configuration.orderSlot != 1U
            || first.configuration.instrumentMode == second.configuration.instrumentMode)
            InvalidManifest("instrumentation pair must be adjacent H/N processes within one backend");
        if (!SameLogicalCondition(first, second, false))
            InvalidManifest("H/N pair has incompatible logical variables");
        if (first.expectedShaderSha256 != second.expectedShaderSha256)
            InvalidManifest("H/N pair has incompatible shader identities");
    }
    for (const Backend backend : {Backend::Cuda, Backend::Vulkan})
    {
        for (std::size_t block = 0; block < 5U; ++block)
        {
            if (!seen[backend][block])
                InvalidManifest("instrumentation manifest is missing a backend/block pair");
            if (block != 0U
                && firstModes[backend][block] == firstModes[backend][block - 1U])
                InvalidManifest("instrumentation mode order must alternate across blocks");
        }
    }
}

std::string TimestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    if (gmtime_s(&utc, &time) != 0)
        throw std::runtime_error("UTC timestamp acquisition failed");
    std::array<char, 40> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &utc) == 0U)
        throw std::runtime_error("UTC timestamp formatting failed");
    std::string result{buffer.data()};
    result.push_back('.');
    const auto count = milliseconds.count();
    result.push_back(static_cast<char>('0' + (count / 100) % 10));
    result.push_back(static_cast<char>('0' + (count / 10) % 10));
    result.push_back(static_cast<char>('0' + count % 10));
    result.push_back('Z');
    return result;
}

} // namespace

std::string_view ToString(ManifestType value) noexcept
{
    switch (value)
    {
    case ManifestType::WarmupCharacterization: return "warmup-characterization";
    case ManifestType::SampleCountQualification: return "sample-count-qualification";
    case ManifestType::InstrumentationControl: return "instrumentation-control";
    }
    return "unknown";
}

std::string CanonicalManifestPayload(std::string_view json)
{
    JsonValue root = JsonParser(json).Parse();
    auto& object = const_cast<JsonObject&>(RequireType<JsonObject>(root, "root"));
    if (object.erase("manifest_sha256") != 1U)
        InvalidManifest("missing required field manifest_sha256");
    std::string output;
    AppendCanonicalJson(output, root);
    return output;
}

std::string CalculateManifestSha256(std::string_view json)
{
    return Sha256(CanonicalManifestPayload(json));
}

QualificationManifest ParseManifest(
    std::string_view json,
    const std::filesystem::path& repositoryRoot)
{
    const JsonValue rootValue = JsonParser(json).Parse();
    const auto& root = RequireType<JsonObject>(rootValue, "root");
    RequireExactKeys(root, {
        "manifest_version", "manifest_type", "manifest_id", "manifest_sha256",
        "machine_id", "protocol_version", "child_executable_path",
        "vulkan_shader_path", "supervisor_record_path",
        "continue_after_fatal_failure", "operation_timeout_ms",
        "child_timeout_ms", "campaign_timeout_ms", "declared_child_count",
        "declared_operation_count", "prerequisite", "children"}, "root");

    QualificationManifest result;
    const auto manifestVersion = RequireUnsigned(root, "manifest_version");
    if (manifestVersion > std::numeric_limits<std::uint32_t>::max())
        InvalidManifest("manifest_version exceeds uint32 range");
    result.manifestVersion = static_cast<std::uint32_t>(manifestVersion);
    result.type = ParseManifestType(RequireString(root, "manifest_type"));
    result.manifestId = RequireString(root, "manifest_id");
    result.manifestSha256 = RequireString(root, "manifest_sha256");
    result.machineId = RequireString(root, "machine_id");
    result.protocolVersion = RequireString(root, "protocol_version");
    result.childExecutablePath = NormalizeDeclaredPath(
        RequireString(root, "child_executable_path"), "child_executable_path");
    result.vulkanShaderPath = NormalizeDeclaredPath(
        RequireString(root, "vulkan_shader_path"), "vulkan_shader_path");
    result.supervisorRecordPath = NormalizeDeclaredPath(
        RequireString(root, "supervisor_record_path"), "supervisor_record_path");
    result.continueAfterFatalFailure = RequireBoolean(
        root, "continue_after_fatal_failure");
    result.operationTimeoutMilliseconds = RequireUnsigned(root, "operation_timeout_ms");
    result.childTimeoutMilliseconds = RequireUnsigned(root, "child_timeout_ms");
    result.campaignTimeoutMilliseconds = RequireUnsigned(root, "campaign_timeout_ms");
    result.declaredChildCount = RequireUnsigned(root, "declared_child_count");
    result.declaredOperationCount = RequireUnsigned(root, "declared_operation_count");

    const JsonValue& prerequisiteValue = RequireMember(root, "prerequisite");
    if (!std::holds_alternative<std::nullptr_t>(prerequisiteValue.storage))
    {
        const auto& object = RequireType<JsonObject>(prerequisiteValue, "prerequisite");
        RequireExactKeys(object, {"authorization_id", "evidence_id",
            "evidence_sha256", "selected_warmup_count",
            "qualified_sample_count"}, "prerequisite");
        PrerequisiteReference prerequisite;
        prerequisite.authorizationId = RequireString(object, "authorization_id");
        prerequisite.evidenceId = RequireString(object, "evidence_id");
        prerequisite.evidenceSha256 = RequireString(object, "evidence_sha256");
        const auto& warmup = RequireMember(object, "selected_warmup_count");
        if (!std::holds_alternative<std::nullptr_t>(warmup.storage))
            prerequisite.selectedWarmupCount =
                RequireType<std::uint64_t>(warmup, "selected_warmup_count");
        const auto& samples = RequireMember(object, "qualified_sample_count");
        if (!std::holds_alternative<std::nullptr_t>(samples.storage))
            prerequisite.qualifiedSampleCount =
                RequireType<std::uint64_t>(samples, "qualified_sample_count");
        result.prerequisite = std::move(prerequisite);
    }

    const auto& children = RequireType<JsonArray>(RequireMember(root, "children"), "children");
    result.children.reserve(children.size());
    for (const auto& childValue : children)
    {
        const auto& child = RequireType<JsonObject>(childValue, "child");
        RequireExactKeys(child, {"sequence_index", "pair_id", "phase", "backend",
            "instrument_mode", "device_index", "expected_gpu_uuid",
            "element_count", "seed", "warmup_count", "planned_sample_count",
            "process_index", "block_index", "order_slot", "run_id",
            "output_package_path", "protocol_version", "expected_source_revision",
            "expected_executable_sha256", "expected_shader_sha256"}, "child");
        ManifestChild parsed;
        parsed.sequenceIndex = RequireUnsigned(child, "sequence_index");
        parsed.pairId = RequireString(child, "pair_id");
        const std::string phase = RequireString(child, "phase");
        if (phase != ToString(result.type))
            InvalidManifest("child phase disagrees with manifest_type");
        parsed.configuration.phase = PhaseFor(result.type);
        parsed.configuration.backend = ParseBackendValue(RequireString(child, "backend"));
        parsed.configuration.instrumentMode = ParseModeValue(
            RequireString(child, "instrument_mode"));
        const auto deviceIndex = RequireUnsigned(child, "device_index");
        if (deviceIndex > std::numeric_limits<std::uint32_t>::max())
            InvalidManifest("child device_index exceeds uint32 range");
        parsed.configuration.deviceIndex = static_cast<std::uint32_t>(deviceIndex);
        parsed.expectedGpuUuid = RequireString(child, "expected_gpu_uuid");
        parsed.configuration.elementCount = RequireUnsigned(child, "element_count");
        parsed.configuration.seed = RequireUnsigned(child, "seed");
        parsed.configuration.warmupCount = RequireUnsigned(child, "warmup_count");
        parsed.configuration.plannedSampleCount = RequireUnsigned(
            child, "planned_sample_count");
        parsed.configuration.processIndex = RequireUnsigned(child, "process_index");
        parsed.configuration.blockIndex = RequireUnsigned(child, "block_index");
        parsed.configuration.orderSlot = RequireUnsigned(child, "order_slot");
        parsed.configuration.runId = RequireString(child, "run_id");
        parsed.declaredOutputPath = NormalizeDeclaredPath(
            RequireString(child, "output_package_path"), "output_package_path");
        parsed.configuration.outputPackageDirectory = ResolvePath(
            repositoryRoot, parsed.declaredOutputPath);
        parsed.configuration.protocolVersion = RequireString(child, "protocol_version");
        parsed.configuration.machineId = result.machineId;
        parsed.expectedSourceRevision = RequireString(child, "expected_source_revision");
        parsed.expectedExecutableSha256 = RequireString(
            child, "expected_executable_sha256");
        const auto& shader = RequireMember(child, "expected_shader_sha256");
        if (!std::holds_alternative<std::nullptr_t>(shader.storage))
            parsed.expectedShaderSha256 = RequireType<std::string>(
                shader, "expected_shader_sha256");
        result.children.push_back(std::move(parsed));
    }

    if (result.manifestSha256 != CalculateManifestSha256(json))
        InvalidManifest("manifest_sha256 does not match canonical manifest payload");
    ValidateManifest(result);
    return result;
}

std::uint64_t ExpectedOperationCount(const ManifestChild& child)
{
    if (child.configuration.phase == QualificationPhase::WarmupCharacterization)
        return WarmupCharacterizationCount;
    return CheckedAdd(child.configuration.warmupCount,
        child.configuration.plannedSampleCount);
}

void ValidateManifest(const QualificationManifest& manifest)
{
    if (manifest.manifestVersion != ManifestVersion)
        InvalidManifest("manifest_version must be 1");
    RequireIdentifier(manifest.manifestId, "manifest_id");
    RequireIdentifier(manifest.machineId, "machine_id");
    if (!IsLowerHex(manifest.manifestSha256, 64U))
        InvalidManifest("manifest_sha256 must be lowercase SHA-256");
    if (manifest.protocolVersion != "1.0")
        InvalidManifest("protocol_version must be exactly 1.0");
    if (manifest.operationTimeoutMilliseconds == 0U
        || manifest.operationTimeoutMilliseconds > MaximumOperationTimeoutMilliseconds)
        InvalidManifest("operation_timeout_ms must be in [1,60000]");
    if (manifest.childTimeoutMilliseconds == 0U
        || manifest.childTimeoutMilliseconds > MaximumChildTimeoutMilliseconds
        || manifest.childTimeoutMilliseconds < manifest.operationTimeoutMilliseconds)
        InvalidManifest("child_timeout_ms must be between operation timeout and 1200000");
    if (manifest.campaignTimeoutMilliseconds == 0U
        || manifest.campaignTimeoutMilliseconds > MaximumCampaignTimeoutMilliseconds
        || manifest.campaignTimeoutMilliseconds < manifest.childTimeoutMilliseconds)
        InvalidManifest("campaign_timeout_ms must be between child timeout and 86400000");
    if (!std::filesystem::path(manifest.supervisorRecordPath).has_filename()
        || std::filesystem::path(manifest.supervisorRecordPath).parent_path()
            != std::filesystem::path("results/local"))
        InvalidManifest("supervisor_record_path must name one file directly under results/local");
    ValidateCommonChildren(manifest);

    if (manifest.type == ManifestType::WarmupCharacterization)
    {
        if (manifest.prerequisite.has_value())
            InvalidManifest("warmup-characterization prerequisite must be null");
        for (const auto& child : manifest.children)
        {
            if (child.configuration.warmupCount != 0U
                || child.configuration.plannedSampleCount != 0U)
                InvalidManifest("warmup children require W=0 and planned samples=0");
        }
        ValidateCudaVulkanPairs(manifest);
    }
    else
    {
        if (!manifest.prerequisite.has_value())
            InvalidManifest("downstream qualification requires a prerequisite reference");
        const auto& prerequisite = *manifest.prerequisite;
        RequireIdentifier(prerequisite.authorizationId, "prerequisite authorization_id");
        RequireIdentifier(prerequisite.evidenceId, "prerequisite evidence_id");
        if (!IsLowerHex(prerequisite.evidenceSha256, 64U))
            InvalidManifest("prerequisite evidence_sha256 must be lowercase SHA-256");
        if (!prerequisite.selectedWarmupCount.has_value())
            InvalidManifest("downstream qualification requires selected_warmup_count");
        constexpr std::array selectedWarmups{0ULL, 1ULL, 2ULL, 4ULL, 8ULL, 16ULL};
        if (std::find(selectedWarmups.begin(), selectedWarmups.end(),
                *prerequisite.selectedWarmupCount) == selectedWarmups.end())
            InvalidManifest("selected_warmup_count is not an approved Gate-0 W");
        for (const auto& child : manifest.children)
        {
            if (child.configuration.warmupCount != *prerequisite.selectedWarmupCount)
                InvalidManifest("child warmup_count disagrees with declared prerequisite W");
        }
        if (manifest.type == ManifestType::SampleCountQualification)
        {
            if (prerequisite.qualifiedSampleCount.has_value())
                InvalidManifest("sample-count prerequisite qualified_sample_count must be null");
            for (const auto& child : manifest.children)
            {
                if (child.configuration.plannedSampleCount
                    != MaximumQualificationSampleCount)
                    InvalidManifest("sample-count qualification requires 200 observations");
            }
            ValidateCudaVulkanPairs(manifest);
        }
        else
        {
            if (!prerequisite.qualifiedSampleCount.has_value()
                || *prerequisite.qualifiedSampleCount == 0U
                || *prerequisite.qualifiedSampleCount > MaximumQualificationSampleCount)
                InvalidManifest("instrumentation prerequisite requires qualified_sample_count in [1,200]");
            for (const auto& child : manifest.children)
            {
                if (child.configuration.plannedSampleCount
                    != *prerequisite.qualifiedSampleCount)
                    InvalidManifest(
                        "child sample count disagrees with declared prerequisite count");
            }
            ValidateInstrumentationPairs(manifest);
        }
    }
}

std::vector<std::string> ChildArguments(const ManifestChild& child)
{
    const auto& configuration = child.configuration;
    return {
        "--phase", std::string(ToString(configuration.phase)),
        "--backend", std::string(ToString(configuration.backend)),
        "--instrument-mode", std::string(ToString(configuration.instrumentMode)),
        "--device-index", std::to_string(configuration.deviceIndex),
        "--element-count", std::to_string(configuration.elementCount),
        "--seed", std::to_string(configuration.seed),
        "--warmup-count", std::to_string(configuration.warmupCount),
        "--planned-sample-count", std::to_string(configuration.plannedSampleCount),
        "--process-index", std::to_string(configuration.processIndex),
        "--block-index", std::to_string(configuration.blockIndex),
        "--order-slot", std::to_string(configuration.orderSlot),
        "--run-id", configuration.runId,
        "--output-package-directory", configuration.outputPackageDirectory.string(),
        "--machine-id", configuration.machineId,
        "--protocol-version", configuration.protocolVersion};
}

namespace
{

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>{stream}, {}};
}

std::vector<std::vector<std::string>> ParseCsv(std::string_view input)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < input.size(); ++index)
    {
        const char value = input[index];
        if (quoted)
        {
            if (value == '"')
            {
                if (index + 1U < input.size() && input[index + 1U] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else quoted = false;
            }
            else field.push_back(value);
            continue;
        }
        if (value == '"')
        {
            if (!field.empty()) throw std::invalid_argument("CSV quote after field content");
            quoted = true;
        }
        else if (value == ',')
        {
            row.push_back(std::move(field));
            field.clear();
        }
        else if (value == '\r' || value == '\n')
        {
            if (value == '\r' && index + 1U < input.size() && input[index + 1U] == '\n')
                ++index;
            row.push_back(std::move(field));
            field.clear();
            rows.push_back(std::move(row));
            row.clear();
        }
        else field.push_back(value);
    }
    if (quoted) throw std::invalid_argument("unterminated CSV quote");
    if (!field.empty() || !row.empty())
    {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint64_t ParseCsvUnsigned(std::string_view value, std::string_view field)
{
    if (value.empty()) throw std::invalid_argument(std::string(field) + " is empty");
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::invalid_argument(std::string(field) + " is not uint64");
    return result;
}

const JsonObject& ObjectMember(
    const JsonObject& object, std::string_view field)
{
    return RequireType<JsonObject>(RequireMember(object, field), field);
}

const JsonArray& ArrayMember(
    const JsonObject& object, std::string_view field)
{
    return RequireType<JsonArray>(RequireMember(object, field), field);
}

bool JsonNull(const JsonObject& object, std::string_view field)
{
    return std::holds_alternative<std::nullptr_t>(RequireMember(object, field).storage);
}

void CheckString(
    std::vector<std::string>& errors,
    const JsonObject& object,
    std::string_view field,
    std::string_view expected)
{
    try
    {
        if (RequireString(object, field) != expected)
            errors.push_back(std::string(field) + " disagrees with manifest");
    }
    catch (const std::exception& error)
    {
        errors.push_back(error.what());
    }
}

void CheckUnsigned(
    std::vector<std::string>& errors,
    const JsonObject& object,
    std::string_view field,
    std::uint64_t expected)
{
    try
    {
        if (RequireUnsigned(object, field) != expected)
            errors.push_back(std::string(field) + " disagrees with manifest");
    }
    catch (const std::exception& error)
    {
        errors.push_back(error.what());
    }
}

std::vector<PackageArtifact> HashArtifacts(const std::filesystem::path& directory)
{
    std::vector<PackageArtifact> result;
    if (!std::filesystem::is_directory(directory)) return result;
    for (const auto& item : std::filesystem::directory_iterator(directory))
    {
        if (!item.is_regular_file()) continue;
        result.push_back({
            item.path().filename().generic_string(),
            static_cast<std::uint64_t>(item.file_size()),
            Sha256File(item.path())});
    }
    std::sort(result.begin(), result.end(),
        [](const PackageArtifact& left, const PackageArtifact& right) {
            return left.relativePath < right.relativePath;
        });
    return result;
}

void RequireExactCsvHeader(
    const std::vector<std::vector<std::string>>& rows,
    std::string_view expected,
    std::string_view fileName)
{
    const auto expectedRows = ParseCsv(std::string(expected) + "\r\n");
    if (rows.empty() || expectedRows.size() != 1U
        || rows.front() != expectedRows.front())
        throw std::invalid_argument(std::string(fileName) + " header is invalid");
}

MetricSummary RegenerateMetricSummary(const std::vector<std::uint64_t>& values)
{
    if (values.empty()) return {};
    std::vector<double> ordered;
    ordered.reserve(values.size());
    for (const auto value : values) ordered.push_back(static_cast<double>(value));

    double sum{};
    for (const double value : ordered) sum += value;
    const double mean = sum / static_cast<double>(ordered.size());
    std::optional<double> standardDeviation;
    if (ordered.size() >= 2U)
    {
        double squaredDeviationSum{};
        for (const double value : ordered)
        {
            const double difference = value - mean;
            squaredDeviationSum += difference * difference;
        }
        standardDeviation = std::sqrt(
            squaredDeviationSum / static_cast<double>(ordered.size() - 1U));
    }
    std::vector<double> sorted = ordered;
    std::sort(sorted.begin(), sorted.end());
    const auto middle = sorted.size() / 2U;
    const double median = sorted.size() % 2U == 0U
        ? (sorted[middle - 1U] + sorted[middle]) / 2.0
        : sorted[middle];
    const auto p95OneBasedRank = sorted.size() - sorted.size() / 20U;
    std::optional<double> coefficientOfVariation;
    if (standardDeviation.has_value() && mean != 0.0)
        coefficientOfVariation = *standardDeviation / mean;
    return {
        static_cast<std::uint64_t>(ordered.size()), sorted.front(), median, mean,
        standardDeviation, coefficientOfVariation,
        sorted[p95OneBasedRank - 1U]};
}

double RequireJsonNumber(const JsonValue& value, std::string_view field)
{
    if (const auto* integer = std::get_if<std::uint64_t>(&value.storage))
        return static_cast<double>(*integer);
    if (const auto* number = std::get_if<double>(&value.storage)) return *number;
    throw std::invalid_argument(std::string(field) + " has the wrong JSON type");
}

void ValidateMetricSummary(
    std::vector<std::string>& errors,
    const JsonObject& metrics,
    std::string_view name,
    const std::optional<MetricSummary>& expected)
{
    const auto& value = RequireMember(metrics, name);
    if (!expected.has_value())
    {
        if (!std::holds_alternative<std::nullptr_t>(value.storage))
            errors.push_back(std::string(name) + " summary must be null for this condition");
        return;
    }
    const auto& actual = RequireType<JsonObject>(value, name);
    RequireExactKeys(actual, {"sample_count", "minimum", "median", "mean",
        "standard_deviation", "coefficient_of_variation", "p95"}, name);
    const auto checkCount = [&](std::string_view field,
                                const std::optional<std::uint64_t>& expectedValue) {
        const auto& member = RequireMember(actual, field);
        if (!expectedValue.has_value())
        {
            if (!std::holds_alternative<std::nullptr_t>(member.storage))
                errors.push_back(std::string(name) + "." + std::string(field)
                    + " must be null");
        }
        else if (!std::holds_alternative<std::uint64_t>(member.storage)
            || std::get<std::uint64_t>(member.storage) != *expectedValue)
            errors.push_back(std::string(name) + "." + std::string(field)
                + " disagrees with samples.csv");
    };
    const auto checkNumber = [&](std::string_view field,
                                 const std::optional<double>& expectedValue) {
        const auto& member = RequireMember(actual, field);
        if (!expectedValue.has_value())
        {
            if (!std::holds_alternative<std::nullptr_t>(member.storage))
                errors.push_back(std::string(name) + "." + std::string(field)
                    + " must be null");
        }
        else if (std::holds_alternative<std::nullptr_t>(member.storage)
            || RequireJsonNumber(member, field) != *expectedValue)
            errors.push_back(std::string(name) + "." + std::string(field)
                + " disagrees with samples.csv");
    };
    checkCount("sample_count", expected->sampleCount);
    checkNumber("minimum", expected->minimum);
    checkNumber("median", expected->median);
    checkNumber("mean", expected->mean);
    checkNumber("standard_deviation", expected->standardDeviation);
    checkNumber("coefficient_of_variation", expected->coefficientOfVariation);
    checkNumber("p95", expected->p95);
}

void ValidateCompletedPackage(
    PackageInspection& result,
    const ManifestChild& child,
    std::optional<std::uint32_t> exitCode)
{
    const auto& directory = child.configuration.outputPackageDirectory;
    const std::array required{"environment.json", "initialization.csv",
        "samples.csv", "summary.json"};
    for (const auto* name : required)
    {
        if (!std::filesystem::is_regular_file(directory / name))
            result.integrityErrors.push_back(std::string("completed package is missing ") + name);
    }
    const bool warmup = child.configuration.phase
        == QualificationPhase::WarmupCharacterization;
    if (warmup && !std::filesystem::is_regular_file(directory / "warmup.csv"))
        result.integrityErrors.push_back("warmup package is missing warmup.csv");
    if (!warmup && std::filesystem::exists(directory / "warmup.csv"))
        result.integrityErrors.push_back("non-warmup package unexpectedly contains warmup.csv");
    if (!result.integrityErrors.empty()) return;

    const auto expectedCondition = ComparisonConditionId({
        child.configuration.protocolVersion,
        child.configuration.machineId,
        child.expectedGpuUuid,
        child.configuration.elementCount,
        child.configuration.seed,
        child.configuration.instrumentMode});
    const auto expectedSeries = SeriesId({
        {child.configuration.protocolVersion,
         child.configuration.machineId,
         child.expectedGpuUuid,
         child.configuration.elementCount,
         child.configuration.seed,
         child.configuration.instrumentMode},
        child.configuration.backend,
        child.configuration.processIndex,
        child.configuration.blockIndex,
        child.configuration.orderSlot,
        child.configuration.warmupCount,
        child.configuration.plannedSampleCount,
        child.expectedSourceRevision,
        child.expectedExecutableSha256,
        child.expectedShaderSha256});

    try
    {
        const JsonValue environmentValue = JsonParser(
            ReadFile(directory / "environment.json")).Parse();
        const auto& environment = RequireType<JsonObject>(environmentValue, "environment");
        CheckUnsigned(result.integrityErrors, environment, "schema_version", SchemaVersion);
        CheckString(result.integrityErrors, environment, "experiment_id", ExperimentId);
        CheckString(result.integrityErrors, environment, "evidence_kind", EvidenceKind);
        CheckString(result.integrityErrors, environment, "run_id", child.configuration.runId);
        CheckString(result.integrityErrors, environment, "machine_id", child.configuration.machineId);
        CheckString(result.integrityErrors, environment, "protocol_version",
            child.configuration.protocolVersion);
        CheckString(result.integrityErrors, environment, "qualification_phase",
            ToString(child.configuration.phase));
        CheckString(result.integrityErrors, environment, "backend",
            ToString(child.configuration.backend));
        CheckString(result.integrityErrors, environment, "instrument_mode",
            ToString(child.configuration.instrumentMode));
        CheckUnsigned(result.integrityErrors, environment, "device_index",
            child.configuration.deviceIndex);
        CheckUnsigned(result.integrityErrors, environment, "warmup_count",
            child.configuration.warmupCount);
        CheckUnsigned(result.integrityErrors, environment, "planned_sample_count",
            child.configuration.plannedSampleCount);
        CheckString(result.integrityErrors, environment, "comparison_condition_id", expectedCondition);
        CheckString(result.integrityErrors, environment, "series_id", expectedSeries);
        CheckUnsigned(result.integrityErrors, environment, "block_index",
            child.configuration.blockIndex);
        CheckUnsigned(result.integrityErrors, environment, "process_index",
            child.configuration.processIndex);
        CheckUnsigned(result.integrityErrors, environment, "order_slot",
            child.configuration.orderSlot);
        CheckString(result.integrityErrors, environment, "source_revision",
            child.expectedSourceRevision);
        CheckString(result.integrityErrors, environment, "git_commit",
            child.expectedSourceRevision);
        CheckString(result.integrityErrors, environment, "executable_sha256",
            child.expectedExecutableSha256);
        CheckString(result.integrityErrors, environment, "gpu_uuid_identity",
            child.expectedGpuUuid);
        CheckString(result.integrityErrors, environment, "workload", WorkloadId);
        CheckString(result.integrityErrors, environment, "variant", VariantId);
        CheckString(result.integrityErrors, environment, "generator_revision", GeneratorRevision);
        CheckString(result.integrityErrors, environment, "execution_mode", ExecutionMode);
        CheckString(result.integrityErrors, environment, "operation_boundary", OperationBoundary);
        CheckUnsigned(result.integrityErrors, environment, "element_count",
            child.configuration.elementCount);
        CheckUnsigned(result.integrityErrors, environment, "seed", child.configuration.seed);
        for (const auto field : {"input_sha256", "expected_output_sha256"})
        {
            if (!IsLowerHex(RequireString(environment, field), 64U))
                result.integrityErrors.push_back(std::string(field) + " is not lowercase SHA-256");
        }
        for (const auto field : {"compiler_name", "compiler_version", "cmake_version",
                 "ninja_version", "configure_preset", "build_type"})
        {
            if (RequireString(environment, field).empty())
                result.integrityErrors.push_back(std::string(field) + " is empty");
        }
        const auto& shader = RequireMember(environment, "shader_sha256");
        if (child.expectedShaderSha256.has_value())
        {
            if (RequireType<std::string>(shader, "shader_sha256")
                != *child.expectedShaderSha256)
                result.integrityErrors.push_back("shader_sha256 disagrees with manifest");
        }
        else if (!std::holds_alternative<std::nullptr_t>(shader.storage))
            result.integrityErrors.push_back("CUDA shader_sha256 must be null");
        const auto& native = ObjectMember(environment, "backend_native");
        const bool markers = RequireBoolean(native, "native_markers_enabled");
        if (markers != (child.configuration.instrumentMode == InstrumentMode::N))
            result.integrityErrors.push_back("native marker declaration disagrees with mode");
        const bool methodNull = JsonNull(native, "native_timing_method");
        if ((child.configuration.instrumentMode == InstrumentMode::H) != methodNull)
            result.integrityErrors.push_back("native timing method scope disagrees with mode");
        if (child.configuration.instrumentMode == InstrumentMode::N)
        {
            if (child.configuration.backend == Backend::Cuda
                && JsonNull(native, "native_timing_resolution_ns"))
                result.integrityErrors.push_back("CUDA N package lacks timing resolution");
            if (child.configuration.backend == Backend::Vulkan
                && (JsonNull(native, "timestamp_valid_bits")
                    || JsonNull(native, "timestamp_period_ns")
                    || JsonNull(native, "native_timing_start_stage")
                    || JsonNull(native, "native_timing_stop_stage")))
                result.integrityErrors.push_back("Vulkan N package lacks declared native timing scope");
        }
    }
    catch (const std::exception& error)
    {
        result.integrityErrors.push_back(std::string("environment.json validation failed: ") + error.what());
    }

    std::uint64_t sampleCount{};
    std::uint64_t failedSamples{};
    std::uint64_t validationFailures{};
    std::vector<std::uint64_t> hostSubmissionValues;
    std::vector<std::uint64_t> hostWaitValues;
    std::vector<std::uint64_t> hostCompletionValues;
    std::vector<std::uint64_t> nativeDeviceValues;
    try
    {
        const auto rows = ParseCsv(ReadFile(directory / "samples.csv"));
        RequireExactCsvHeader(rows, SamplesCsvHeader(), "samples.csv");
        sampleCount = rows.size() - 1U;
        for (std::size_t index = 1; index < rows.size(); ++index)
        {
            const auto& row = rows[index];
            if (row.size() != 31U) throw std::invalid_argument("samples.csv row width is invalid");
            if (row[0] != "2" || row[1] != child.configuration.runId
                || row[2] != ExperimentId || row[3] != expectedCondition
                || row[4] != expectedSeries
                || row[5] != ToString(child.configuration.backend)
                || row[6] != WorkloadId || row[7] != VariantId
                || ParseCsvUnsigned(row[8], "seed") != child.configuration.seed
                || ParseCsvUnsigned(row[9], "element_count")
                    != child.configuration.elementCount
                || row[16] != ToString(child.configuration.instrumentMode)
                || ParseCsvUnsigned(row[17], "warmup_count")
                    != child.configuration.warmupCount
                || ParseCsvUnsigned(row[18], "planned_sample_count")
                    != child.configuration.plannedSampleCount
                || ParseCsvUnsigned(row[19], "block_index")
                    != child.configuration.blockIndex
                || ParseCsvUnsigned(row[20], "order_slot")
                    != child.configuration.orderSlot
                || ParseCsvUnsigned(row[21], "process_index")
                    != child.configuration.processIndex
                || ParseCsvUnsigned(row[22], "sample_index") != index - 1U)
                throw std::invalid_argument("samples.csv identity or ordering mismatch");
            if (row[23] != "true" && row[23] != "false")
                throw std::invalid_argument("samples.csv validation_passed is invalid");
            if (row[24].empty())
                throw std::invalid_argument("samples.csv status is empty");
            if (row[24] != "ok") ++failedSamples;
            if (row[23] == "false") ++validationFailures;
            const bool eligible = row[24] == "ok" && row[23] == "true";
            if (child.configuration.instrumentMode == InstrumentMode::H && !row[30].empty())
                throw std::invalid_argument("H sample contains a native device interval");
            if (child.configuration.instrumentMode == InstrumentMode::N
                && row[24] == "ok" && row[30].empty())
                throw std::invalid_argument("successful N sample lacks native device interval");
            if (row[24] == "ok")
            {
                const auto submit = ParseCsvUnsigned(row[27], "host_submission_ns");
                const auto wait = ParseCsvUnsigned(row[28], "host_wait_ns");
                const auto completion = ParseCsvUnsigned(row[29], "host_completion_ns");
                if (submit > std::numeric_limits<std::uint64_t>::max() - wait
                    || submit + wait != completion)
                    throw std::invalid_argument("host timing decomposition does not reconcile");
                if (eligible)
                {
                    hostSubmissionValues.push_back(submit);
                    hostWaitValues.push_back(wait);
                    hostCompletionValues.push_back(completion);
                    if (child.configuration.instrumentMode == InstrumentMode::N)
                        nativeDeviceValues.push_back(ParseCsvUnsigned(
                            row[30], "native_device_interval_ns"));
                }
            }
        }
        if (warmup && sampleCount != 0U)
            throw std::invalid_argument("warmup package contains steady-state samples");
        if (!warmup && sampleCount > child.configuration.plannedSampleCount)
            throw std::invalid_argument("package contains more samples than declared");
        if (exitCode.has_value() && *exitCode == 0U && !warmup
            && sampleCount != child.configuration.plannedSampleCount)
            throw std::invalid_argument("successful package sample count disagrees with plan");
    }
    catch (const std::exception& error)
    {
        result.integrityErrors.push_back(
            std::string("samples.csv validation failed: ") + error.what());
    }

    std::uint64_t warmupCount{};
    if (warmup)
    {
        try
        {
            const auto rows = ParseCsv(ReadFile(directory / "warmup.csv"));
            RequireExactCsvHeader(rows, WarmupCsvHeader(), "warmup.csv");
            warmupCount = rows.size() - 1U;
            if (warmupCount > WarmupCharacterizationCount)
                throw std::invalid_argument("warmup package contains more than 48 rows");
            for (std::size_t index = 1; index < rows.size(); ++index)
            {
                const auto& row = rows[index];
                if (row.size() != 9U || row[0] != "2"
                    || row[1] != child.configuration.runId || row[2] != expectedSeries
                    || ParseCsvUnsigned(row[3], "process_index")
                        != child.configuration.processIndex
                    || ParseCsvUnsigned(row[4], "sequence_index") != index - 1U)
                    throw std::invalid_argument("warmup.csv identity or ordering mismatch");
            }
            if (exitCode.has_value() && *exitCode == 0U
                && warmupCount != WarmupCharacterizationCount)
                throw std::invalid_argument("successful warmup package does not contain 48 rows");
        }
        catch (const std::exception& error)
        {
            result.integrityErrors.push_back(
                std::string("warmup.csv validation failed: ") + error.what());
        }
    }

    try
    {
        const auto initialization = ParseCsv(ReadFile(directory / "initialization.csv"));
        RequireExactCsvHeader(initialization, InitializationCsvHeader(),
            "initialization.csv");
        if (initialization.size() < 2U)
            throw std::invalid_argument("initialization.csv has no records");
        for (std::size_t index = 1; index < initialization.size(); ++index)
        {
            const auto& row = initialization[index];
            if (row.size() != 13U || row[0] != "2"
                || row[1] != child.configuration.runId || row[2] != ExperimentId
                || row[3] != ToString(child.configuration.backend)
                || ParseCsvUnsigned(row[4], "initialization process_index")
                    != child.configuration.processIndex)
                throw std::invalid_argument("initialization.csv identity mismatch");
        }

        const JsonValue summaryValue = JsonParser(ReadFile(directory / "summary.json")).Parse();
        const auto& summary = RequireType<JsonObject>(summaryValue, "summary");
        CheckUnsigned(result.integrityErrors, summary, "schema_version", SchemaVersion);
        CheckString(result.integrityErrors, summary, "run_id", child.configuration.runId);
        CheckString(result.integrityErrors, summary, "experiment_id", ExperimentId);
        CheckString(result.integrityErrors, summary, "evidence_kind", EvidenceKind);
        const std::string processStatus = RequireString(summary, "process_status");
        if (exitCode.has_value()
            && ((*exitCode == 0U) != (processStatus == "ok")))
            result.integrityErrors.push_back("exit code and summary process_status disagree");
        const auto& groups = ArrayMember(summary, "sample_groups");
        if (groups.size() != 1U) throw std::invalid_argument("summary must contain one sample group");
        const auto& groupContainer = RequireType<JsonObject>(groups.front(), "sample_group");
        const auto& group = ObjectMember(groupContainer, "group");
        CheckString(result.integrityErrors, group, "comparison_condition_id", expectedCondition);
        CheckString(result.integrityErrors, group, "series_id", expectedSeries);
        CheckString(result.integrityErrors, group, "run_id", child.configuration.runId);
        CheckUnsigned(result.integrityErrors, groupContainer,
            "recorded_sample_count", sampleCount);
        const auto summaryFailed = RequireUnsigned(groupContainer, "failed_sample_count");
        if (summaryFailed != failedSamples)
            result.integrityErrors.push_back(
                "summary failed_sample_count disagrees with samples.csv");
        const auto summaryValidationFailures = RequireUnsigned(
            groupContainer, "validation_failures");
        if (summaryValidationFailures != validationFailures)
            result.integrityErrors.push_back(
                "summary validation_failures disagrees with samples.csv");
        const auto& metrics = ObjectMember(groupContainer, "metrics");
        RequireExactKeys(metrics, {"host_submission_ns", "host_wait_ns",
            "host_completion_ns", "native_device_interval_ns"}, "metrics");
        ValidateMetricSummary(result.integrityErrors, metrics, "host_submission_ns",
            warmup ? std::nullopt
                   : std::optional<MetricSummary>{RegenerateMetricSummary(
                       hostSubmissionValues)});
        ValidateMetricSummary(result.integrityErrors, metrics, "host_wait_ns",
            warmup ? std::nullopt
                   : std::optional<MetricSummary>{RegenerateMetricSummary(hostWaitValues)});
        ValidateMetricSummary(result.integrityErrors, metrics, "host_completion_ns",
            warmup ? std::nullopt
                   : std::optional<MetricSummary>{RegenerateMetricSummary(
                       hostCompletionValues)});
        ValidateMetricSummary(result.integrityErrors, metrics,
            "native_device_interval_ns",
            !warmup && child.configuration.instrumentMode == InstrumentMode::N
                ? std::optional<MetricSummary>{RegenerateMetricSummary(
                    nativeDeviceValues)}
                : std::nullopt);
    }
    catch (const std::exception& error)
    {
        result.integrityErrors.push_back(
            std::string("summary or initialization validation failed: ")
                + error.what());
    }

}

} // namespace

PackageInspection InspectPackage(
    const ManifestChild& child,
    std::optional<std::uint32_t> exitCode)
{
    PackageInspection result;
    const auto finalPath = child.configuration.outputPackageDirectory;
    const auto incompletePath = std::filesystem::path(finalPath.string() + ".incomplete");
    std::error_code error;
    const bool finalExists = std::filesystem::exists(finalPath, error);
    if (error) result.integrityErrors.push_back("unable to inspect final package path");
    error.clear();
    const bool incompleteExists = std::filesystem::exists(incompletePath, error);
    if (error) result.integrityErrors.push_back("unable to inspect incomplete package path");
    if (finalExists && incompleteExists)
    {
        result.packageState = "collision";
        result.integrityErrors.push_back(
            "both completed and incomplete package paths exist");
    }
    else if (finalExists)
    {
        result.packageState = "complete";
        result.artifacts = HashArtifacts(finalPath);
        ValidateCompletedPackage(result, child, exitCode);
    }
    else if (incompleteExists)
    {
        result.packageState = "incomplete";
        result.artifacts = HashArtifacts(incompletePath);
        if (exitCode.has_value() && *exitCode == 0U)
            result.integrityErrors.push_back("successful child left an incomplete package");
    }
    else
    {
        result.packageState = "missing";
        if (exitCode.has_value() && *exitCode == 0U)
            result.integrityErrors.push_back("successful child produced no package");
    }
    result.structurallyValid = result.packageState == "complete"
        && result.integrityErrors.empty();
    if (!exitCode.has_value())
        result.admissionReasons.push_back("child_exit_outcome_unavailable");
    else if (*exitCode != 0U)
        result.admissionReasons.push_back("child_exit_code_nonzero");
    if (!result.structurallyValid)
        result.admissionReasons.push_back("package_integrity_failed");
    result.admitted = exitCode.has_value() && *exitCode == 0U
        && result.structurallyValid;
    return result;
}

void ApplySupervisorAdmission(
    PackageInspection& package,
    const SupervisedProcessResult& execution,
    std::uint64_t expectedOperationCount)
{
    const auto reject = [&](std::string reason) {
        package.admitted = false;
        if (std::find(package.admissionReasons.begin(),
                package.admissionReasons.end(), reason)
            == package.admissionReasons.end())
            package.admissionReasons.push_back(std::move(reason));
    };
    if (execution.terminationReason.has_value())
        reject("supervisor_termination:" + *execution.terminationReason);
    if (execution.progressValidation != "passed"
        || execution.operationStarts != expectedOperationCount
        || execution.operationCompletions != expectedOperationCount)
        reject("operation_progress_disagrees_with_manifest");
    if (execution.exitCode != 0U) reject("child_exit_code_nonzero");
}

std::string_view PrerequisiteVerificationBoundary(
    const QualificationManifest& manifest) noexcept
{
    return manifest.prerequisite.has_value()
        ? "external_human_review_required_not_machine_verified"
        : "not_applicable";
}

namespace
{

void AppendOptionalString(
    std::string& output,
    const std::optional<std::string>& value)
{
    if (value.has_value()) AppendJsonString(output, *value);
    else output += "null";
}

void AppendOptionalUnsigned(
    std::string& output,
    const std::optional<std::uint32_t>& value)
{
    if (value.has_value()) AppendUnsigned(output, *value);
    else output += "null";
}

std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::invalid_argument("argument is not valid UTF-8");
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), output.data(), size) != size)
        throw std::runtime_error("UTF-8 argument conversion failed");
    return output;
}

std::wstring QuoteWindowsArgument(std::wstring_view value)
{
    std::wstring output{L"\""};
    std::size_t backslashes{};
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'"')
        {
            output.append(backslashes * 2U + 1U, L'\\');
            output.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        output.append(backslashes, L'\\');
        backslashes = 0U;
        output.push_back(character);
    }
    output.append(backslashes * 2U, L'\\');
    output.push_back(L'"');
    return output;
}

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_{handle} {}
    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                CloseHandle(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }

private:
    HANDLE handle_{};
};

void WriteDurableFile(const std::filesystem::path& path, std::string_view contents)
{
    const UniqueHandle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.Get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("unable to create supervisor execution record");
    std::size_t offset{};
    while (offset < contents.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            contents.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(file.Get(), contents.data() + offset, chunk, &written, nullptr)
            || written != chunk)
            throw std::runtime_error("unable to write supervisor execution record");
        offset += written;
    }
    if (!FlushFileBuffers(file.Get()))
        throw std::runtime_error("unable to flush supervisor execution record");
}

void DrainProgressPipe(
    HANDLE pipe,
    std::vector<std::byte>& buffer,
    SupervisedProcessResult& result,
    std::optional<std::chrono::steady_clock::time_point>& operationDeadline,
    std::optional<std::int64_t>& operationStartCounter,
    std::int64_t operationTimeoutCounterTicks,
    std::int64_t performanceCounterFrequency,
    bool& operationTimedOut,
    bool& invalid)
{
    for (;;)
    {
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0U, nullptr, &available, nullptr))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) break;
            throw std::runtime_error("progress pipe inspection failed");
        }
        if (available == 0U) break;
        const std::size_t oldSize = buffer.size();
        buffer.resize(oldSize + available);
        DWORD read{};
        if (!::ReadFile(pipe, buffer.data() + oldSize, available, &read, nullptr))
            throw std::runtime_error("progress pipe read failed");
        buffer.resize(oldSize + read);
    }

    std::size_t consumed{};
    while (buffer.size() - consumed >= sizeof(ProgressEvent))
    {
        ProgressEvent event{};
        std::memcpy(&event, buffer.data() + consumed, sizeof(event));
        consumed += sizeof(event);
        if (event.magic != ProgressMagic || event.version != ProgressVersion)
        {
            invalid = true;
            continue;
        }
        if (event.type == ProgressEventType::OperationStarted)
        {
            if (operationDeadline.has_value()
                || event.operationIndex != result.operationStarts
                || result.operationStarts != result.operationCompletions)
                invalid = true;
            else
            {
                ++result.operationStarts;
                LARGE_INTEGER currentCounter{};
                if (event.performanceCounter <= 0
                    || !QueryPerformanceCounter(&currentCounter)
                    || currentCounter.QuadPart < event.performanceCounter)
                {
                    invalid = true;
                    continue;
                }
                operationStartCounter = event.performanceCounter;
                const std::int64_t elapsedTicks =
                    currentCounter.QuadPart - event.performanceCounter;
                if (elapsedTicks >= operationTimeoutCounterTicks)
                {
                    operationTimedOut = true;
                    operationDeadline = std::chrono::steady_clock::now();
                }
                else
                {
                    const auto remainingNanoseconds =
                        static_cast<std::int64_t>(
                            static_cast<long double>(operationTimeoutCounterTicks
                                - elapsedTicks) * 1'000'000'000.0L
                            / static_cast<long double>(performanceCounterFrequency));
                    operationDeadline = std::chrono::steady_clock::now()
                        + std::chrono::nanoseconds(remainingNanoseconds);
                }
            }
        }
        else if (event.type == ProgressEventType::OperationCompleted)
        {
            if (!operationDeadline.has_value()
                || event.operationIndex != result.operationCompletions
                || result.operationStarts != result.operationCompletions + 1U)
                invalid = true;
            else
            {
                ++result.operationCompletions;
                if (!operationStartCounter.has_value()
                    || event.performanceCounter < *operationStartCounter
                    || event.performanceCounter - *operationStartCounter
                        > operationTimeoutCounterTicks)
                    operationTimedOut = true;
                operationDeadline.reset();
                operationStartCounter.reset();
            }
        }
        else invalid = true;
    }
    if (consumed != 0U)
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(consumed));
}

} // namespace

std::string SerializeExecutionRecord(const SupervisorExecutionRecord& record)
{
    std::string output{"{\"schema_version\":"};
    AppendUnsigned(output, ExecutionRecordVersion);
    output += ",\"record_kind\":\"ex2-gate0-supervisor-execution\",\"manifest_id\":";
    AppendJsonString(output, record.manifest.manifestId);
    output += ",\"manifest_sha256\":";
    AppendJsonString(output, record.manifest.manifestSha256);
    output += ",\"manifest_type\":";
    AppendJsonString(output, ToString(record.manifest.type));
    output += ",\"protocol_version\":";
    AppendJsonString(output, record.manifest.protocolVersion);
    output += ",\"machine_id\":";
    AppendJsonString(output, record.manifest.machineId);
    output += ",\"prerequisite_verification_boundary\":";
    AppendJsonString(output, PrerequisiteVerificationBoundary(record.manifest));
    output += ",\"execution_scope\":\"qualification_only_no_gate_verdict\"";
    output += ",\"operation_progress_protocol\":\"inherited-pipe-v1-outside-t0-t1-t2\"";
    output += ",\"strict_sequential_execution\":true,\"continue_after_fatal_failure\":";
    output += record.manifest.continueAfterFatalFailure ? "true" : "false";
    output += ",\"deadlines_ms\":{\"operation\":";
    AppendUnsigned(output, record.manifest.operationTimeoutMilliseconds);
    output += ",\"child\":";
    AppendUnsigned(output, record.manifest.childTimeoutMilliseconds);
    output += ",\"campaign\":";
    AppendUnsigned(output, record.manifest.campaignTimeoutMilliseconds);
    output += "},\"start_time_utc\":";
    AppendJsonString(output, record.startTimeUtc);
    output += ",\"end_time_utc\":";
    AppendJsonString(output, record.endTimeUtc);
    output += ",\"scheduled_children\":[";
    for (std::size_t index = 0; index < record.manifest.children.size(); ++index)
    {
        if (index != 0U) output.push_back(',');
        const auto& child = record.manifest.children[index];
        output += "{\"sequence_index\":";
        AppendUnsigned(output, child.sequenceIndex);
        output += ",\"pair_id\":"; AppendJsonString(output, child.pairId);
        output += ",\"run_id\":"; AppendJsonString(output, child.configuration.runId);
        output += ",\"phase\":"; AppendJsonString(output, ToString(child.configuration.phase));
        output += ",\"backend\":"; AppendJsonString(output, ToString(child.configuration.backend));
        output += ",\"instrument_mode\":";
        AppendJsonString(output, ToString(child.configuration.instrumentMode));
        output += ",\"process_index\":"; AppendUnsigned(output, child.configuration.processIndex);
        output += ",\"block_index\":"; AppendUnsigned(output, child.configuration.blockIndex);
        output += ",\"order_slot\":"; AppendUnsigned(output, child.configuration.orderSlot);
        output += ",\"expected_output_path\":";
        AppendJsonString(output, child.declaredOutputPath);
        output += "}";
    }
    output += "],\"actual_executions\":[";
    for (std::size_t index = 0; index < record.actualExecutions.size(); ++index)
    {
        if (index != 0U) output.push_back(',');
        const auto& child = record.actualExecutions[index];
        output += "{\"sequence_index\":"; AppendUnsigned(output, child.sequenceIndex);
        output += ",\"run_id\":"; AppendJsonString(output, child.runId);
        output += ",\"expected_output_path\":";
        AppendJsonString(output, child.expectedOutputPath);
        output += ",\"launch_time_utc\":"; AppendJsonString(output, child.launchTimeUtc);
        output += ",\"exit_time_utc\":"; AppendJsonString(output, child.exitTimeUtc);
        output += ",\"exit_code\":"; AppendOptionalUnsigned(output, child.exitCode);
        output += ",\"termination_reason\":";
        AppendOptionalString(output, child.terminationReason);
        output += ",\"operation_starts\":"; AppendUnsigned(output, child.operationStarts);
        output += ",\"operation_completions\":";
        AppendUnsigned(output, child.operationCompletions);
        output += ",\"progress_validation\":";
        AppendJsonString(output, child.progressValidation);
        output += ",\"pairing_validation\":";
        AppendJsonString(output, child.pairingValidation);
        output += ",\"package_state\":";
        AppendJsonString(output, child.package.packageState);
        output += ",\"package_admitted\":";
        output += child.package.admitted ? "true" : "false";
        output += ",\"admission_eligibility\":";
        AppendJsonString(output, child.package.admitted ? "admitted" : "rejected");
        output += ",\"integrity_validation\":";
        AppendJsonString(output,
            child.package.structurallyValid ? "passed" : "failed");
        output += ",\"integrity_errors\":[";
        for (std::size_t errorIndex = 0;
             errorIndex < child.package.integrityErrors.size(); ++errorIndex)
        {
            if (errorIndex != 0U) output.push_back(',');
            AppendJsonString(output, child.package.integrityErrors[errorIndex]);
        }
        output += "],\"admission_reasons\":[";
        for (std::size_t reasonIndex = 0;
             reasonIndex < child.package.admissionReasons.size(); ++reasonIndex)
        {
            if (reasonIndex != 0U) output.push_back(',');
            AppendJsonString(output, child.package.admissionReasons[reasonIndex]);
        }
        output += "],\"package_artifacts\":[";
        for (std::size_t artifactIndex = 0;
             artifactIndex < child.package.artifacts.size(); ++artifactIndex)
        {
            if (artifactIndex != 0U) output.push_back(',');
            const auto& artifact = child.package.artifacts[artifactIndex];
            output += "{\"relative_path\":";
            AppendJsonString(output, artifact.relativePath);
            output += ",\"size_bytes\":"; AppendUnsigned(output, artifact.sizeBytes);
            output += ",\"sha256\":"; AppendJsonString(output, artifact.sha256);
            output += "}";
        }
        output += "]}";
    }
    output += "],\"overall_execution_status\":";
    AppendJsonString(output, record.overallExecutionStatus);
    output += ",\"failure_reason\":";
    AppendOptionalString(output, record.failureReason);
    output += "}\n";
    return output;
}

void WriteExecutionRecord(
    const std::filesystem::path& path,
    const SupervisorExecutionRecord& record,
    bool complete)
{
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path incomplete{path.string() + ".incomplete"};
    const std::filesystem::path temporary{incomplete.string() + ".tmp"};
    WriteDurableFile(temporary, SerializeExecutionRecord(record));
    if (!MoveFileExW(temporary.c_str(), incomplete.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("unable to publish incomplete supervisor record");
    if (complete && !MoveFileExW(incomplete.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("unable to finalize supervisor execution record");
}

SupervisedProcessResult RunSupervisedProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds operationTimeout,
    std::chrono::milliseconds childTimeout,
    std::chrono::steady_clock::time_point campaignDeadline)
{
    if (operationTimeout.count() <= 0 || childTimeout < operationTimeout)
        throw std::invalid_argument("invalid supervised process deadlines");
    if (std::chrono::steady_clock::now() >= campaignDeadline)
        throw std::runtime_error("campaign deadline expired before child launch");

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE rawRead{};
    HANDLE rawWrite{};
    if (!CreatePipe(&rawRead, &rawWrite, &security, 4096U))
        throw std::runtime_error("unable to create operation progress pipe");
    UniqueHandle readPipe{rawRead};
    UniqueHandle writePipe{rawWrite};
    if (!SetHandleInformation(readPipe.Get(), HANDLE_FLAG_INHERIT, 0U))
        throw std::runtime_error("unable to restrict progress read handle inheritance");

    UniqueHandle job{CreateJobObjectW(nullptr, nullptr)};
    if (job.Get() == nullptr)
        throw std::runtime_error("unable to create child ownership job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation,
            &jobLimits, sizeof(jobLimits)))
        throw std::runtime_error("unable to configure child ownership job");

    SIZE_T attributeBytes{};
    InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attributeBytes);
    std::vector<std::byte> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1U, 0U, &attributeBytes))
        throw std::runtime_error("unable to initialize child handle list");
    struct AttributeCleanup final
    {
        PPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } attributeCleanup{attributes};
    HANDLE inherited[] = {writePipe.Get()};
    if (!UpdateProcThreadAttribute(attributes, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("unable to set child handle list");

    std::wstring commandLine = QuoteWindowsArgument(executable.native());
    for (const auto& argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteWindowsArgument(Utf8ToWide(argument));
    }
    commandLine += L" --supervisor-progress-handle ";
    commandLine += QuoteWindowsArgument(
        std::to_wstring(reinterpret_cast<std::uintptr_t>(writePipe.Get())));
    commandLine.push_back(L'\0');

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION processInfo{};
    const DWORD creationFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW
        | EXTENDED_STARTUPINFO_PRESENT;
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr,
            TRUE, creationFlags, nullptr, nullptr, &startup.StartupInfo, &processInfo))
        throw std::runtime_error("CreateProcessW failed for qualification child");
    UniqueHandle process{processInfo.hProcess};
    UniqueHandle thread{processInfo.hThread};
    if (!AssignProcessToJobObject(job.Get(), process.Get()))
    {
        TerminateProcess(process.Get(), static_cast<UINT>(ExitCode::IncompleteOrTimeout));
        throw std::runtime_error("unable to assign qualification child to ownership job");
    }
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
    {
        TerminateJobObject(job.Get(), static_cast<UINT>(ExitCode::IncompleteOrTimeout));
        throw std::runtime_error("unable to resume qualification child");
    }
    thread = UniqueHandle{};
    writePipe = UniqueHandle{};

    SupervisedProcessResult result;
    result.launchTimeUtc = TimestampUtc();
    const auto launchMonotonic = std::chrono::steady_clock::now();
    const auto childDeadline = launchMonotonic + childTimeout;
    std::optional<std::chrono::steady_clock::time_point> operationDeadline;
    std::optional<std::int64_t> operationStartCounter;
    LARGE_INTEGER performanceCounterFrequency{};
    if (!QueryPerformanceFrequency(&performanceCounterFrequency)
        || performanceCounterFrequency.QuadPart <= 0)
        throw std::runtime_error("performance counter frequency acquisition failed");
    const auto operationTimeoutCounterTicks = static_cast<std::int64_t>(
        (static_cast<long double>(operationTimeout.count())
            * static_cast<long double>(performanceCounterFrequency.QuadPart))
        / 1000.0L);
    std::vector<std::byte> progressBytes;
    bool progressInvalid{};
    bool operationTimedOut{};
    const auto classifyDeadline = [&](std::chrono::steady_clock::time_point now) {
        if (result.terminationReason.has_value()) return;
        if (progressInvalid)
            result.terminationReason = "progress_protocol_error";
        else if (operationTimedOut
            || (operationDeadline.has_value() && now >= *operationDeadline))
            result.terminationReason = "operation_timeout";
        else if (now >= childDeadline)
            result.terminationReason = "child_timeout";
        else if (now >= campaignDeadline)
            result.terminationReason = "campaign_timeout";
    };
    bool exited{};
    while (!exited)
    {
        const DWORD wait = WaitForSingleObject(process.Get(), 20U);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT)
            throw std::runtime_error("qualification child wait failed");
        DrainProgressPipe(readPipe.Get(), progressBytes, result,
            operationDeadline, operationStartCounter,
            operationTimeoutCounterTicks, performanceCounterFrequency.QuadPart,
            operationTimedOut, progressInvalid);
        classifyDeadline(std::chrono::steady_clock::now());
        if (wait == WAIT_OBJECT_0)
        {
            exited = true;
            break;
        }
        if (result.terminationReason.has_value())
        {
            if (!TerminateJobObject(job.Get(),
                    static_cast<UINT>(ExitCode::IncompleteOrTimeout)))
                throw std::runtime_error("unable to terminate timed-out qualification child");
            WaitForSingleObject(process.Get(), 5000U);
            exited = true;
            break;
        }
    }
    DrainProgressPipe(readPipe.Get(), progressBytes, result,
        operationDeadline, operationStartCounter,
        operationTimeoutCounterTicks, performanceCounterFrequency.QuadPart,
        operationTimedOut, progressInvalid);
    classifyDeadline(std::chrono::steady_clock::now());
    DWORD exitCode{};
    if (!GetExitCodeProcess(process.Get(), &exitCode) || exitCode == STILL_ACTIVE)
        throw std::runtime_error("unable to acquire terminated child exit code");
    result.exitCode = exitCode;
    result.exitTimeUtc = TimestampUtc();
    if (progressInvalid || !progressBytes.empty())
        result.progressValidation = "invalid";
    else if (operationDeadline.has_value()
        || result.operationStarts != result.operationCompletions)
        result.progressValidation = "incomplete_operation";
    else result.progressValidation = "passed";
    return result;
}

ProgressReporter::ProgressReporter(std::uintptr_t nativeHandle)
    : nativeHandle_{nativeHandle}
{
    const HANDLE handle = reinterpret_cast<HANDLE>(nativeHandle_);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE
        || GetFileType(handle) != FILE_TYPE_PIPE)
        throw std::invalid_argument("supervisor progress handle is not an inherited pipe");
}

void ProgressReporter::OperationStarted(std::uint64_t index) const
{
    if (!Enabled()) return;
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter))
        throw std::runtime_error("unable to timestamp operation-start progress");
    const ProgressEvent event{ProgressMagic, ProgressVersion,
        ProgressEventType::OperationStarted, index, counter.QuadPart};
    DWORD written{};
    if (!WriteFile(reinterpret_cast<HANDLE>(nativeHandle_), &event,
            sizeof(event), &written, nullptr) || written != sizeof(event))
        throw std::runtime_error("unable to publish operation-start progress");
}

void ProgressReporter::OperationCompleted(std::uint64_t index) const
{
    if (!Enabled()) return;
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter))
        throw std::runtime_error("unable to timestamp operation-complete progress");
    const ProgressEvent event{ProgressMagic, ProgressVersion,
        ProgressEventType::OperationCompleted, index, counter.QuadPart};
    DWORD written{};
    if (!WriteFile(reinterpret_cast<HANDLE>(nativeHandle_), &event,
            sizeof(event), &written, nullptr) || written != sizeof(event))
        throw std::runtime_error("unable to publish operation-complete progress");
}

bool ProgressReporter::Enabled() const noexcept
{
    return nativeHandle_ != 0U;
}

std::optional<ProgressReporter> ExtractProgressReporter(
    std::vector<std::string_view>& arguments)
{
    constexpr std::string_view option = "--supervisor-progress-handle";
    std::optional<std::uintptr_t> handle;
    for (std::size_t index = 0; index < arguments.size();)
    {
        if (arguments[index] != option)
        {
            ++index;
            continue;
        }
        if (handle.has_value() || index + 1U >= arguments.size())
            throw std::invalid_argument("invalid duplicate or valueless supervisor progress handle");
        std::uint64_t parsed{};
        const auto value = arguments[index + 1U];
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size()
            || parsed == 0U || parsed > std::numeric_limits<std::uintptr_t>::max())
            throw std::invalid_argument("invalid supervisor progress handle value");
        handle = static_cast<std::uintptr_t>(parsed);
        arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(index),
            arguments.begin() + static_cast<std::ptrdiff_t>(index + 2U));
    }
    if (!handle.has_value()) return std::nullopt;
    return ProgressReporter{*handle};
}

} // namespace computelab::ex2::gate0::supervisor
