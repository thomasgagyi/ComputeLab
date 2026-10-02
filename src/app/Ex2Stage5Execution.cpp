#include "app/Ex2Stage5Execution.hpp"

#include "cuda/Ex2CudaA1.hpp"
#include "cuda/Ex2CudaD1.hpp"
#include "vulkan/Ex2VulkanA1.hpp"
#include "vulkan/Ex2VulkanD1.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>

namespace computelab::ex2::stage5::execution
{
namespace
{
namespace old = computelab::ex2::evidence;
namespace cu = computelab::cuda;
namespace vk = computelab::vulkan;
using Failure = ev::FailurePhase;
using Status = ev::Status;
void Require(bool value, const char* message)
{ if (!value) throw std::invalid_argument(message); }
bool Hex(std::string_view value, std::size_t size)
{
    return value.size() == size && std::ranges::all_of(value, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool Anonymous(std::string_view value)
{
    std::string upper{value};
    for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const bool reserved = upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL"
        || (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT"))
            && upper[3] >= '1' && upper[3] <= '9');
    return !reserved && !value.empty() && value.size() <= 96 && value.front() != '.'
        && value.find("..") == value.npos && std::ranges::all_of(value, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}
bool CanonicalUuid(std::string_view value)
{
    if (value.size() != 36) return false;
    std::string digits;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
        { if (value[i] != '-') return false; }
        else digits += value[i];
    }
    return Hex(digits, 32) && digits != std::string(32, '0');
}
template<class T> T Number(std::string_view value)
{
    std::uint64_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    Require(result.ec == std::errc{} && result.ptr == value.data() + value.size()
        && parsed <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()), "invalid unsigned option");
    return static_cast<T>(parsed);
}
std::string PhaseName(Stage5Phase phase)
{
    switch (phase)
    {
    case Stage5Phase::A1Sentinel: return "a1-sentinel";
    case Stage5Phase::D1Warmup: return "d1-warmup";
    case Stage5Phase::D1Sample: return "d1-sample";
    }
    throw std::invalid_argument("invalid phase");
}
std::string Quote(std::string_view value)
{
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value)
    {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
std::string OptionalText(const std::optional<std::string>& s) { return s ? Quote(*s) : "null"; }
bool Exists(const std::filesystem::path& path)
{ return std::filesystem::symlink_status(path).type() != std::filesystem::file_type::not_found; }
void PlainDirectory(const std::filesystem::path& path)
{
    Require(std::filesystem::symlink_status(path).type() == std::filesystem::file_type::directory,
        "package directory must be a plain directory");
    const DWORD attributes = GetFileAttributesW(path.c_str());
    Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
        "package directory cannot be a reparse point");
}
void WriteFileBytes(const std::filesystem::path& path, std::string_view bytes, bool append = false)
{
    HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, 0, nullptr,
        append ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("package file open failed");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    { CloseHandle(file); throw std::runtime_error("package file is a reparse point"); }
    std::size_t offset{};
    while (offset < bytes.size())
    {
        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD written{};
        if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || written != count)
        { CloseHandle(file); throw std::runtime_error("package file write failed"); }
        offset += written;
    }
    const bool flushed = FlushFileBuffers(file) != 0;
    const bool closed = CloseHandle(file) != 0;
    if (!flushed || !closed) throw std::runtime_error("package file flush/close failed");
}
std::string ReadFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("package reread failed");
    std::string result{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) throw std::runtime_error("package reread failed");
    return result;
}
std::map<std::string, std::string> Artifacts(const Bundle& bundle)
{
    const auto& f = bundle.foundation;
    Require(std::ranges::all_of(bundle.warmup, [](const auto& r) { return r.status == Status::Ok; })
        && std::ranges::all_of(bundle.samples, [](const auto& r) { return r.status == Status::Ok; }),
        "failed operations cannot finalize");
    const auto summary = ev::SummarizeSamples(f, bundle.samples, Status::Ok);
    ev::ValidateBundle(f, bundle.environment, bundle.initialization, bundle.clock,
        bundle.warmup, bundle.samples, summary);
    return {{"environment.json", ev::SerializeEnvironmentJson(f, bundle.environment)},
        {"initialization.csv", ev::SerializeInitializationCsv(f, bundle.initialization)},
        {"host-clock.csv", ev::SerializeHostClockCsv(f, bundle.clock)},
        {"warmup.csv", ev::SerializeWarmupCsv(f, bundle.warmup)},
        {"samples.csv", ev::SerializeSamplesCsv(f, bundle.samples)},
        {"summary.json", ev::SerializeSummaryJson(f, summary, bundle.samples)}};
}
std::string Command(const std::string& command)
{
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("provenance command failed");
    std::string result;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) result += buffer.data();
    if (_pclose(pipe) != 0) throw std::runtime_error("provenance command failed");
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) result.pop_back();
    return result;
}
struct Provenance { std::string source, executable; bool dirty{}; std::optional<std::string> shader; };
Provenance ReadProvenance(const Route& route, const RuntimePaths& paths, std::optional<std::string>* knownSource = nullptr)
{
    const auto root = paths.repository.string();
    Require(root.find_first_of("\"\r\n%!") == root.npos, "unsafe repository path");
    const auto prefix = "git -C \"" + root + "\" ";
    Provenance p;
    p.source = Command(prefix + "rev-parse --verify HEAD 2>NUL");
    if (knownSource && Hex(p.source, 40)) *knownSource = p.source;
    p.dirty = !Command(prefix + "status --porcelain=v1 --untracked-files=all 2>NUL").empty();
    p.executable = Sha256File(paths.executable);
    if (route.process.backend == Stage5Backend::Vulkan)
        p.shader = Sha256File(route.condition.phase == Stage5Phase::A1Sentinel ? paths.a1Shader : paths.d1Shader);
    return p;
}
std::string Utc()
{
    const auto value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{}; std::array<char, 32> buffer{};
    Require(gmtime_s(&utc, &value) == 0 && std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc),
        "UTC acquisition failed");
    return buffer.data();
}
void NativeFailure(Attempt& a, Failure phase, std::string nativePhase,
    std::int64_t code, std::string nativeDetail, bool timeout, bool deviceLost)
{
    // A failed transfer readback can leave transfer completion uncertain even
    // after successful compute t2. Preparation alone has no pending GPU work.
    a.completionUncertain = phase == Failure::Submission || phase == Failure::CompletionWait
        || phase == Failure::Readback || nativePhase == "upload" || nativePhase == "initial upload"
        || nativePhase == "upload completion";
    a.failurePhase = phase; a.nativePhase = std::move(nativePhase);
    a.nativeCode = code; a.nativeDetail = std::move(nativeDetail);
    a.status = Status::Incomplete;
    if (phase == Failure::Submission) { a.status = Status::SubmitFailed; a.errorCode = old::error_code::SubmissionFailed; }
    else if (phase == Failure::CompletionWait) { a.status = Status::WaitFailed; a.errorCode = old::error_code::CompletionFailed; }
    else if (phase == Failure::Readback) a.errorCode = old::error_code::ReadbackFailed;
    else a.errorCode = old::error_code::BackendInitializationFailed;
    if (timeout && (phase == Failure::Submission || phase == Failure::CompletionWait))
    { a.status = Status::Timeout; a.errorCode = old::error_code::OperationTimeout; }
    if (deviceLost) { a.status = Status::DeviceLost; a.errorCode = old::error_code::DeviceLost; }
}

Attempt ObserveCudaA1(cu::Ex2CudaA1Operation& operation, bool repeat)
{
    Attempt a; Failure phase = Failure::BackendInitialization;
    try
    {
        if (repeat) operation.PrepareNextA1WithoutUpload();
        phase = Failure::Submission;
        a.t0 = timing::CaptureHostTime(); operation.SubmitA1(); a.t1 = timing::CaptureHostTime();
        phase = Failure::CompletionWait; operation.WaitForCompletion(); a.t2 = timing::CaptureHostTime(); a.waitSucceeded = true;
        phase = Failure::Readback; a.output = operation.RetrieveOutput();
        a.executed = operation.LastCompletionExecutedKernel();
    }
    catch (const cu::Ex2CudaA1NativeError& e)
    { NativeFailure(a, phase, std::string(cu::ToString(e.Phase())), e.NativeErrorCode(), e.NativeErrorName(),
        e.NativeErrorName() == "cudaErrorLaunchTimeout", false); }
    catch (const std::logic_error&)
    {
        if (phase != Failure::BackendInitialization) throw;
        a.status = Status::Incomplete; a.failurePhase = phase;
        a.errorCode = old::error_code::BackendInitializationFailed;
    }
    return a;
}
Attempt ObserveVulkanA1(vk::Ex2VulkanA1Operation& operation, bool repeat)
{
    Attempt a; Failure phase = Failure::BackendInitialization;
    try
    {
        if (repeat) operation.PrepareNextA1WithoutUpload();
        else operation.Prepare();
        phase = Failure::Submission;
        a.t0 = timing::CaptureHostTime(); operation.SubmitA1(); a.t1 = timing::CaptureHostTime();
        phase = Failure::CompletionWait; operation.WaitForCompletion(); a.t2 = timing::CaptureHostTime(); a.waitSucceeded = true;
        phase = Failure::Readback; a.output = operation.RetrieveOutput();
        a.executed = operation.LastCompletionExecutedShader();
    }
    catch (const vk::Ex2VulkanA1NativeError& e)
    {
        NativeFailure(a, phase, std::string(vk::ToString(e.Phase())), e.NativeResult(), e.Operation(),
            e.NativeResult() == VK_TIMEOUT, e.NativeResult() == VK_ERROR_DEVICE_LOST);
        if (phase == Failure::Submission && vk::detail::ClassifyEx2VulkanA1SubmissionResult(e.NativeResult())
            == vk::detail::Ex2VulkanA1SubmissionDisposition::FailedWithoutSubmission) a.completionUncertain = false;
    }
    catch (const std::logic_error&)
    {
        if (phase != Failure::BackendInitialization) throw;
        a.status = Status::Incomplete; a.failurePhase = phase;
        a.errorCode = old::error_code::BackendInitializationFailed;
    }
    return a;
}
Attempt ObserveCudaD1(cu::Ex2CudaD1Operation& operation, std::span<const std::uint32_t> input)
{
    Attempt a; Failure phase = Failure::BackendInitialization;
    try
    {
        operation.UploadInitialState(input); phase = Failure::Submission;
        a.t0 = timing::CaptureHostTime(); operation.SubmitSequence(); a.t1 = timing::CaptureHostTime();
        phase = Failure::CompletionWait; operation.WaitForCompletion(); a.t2 = timing::CaptureHostTime(); a.waitSucceeded = true;
        phase = Failure::Readback; a.output = operation.RetrieveFinalState();
        a.executed = operation.LastCompletionExecutedSequence();
        a.dispatchCount = operation.LastCompletionNativeDispatchCount(); a.finalBuffer = operation.CompletedFinalBuffer();
    }
    catch (const cu::Ex2CudaD1NativeError& e)
    {
        std::string detail = e.NativeErrorName() + "; accepted_dispatches=" + std::to_string(e.AcceptedDispatchCount());
        if (e.AttemptedPass()) detail += "; attempted_pass=" + std::to_string(*e.AttemptedPass());
        NativeFailure(a, phase, std::string(cu::ToString(e.Phase())), e.NativeErrorCode(), detail,
            e.NativeErrorName() == "cudaErrorLaunchTimeout", false);
    }
    return a;
}
Attempt ObserveVulkanD1(vk::Ex2VulkanD1Operation& operation, std::span<const std::uint32_t> input)
{
    Attempt a; Failure phase = Failure::BackendInitialization;
    try
    {
        operation.UploadInitialState(input); phase = Failure::Submission;
        a.t0 = timing::CaptureHostTime(); operation.SubmitSequence(); a.t1 = timing::CaptureHostTime();
        phase = Failure::CompletionWait; operation.WaitForCompletion(); a.t2 = timing::CaptureHostTime(); a.waitSucceeded = true;
        phase = Failure::Readback; a.output = operation.RetrieveFinalState();
        a.executed = operation.LastCompletionExecutedSequence(); a.dispatchCount = operation.LastCompletionNativeDispatchCount();
        a.barrierCount = operation.LastCompletionInterPassBarrierCount(); a.finalBuffer = operation.CompletedFinalBuffer();
    }
    catch (const vk::Ex2VulkanD1NativeError& e)
    {
        NativeFailure(a, phase, std::string(vk::ToString(e.Phase())), e.NativeResult(), e.Operation(),
            e.NativeResult() == VK_TIMEOUT, e.NativeResult() == VK_ERROR_DEVICE_LOST);
        if (phase == Failure::Submission && e.Phase() == vk::Ex2VulkanD1NativePhase::Submission
            && vk::detail::ClassifyEx2VulkanD1SubmissionResult(e.NativeResult())
                == vk::detail::Ex2VulkanD1SubmissionDisposition::FailedWithoutSubmission) a.completionUncertain = false;
        if (e.Phase() == vk::Ex2VulkanD1NativePhase::CommandBufferReset || e.Phase() == vk::Ex2VulkanD1NativePhase::CommandRecording)
            a.completionUncertain = false;
    }
    return a;
}
template<class Diagnostics> ev::BackendDiagnostics VulkanDiagnostics(const Diagnostics& d, bool iterative)
{
    ev::BackendDiagnostics result;
    result.implementation = iterative ? "ex2-vulkan-d1-native" : "ex2-vulkan-a1-native";
    result.queueFamilyIndex = d.queueFamilyIndex; result.queueFlags = d.queueFamily.queueFlags;
    result.queueCount = d.queueFamily.queueCount; result.uploadMemoryFlags = d.uploadMemoryFlags;
    result.readbackMemoryFlags = d.readbackMemoryFlags;
    return result;
}

// This sink owns records/publication only. Actual native execution above and
// below remains explicit; no backend operation interface is introduced.
class Sink
{
public:
    Sink(Bundle& b, const SessionPaths& paths, FailureRecord& failure, std::span<const std::uint32_t> expected)
        : b_(b), paths_(paths), failure_(failure), expected_(expected) {}
    void Start()
    {
        failure_.category = ExitCode::PublicationFailure; failure_.failurePhase = "evidence_serialization";
        failure_.errorCode = "evidence_serialization_failed";
        WriteFileBytes(paths_.staging / "environment.json", ev::SerializeEnvironmentJson(b_.foundation, b_.environment));
        WriteFileBytes(paths_.staging / "initialization.csv", ev::SerializeInitializationCsv(b_.foundation, b_.initialization));
        WriteFileBytes(paths_.staging / "host-clock.csv", ev::HostClockHeader() + "\r\n");
        WriteFileBytes(paths_.staging / "warmup.csv", ev::WarmupCsvHeader() + "\r\n");
        WriteFileBytes(paths_.staging / "samples.csv", ev::SamplesCsvHeader() + "\r\n");
        std::array<std::optional<timing::HostTimePoint>, 4096> captures;
        for (auto& capture : captures) capture = timing::CaptureHostTime();
        b_.clock = ClockRecords(b_.foundation, captures);
        // Retain raw null/zero deltas. Calibration validity/adequacy belongs to
        // the operational-fact analysis layer, not this execution child.
        for (const auto& r : b_.clock)
            WriteFileBytes(paths_.staging / "host-clock.csv", ev::SerializeHostClockRow(b_.foundation, r), true);
        ev::ValidateHostClockRecords(b_.foundation, b_.clock);
        failure_.category = ExitCode::ExecutionFailure;
        failure_.failurePhase = "backend_execution"; failure_.errorCode = "operation_failed";
    }
    bool Accept(const Attempt& a, std::uint64_t index, bool preparation)
    {
        failure_.category = ExitCode::ExecutionFailure; failure_.failurePhase = "backend_execution";
        failure_.errorCode = "operation_failed"; failure_.attemptIndex = index;
        failure_.preparationAttempt = preparation; failure_.successfulWait = a.waitSucceeded;
        failure_.nativePhase = a.nativePhase; failure_.nativeCode = a.nativeCode; failure_.nativeDetail = a.nativeDetail;
        failure_.completionUncertain = a.completionUncertain;
        const auto mapped = MapAttempt(b_.foundation, index, a, expected_);
        failure_.errorCode = mapped.errorCode;
        failure_.validationPassed = mapped.validationPassed;
        failure_.hostSubmissionNanoseconds = mapped.record ? mapped.record->hostSubmissionNanoseconds : std::nullopt;
        failure_.hostWaitNanoseconds = mapped.record ? mapped.record->hostWaitNanoseconds : std::nullopt;
        failure_.hostCompletionNanoseconds = mapped.record ? mapped.record->hostCompletionNanoseconds : std::nullopt;
        failure_.failurePhase = mapped.errorCode == old::error_code::TimestampInvalid ? "timing"
            : a.failurePhase ? std::string(old::ToString(*a.failurePhase)) : "validation";
        if (!preparation && mapped.record)
        {
            failure_.category = ExitCode::PublicationFailure;
            failure_.failurePhase = "evidence_publication"; failure_.errorCode = "evidence_publication_failed";
            if (b_.foundation.Identity().condition.phase == Stage5Phase::D1Sample)
            {
                ev::SampleRecord row; static_cast<ev::OperationRecord&>(row) = *mapped.record;
                WriteFileBytes(paths_.staging / "samples.csv", ev::SerializeSampleRow(b_.foundation, row), true);
                b_.samples.push_back(row);
            }
            else
            {
                ev::WarmupRecord row; static_cast<ev::OperationRecord&>(row) = *mapped.record;
                WriteFileBytes(paths_.staging / "warmup.csv", ev::SerializeWarmupRow(b_.foundation, row), true);
                b_.warmup.push_back(row);
            }
            failure_.category = ExitCode::ExecutionFailure;
            failure_.failurePhase = mapped.errorCode == old::error_code::TimestampInvalid ? "timing"
                : a.failurePhase ? std::string(old::ToString(*a.failurePhase)) : "validation";
            failure_.errorCode = mapped.errorCode;
        }
        return mapped.successful;
    }
private:
    Bundle& b_; const SessionPaths& paths_; FailureRecord& failure_; std::span<const std::uint32_t> expected_;
};
} // namespace

Configuration ParseArguments(std::span<const std::string_view> args)
{
    Require(args.size() == 16, "exactly eight named options are required");
    Configuration c; std::set<std::string_view> seen;
    for (std::size_t i = 0; i < args.size(); i += 2)
    {
        const auto name = args[i], value = args[i + 1];
        Require(seen.insert(name).second, "duplicate option");
        if (name == "--phase")
        {
            if (value == "a1-sentinel") c.phase = Stage5Phase::A1Sentinel;
            else if (value == "d1-warmup") c.phase = Stage5Phase::D1Warmup;
            else if (value == "d1-sample") c.phase = Stage5Phase::D1Sample;
            else throw std::invalid_argument("unknown phase");
        }
        else if (name == "--plan-index") c.planIndex = Number<std::size_t>(value);
        else if (name == "--selected-w") { if (value != "none") c.selectedW = Number<std::uint64_t>(value); }
        else if (name == "--cuda-device") c.cudaDevice = Number<int>(value);
        else if (name == "--vulkan-device") c.vulkanDevice = Number<std::uint32_t>(value);
        else if (name == "--machine-id") c.machineId = value;
        else if (name == "--session-id") c.sessionId = value;
        else if (name == "--expected-gpu-uuid") c.expectedGpuUuid = value;
        else throw std::invalid_argument("unknown option");
    }
    static_cast<void>(ResolveRoute(c)); return c;
}
Route ResolveRoute(const Configuration& c)
{
    Require(c.planIndex < ChildrenPerGroup && c.cudaDevice >= 0, "invalid plan/device index");
    Require(Anonymous(c.machineId) && Anonymous(c.sessionId), "identifiers must be anonymous path-free tokens");
    Require(CanonicalUuid(c.expectedGpuUuid), "expected GPU UUID must be nonzero canonical lowercase");
    const bool a1 = c.phase == Stage5Phase::A1Sentinel, sample = c.phase == Stage5Phase::D1Sample;
    Require(a1 || sample || c.phase == Stage5Phase::D1Warmup, "invalid phase");
    Require(sample ? c.selectedW && IsCandidateWarmup(*c.selectedW) : !c.selectedW, "phase/W mismatch");
    Condition condition{c.phase, a1 ? Stage5Workload::A1 : Stage5Workload::D1,
        a1 ? A1ElementCount : D1ElementCount, a1 ? std::nullopt : std::optional{D1IterationCount}, "H",
        sample ? 0U : DiagnosticObservationCount, c.selectedW, sample ? SampleObservationCount : 0U};
    Require(ValidateCondition(condition), "invalid frozen condition");
    return {condition, FrozenProcessPlan()[c.planIndex], sample ? *c.selectedW : 0U};
}
std::string FormatUuid(const std::array<std::uint8_t, 16>& uuid)
{
    Require(std::ranges::any_of(uuid, [](auto v) { return v != 0; }), "native GPU UUID is zero");
    constexpr char hex[] = "0123456789abcdef"; std::string result;
    for (std::size_t i = 0; i < uuid.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
        result += hex[uuid[i] >> 4]; result += hex[uuid[i] & 15];
    }
    return result;
}
void RequireExpectedUuid(std::string_view expected, const std::array<std::uint8_t, 16>& actual)
{ Require(CanonicalUuid(expected) && expected == FormatUuid(actual), "selected native GPU UUID differs from expected"); }
void ValidateProvenance(const Route& r, std::string_view source, bool dirty,
    std::string_view executable, const std::optional<std::string>& shader)
{
    Require(Hex(source, 40) && !dirty && Hex(executable, 64), "clean full source/executable provenance required");
    Require(r.process.backend == Stage5Backend::Cuda ? !shader : shader && Hex(*shader, 64), "backend shader provenance mismatch");
}
std::vector<ev::HostClockRecord> ClockRecords(const ev::Foundation& f,
    std::span<const std::optional<timing::HostTimePoint>> captures)
{
    Require(captures.size() == 4096, "host clock requires exactly 4096 captures");
    std::vector<ev::HostClockRecord> rows; rows.reserve(4095);
    for (std::size_t i = 1; i < captures.size(); ++i)
    {
        const auto interval = CalculateHostTimingIntervals(HostTimingStatus::SubmitFailed,
            captures[i - 1], captures[i], std::nullopt);
        rows.push_back(ev::MakeHostClockRecord(f, i - 1, interval.hostSubmissionNanoseconds));
    }
    return rows;
}
MappedAttempt MapAttempt(const ev::Foundation& f, std::uint64_t index,
    const Attempt& a, std::span<const std::uint32_t> expected)
{
    Require(!a.t2 || a.waitSucceeded, "failed wait cannot have t2");
    Require(!a.output || a.waitSucceeded, "readback cannot precede completion");
    std::optional<bool> validation;
    if (a.status == Status::Ok)
    {
        Require(a.waitSucceeded && a.output, "successful observation requires actual completed readback");
        Require(expected.size() == f.Identity().condition.elementCount, "oracle length mismatch");
        bool valid = a.executed && std::ranges::equal(*a.output, expected);
        if (f.Identity().condition.workload == Stage5Workload::D1)
            valid = valid && a.dispatchCount == D1IterationCount && a.finalBuffer == IterativeFinalBuffer::StateA
                && (f.Identity().process.backend != Stage5Backend::Vulkan || a.barrierCount == D1IterationCount - 1);
        validation = valid;
    }
    const auto timing = CalculateHostTimingIntervals(a.waitSucceeded ? HostTimingStatus::Ok : HostTimingStatus::WaitFailed,
        a.t0, a.t1, a.t2);
    if (a.waitSucceeded && timing.status != HostTimingStatus::Ok)
        return {std::nullopt, false, std::string(old::error_code::TimestampInvalid), validation};
    ev::OperationRecord r; r.plan = f.Identity(); r.sequenceIndex = index;
    r.correctness.expectedOutputGenerated = true; r.correctness.operationCompleted = a.waitSucceeded;
    r.status = a.status; r.failurePhase = a.failurePhase; r.errorCode = a.errorCode;
    r.hostSubmissionNanoseconds = timing.hostSubmissionNanoseconds;
    r.hostWaitNanoseconds = timing.hostWaitNanoseconds; r.hostCompletionNanoseconds = timing.hostCompletionNanoseconds;
    if (a.status == Status::Ok)
    {
        r.correctness.outputObserved = true; r.correctness.comparisonPerformed = true; r.correctness.validationPassed = validation;
        if (!*validation) { r.status = Status::ValidationFailed; r.failurePhase = Failure::Validation; r.errorCode = old::error_code::OutputMismatch; }
    }
    ev::ValidateOperationRecord(f, r);
    return {r, r.status == Status::Ok, r.errorCode.value_or(""), validation};
}
SessionPaths MakeSessionPaths(const std::filesystem::path& root, std::string_view session)
{
    Require(Anonymous(session), "invalid session path token");
    const auto local = std::filesystem::absolute(root).lexically_normal();
    const std::string id{session};
    return {local / (id + ".incomplete"), local / id, local / (id + ".failure.json")};
}
void RequireUnused(const SessionPaths& p)
{ Require(!Exists(p.staging) && !Exists(p.final) && !Exists(p.failure), "session path collision"); }
void CreateStaging(const SessionPaths& p)
{
    RequireUnused(p); std::filesystem::create_directories(p.staging.parent_path());
    PlainDirectory(p.staging.parent_path());
    Require(std::filesystem::create_directory(p.staging), "staging creation failed");
    PlainDirectory(p.staging);
}
void WriteCompleteBundle(const SessionPaths& p, const Bundle& bundle)
{
    PlainDirectory(p.staging);
    for (const auto& [name, bytes] : Artifacts(bundle)) WriteFileBytes(p.staging / name, bytes);
}
void VerifyStagedBundle(const SessionPaths& p, const Bundle& bundle)
{
    PlainDirectory(p.staging); const auto expected = Artifacts(bundle);
    std::set<std::string> names;
    for (const auto& file : std::filesystem::directory_iterator(p.staging))
    {
        const auto attrs = GetFileAttributesW(file.path().c_str());
        Require(file.symlink_status().type() == std::filesystem::file_type::regular
            && attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "unexpected package entry");
        const auto name = file.path().filename().string();
        Require(expected.contains(name), "unexpected package file"); names.insert(name);
        Require(ReadFileBytes(file.path()) == expected.at(name), "staged bytes differ from validated bundle");
    }
    Require(names.size() == expected.size(), "missing package file");
}
void FinalizeBundle(const SessionPaths& p, const Bundle& bundle)
{
    Require(!Exists(p.final) && !Exists(p.failure), "final/sidecar collision");
    VerifyStagedBundle(p, bundle);
    if (!MoveFileExW(p.staging.c_str(), p.final.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("package final rename failed");
}
std::string SerializeFailure(const FailureRecord& r)
{
    Require(Anonymous(r.sessionId) && !r.failurePhase.empty() && !r.errorCode.empty(), "invalid control failure");
    Require(r.foundationEstablished ? r.runId && r.seriesId : !r.runId && !r.seriesId, "fabricated foundation identity");
    std::string json = "{\"record_version\":1,\"record_type\":\"ex2-stage5-execution-failure\",\"session_id\":" + Quote(r.sessionId);
    json += ",\"phase\":" + (r.phase ? Quote(PhaseName(*r.phase)) : "null");
    json += ",\"plan_index\":" + (r.planIndex ? std::to_string(*r.planIndex) : "null");
    json += ",\"backend\":" + (r.process ? Quote(r.process->backend == Stage5Backend::Cuda ? "cuda" : "vulkan") : "null");
    json += ",\"process_index\":" + (r.process ? std::to_string(r.process->processIndex) : "null");
    json += ",\"block_index\":" + (r.process ? std::to_string(r.process->blockIndex) : "null");
    json += ",\"order_slot\":" + (r.process ? std::to_string(r.process->orderSlot) : "null");
    json += ",\"exit_category\":" + std::to_string(static_cast<int>(r.category));
    json += ",\"failure_phase\":" + Quote(r.failurePhase) + ",\"error_code\":" + Quote(r.errorCode);
    json += ",\"foundation_established\":" + std::string(r.foundationEstablished ? "true" : "false");
    json += ",\"source_revision\":" + OptionalText(r.sourceRevision) + ",\"run_id\":" + OptionalText(r.runId)
        + ",\"series_id\":" + OptionalText(r.seriesId);
    json += ",\"staging_retained\":" + std::string(r.stagingRetained ? "true" : "false");
    json += ",\"completion_uncertain\":" + std::string(r.completionUncertain ? "true" : "false");
    json += ",\"native_phase\":" + OptionalText(r.nativePhase) + ",\"native_code\":"
        + (r.nativeCode ? std::to_string(*r.nativeCode) : "null") + ",\"native_detail\":" + OptionalText(r.nativeDetail);
    json += ",\"attempt_index\":" + (r.attemptIndex ? std::to_string(*r.attemptIndex) : "null");
    json += ",\"preparation_attempt\":" + std::string(r.preparationAttempt ? "true" : "false");
    json += ",\"successful_wait\":" + (r.successfulWait ? std::string(*r.successfulWait ? "true" : "false") : "null");
    json += ",\"validation_passed\":" + (r.validationPassed ? std::string(*r.validationPassed ? "true" : "false") : "null");
    json += ",\"host_submission_ns\":" + (r.hostSubmissionNanoseconds ? std::to_string(*r.hostSubmissionNanoseconds) : "null");
    json += ",\"host_wait_ns\":" + (r.hostWaitNanoseconds ? std::to_string(*r.hostWaitNanoseconds) : "null");
    json += ",\"host_completion_ns\":" + (r.hostCompletionNanoseconds ? std::to_string(*r.hostCompletionNanoseconds) : "null");
    return json + ",\"detail\":" + Quote(r.detail) + "}\n";
}
void WriteFailure(const SessionPaths& p, const FailureRecord& r)
{ Require(!Exists(p.final), "cannot attach failure to successful package"); WriteFileBytes(p.failure, SerializeFailure(r)); }

bool ExecuteObservedAttempt(control::AttemptObserver observer, const control::AttemptIdentity& identity,
    const std::function<Attempt()>& attempt, const std::function<bool(const Attempt&)>& retain)
{
    observer.Report(identity, control::AttemptEvent::Started);
    if (!retain(attempt())) return false;
    observer.Report(identity, control::AttemptEvent::Returned); return true;
}
ExitCode RunChild(const Configuration& c, const RuntimePaths& paths, control::AttemptObserver observer)
{
    FailureRecord failure; failure.sessionId = c.sessionId;
    failure.failurePhase = "preflight"; failure.errorCode = "preflight_failed";
    std::optional<SessionPaths> session; bool ownsNamespace = false;
    try
    {
        const auto route = ResolveRoute(c); failure.phase = c.phase; failure.planIndex = c.planIndex; failure.process = route.process;
        session = MakeSessionPaths(paths.repository / "results" / "local", c.sessionId);
        RequireUnused(*session); ownsNamespace = true;
        const auto provenance = ReadProvenance(route, paths, &failure.sourceRevision);
        ValidateProvenance(route, provenance.source, provenance.dirty, provenance.executable, provenance.shader);
        const auto input = GenerateWordInput(CoreInputSeed, route.condition.elementCount);
        const auto expected = c.phase == Stage5Phase::A1Sentinel ? ReferenceA1(input) : ReferenceD1(input, D1IterationCount).finalState;
        const auto workload = ev::WorkloadFor(route.condition);
        const auto establish = [&](const auto& uuid, ev::BackendDiagnostics diagnostics) {
            RequireExpectedUuid(c.expectedGpuUuid, uuid);
            auto foundation = ev::MakeWordFoundation({route.condition, route.process, c.sessionId, c.machineId,
                {FormatUuid(uuid), true}, provenance.source, provenance.executable, provenance.shader}, input, expected);
            failure.foundationEstablished = true; failure.runId = foundation.Identity().runId; failure.seriesId = foundation.Identity().seriesId;
            environment::EnvironmentRunContext context{"EX-2", c.sessionId, Utc(), provenance.source, false, c.machineId, false, false};
            auto common = route.process.backend == Stage5Backend::Cuda
                ? environment::CollectCudaEnvironmentRecord(context, static_cast<std::uint32_t>(c.cudaDevice), uuid)
                : environment::CollectVulkanEnvironmentRecord(context, c.vulkanDevice, uuid);
            common.schemaVersion = SchemaVersion;
            auto env = ev::MakeEnvironmentRecord(std::move(common), foundation, std::move(diagnostics));
            auto init = ev::MakeSetupCompleteInitialization(foundation, "native setup and initial upload completed");
            failure.category = ExitCode::PublicationFailure; failure.failurePhase = "staging"; failure.errorCode = "staging_failed";
            CreateStaging(*session);
            return Bundle{std::move(foundation), std::move(env), {std::move(init)}, {}, {}, {}};
        };
        std::optional<Bundle> bundle;
        bool completed = true;
        const auto observed = [&](std::uint64_t index, bool preparation, const std::function<Attempt()>& attempt, Sink& sink) {
            const control::AttemptIdentity identity{c.phase, route.process.backend,
                preparation ? control::AttemptKind::SelectedWarmupPreparation : c.phase == Stage5Phase::D1Sample
                    ? control::AttemptKind::MeasuredObservation : control::AttemptKind::DiagnosticObservation,
                static_cast<std::uint32_t>(c.planIndex), static_cast<std::uint32_t>(index)};
            return ExecuteObservedAttempt(observer, identity, attempt,
                [&](const Attempt& a) { return sink.Accept(a, index, preparation); });
        };
        // Each branch initializes and invokes only the selected native backend.
        if (route.process.backend == Stage5Backend::Cuda && c.phase == Stage5Phase::A1Sentinel)
        {
            cu::Ex2CudaA1Operation operation(c.cudaDevice, std::get<LinearConfiguration>(workload.parameters));
            RequireExpectedUuid(c.expectedGpuUuid, operation.SelectedDeviceUuid()); operation.Upload(input);
            bundle = establish(operation.SelectedDeviceUuid(), {.implementation = "ex2-cuda-a1-native", .streamFlags = "nonblocking"});
            Sink sink(*bundle, *session, failure, expected); sink.Start();
            for (std::uint64_t i = 0; i < DiagnosticObservationCount; ++i)
            {
                BeginA1Observation(failure, i);
                if (!observed(i, false, [&] { return ObserveCudaA1(operation, i != 0); }, sink)) { completed = false; break; }
            }
        }
        else if (route.process.backend == Stage5Backend::Vulkan && c.phase == Stage5Phase::A1Sentinel)
        {
            vk::Ex2VulkanA1Operation operation(std::get<LinearConfiguration>(workload.parameters), paths.a1Shader, c.vulkanDevice);
            RequireExpectedUuid(c.expectedGpuUuid, operation.SelectedDeviceUuid());
            Require(Sha256(std::as_bytes(operation.LoadedSpirv())) == provenance.shader, "loaded A1 shader hash mismatch");
            operation.Upload(input); auto diag = VulkanDiagnostics(operation.Diagnostics(), false);
            diag.inputMemoryFlags = operation.Diagnostics().inputMemoryFlags; diag.outputMemoryFlags = operation.Diagnostics().outputMemoryFlags;
            bundle = establish(operation.SelectedDeviceUuid(), diag);
            Sink sink(*bundle, *session, failure, expected); sink.Start();
            for (std::uint64_t i = 0; i < DiagnosticObservationCount; ++i)
            {
                BeginA1Observation(failure, i);
                if (!observed(i, false, [&] { return ObserveVulkanA1(operation, i != 0); }, sink)) { completed = false; break; }
            }
        }
        else if (route.process.backend == Stage5Backend::Cuda)
        {
            static_assert(!cu::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
            cu::Ex2CudaD1Operation operation(c.cudaDevice, std::get<IterativeConfiguration>(workload.parameters));
            RequireExpectedUuid(c.expectedGpuUuid, operation.SelectedDeviceUuid()); operation.UploadInitialState(input);
            bundle = establish(operation.SelectedDeviceUuid(), {.implementation = "ex2-cuda-d1-native", .streamFlags = "nonblocking"});
            Sink sink(*bundle, *session, failure, expected); sink.Start();
            for (std::uint64_t i = 0; i < route.preparationCount; ++i)
                if (!observed(i, true, [&] { return ObserveCudaD1(operation, input); }, sink)) { completed = false; break; }
            const auto count = route.condition.diagnosticCount + route.condition.measuredSampleCount;
            for (std::uint64_t i = 0; completed && i < count; ++i)
                if (!observed(i, false, [&] { return ObserveCudaD1(operation, input); }, sink)) completed = false;
        }
        else
        {
            static_assert(!vk::Ex2VulkanD1Operation::EnqueuesDeviceTimestamps);
            vk::Ex2VulkanD1Operation operation(std::get<IterativeConfiguration>(workload.parameters), paths.d1Shader, c.vulkanDevice);
            RequireExpectedUuid(c.expectedGpuUuid, operation.SelectedDeviceUuid());
            Require(Sha256(std::as_bytes(operation.LoadedSpirv())) == provenance.shader && operation.LoadedShaderName() == "Ex2D1.comp.spv",
                "loaded D1 shader mismatch");
            const auto& d = operation.Diagnostics();
            Require(!d.validationEnabled && !d.synchronizationValidationEnabled && !d.timestampQueryPoolCreated, "instrumentation forbidden in H");
            operation.UploadInitialState(input); auto diag = VulkanDiagnostics(d, true);
            diag.inputMemoryFlags = d.stateAMemoryFlags; diag.outputMemoryFlags = d.stateBMemoryFlags;
            bundle = establish(operation.SelectedDeviceUuid(), diag);
            Sink sink(*bundle, *session, failure, expected); sink.Start();
            for (std::uint64_t i = 0; i < route.preparationCount; ++i)
                if (!observed(i, true, [&] { return ObserveVulkanD1(operation, input); }, sink)) { completed = false; break; }
            const auto count = route.condition.diagnosticCount + route.condition.measuredSampleCount;
            for (std::uint64_t i = 0; completed && i < count; ++i)
                if (!observed(i, false, [&] { return ObserveVulkanD1(operation, input); }, sink)) completed = false;
        }
        if (!completed) throw std::runtime_error("operation did not complete the frozen process");
        failure.attemptIndex.reset(); failure.successfulWait.reset(); failure.preparationAttempt = false;
        failure.validationPassed.reset(); failure.hostSubmissionNanoseconds.reset();
        failure.hostWaitNanoseconds.reset(); failure.hostCompletionNanoseconds.reset();
        failure.nativePhase.reset(); failure.nativeCode.reset(); failure.nativeDetail.reset();
        failure.category = ExitCode::PublicationFailure; failure.failurePhase = "package_verification"; failure.errorCode = "package_verification_failed";
        const auto artifacts = Artifacts(*bundle);
        WriteFileBytes(session->staging / "summary.json", artifacts.at("summary.json"));
        const auto current = ReadProvenance(route, paths);
        ValidateProvenance(route, current.source, current.dirty, current.executable, current.shader);
        Require(current.source == provenance.source && current.executable == provenance.executable && current.shader == provenance.shader,
            "source/binary/shader provenance drift");
        FinalizeBundle(*session, *bundle); return ExitCode::Completed;
    }
    catch (const cu::Ex2CudaA1NativeError& e)
    { failure.nativePhase = cu::ToString(e.Phase()); failure.nativeCode = e.NativeErrorCode(); failure.nativeDetail = e.NativeErrorName();
        failure.completionUncertain = e.Phase() == cu::Ex2CudaA1NativePhase::UploadCompletion; }
    catch (const cu::Ex2CudaD1NativeError& e)
    { failure.nativePhase = cu::ToString(e.Phase()); failure.nativeCode = e.NativeErrorCode(); failure.nativeDetail = e.NativeErrorName();
        failure.completionUncertain = e.Phase() == cu::Ex2CudaD1NativePhase::UploadCompletion; }
    catch (const vk::Ex2VulkanA1NativeError& e)
    { failure.nativePhase = vk::ToString(e.Phase()); failure.nativeCode = e.NativeResult(); failure.nativeDetail = e.Operation();
        failure.completionUncertain = e.Phase() == vk::Ex2VulkanA1NativePhase::Upload; }
    catch (const vk::Ex2VulkanD1NativeError& e)
    { failure.nativePhase = vk::ToString(e.Phase()); failure.nativeCode = e.NativeResult(); failure.nativeDetail = e.Operation();
        failure.completionUncertain = e.Phase() == vk::Ex2VulkanD1NativePhase::InitialUpload; }
    catch (const std::exception&) { /* Stable context avoids publishing private paths from what(). */ }
    if (failure.detail.empty()) failure.detail = "child stopped; no retry or successful package publication";
    if (session && ownsNamespace)
    {
        failure.stagingRetained = std::filesystem::is_directory(session->staging);
        try { std::filesystem::create_directories(session->failure.parent_path()); WriteFailure(*session, failure); }
        catch (...) { return ExitCode::PublicationFailure; }
    }
    return failure.category;
}
void BeginA1Observation(FailureRecord& failure, std::uint64_t index)
{
    Require(failure.foundationEstablished && failure.phase == Stage5Phase::A1Sentinel
        && index < DiagnosticObservationCount, "A1 observation requires established sentinel foundation");
    failure.category = ExitCode::ExecutionFailure;
    failure.failurePhase = "a1_preparation"; failure.errorCode = "operation_failed";
    failure.detail = "A1 observation stopped during native operation or lifecycle guard; no retry or successful package publication";
    failure.attemptIndex = index; failure.preparationAttempt = false;
    failure.successfulWait.reset(); failure.validationPassed.reset();
    failure.hostSubmissionNanoseconds.reset(); failure.hostWaitNanoseconds.reset();
    failure.hostCompletionNanoseconds.reset(); failure.nativePhase.reset();
    failure.nativeCode.reset(); failure.nativeDetail.reset(); failure.completionUncertain = false;
}

bool RetainAttempt(Bundle& bundle, const SessionPaths& paths, FailureRecord& failure,
    std::span<const std::uint32_t> expected, const Attempt& attempt, std::uint64_t index, bool preparation)
{
    Require(preparation ? bundle.foundation.Identity().condition.phase == Stage5Phase::D1Sample
        && index < *bundle.foundation.Identity().condition.selectedW : true, "preparation outside selected W");
    return Sink(bundle, paths, failure, expected).Accept(attempt, index, preparation);
}
} // namespace computelab::ex2::stage5::execution
