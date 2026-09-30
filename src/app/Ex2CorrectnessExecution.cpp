#include "app/Ex2CorrectnessExecution.hpp"

#include "app/Ex2A1Integration.hpp"
#include "app/Ex2A2Integration.hpp"
#include "app/Ex2BIntegration.hpp"
#include "app/Ex2CIntegration.hpp"
#include "app/Ex2D1Integration.hpp"
#include "app/Ex2EIntegration.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include "ex2/Ex2Sha256.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace computelab::ex2::correctness
{
namespace
{

constexpr std::array<std::string_view, 4> kSeriesFiles{
    "environment.json", "initialization.csv", "samples.csv", "summary.json"};

struct GitState
{
    std::string revision;
    bool dirty{};
    std::string workingTreeState;
};

bool IsWithin(const std::filesystem::path& child,
    const std::filesystem::path& root)
{
    const auto normalizedChild = std::filesystem::absolute(child).lexically_normal();
    const auto normalizedRoot = std::filesystem::absolute(root).lexically_normal();
    auto childPart = normalizedChild.begin();
    for (auto rootPart = normalizedRoot.begin(); rootPart != normalizedRoot.end();
        ++rootPart, ++childPart)
    {
        if (childPart == normalizedChild.end() || *childPart != *rootPart)
            return false;
    }
    return true;
}

std::string Trim(std::string value)
{
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'
        || value.back() == ' ' || value.back() == '\t'))
    {
        value.pop_back();
    }
    return value;
}

std::string CaptureCommand(const std::string& command)
{
    FILE* raw = _popen(command.c_str(), "r");
    if (raw == nullptr)
        throw std::runtime_error("required provenance command could not start");
    std::array<char, 4096> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), raw))
        output += buffer.data();
    const int result = _pclose(raw);
    if (result != 0)
        throw std::runtime_error("required provenance command failed");
    return Trim(std::move(output));
}

GitState ReadGitState(const std::filesystem::path& repositoryRoot)
{
    const std::string prefix = "git -C \"" + repositoryRoot.string() + "\" ";
    const std::string revision = CaptureCommand(
        prefix + "rev-parse --verify HEAD 2>NUL");
    if (revision.size() != 40U
        || !std::ranges::all_of(revision, [](char value) {
            return (value >= '0' && value <= '9')
                || (value >= 'a' && value <= 'f');
        }))
    {
        throw std::runtime_error(
            "source Git revision is not a full lowercase SHA-1 identity");
    }
    const std::string state = CaptureCommand(
        prefix + "status --porcelain=v1 --untracked-files=all 2>NUL");
    return {revision, !state.empty(), state};
}

std::string TimestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    if (gmtime_s(&utc, &value) != 0)
        throw std::runtime_error("UTC timestamp acquisition failed");
    std::array<char, 32> buffer{};
    if (std::strftime(
            buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U)
    {
        throw std::runtime_error("UTC timestamp formatting failed");
    }
    return buffer.data();
}

std::string FormatUuid(const environment::DeviceUuid& uuid)
{
    if (std::ranges::all_of(uuid, [](std::uint8_t value) { return value == 0U; }))
        throw std::invalid_argument("I7 correctness GPU UUID must not be all zero");
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(36U);
    for (std::size_t index = 0U; index < uuid.size(); ++index)
    {
        if (index == 4U || index == 6U || index == 8U || index == 10U)
            output.push_back('-');
        output.push_back(hex[uuid[index] >> 4U]);
        output.push_back(hex[uuid[index] & 0x0FU]);
    }
    return output;
}

void RequireSameUuid(
    const environment::DeviceUuid& cudaUuid,
    const environment::DeviceUuid& vulkanUuid)
{
    static_cast<void>(FormatUuid(cudaUuid));
    static_cast<void>(FormatUuid(vulkanUuid));
    if (cudaUuid != vulkanUuid)
        throw std::invalid_argument(
            "selected CUDA and Vulkan devices do not share one physical UUID");
}

std::string JsonEscape(std::string_view value)
{
    std::string output;
    for (const char character : value)
    {
        if (character == '"' || character == '\\')
            output.push_back('\\');
        output.push_back(character);
    }
    return output;
}

bool IsSafeFailureToken(std::string_view value, std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum
        && std::ranges::all_of(value, [](char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '-' || character == '_' || character == '.';
        });
}

bool IsSafeFailureDetail(std::string_view value)
{
    return !value.empty() && value.size() <= 256U
        && std::ranges::all_of(value, [](char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == ' ' || character == '-' || character == '_'
                || character == '.' || character == ',' || character == '('
                || character == ')';
        });
}

void WriteNewFile(const std::filesystem::path& path, std::string_view content)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("I7 correctness evidence file creation failed");
    const auto close = [&] { CloseHandle(file); };
    std::size_t offset = 0U;
    while (offset < content.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            content.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(file, content.data() + offset, chunk, &written, nullptr)
            || written != chunk)
        {
            close();
            throw std::runtime_error("I7 correctness evidence file write failed");
        }
        offset += written;
    }
    if (!FlushFileBuffers(file))
    {
        close();
        throw std::runtime_error("I7 correctness evidence file flush failed");
    }
    close();
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("I7 correctness evidence file is unavailable");
    std::string content{std::istreambuf_iterator<char>{stream}, {}};
    if (stream.bad())
        throw std::runtime_error("I7 correctness evidence file could not be read");
    return content;
}

std::string ParseJsonString(std::string_view raw)
{
    if (raw.size() < 2U || raw.front() != '"' || raw.back() != '"')
        throw std::invalid_argument("expected a JSON string value");
    std::string value;
    for (std::size_t index = 1U; index + 1U < raw.size(); ++index)
    {
        char character = raw[index];
        if (character == '\\')
        {
            if (++index + 1U >= raw.size())
                throw std::invalid_argument("invalid JSON string escape");
            character = raw[index];
            if (character != '"' && character != '\\' && character != '/')
                throw std::invalid_argument("unsupported JSON string escape");
        }
        value.push_back(character);
    }
    return value;
}

std::map<std::string, std::string> ParseTopLevelObject(std::string_view json)
{
    auto skipWhitespace = [&](std::size_t& index) {
        while (index < json.size()
            && (json[index] == ' ' || json[index] == '\t'
                || json[index] == '\r' || json[index] == '\n'))
            ++index;
    };
    std::size_t index = 0U;
    skipWhitespace(index);
    if (index >= json.size() || json[index++] != '{')
        throw std::invalid_argument("expected a JSON object");
    std::map<std::string, std::string> fields;
    for (;;)
    {
        skipWhitespace(index);
        if (index < json.size() && json[index] == '}')
        {
            ++index;
            break;
        }
        const std::size_t keyStart = index;
        if (index >= json.size() || json[index++] != '"')
            throw std::invalid_argument("expected a JSON object key");
        bool escaped = false;
        while (index < json.size())
        {
            const char character = json[index++];
            if (!escaped && character == '"') break;
            escaped = !escaped && character == '\\';
            if (character != '\\') escaped = false;
        }
        const std::string key = ParseJsonString(json.substr(
            keyStart, index - keyStart));
        skipWhitespace(index);
        if (index >= json.size() || json[index++] != ':')
            throw std::invalid_argument("expected a JSON object colon");
        skipWhitespace(index);
        const std::size_t valueStart = index;
        unsigned int objectDepth = 0U;
        unsigned int arrayDepth = 0U;
        bool inString = false;
        escaped = false;
        while (index < json.size())
        {
            const char character = json[index];
            if (inString)
            {
                ++index;
                if (!escaped && character == '"') inString = false;
                escaped = !escaped && character == '\\';
                if (character != '\\') escaped = false;
                continue;
            }
            if (character == '"') { inString = true; ++index; continue; }
            if (character == '{') ++objectDepth;
            else if (character == '}')
            {
                if (objectDepth == 0U && arrayDepth == 0U) break;
                --objectDepth;
            }
            else if (character == '[') ++arrayDepth;
            else if (character == ']') --arrayDepth;
            else if (character == ',' && objectDepth == 0U && arrayDepth == 0U)
                break;
            ++index;
        }
        std::size_t valueEnd = index;
        while (valueEnd > valueStart
            && (json[valueEnd - 1U] == ' ' || json[valueEnd - 1U] == '\t'
                || json[valueEnd - 1U] == '\r' || json[valueEnd - 1U] == '\n'))
            --valueEnd;
        if (valueEnd == valueStart
            || !fields.emplace(key, std::string(json.substr(
                valueStart, valueEnd - valueStart))).second)
        {
            throw std::invalid_argument("JSON object has an empty or duplicate field");
        }
        if (index < json.size() && json[index] == ',') { ++index; continue; }
        if (index < json.size() && json[index] == '}') { ++index; break; }
        throw std::invalid_argument("JSON object is not terminated correctly");
    }
    skipWhitespace(index);
    if (index != json.size())
        throw std::invalid_argument("JSON object has trailing data");
    return fields;
}

const std::string& RequireField(
    const std::map<std::string, std::string>& object,
    std::string_view name)
{
    const auto found = object.find(std::string(name));
    if (found == object.end())
        throw std::invalid_argument("required evidence field is missing");
    return found->second;
}

std::string JsonStringField(
    const std::map<std::string, std::string>& object,
    std::string_view name)
{
    return ParseJsonString(RequireField(object, name));
}

std::uint64_t JsonUnsignedField(
    const std::map<std::string, std::string>& object,
    std::string_view name)
{
    const auto& raw = RequireField(object, name);
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(
        raw.data(), raw.data() + raw.size(), value);
    if (error != std::errc{} || end != raw.data() + raw.size())
        throw std::invalid_argument("evidence integer field is invalid");
    return value;
}

std::optional<std::string> JsonOptionalStringField(
    const std::map<std::string, std::string>& object,
    std::string_view name)
{
    const auto& raw = RequireField(object, name);
    if (raw == "null") return std::nullopt;
    return ParseJsonString(raw);
}

std::vector<std::vector<std::string>> ParseCsv(std::string_view csv)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0U; index < csv.size(); ++index)
    {
        const char character = csv[index];
        if (quoted)
        {
            if (character == '"')
            {
                if (index + 1U < csv.size() && csv[index + 1U] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else quoted = false;
            }
            else field.push_back(character);
            continue;
        }
        if (character == '"')
        {
            if (!field.empty())
                throw std::invalid_argument("CSV quote does not begin a field");
            quoted = true;
        }
        else if (character == ',')
        {
            row.push_back(std::move(field));
            field.clear();
        }
        else if (character == '\r')
        {
            if (index + 1U >= csv.size() || csv[index + 1U] != '\n')
                throw std::invalid_argument("CSV requires CRLF row endings");
            ++index;
            row.push_back(std::move(field));
            field.clear();
            rows.push_back(std::move(row));
            row.clear();
        }
        else if (character == '\n')
            throw std::invalid_argument("CSV requires CRLF row endings");
        else field.push_back(character);
    }
    if (quoted || !field.empty() || !row.empty())
        throw std::invalid_argument("CSV is incomplete or lacks final CRLF");
    return rows;
}

void RequireEqual(std::string_view actual, std::string_view expected,
    std::string_view field)
{
    if (actual != expected)
        throw std::invalid_argument(
            "disk evidence field mismatch: " + std::string(field));
}

std::string RouteImplementation(CoreCellRoute route, Backend backend)
{
    const std::string prefix = backend == Backend::Cuda
        ? "ex2-cuda-" : "ex2-vulkan-";
    switch (route)
    {
    case CoreCellRoute::A1: return prefix + "a1";
    case CoreCellRoute::A2: return prefix + "a2";
    case CoreCellRoute::B1: return prefix + "b1";
    case CoreCellRoute::B2: return prefix + "b2";
    case CoreCellRoute::C: return prefix + "c";
    case CoreCellRoute::D1: return prefix + "d1";
    case CoreCellRoute::E1: return prefix + "e1";
    case CoreCellRoute::E2: return prefix + "e2";
    }
    throw std::invalid_argument("I7 correctness route is invalid");
}

evidence::BackendDiagnostics Diagnostics(CoreCellRoute route, Backend backend)
{
    evidence::BackendDiagnostics result;
    result.implementation = RouteImplementation(route, backend);
    return result;
}

evidence::I7FailureKind MapFailure(
    evidence::OperationStatus status,
    std::optional<evidence::FailurePhase> phase,
    bool completed,
    bool outputObserved)
{
    const auto actualPhase = phase.value_or(evidence::FailurePhase::Interrupted);
    PostFoundationFailureStage stage = PostFoundationFailureStage::Interrupted;
    if (actualPhase == evidence::FailurePhase::BackendInitialization)
        stage = PostFoundationFailureStage::BackendInitialization;
    else if (actualPhase == evidence::FailurePhase::ResourceAllocation)
        stage = PostFoundationFailureStage::ResourceAllocation;
    else if (actualPhase == evidence::FailurePhase::Submission
        || status == evidence::OperationStatus::SubmitFailed)
        stage = PostFoundationFailureStage::Submission;
    else if (actualPhase == evidence::FailurePhase::CompletionWait
        || status == evidence::OperationStatus::WaitFailed)
        stage = PostFoundationFailureStage::Completion;
    else if (actualPhase == evidence::FailurePhase::Readback)
        stage = PostFoundationFailureStage::Readback;
    return MapPostFoundationFailure({stage,
        status == evidence::OperationStatus::Timeout,
        status == evidence::OperationStatus::DeviceLost,
        completed, outputObserved});
}

template <class Phase>
evidence::FailurePhase NormalizePhase(Phase phase)
{
    using P = Phase;
    if constexpr (std::is_same_v<P, b::IntegrationFailurePhase>)
    {
        switch (phase)
        {
        case P::BackendInitialization: return evidence::FailurePhase::BackendInitialization;
        case P::ResourceAllocation: return evidence::FailurePhase::ResourceAllocation;
        case P::Submission: return evidence::FailurePhase::Submission;
        case P::CompletionWait: return evidence::FailurePhase::CompletionWait;
        case P::OutputReadback:
        case P::InputDiagnosticReadback:
        case P::IndexDiagnosticReadback: return evidence::FailurePhase::Readback;
        case P::ValidationMismatch: return evidence::FailurePhase::Validation;
        case P::InterruptedSecondBackend: return evidence::FailurePhase::Interrupted;
        default: return evidence::FailurePhase::ResourceAllocation;
        }
    }
    else if constexpr (std::is_same_v<P, c::IntegrationFailurePhase>)
    {
        switch (phase)
        {
        case P::BackendInitialization: return evidence::FailurePhase::BackendInitialization;
        case P::ResourceAllocation: return evidence::FailurePhase::ResourceAllocation;
        case P::Submission: return evidence::FailurePhase::Submission;
        case P::CompletionWait: return evidence::FailurePhase::CompletionWait;
        case P::CounterReadback:
        case P::TargetDiagnosticReadback: return evidence::FailurePhase::Readback;
        case P::ValidationMismatch: return evidence::FailurePhase::Validation;
        case P::InterruptedSecondBackend: return evidence::FailurePhase::Interrupted;
        default: return evidence::FailurePhase::ResourceAllocation;
        }
    }
    else if constexpr (std::is_same_v<P, d1::IntegrationFailurePhase>)
    {
        switch (phase)
        {
        case P::BackendInitialization: return evidence::FailurePhase::BackendInitialization;
        case P::ResourceAllocation: return evidence::FailurePhase::ResourceAllocation;
        case P::SequenceSubmission: return evidence::FailurePhase::Submission;
        case P::CompletionWait: return evidence::FailurePhase::CompletionWait;
        case P::FinalReadback: return evidence::FailurePhase::Readback;
        case P::ValidationMismatch: return evidence::FailurePhase::Validation;
        case P::InterruptedSecondBackend: return evidence::FailurePhase::Interrupted;
        default: return evidence::FailurePhase::ResourceAllocation;
        }
    }
    else
    {
        switch (phase)
        {
        case P::BackendInitialization: return evidence::FailurePhase::BackendInitialization;
        case P::ResourceAllocation: return evidence::FailurePhase::ResourceAllocation;
        case P::TransferSubmission: return evidence::FailurePhase::Submission;
        case P::CompletionWait: return evidence::FailurePhase::CompletionWait;
        case P::ValidationReadback:
        case P::ValidationReadbackCompletion:
        case P::HostReadVisibility:
        case P::NoncoherentInvalidate: return evidence::FailurePhase::Readback;
        case P::ValidationMismatch: return evidence::FailurePhase::Validation;
        case P::InterruptedSecondBackend: return evidence::FailurePhase::Interrupted;
        default: return evidence::FailurePhase::ResourceAllocation;
        }
    }
}

template <class Status>
evidence::OperationStatus NormalizeStatus(Status status)
{
    switch (status)
    {
    case Status::Ok: return evidence::OperationStatus::Ok;
    case Status::ValidationFailed: return evidence::OperationStatus::ValidationFailed;
    case Status::SubmitFailed: return evidence::OperationStatus::SubmitFailed;
    case Status::WaitFailed: return evidence::OperationStatus::WaitFailed;
    case Status::Timeout: return evidence::OperationStatus::Timeout;
    case Status::DeviceLost: return evidence::OperationStatus::DeviceLost;
    case Status::Incomplete: return evidence::OperationStatus::Incomplete;
    }
    return evidence::OperationStatus::Incomplete;
}

template <class Observation>
evidence::I7FailureKind MapIntegrationFailure(
    const Observation& observation,
    bool completed,
    bool outputObserved)
{
    std::optional<evidence::FailurePhase> phase;
    if (observation.failurePhase.has_value())
        phase = NormalizePhase(*observation.failurePhase);
    return MapFailure(
        NormalizeStatus(observation.status), phase, completed, outputObserved);
}

void RequireObservationIdentity(
    const WorkloadConfiguration& actualConfiguration,
    const WorkloadConfiguration& expectedConfiguration,
    bool physicalIdentityVerified,
    const environment::DeviceUuid& actualUuid,
    std::string_view actualUuidText,
    const environment::DeviceUuid& expectedUuid)
{
    if (actualConfiguration != expectedConfiguration
        || !physicalIdentityVerified
        || actualUuid != expectedUuid
        || actualUuidText != FormatUuid(expectedUuid))
    {
        throw std::runtime_error(
            "native observation contradicts the preconstructed I7 identity");
    }
}

std::filesystem::path ShaderPath(
    CoreCellRoute route,
    const CorrectnessRuntimePaths& paths)
{
    switch (route)
    {
    case CoreCellRoute::A1: return paths.a1Spirv;
    case CoreCellRoute::A2: return paths.a2Spirv;
    case CoreCellRoute::B1: return paths.b1Spirv;
    case CoreCellRoute::B2: return paths.b2Spirv;
    case CoreCellRoute::C: return paths.cSpirv;
    case CoreCellRoute::D1: return paths.d1Spirv;
    case CoreCellRoute::E1:
    case CoreCellRoute::E2: return {};
    }
    throw std::invalid_argument("I7 correctness route is invalid");
}

} // namespace

bool HasEstablishedSetup(const a1::BackendObservation& observation)
{
    if (!observation.failurePhase) return observation.operationCompleted;
    switch (*observation.failurePhase)
    {
    case evidence::FailurePhase::Configuration:
    case evidence::FailurePhase::InputGeneration:
    case evidence::FailurePhase::BackendInitialization:
    case evidence::FailurePhase::ResourceAllocation:
    case evidence::FailurePhase::Interrupted: return false;
    case evidence::FailurePhase::Submission:
    case evidence::FailurePhase::CompletionWait:
    case evidence::FailurePhase::Readback:
    case evidence::FailurePhase::Validation: return true;
    default: return observation.operationCompleted;
    }
}

bool HasEstablishedSetup(const b::BackendObservation& observation)
{
    if (!observation.failurePhase) return observation.nativeOperationCompleted;
    switch (*observation.failurePhase)
    {
    case b::IntegrationFailurePhase::ConfigurationValidation:
    case b::IntegrationFailurePhase::BackendInitialization:
    case b::IntegrationFailurePhase::ResourceAllocation:
    case b::IntegrationFailurePhase::InputUpload:
    case b::IntegrationFailurePhase::IndexUpload:
    case b::IntegrationFailurePhase::OutputInitialization:
    case b::IntegrationFailurePhase::UploadCompletion:
    case b::IntegrationFailurePhase::Preparation:
    case b::IntegrationFailurePhase::InterruptedSecondBackend: return false;
    case b::IntegrationFailurePhase::Submission:
    case b::IntegrationFailurePhase::CompletionWait:
    case b::IntegrationFailurePhase::OutputReadback:
    case b::IntegrationFailurePhase::InputDiagnosticReadback:
    case b::IntegrationFailurePhase::IndexDiagnosticReadback:
    case b::IntegrationFailurePhase::ValidationMismatch: return true;
    default: return observation.nativeOperationCompleted;
    }
}

bool HasEstablishedSetup(const c::BackendObservation& observation)
{
    if (!observation.failurePhase) return observation.nativeOperationCompleted;
    switch (*observation.failurePhase)
    {
    case c::IntegrationFailurePhase::ConfigurationValidation:
    case c::IntegrationFailurePhase::BackendInitialization:
    case c::IntegrationFailurePhase::ResourceAllocation:
    case c::IntegrationFailurePhase::TargetUpload:
    case c::IntegrationFailurePhase::UploadCompletion:
    case c::IntegrationFailurePhase::CounterReset:
    case c::IntegrationFailurePhase::ResetCompletion:
    case c::IntegrationFailurePhase::InterruptedSecondBackend: return false;
    case c::IntegrationFailurePhase::Submission:
    case c::IntegrationFailurePhase::CompletionWait:
    case c::IntegrationFailurePhase::CounterReadback:
    case c::IntegrationFailurePhase::TargetDiagnosticReadback:
    case c::IntegrationFailurePhase::ValidationMismatch: return true;
    default: return observation.nativeOperationCompleted;
    }
}

bool HasEstablishedSetup(const d1::BackendObservation& observation)
{
    if (!observation.failurePhase) return observation.nativeSequenceCompleted;
    switch (*observation.failurePhase)
    {
    case d1::IntegrationFailurePhase::ConfigurationValidation:
    case d1::IntegrationFailurePhase::BackendInitialization:
    case d1::IntegrationFailurePhase::ResourceAllocation:
    case d1::IntegrationFailurePhase::InitialUpload:
    case d1::IntegrationFailurePhase::UploadCompletion:
    case d1::IntegrationFailurePhase::StartMarker:
    case d1::IntegrationFailurePhase::CommandBufferReset:
    case d1::IntegrationFailurePhase::CommandRecording:
    case d1::IntegrationFailurePhase::QueryReset:
    case d1::IntegrationFailurePhase::InterruptedSecondBackend: return false;
    case d1::IntegrationFailurePhase::SequenceSubmission:
    case d1::IntegrationFailurePhase::StopMarker:
    case d1::IntegrationFailurePhase::CompletionWait:
    case d1::IntegrationFailurePhase::FinalReadback:
    case d1::IntegrationFailurePhase::NativeTimingRetrieval:
    case d1::IntegrationFailurePhase::NativeTimingConversion:
    case d1::IntegrationFailurePhase::ValidationMismatch: return true;
    default: return observation.nativeSequenceCompleted;
    }
}

bool HasEstablishedSetup(const e::BackendObservation& observation)
{
    if (!observation.failurePhase) return observation.nativeTransferCompleted;
    switch (*observation.failurePhase)
    {
    case e::IntegrationFailurePhase::ConfigurationValidation:
    case e::IntegrationFailurePhase::BackendInitialization:
    case e::IntegrationFailurePhase::ResourceAllocation:
    case e::IntegrationFailurePhase::HostSourcePreparation:
    case e::IntegrationFailurePhase::NoncoherentFlush:
    case e::IntegrationFailurePhase::DeviceSourcePreparation:
    case e::IntegrationFailurePhase::PreparationCompletion:
    case e::IntegrationFailurePhase::StartMarker:
    case e::IntegrationFailurePhase::CommandPreparation:
    case e::IntegrationFailurePhase::QueryReset:
    case e::IntegrationFailurePhase::InterruptedSecondBackend: return false;
    case e::IntegrationFailurePhase::TransferSubmission:
    case e::IntegrationFailurePhase::StopMarker:
    case e::IntegrationFailurePhase::CompletionWait:
    case e::IntegrationFailurePhase::ValidationReadback:
    case e::IntegrationFailurePhase::ValidationReadbackCompletion:
    case e::IntegrationFailurePhase::HostReadVisibility:
    case e::IntegrationFailurePhase::NoncoherentInvalidate:
    case e::IntegrationFailurePhase::NativeTimingRetrieval:
    case e::IntegrationFailurePhase::NativeTimingConversion:
    case e::IntegrationFailurePhase::ValidationMismatch: return true;
    default: return observation.nativeTransferCompleted;
    }
}

FailureDisposition StagingCreationFailure() noexcept
{
    return {ExternalFailurePhase::EvidencePublication,
        "staging_creation_failed", ExitCode::PackageFailure};
}

FailureDisposition ClassifyExecutionFailure(bool completedObservations) noexcept
{
    return completedObservations
        ? FailureDisposition{ExternalFailurePhase::Validation,
            "completed_validation_failed", ExitCode::ExecutionFailure}
        : FailureDisposition{ExternalFailurePhase::BackendExecution,
            "post_foundation_execution_failed", ExitCode::ExecutionFailure};
}

bool IsStagingRetained(const SessionPlan& plan) noexcept
{
    std::error_code error;
    return std::filesystem::is_directory(plan.stagingDirectory, error) && !error;
}

CoreCellSelection SelectApprovedCoreCell(std::size_t index)
{
    const auto& cells = ApprovedCoreCells();
    if (index >= cells.size())
        throw std::invalid_argument("I7 core-cell index is outside 0..21");
    const auto& configuration = cells[index];
    const CoreCellRoute route = std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
            return value.variant == LinearVariant::A1
                ? CoreCellRoute::A1 : CoreCellRoute::A2;
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            return value.variant == IndexedVariant::B1
                ? CoreCellRoute::B1 : CoreCellRoute::B2;
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
            return CoreCellRoute::C;
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        {
            if (value.variant != IterativeVariant::D1)
                throw std::invalid_argument("I7 correctness child rejects D2");
            return CoreCellRoute::D1;
        }
        else
            return value.variant == TransferVariant::E1
                ? CoreCellRoute::E1 : CoreCellRoute::E2;
    }, configuration.parameters);
    return {index, route, configuration};
}

void RequireApprovedCoreCell(const WorkloadConfiguration& configuration)
{
    if (ClassifyCellEligibility(configuration) != CellEligibility::ApprovedCoreCell)
        throw std::invalid_argument(
            "I7 correctness child accepts only one approved core cell");
}

evidence::I7FailureKind MapPostFoundationFailure(
    const PostFoundationFailureFacts& facts)
{
    if (facts.deviceLost)
    {
        switch (facts.stage)
        {
        case PostFoundationFailureStage::Submission:
            return evidence::I7FailureKind::DeviceLostDuringSubmission;
        case PostFoundationFailureStage::Completion:
            return evidence::I7FailureKind::DeviceLostDuringCompletion;
        case PostFoundationFailureStage::Readback:
            return evidence::I7FailureKind::DeviceLostDuringReadback;
        default:
            return evidence::I7FailureKind::DeviceLostDuringInitialization;
        }
    }
    if (facts.timeout)
    {
        return facts.stage == PostFoundationFailureStage::Submission
            ? evidence::I7FailureKind::SubmissionTimeout
            : evidence::I7FailureKind::CompletionTimeout;
    }
    switch (facts.stage)
    {
    case PostFoundationFailureStage::BackendInitialization:
        return evidence::I7FailureKind::BackendInitializationFailed;
    case PostFoundationFailureStage::ResourceAllocation:
        return evidence::I7FailureKind::ResourceAllocationFailed;
    case PostFoundationFailureStage::Submission:
        return evidence::I7FailureKind::SubmissionFailed;
    case PostFoundationFailureStage::Completion:
        return evidence::I7FailureKind::CompletionFailed;
    case PostFoundationFailureStage::Readback:
        return evidence::I7FailureKind::ReadbackFailed;
    case PostFoundationFailureStage::Interrupted:
        if (facts.outputObserved)
            return evidence::I7FailureKind::InterruptedAfterOutput;
        if (facts.operationCompleted)
            return evidence::I7FailureKind::InterruptedAfterCompletion;
        return evidence::I7FailureKind::InterruptedBeforeCompletion;
    }
    throw std::invalid_argument("I7 post-foundation failure stage is invalid");
}

SessionPlan MakeSessionPlan(
    const std::filesystem::path& localResultsRoot,
    std::string sessionId)
{
    if (!IsValidAnonymousIdentifier(sessionId))
        throw std::invalid_argument("I7 session ID is not a valid anonymous identifier");
    const std::string cudaRunId = sessionId + "-cuda";
    const std::string vulkanRunId = sessionId + "-vulkan";
    if (!IsValidAnonymousIdentifier(cudaRunId)
        || !IsValidAnonymousIdentifier(vulkanRunId))
    {
        throw std::invalid_argument("I7 derived run ID is invalid");
    }
    const auto root = std::filesystem::absolute(localResultsRoot).lexically_normal();
    const auto staging = root / (sessionId + ".incomplete");
    const auto final = root / sessionId;
    const auto sidecar = root / (sessionId + ".failure.json");
    if (!IsWithin(staging, root) || !IsWithin(final, root)
        || !IsWithin(sidecar, root))
    {
        throw std::invalid_argument("I7 session paths escape results/local");
    }
    return {std::move(sessionId), cudaRunId, vulkanRunId, root, staging,
        final, sidecar, staging / cudaRunId, staging / vulkanRunId};
}

void RequireUnusedSessionPlan(const SessionPlan& plan)
{
    for (const auto& path : {
        plan.finalDirectory, plan.stagingDirectory, plan.failureSidecar})
    {
        std::error_code error;
        if (std::filesystem::exists(path, error) || error)
            throw std::invalid_argument(
                "I7 session destination already exists or cannot be checked");
    }
}

PreparedFoundationPair PrepareFoundationPair(
    CoreCellSelection cell,
    std::string machineId,
    std::string verifiedGpuUuid,
    std::string sourceRevision,
    std::string executableSha256,
    std::optional<std::string> vulkanShaderSha256,
    std::string cudaRunId,
    std::string vulkanRunId)
{
    RequireApprovedCoreCell(cell.configuration);
    const bool transfer = std::holds_alternative<TransferConfiguration>(
        cell.configuration.parameters);
    if (transfer != !vulkanShaderSha256.has_value())
        throw std::invalid_argument("I7 Vulkan shader provenance applicability is wrong");
    const ComparisonConditionContext condition{
        std::string(evidence::I7CorrectnessProtocolVersion),
        std::move(machineId), {std::move(verifiedGpuUuid), true},
        cell.configuration, InstrumentMode::P};
    SeriesIdentityContext cudaIdentity{
        condition, Backend::Cuda, 0U, 0U, 0U, 0U, 1U,
        sourceRevision, executableSha256, std::nullopt};
    SeriesIdentityContext vulkanIdentity{
        condition, Backend::Vulkan, 0U, 0U, 1U, 0U, 1U,
        std::move(sourceRevision), std::move(executableSha256),
        std::move(vulkanShaderSha256)};

    return std::visit([&](const auto& parameters) -> PreparedFoundationPair {
        using T = std::decay_t<decltype(parameters)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
        {
            auto input = GenerateWordInput(CoreInputSeed, parameters.elementCount);
            auto expected = parameters.variant == LinearVariant::A1
                ? ReferenceA1(input) : ReferenceA2(input);
            auto identity = evidence::MakeI7WordInputIdentity(
                cell.configuration, input);
            auto cuda = evidence::MakeI7WordCorrectnessFoundation(
                std::move(cudaRunId), cudaIdentity, identity, expected);
            auto vulkan = evidence::MakeI7WordCorrectnessFoundation(
                std::move(vulkanRunId), vulkanIdentity, identity, expected);
            return {std::move(cell), WordLogicalData{
                std::move(input), std::move(expected)},
                std::move(cuda), std::move(vulkan)};
        }
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
        {
            auto primary = GenerateWordInput(CoreInputSeed, parameters.elementCount);
            auto permutation = parameters.indexPattern == IndexPattern::StructuredV1
                ? GenerateStructuredPermutation(parameters.elementCount)
                : GenerateShuffledPermutation(CoreInputSeed, parameters.elementCount);
            auto expected = parameters.variant == IndexedVariant::B1
                ? ReferenceB1Gather(primary, permutation)
                : ReferenceB2Scatter(primary, permutation);
            auto identity = evidence::MakeI7IndexedInputIdentity(
                cell.configuration, primary, permutation);
            auto cuda = evidence::MakeI7WordCorrectnessFoundation(
                std::move(cudaRunId), cudaIdentity, identity, expected);
            auto vulkan = evidence::MakeI7WordCorrectnessFoundation(
                std::move(vulkanRunId), vulkanIdentity, identity, expected);
            return {std::move(cell), IndexedLogicalData{
                std::move(primary), std::move(permutation), std::move(expected)},
                std::move(cuda), std::move(vulkan)};
        }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        {
            auto targets = GenerateContentionTargets(
                parameters.elementCount, parameters.activeCounterCount);
            auto initial = MakeZeroInitialCounterState(
                parameters.allocatedCounterCount);
            auto expected = ReferenceContentionHistogram(
                targets, parameters.allocatedCounterCount);
            auto identity = evidence::MakeI7ContentionInputIdentity(
                cell.configuration, targets, initial);
            auto cuda = evidence::MakeI7WordCorrectnessFoundation(
                std::move(cudaRunId), cudaIdentity, identity, expected);
            auto vulkan = evidence::MakeI7WordCorrectnessFoundation(
                std::move(vulkanRunId), vulkanIdentity, identity, expected);
            return {std::move(cell), ContentionLogicalData{
                std::move(targets), std::move(initial), std::move(expected)},
                std::move(cuda), std::move(vulkan)};
        }
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        {
            if (parameters.variant != IterativeVariant::D1)
                throw std::invalid_argument("I7 correctness child rejects D2");
            auto input = GenerateWordInput(CoreInputSeed, parameters.elementCount);
            auto expected = ReferenceD1(input, parameters.iterationCount).finalState;
            auto identity = evidence::MakeI7WordInputIdentity(
                cell.configuration, input);
            auto cuda = evidence::MakeI7WordCorrectnessFoundation(
                std::move(cudaRunId), cudaIdentity, identity, expected);
            auto vulkan = evidence::MakeI7WordCorrectnessFoundation(
                std::move(vulkanRunId), vulkanIdentity, identity, expected);
            return {std::move(cell), WordLogicalData{
                std::move(input), std::move(expected)},
                std::move(cuda), std::move(vulkan)};
        }
        else
        {
            auto reference = ReferenceTransfer(
                CoreInputSeed, parameters.byteCount, parameters.direction);
            auto identity = evidence::MakeI7ByteInputIdentity(
                cell.configuration, reference.source);
            auto cuda = evidence::MakeI7ByteCorrectnessFoundation(
                std::move(cudaRunId), cudaIdentity, identity,
                reference.expectedDestination);
            auto vulkan = evidence::MakeI7ByteCorrectnessFoundation(
                std::move(vulkanRunId), vulkanIdentity, identity,
                reference.expectedDestination);
            return {std::move(cell), ByteLogicalData{
                std::move(reference.source),
                std::move(reference.expectedDestination)},
                std::move(cuda), std::move(vulkan)};
        }
    }, cell.configuration.parameters);
}

SerializedSeries BuildSerializedSeries(
    const evidence::I7CorrectnessFoundation& foundation,
    results::EnvironmentRecord commonEnvironment,
    evidence::SampleRecord sample,
    bool setupEstablished,
    evidence::BackendDiagnostics diagnostics,
    std::string setupObservation)
{
    if (!setupEstablished)
        throw std::invalid_argument(
            "setup_complete requires an established backend setup");
    commonEnvironment.schemaVersion = evidence::SchemaVersion;
    auto environmentRecord = evidence::MakeI7EnvironmentRecord(
        std::move(commonEnvironment), foundation, std::move(diagnostics));
    std::vector initialization{
        evidence::MakeI7SetupCompleteInitialization(
            foundation, std::move(setupObservation))};
    std::vector samples{std::move(sample)};
    const auto& row = samples.front();
    const auto summary = evidence::SummarizeSamples(
        foundation.Plan(), samples, row.status, row.failurePhase, row.errorCode);
    evidence::ValidateEvidenceBundle(
        environmentRecord, initialization, samples, summary);
    SerializedSeries result{std::move(environmentRecord),
        std::move(initialization), std::move(samples), summary};
    result.environmentJson = evidence::SerializeEnvironmentJson(result.environment);
    result.initializationCsv = evidence::SerializeInitializationCsv(
        result.environment.plan, result.initialization);
    result.samplesCsv = evidence::SerializeSamplesCsv(
        result.environment.plan, result.samples);
    result.summaryJson = evidence::SerializeSummaryJson(
        result.summary, result.samples);
    return result;
}

void CreateStagingSession(const SessionPlan& plan)
{
    RequireUnusedSessionPlan(plan);
    std::error_code error;
    std::filesystem::create_directories(plan.localResultsRoot, error);
    if (error || !std::filesystem::create_directory(plan.stagingDirectory, error)
        || error)
    {
        throw std::runtime_error("I7 staging directory creation failed");
    }
    if (!std::filesystem::create_directory(plan.cudaDirectory, error) || error
        || !std::filesystem::create_directory(plan.vulkanDirectory, error) || error)
    {
        throw std::runtime_error("I7 backend staging directory creation failed");
    }
}

void WriteStagedSession(const SessionPlan& plan, const SerializedPair& pair)
{
    const auto writeSeries = [](const std::filesystem::path& directory,
        const SerializedSeries& series) {
        WriteNewFile(directory / "environment.json", series.environmentJson);
        WriteNewFile(directory / "initialization.csv", series.initializationCsv);
        WriteNewFile(directory / "samples.csv", series.samplesCsv);
        WriteNewFile(directory / "summary.json", series.summaryJson);
    };
    writeSeries(plan.cudaDirectory, pair.cuda);
    writeSeries(plan.vulkanDirectory, pair.vulkan);
}

namespace
{

void VerifyEnvironmentFile(
    const std::filesystem::path& path,
    const evidence::EnvironmentRecord& expected)
{
    const auto object = ParseTopLevelObject(ReadFile(path));
    const auto& plan = expected.plan;
    const auto& series = plan.seriesIdentity;
    const auto& condition = series.condition;
    if (JsonUnsignedField(object, "schema_version") != evidence::SchemaVersion)
        throw std::invalid_argument("disk environment schema_version mismatch");
    RequireEqual(JsonStringField(object, "experiment_id"),
        evidence::ExperimentId, "experiment_id");
    RequireEqual(JsonStringField(object, "evidence_kind"),
        evidence::EvidenceKind, "evidence_kind");
    RequireEqual(JsonStringField(object, "protocol_version"),
        evidence::I7CorrectnessProtocolVersion, "protocol_version");
    RequireEqual(JsonStringField(object, "run_id"), plan.runId, "run_id");
    RequireEqual(JsonStringField(object, "backend"),
        ex2::ToString(series.backend), "backend");
    RequireEqual(JsonStringField(object, "instrument_mode"), "P",
        "instrument_mode");
    RequireEqual(JsonStringField(object, "comparison_condition_id"),
        plan.comparisonConditionId, "comparison_condition_id");
    RequireEqual(JsonStringField(object, "series_id"), plan.seriesId,
        "series_id");
    RequireEqual(JsonStringField(object, "source_revision"),
        series.sourceRevision, "source_revision");
    RequireEqual(JsonStringField(object, "executable_sha256"),
        series.executableSha256, "executable_sha256");
    RequireEqual(JsonStringField(object, "input_sha256"),
        expected.inputSha256, "input_sha256");
    RequireEqual(JsonStringField(object, "expected_output_sha256"),
        expected.expectedOutputSha256, "expected_output_sha256");
    RequireEqual(JsonStringField(object, "gpu_uuid_identity"),
        condition.gpuIdentity.uuid, "gpu_uuid_identity");
    if (JsonOptionalStringField(object, "shader_sha256") != series.shaderSha256)
        throw std::invalid_argument("disk environment shader_sha256 mismatch");
    if (JsonUnsignedField(object, "warmup_count") != 0U
        || JsonUnsignedField(object, "planned_sample_count") != 1U
        || JsonUnsignedField(object, "block_index") != 0U
        || JsonUnsignedField(object, "process_index") != 0U
        || JsonUnsignedField(object, "order_slot") != series.orderSlot)
    {
        throw std::invalid_argument("disk environment correctness identity mismatch");
    }
}

void VerifyInitializationFile(
    const std::filesystem::path& path,
    const SerializedSeries& expected)
{
    const auto rows = ParseCsv(ReadFile(path));
    if (rows.size() != 2U || rows.front().size() != 13U
        || rows.front() != ParseCsv(evidence::InitializationCsvHeader() + "\r\n").front())
    {
        throw std::invalid_argument("disk initialization.csv header or row count is invalid");
    }
    const auto& row = rows[1];
    if (row.size() != 13U)
        throw std::invalid_argument("disk initialization.csv row width is invalid");
    RequireEqual(row[0], "2", "initialization.schema_version");
    RequireEqual(row[1], expected.environment.plan.runId, "initialization.run_id");
    RequireEqual(row[2], "EX-2", "initialization.experiment_id");
    RequireEqual(row[3], ex2::ToString(
        expected.environment.plan.seriesIdentity.backend), "initialization.backend");
    RequireEqual(row[4], "0", "initialization.process_index");
    RequireEqual(row[10], "setup_complete", "initialization.metric");
    if (!row[11].empty() || row[12].empty())
        throw std::invalid_argument("disk initialization row fabricates or omits setup truth");
}

void VerifySamplesFile(
    const std::filesystem::path& path,
    const SerializedSeries& expected)
{
    const auto rows = ParseCsv(ReadFile(path));
    if (rows.size() != 2U || rows.front().size() != 31U
        || rows.front() != ParseCsv(evidence::SamplesCsvHeader() + "\r\n").front())
    {
        throw std::invalid_argument("disk samples.csv header or row count is invalid");
    }
    const auto& row = rows[1];
    const auto& plan = expected.environment.plan;
    const auto& sample = expected.samples.front();
    if (row.size() != 31U)
        throw std::invalid_argument("disk samples.csv row width is invalid");
    RequireEqual(row[0], "2", "sample.schema_version");
    RequireEqual(row[1], plan.runId, "sample.run_id");
    RequireEqual(row[2], "EX-2", "sample.experiment_id");
    RequireEqual(row[3], plan.comparisonConditionId,
        "sample.comparison_condition_id");
    RequireEqual(row[4], plan.seriesId, "sample.series_id");
    RequireEqual(row[5], ex2::ToString(plan.seriesIdentity.backend),
        "sample.backend");
    RequireEqual(row[16], "P", "sample.instrument_mode");
    RequireEqual(row[17], "0", "sample.warmup_count");
    RequireEqual(row[18], "1", "sample.planned_sample_count");
    RequireEqual(row[19], "0", "sample.block_index");
    RequireEqual(row[20], std::to_string(plan.seriesIdentity.orderSlot),
        "sample.order_slot");
    RequireEqual(row[21], "0", "sample.process_index");
    RequireEqual(row[22], "0", "sample.sample_index");
    RequireEqual(row[23], sample.correctness.validationPassed.has_value()
        ? (*sample.correctness.validationPassed ? "true" : "false") : "",
        "sample.validation_passed");
    RequireEqual(row[24], evidence::ToString(sample.status), "sample.status");
    for (std::size_t index = 27U; index <= 30U; ++index)
    {
        if (!row[index].empty())
            throw std::invalid_argument("disk correctness sample contains timing data");
    }
}

void VerifySummaryFile(
    const std::filesystem::path& path,
    const SerializedSeries& expected)
{
    const auto object = ParseTopLevelObject(ReadFile(path));
    const auto& summary = expected.summary;
    if (JsonUnsignedField(object, "schema_version") != 2U)
        throw std::invalid_argument("disk summary schema_version mismatch");
    RequireEqual(JsonStringField(object, "run_id"), summary.plan.runId,
        "summary.run_id");
    RequireEqual(JsonStringField(object, "experiment_id"), "EX-2",
        "summary.experiment_id");
    RequireEqual(JsonStringField(object, "evidence_kind"), "correctness",
        "summary.evidence_kind");
    RequireEqual(JsonStringField(object, "process_status"),
        evidence::ToString(summary.processStatus), "summary.process_status");
    const auto& groupsRaw = RequireField(object, "sample_groups");
    if (groupsRaw.size() < 4U || groupsRaw.front() != '['
        || groupsRaw.back() != ']')
        throw std::invalid_argument("disk summary sample_groups is invalid");
    const auto groupEntry = ParseTopLevelObject(std::string_view(groupsRaw).substr(
        1U, groupsRaw.size() - 2U));
    if (JsonUnsignedField(groupEntry, "recorded_sample_count")
            != summary.recordedSampleCount
        || JsonUnsignedField(groupEntry, "validation_failures")
            != summary.validationFailures
        || JsonUnsignedField(groupEntry, "failed_sample_count")
            != summary.failedSampleCount)
    {
        throw std::invalid_argument("disk summary reconstructed counts mismatch");
    }
    const auto group = ParseTopLevelObject(RequireField(groupEntry, "group"));
    RequireEqual(JsonStringField(group, "comparison_condition_id"),
        summary.plan.comparisonConditionId, "summary.comparison_condition_id");
    RequireEqual(JsonStringField(group, "series_id"), summary.plan.seriesId,
        "summary.series_id");
}

void VerifySeriesDirectory(
    const std::filesystem::path& directory,
    const SerializedSeries& expected,
    std::vector<DiskArtifact>& artifacts,
    std::string_view prefix)
{
    std::set<std::string> found;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            throw std::invalid_argument("I7 series directory contains a non-file entry");
        found.insert(entry.path().filename().string());
    }
    if (found != std::set<std::string>{
            "environment.json", "initialization.csv", "samples.csv", "summary.json"})
    {
        throw std::invalid_argument("I7 series directory does not contain exactly four files");
    }
    VerifyEnvironmentFile(directory / "environment.json", expected.environment);
    VerifyInitializationFile(directory / "initialization.csv", expected);
    VerifySamplesFile(directory / "samples.csv", expected);
    VerifySummaryFile(directory / "summary.json", expected);
    for (const auto file : kSeriesFiles)
    {
        const auto path = directory / file;
        artifacts.push_back({std::string(prefix) + "/" + std::string(file),
            std::filesystem::file_size(path), Sha256File(path)});
    }
}

} // namespace

DiskVerification VerifyStagedSession(
    const SessionPlan& plan,
    const SerializedPair& expected)
{
    DiskVerification result;
    VerifySeriesDirectory(
        plan.cudaDirectory, expected.cuda, result.artifacts, plan.cudaRunId);
    VerifySeriesDirectory(
        plan.vulkanDirectory, expected.vulkan, result.artifacts, plan.vulkanRunId);
    const auto& cuda = expected.cuda.environment;
    const auto& vulkan = expected.vulkan.environment;
    if (cuda.plan.comparisonConditionId != vulkan.plan.comparisonConditionId
        || cuda.plan.seriesId == vulkan.plan.seriesId
        || cuda.plan.runId == vulkan.plan.runId
        || cuda.inputSha256 != vulkan.inputSha256
        || cuda.expectedOutputSha256 != vulkan.expectedOutputSha256
        || cuda.plan.seriesIdentity.condition.gpuIdentity.uuid
            != vulkan.plan.seriesIdentity.condition.gpuIdentity.uuid
        || cuda.plan.seriesIdentity.sourceRevision
            != vulkan.plan.seriesIdentity.sourceRevision
        || cuda.plan.seriesIdentity.executableSha256
            != vulkan.plan.seriesIdentity.executableSha256)
    {
        throw std::invalid_argument("I7 disk pair identity invariants failed");
    }
    if (result.artifacts.size() != 8U)
        throw std::logic_error("I7 disk verification artifact count is invalid");
    return result;
}

void FinalizeStagedSession(const SessionPlan& plan)
{
    std::error_code error;
    if (std::filesystem::exists(plan.finalDirectory, error) || error)
        throw std::runtime_error("I7 final session destination is unavailable");
    std::filesystem::rename(plan.stagingDirectory, plan.finalDirectory, error);
    if (error)
        throw std::runtime_error("I7 staged session final rename failed");
}

std::string_view ToString(ExternalFailurePhase phase) noexcept
{
    switch (phase)
    {
    case ExternalFailurePhase::Configuration: return "configuration";
    case ExternalFailurePhase::Provenance: return "provenance";
    case ExternalFailurePhase::InputGeneration: return "input_generation";
    case ExternalFailurePhase::DeviceIdentity: return "device_identity";
    case ExternalFailurePhase::BackendExecution: return "backend_execution";
    case ExternalFailurePhase::Validation: return "validation";
    case ExternalFailurePhase::EvidenceSerialization: return "evidence_serialization";
    case ExternalFailurePhase::EvidencePublication: return "evidence_publication";
    case ExternalFailurePhase::PackageVerification: return "package_verification";
    case ExternalFailurePhase::Interrupted: return "interrupted";
    }
    return "invalid";
}

std::string SerializeExternalFailureRecord(const ExternalFailureRecord& record)
{
    if (record.recordVersion != 1U
        || !IsValidAnonymousIdentifier(record.sessionId)
        || ToString(record.phase) == "invalid"
        || !IsSafeFailureToken(record.errorCode, 96U)
        || (record.sourceRevision.has_value()
            && (record.sourceRevision->size() != 40U
                || !std::ranges::all_of(*record.sourceRevision, [](char value) {
                    return (value >= '0' && value <= '9')
                        || (value >= 'a' && value <= 'f');
                })))
        || (record.cudaRunId.has_value()
            && !IsValidAnonymousIdentifier(*record.cudaRunId))
        || (record.vulkanRunId.has_value()
            && !IsValidAnonymousIdentifier(*record.vulkanRunId))
        || (record.detail.has_value() && !IsSafeFailureDetail(*record.detail)))
    {
        throw std::invalid_argument("I7 external failure record is invalid");
    }
    const auto appendOptional = [](std::string& output,
        const std::optional<std::string>& value) {
        if (!value.has_value()) output += "null";
        else output += "\"" + JsonEscape(*value) + "\"";
    };
    std::string output = "{\"record_version\":1,"
        "\"record_type\":\"ex2-i7-correctness-child-failure\","
        "\"session_id\":\"" + JsonEscape(record.sessionId)
        + "\",\"failure_phase\":\"" + std::string(ToString(record.phase))
        + "\",\"error_code\":\"" + JsonEscape(record.errorCode)
        + "\",\"foundation_established\":"
        + (record.foundationEstablished ? "true" : "false")
        + ",\"source_revision\":";
    appendOptional(output, record.sourceRevision);
    output += ",\"cuda_run_id\":"; appendOptional(output, record.cudaRunId);
    output += ",\"vulkan_run_id\":"; appendOptional(output, record.vulkanRunId);
    output += ",\"staging_retained\":";
    output += record.stagingRetained ? "true" : "false";
    output += ",\"detail\":"; appendOptional(output, record.detail);
    output += "}\n";
    return output;
}

void WriteExternalFailureRecord(
    const SessionPlan& plan,
    const ExternalFailureRecord& record)
{
    if (!IsWithin(plan.failureSidecar, plan.localResultsRoot))
        throw std::invalid_argument("I7 failure sidecar escapes results/local");
    std::error_code error;
    std::filesystem::create_directories(plan.localResultsRoot, error);
    if (error)
        throw std::runtime_error("I7 results/local root creation failed");
    WriteNewFile(plan.failureSidecar, SerializeExternalFailureRecord(record));
}

ExitCode RunCorrectnessChild(
    const CorrectnessChildConfiguration& configuration,
    const CorrectnessRuntimePaths& paths,
    control::AttemptObserver observer)
{
    ExitCode failureExit = ExitCode::PreFoundationFailure;
    std::optional<SessionPlan> plan;
    std::optional<GitState> gitBefore;
    bool foundationEstablished = false;
    ExternalFailurePhase activeFailurePhase = ExternalFailurePhase::Configuration;
    std::string activeFailureCode = "pre_foundation_failure";
    std::optional<std::string> activeFailureDetail;

    const auto retainFailure = [&](ExternalFailurePhase phase,
        std::string errorCode) noexcept {
        if (!plan.has_value()) return;
        try
        {
            WriteExternalFailureRecord(*plan, {1U, plan->sessionId, phase,
                std::move(errorCode), foundationEstablished,
                gitBefore.has_value()
                    ? std::optional<std::string>(gitBefore->revision) : std::nullopt,
                foundationEstablished
                    ? std::optional<std::string>(plan->cudaRunId) : std::nullopt,
                foundationEstablished
                    ? std::optional<std::string>(plan->vulkanRunId) : std::nullopt,
                IsStagingRetained(*plan), activeFailureDetail});
        }
        catch (...) {}
    };
    const auto failureResult = [&]() noexcept {
        if (failureExit == ExitCode::ExecutionFailure)
        {
            retainFailure(activeFailurePhase, activeFailureCode);
            return ExitCode::ExecutionFailure;
        }
        retainFailure(activeFailurePhase, activeFailureCode);
        return failureExit;
    };

    try
    {
        if (!IsValidAnonymousIdentifier(configuration.machineId))
            throw std::invalid_argument("I7 machine ID is invalid");
        plan = MakeSessionPlan(paths.localResultsRoot, configuration.sessionId);
        RequireUnusedSessionPlan(*plan);
        const auto cell = SelectApprovedCoreCell(configuration.coreCellIndex);
        activeFailurePhase = ExternalFailurePhase::Provenance;
        activeFailureCode = "provenance_acquisition_failed";
        gitBefore = ReadGitState(paths.repositoryRoot);
        const std::string executableHash = Sha256File(paths.executable);

        activeFailurePhase = ExternalFailurePhase::DeviceIdentity;
        activeFailureCode = "device_identity_preflight_failed";
        const auto cudaDevices = environment::EnumerateCudaDeviceMetadata();
        const auto vulkanDevices = environment::EnumerateVulkanDeviceMetadata();
        if (configuration.cudaDeviceOrdinal < 0
            || static_cast<std::size_t>(configuration.cudaDeviceOrdinal)
                >= cudaDevices.size())
        {
            throw std::invalid_argument("selected CUDA device ordinal is out of range");
        }
        if (configuration.vulkanPhysicalDeviceIndex >= vulkanDevices.size())
            throw std::invalid_argument(
                "selected Vulkan physical-device index is out of range");
        const auto& cudaUuid = cudaDevices[static_cast<std::size_t>(
            configuration.cudaDeviceOrdinal)].uuid;
        const auto& vulkanUuid = vulkanDevices[
            configuration.vulkanPhysicalDeviceIndex].uuid;
        RequireSameUuid(cudaUuid, vulkanUuid);
        const std::string uuidText = FormatUuid(cudaUuid);

        const auto shaderPath = ShaderPath(cell.route, paths);
        const std::optional<std::string> shaderHash = shaderPath.empty()
            ? std::nullopt
            : std::optional<std::string>(Sha256File(shaderPath));
        activeFailurePhase = ExternalFailurePhase::InputGeneration;
        activeFailureCode = "foundation_construction_failed";
        auto foundations = PrepareFoundationPair(
            cell, configuration.machineId, uuidText, gitBefore->revision,
            executableHash, shaderHash, plan->cudaRunId, plan->vulkanRunId);
        foundationEstablished = true;

        activeFailurePhase = ExternalFailurePhase::Provenance;
        activeFailureCode = "environment_acquisition_failed";
        const std::string timestamp = TimestampUtc();
        const environment::EnvironmentRunContext cudaContext{
            "EX-2", plan->cudaRunId, timestamp, gitBefore->revision,
            gitBefore->dirty, configuration.machineId, true, true};
        const environment::EnvironmentRunContext vulkanContext{
            "EX-2", plan->vulkanRunId, timestamp, gitBefore->revision,
            gitBefore->dirty, configuration.machineId, true, true};
        auto cudaEnvironment = environment::CollectCudaEnvironmentRecord(
            cudaContext, static_cast<std::uint32_t>(configuration.cudaDeviceOrdinal),
            cudaUuid);
        auto vulkanEnvironment = environment::CollectVulkanEnvironmentRecord(
            vulkanContext, configuration.vulkanPhysicalDeviceIndex, vulkanUuid);
        cudaEnvironment.schemaVersion = evidence::SchemaVersion;
        vulkanEnvironment.schemaVersion = evidence::SchemaVersion;
        const auto requireCommonIdentity = [](const results::EnvironmentRecord& common,
            const evidence::I7CorrectnessFoundation& foundation) {
            const auto& identity = foundation.Plan().seriesIdentity;
            if (common.schemaVersion != evidence::SchemaVersion)
                throw std::runtime_error("environment schema identity mismatch");
            if (common.experimentId != evidence::ExperimentId)
                throw std::runtime_error("environment experiment identity mismatch");
            if (common.runId != foundation.Plan().runId)
                throw std::runtime_error("environment run identity mismatch");
            if (common.machineId != identity.condition.machineId)
                throw std::runtime_error("environment machine identity mismatch");
            if (common.gitCommit != identity.sourceRevision)
                throw std::runtime_error("environment source identity mismatch");
        };
        requireCommonIdentity(cudaEnvironment, foundations.cuda);
        requireCommonIdentity(vulkanEnvironment, foundations.vulkan);

        const auto stagingFailure = StagingCreationFailure();
        failureExit = stagingFailure.exitCode;
        activeFailurePhase = stagingFailure.phase;
        activeFailureCode = stagingFailure.errorCode;
        CreateStagingSession(*plan);
        failureExit = ExitCode::ExecutionFailure;
        activeFailurePhase = ExternalFailurePhase::BackendExecution;
        activeFailureCode = "post_foundation_execution_failed";

        evidence::SampleRecord cudaSample;
        evidence::SampleRecord vulkanSample;
        bool executionPassed = false;
        bool completedObservations = false;
        const auto classifyReturnedPair = [&](bool cudaCompleted,
            bool vulkanCompleted, bool cudaObserved, bool vulkanObserved,
            auto cudaStatus, auto vulkanStatus) {
            const auto acceptedStatus = [](auto status) {
                const auto normalized = NormalizeStatus(status);
                return normalized == evidence::OperationStatus::Ok
                    || normalized == evidence::OperationStatus::ValidationFailed;
            };
            completedObservations = cudaCompleted && vulkanCompleted
                && cudaObserved && vulkanObserved
                && acceptedStatus(cudaStatus) && acceptedStatus(vulkanStatus);
            if (completedObservations)
            {
                activeFailurePhase = ExternalFailurePhase::Validation;
                activeFailureCode = "completed_validation_failed";
            }
        };
        const auto requirePairSetup = [&](bool cudaSetup, bool vulkanSetup) {
            if (cudaSetup && vulkanSetup) return false;
            retainFailure(ExternalFailurePhase::BackendExecution,
                "backend_setup_not_established");
            return true;
        };

        if (cell.route == CoreCellRoute::A1 || cell.route == CoreCellRoute::A2)
        {
            const auto parameters = std::get<LinearConfiguration>(
                cell.configuration.parameters);
            const auto observation = cell.route == CoreCellRoute::A1
                ? a1::RunCrossBackendCorrectness(parameters.elementCount,
                    CoreInputSeed, configuration.cudaDeviceOrdinal,
                    configuration.vulkanPhysicalDeviceIndex, shaderPath, observer)
                : a2::RunCrossBackendCorrectness(parameters.elementCount,
                    CoreInputSeed, configuration.cudaDeviceOrdinal,
                    configuration.vulkanPhysicalDeviceIndex, shaderPath, observer);
            classifyReturnedPair(observation.cuda.operationCompleted,
                observation.vulkan.operationCompleted,
                observation.cuda.outputObserved, observation.vulkan.outputObserved,
                observation.cuda.status, observation.vulkan.status);
            const auto& data = std::get<WordLogicalData>(foundations.logicalData);
            RequireObservationIdentity(observation.configuration, cell.configuration,
                observation.physicalIdentityVerified, observation.verifiedDeviceUuid,
                observation.verifiedDeviceUuidText, cudaUuid);
            if (observation.input != data.input
                || observation.expectedOutput != data.expected
                || observation.inputSha256 != foundations.cuda.InputIdentity().Sha256()
                || observation.expectedOutputSha256
                    != foundations.cuda.ExpectedOutputSha256()
                || observation.vulkanShaderSha256 != *shaderHash)
            {
                throw std::runtime_error("linear observation identity mismatch");
            }
            if (requirePairSetup(HasEstablishedSetup(observation.cuda),
                    HasEstablishedSetup(observation.vulkan)))
                return ExitCode::ExecutionFailure;
            const auto makeSample = [&](const a1::BackendObservation& backend,
                const evidence::I7CorrectnessFoundation& foundation) {
                if ((backend.status == evidence::OperationStatus::Ok
                        || backend.status == evidence::OperationStatus::ValidationFailed)
                    && backend.outputObserved && backend.comparisonPerformed)
                    return evidence::MakeI7ComparedWordSample(
                        foundation, 0U, data.expected, backend.output);
                return evidence::MakeI7FailureObservation(foundation, 0U,
                    MapFailure(backend.status, backend.failurePhase,
                        backend.operationCompleted, backend.outputObserved)).sample;
            };
            cudaSample = makeSample(observation.cuda, foundations.cuda);
            vulkanSample = makeSample(observation.vulkan, foundations.vulkan);
            executionPassed = observation.Passed();
        }
        else if (cell.route == CoreCellRoute::B1 || cell.route == CoreCellRoute::B2)
        {
            const auto parameters = std::get<IndexedConfiguration>(
                cell.configuration.parameters);
            const auto observation = b::RunCrossBackendCorrectness(
                parameters.variant, parameters.elementCount,
                parameters.indexPattern, CoreInputSeed,
                configuration.cudaDeviceOrdinal,
                configuration.vulkanPhysicalDeviceIndex,
                paths.b1Spirv, paths.b2Spirv, observer);
            classifyReturnedPair(observation.cuda.nativeOperationCompleted,
                observation.vulkan.nativeOperationCompleted,
                observation.cuda.outputObserved, observation.vulkan.outputObserved,
                observation.cuda.status, observation.vulkan.status);
            const auto& data = std::get<IndexedLogicalData>(foundations.logicalData);
            RequireObservationIdentity(observation.configuration, cell.configuration,
                observation.physicalIdentityVerified, observation.verifiedDeviceUuid,
                observation.verifiedDeviceUuidText, cudaUuid);
            if (observation.primaryInput != data.primary
                || observation.permutation != data.permutation
                || observation.expectedOutput != data.expected
                || observation.primaryInputSha256 != WordInputSha256(data.primary)
                || observation.permutationSha256 != WordInputSha256(data.permutation)
                || observation.expectedOutputSha256
                    != foundations.cuda.ExpectedOutputSha256()
                || !observation.vulkanShaderProvenanceVerified
                || observation.vulkanLoadedSpirvSha256 != *shaderHash
                || observation.vulkanShaderFileSha256 != *shaderHash)
            {
                throw std::runtime_error("indexed observation identity mismatch");
            }
            if (requirePairSetup(HasEstablishedSetup(observation.cuda),
                    HasEstablishedSetup(observation.vulkan)))
                return ExitCode::ExecutionFailure;
            const auto makeSample = [&](const b::BackendObservation& backend,
                const evidence::I7CorrectnessFoundation& foundation) {
                if ((backend.status == b::IntegrationStatus::Ok
                        || backend.status == b::IntegrationStatus::ValidationFailed)
                    && backend.outputObserved && backend.cpuComparisonPerformed)
                    return evidence::MakeI7ComparedWordSample(
                        foundation, 0U, data.expected, backend.output);
                return evidence::MakeI7FailureObservation(foundation, 0U,
                    MapIntegrationFailure(backend, backend.nativeOperationCompleted,
                        backend.outputObserved)).sample;
            };
            cudaSample = makeSample(observation.cuda, foundations.cuda);
            vulkanSample = makeSample(observation.vulkan, foundations.vulkan);
            executionPassed = observation.Passed();
        }
        else if (cell.route == CoreCellRoute::C)
        {
            const auto parameters = std::get<ContentionConfiguration>(
                cell.configuration.parameters);
            const auto observation = c::RunCrossBackendCorrectness(
                parameters.elementCount, parameters.activeCounterCount,
                configuration.cudaDeviceOrdinal,
                configuration.vulkanPhysicalDeviceIndex, paths.cSpirv, observer);
            classifyReturnedPair(observation.cuda.nativeOperationCompleted,
                observation.vulkan.nativeOperationCompleted,
                observation.cuda.countersObserved, observation.vulkan.countersObserved,
                observation.cuda.status, observation.vulkan.status);
            const auto& data = std::get<ContentionLogicalData>(foundations.logicalData);
            RequireObservationIdentity(observation.configuration, cell.configuration,
                observation.physicalIdentityVerified, observation.verifiedDeviceUuid,
                observation.verifiedDeviceUuidText, cudaUuid);
            if (observation.targets != data.targets
                || observation.expectedCounters != data.expected
                || observation.targetsSha256 != WordInputSha256(data.targets)
                || observation.expectedCountersSha256
                    != foundations.cuda.ExpectedOutputSha256()
                || !std::ranges::all_of(data.initialCounters,
                    [](std::uint32_t value) { return value == 0U; })
                || !observation.vulkanShaderProvenanceVerified
                || observation.vulkanLoadedSpirvSha256 != *shaderHash
                || observation.vulkanShaderFileSha256 != *shaderHash)
            {
                throw std::runtime_error("contention observation identity mismatch");
            }
            if (requirePairSetup(HasEstablishedSetup(observation.cuda),
                    HasEstablishedSetup(observation.vulkan)))
                return ExitCode::ExecutionFailure;
            const auto makeSample = [&](const c::BackendObservation& backend,
                const evidence::I7CorrectnessFoundation& foundation) {
                if ((backend.status == c::IntegrationStatus::Ok
                        || backend.status == c::IntegrationStatus::ValidationFailed)
                    && backend.countersObserved && backend.cpuComparisonPerformed)
                    return evidence::MakeI7ComparedWordSample(
                        foundation, 0U, data.expected, backend.counters);
                return evidence::MakeI7FailureObservation(foundation, 0U,
                    MapIntegrationFailure(backend, backend.nativeOperationCompleted,
                        backend.countersObserved)).sample;
            };
            cudaSample = makeSample(observation.cuda, foundations.cuda);
            vulkanSample = makeSample(observation.vulkan, foundations.vulkan);
            executionPassed = observation.Passed();
        }
        else if (cell.route == CoreCellRoute::D1)
        {
            const auto parameters = std::get<IterativeConfiguration>(
                cell.configuration.parameters);
            const auto observation = d1::RunCrossBackendCorrectness(
                parameters.elementCount, parameters.iterationCount,
                configuration.cudaDeviceOrdinal,
                configuration.vulkanPhysicalDeviceIndex, paths.d1Spirv, observer);
            classifyReturnedPair(observation.cuda.nativeSequenceCompleted,
                observation.vulkan.nativeSequenceCompleted,
                observation.cuda.finalStateObserved,
                observation.vulkan.finalStateObserved,
                observation.cuda.status, observation.vulkan.status);
            const auto& data = std::get<WordLogicalData>(foundations.logicalData);
            RequireObservationIdentity(observation.configuration, cell.configuration,
                observation.physicalIdentityVerified, observation.verifiedDeviceUuid,
                observation.verifiedDeviceUuidText, cudaUuid);
            if (observation.initialState != data.input
                || observation.expectedFinalState != data.expected
                || observation.initialStateSha256 != WordInputSha256(data.input)
                || observation.expectedFinalStateSha256
                    != foundations.cuda.ExpectedOutputSha256()
                || !observation.vulkanShaderProvenanceVerified
                || observation.vulkanLoadedSpirvSha256 != *shaderHash
                || observation.vulkanShaderFileSha256 != *shaderHash)
            {
                throw std::runtime_error("iterative observation identity mismatch");
            }
            if (requirePairSetup(HasEstablishedSetup(observation.cuda),
                    HasEstablishedSetup(observation.vulkan)))
                return ExitCode::ExecutionFailure;
            const auto makeSample = [&](const d1::BackendObservation& backend,
                const evidence::I7CorrectnessFoundation& foundation) {
                if ((backend.status == d1::IntegrationStatus::Ok
                        || backend.status == d1::IntegrationStatus::ValidationFailed)
                    && backend.finalStateObserved && backend.cpuComparisonPerformed)
                    return evidence::MakeI7ComparedWordSample(
                        foundation, 0U, data.expected, backend.finalState);
                return evidence::MakeI7FailureObservation(foundation, 0U,
                    MapIntegrationFailure(backend, backend.nativeSequenceCompleted,
                        backend.finalStateObserved)).sample;
            };
            cudaSample = makeSample(observation.cuda, foundations.cuda);
            vulkanSample = makeSample(observation.vulkan, foundations.vulkan);
            executionPassed = observation.Passed();
        }
        else
        {
            const auto parameters = std::get<TransferConfiguration>(
                cell.configuration.parameters);
            const auto observation = e::RunCrossBackendCorrectness(
                parameters.direction, parameters.byteCount,
                configuration.cudaDeviceOrdinal,
                configuration.vulkanPhysicalDeviceIndex, observer);
            classifyReturnedPair(observation.cuda.nativeTransferCompleted,
                observation.vulkan.nativeTransferCompleted,
                observation.cuda.destinationObserved,
                observation.vulkan.destinationObserved,
                observation.cuda.status, observation.vulkan.status);
            const auto& data = std::get<ByteLogicalData>(foundations.logicalData);
            RequireObservationIdentity(observation.configuration, cell.configuration,
                observation.physicalIdentityVerified, observation.verifiedDeviceUuid,
                observation.verifiedDeviceUuidText, cudaUuid);
            if (observation.source != data.source
                || observation.expectedDestination != data.expected
                || observation.sourceSha256 != foundations.cuda.InputIdentity().Sha256()
                || observation.expectedDestinationSha256
                    != foundations.cuda.ExpectedOutputSha256()
                || shaderHash.has_value())
            {
                throw std::runtime_error("transfer observation identity mismatch");
            }
            if (requirePairSetup(HasEstablishedSetup(observation.cuda),
                    HasEstablishedSetup(observation.vulkan)))
                return ExitCode::ExecutionFailure;
            const auto makeSample = [&](const e::BackendObservation& backend,
                const evidence::I7CorrectnessFoundation& foundation) {
                if ((backend.status == e::IntegrationStatus::Ok
                        || backend.status == e::IntegrationStatus::ValidationFailed)
                    && backend.destinationObserved && backend.cpuComparisonPerformed)
                    return evidence::MakeI7ComparedByteSample(
                        foundation, 0U, data.expected, backend.destination);
                return evidence::MakeI7FailureObservation(foundation, 0U,
                    MapIntegrationFailure(backend, backend.nativeTransferCompleted,
                        backend.destinationObserved)).sample;
            };
            cudaSample = makeSample(observation.cuda, foundations.cuda);
            vulkanSample = makeSample(observation.vulkan, foundations.vulkan);
            executionPassed = observation.Passed();
        }

        failureExit = ExitCode::PackageFailure;
        activeFailurePhase = ExternalFailurePhase::EvidenceSerialization;
        activeFailureCode = "evidence_serialization_failed";
        SerializedPair serialized{
            BuildSerializedSeries(foundations.cuda, std::move(cudaEnvironment),
                std::move(cudaSample), true, Diagnostics(cell.route, Backend::Cuda),
                "setup_complete"),
            BuildSerializedSeries(foundations.vulkan, std::move(vulkanEnvironment),
                std::move(vulkanSample), true,
                Diagnostics(cell.route, Backend::Vulkan),
                "setup_complete")};
        activeFailurePhase = ExternalFailurePhase::EvidencePublication;
        activeFailureCode = "staged_write_failed";
        WriteStagedSession(*plan, serialized);
        activeFailurePhase = ExternalFailurePhase::PackageVerification;
        activeFailureCode = "package_verification_failed";
        static_cast<void>(VerifyStagedSession(*plan, serialized));

        activeFailurePhase = ExternalFailurePhase::Provenance;
        activeFailureCode = "source_state_changed";
        const auto gitAfter = ReadGitState(paths.repositoryRoot);
        if (gitAfter.revision != gitBefore->revision
            || gitAfter.workingTreeState != gitBefore->workingTreeState)
        {
            throw std::runtime_error(
                "source revision or working-tree state changed during execution");
        }
        if (!executionPassed)
        {
            const auto failure = ClassifyExecutionFailure(completedObservations);
            retainFailure(failure.phase, std::string(failure.errorCode));
            return failure.exitCode;
        }
        activeFailurePhase = ExternalFailurePhase::EvidencePublication;
        activeFailureCode = "final_rename_failed";
        FinalizeStagedSession(*plan);
        return ExitCode::Completed;
    }
    catch (const std::exception& error)
    {
        if (IsSafeFailureDetail(error.what())) activeFailureDetail = error.what();
        return failureResult();
    }
    catch (...) { return failureResult(); }
}

} // namespace computelab::ex2::correctness
