#include "app/Ex2Stage6Execution.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include "ex2/Ex2Sha256.hpp"
#include "cuda/Ex2CudaA1.hpp"
#include "cuda/Ex2CudaA2.hpp"
#include "cuda/Ex2CudaB.hpp"
#include "cuda/Ex2CudaC.hpp"
#include "cuda/Ex2CudaD1.hpp"
#include "cuda/Ex2CudaE.hpp"
#include "vulkan/Ex2VulkanA1.hpp"
#include "vulkan/Ex2VulkanA2.hpp"
#include "vulkan/Ex2VulkanB.hpp"
#include "vulkan/Ex2VulkanC.hpp"
#include "vulkan/Ex2VulkanD1.hpp"
#include "vulkan/Ex2VulkanE.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <limits>
#include <numeric>
#include <ranges>
#include <set>
#include <stdexcept>
#include <variant>

namespace computelab::ex2::stage6::execution
{
namespace old = computelab::ex2::evidence;
namespace cu = computelab::cuda;
namespace vk = computelab::vulkan;
namespace env = computelab::environment;
namespace
{
void Require(bool condition, const char* message)
{ if (!condition) throw std::invalid_argument(message); }
bool Hex(std::string_view s, std::size_t n)
{ return s.size() == n && std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
bool Anonymous(std::string_view s)
{
    std::string upper{s}; for (auto& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    const auto stem = upper.substr(0, upper.find('.'));
    const bool reserved = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
        || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9');
    return IsValidAnonymousIdentifier(s) && !reserved && s.front() != '.' && s.back() != '.' && s.find("..") == s.npos;
}
bool Uuid(std::string_view s)
{
    if (s.size() != 36) return false;
    std::string digits;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else digits += s[i];
    return Hex(digits, 32) && digits != std::string(32, '0');
}
template<class T> T Number(std::string_view s)
{
    std::uint64_t n{}; const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    Require(!s.empty() && r.ec == std::errc{} && r.ptr == s.data() + s.size()
        && n <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()), "invalid unsigned option");
    return static_cast<T>(n);
}
bool Exists(const std::filesystem::path& p)
{ return std::filesystem::symlink_status(p).type() != std::filesystem::file_type::not_found; }
void Plain(const std::filesystem::path& p, bool directory)
{
    const auto a = GetFileAttributesW(p.c_str());
    Require(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT)
        && !!(a & FILE_ATTRIBUTE_DIRECTORY) == directory, "package path is not plain");
}
void PlainAncestors(std::filesystem::path p)
{
    while (!p.empty()) { if (Exists(p)) Plain(p, true); const auto parent = p.parent_path(); if (parent == p) break; p = parent; }
}
std::string ReadBytes(const std::filesystem::path& p)
{
    Plain(p, false); std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("exact reread open failed");
    std::string s{std::istreambuf_iterator<char>(f), {}};
    if (f.bad()) throw std::runtime_error("exact reread failed"); return s;
}
void WriteOnce(const std::filesystem::path& p, std::string_view bytes)
{
    PlainAncestors(p.parent_path());
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f == INVALID_HANDLE_VALUE) throw std::runtime_error("create-once file open failed");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(f, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
    { CloseHandle(f); throw std::runtime_error("file topology invalid"); }
    std::size_t offset{};
    while (offset < bytes.size())
    {
        const DWORD count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD)); DWORD written{};
        if (!WriteFile(f, bytes.data() + offset, count, &written, nullptr) || written != count)
        { CloseHandle(f); throw std::runtime_error("file write failed"); } offset += written;
    }
    const bool flushed = FlushFileBuffers(f) != 0; const bool closed = CloseHandle(f) != 0;
    if (!flushed || !closed) throw std::runtime_error("file flush or close failed");
    if (ReadBytes(p) != bytes) throw std::runtime_error("file exact reread mismatch");
}
std::string Git(const std::filesystem::path& root, const char* args)
{
    const auto path = root.string();
    Require(path.find_first_of("\"\r\n%&|<>^!") == path.npos, "unsafe git repository path");
    const auto command = "git -C \"" + path + "\" " + args;
    FILE* f = _popen(command.c_str(), "r"); if (!f) throw std::runtime_error("git query unavailable");
    std::string s; std::array<char, 1024> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), f)) s += buffer.data();
    if (_pclose(f) != 0) throw std::runtime_error("git query failed");
    return s;
}
std::string Utc()
{
    const std::time_t now = std::time(nullptr); std::tm t{};
    if (gmtime_s(&t, &now)) throw std::runtime_error("UTC unavailable");
    std::array<char, 32> s{}; if (!std::strftime(s.data(), s.size(), "%Y-%m-%dT%H:%M:%SZ", &t)) throw std::runtime_error("UTC unavailable");
    return s.data();
}
std::string ExpectedShaderName(const PlannedSlot& slot)
{
    const auto& w = WorkloadForCell(slot.cellIndex);
    return std::visit([](const auto& c) -> std::string {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>) return c.variant == LinearVariant::A1 ? "Ex2A1.comp.spv" : "Ex2A2.comp.spv";
        else if constexpr (std::is_same_v<T, IndexedConfiguration>) return c.variant == IndexedVariant::B1 ? "Ex2B1.comp.spv" : "Ex2B2.comp.spv";
        else if constexpr (std::is_same_v<T, ContentionConfiguration>) return "Ex2C.comp.spv";
        else if constexpr (std::is_same_v<T, IterativeConfiguration>) return "Ex2D1.comp.spv";
        else return {};
    }, w.parameters);
}
void SafeText(const std::optional<std::string>& s)
{
    if (s) Require(!s->empty() && s->size() <= 512 && s->find_first_of("\\/\r\n") == s->npos
        && s->find('\0') == s->npos, "unsafe sidecar diagnostic");
}
std::string Bounded(std::string_view s)
{
    std::string out;
    for (const char c : s) if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')')
    { if (out.size() < 256) out += c; }
    return out.empty() ? "native failure" : out;
}
struct NativeFailure : std::runtime_error
{
    FailureContext context;
    explicit NativeFailure(FailureContext c) : std::runtime_error("native operation failed"), context(std::move(c)) {}
};
template<class E> FailureContext CudaFailure(const E& e, ev::FailurePhase phase)
{
    return CudaFailureContext(phase, cu::ToString(e.Phase()), e.NativeErrorCode(), e.NativeErrorName());
}
template<class E> FailureContext VulkanFailure(const E& e, ev::FailurePhase phase)
{
    return VulkanFailureContext(phase, vk::ToString(e.Phase()), static_cast<int>(e.NativeResult()), e.Operation());
}
template<class F> decltype(auto) NativeCall(ev::FailurePhase phase, F&& f)
{
    try { return f(); }
#define CUDA_CATCH(T) catch (const cu::T& e) { throw NativeFailure(CudaFailure(e, phase)); }
#define VK_CATCH(T) catch (const vk::T& e) { throw NativeFailure(VulkanFailure(e, phase)); }
    CUDA_CATCH(Ex2CudaA1NativeError) CUDA_CATCH(Ex2CudaA2NativeError) CUDA_CATCH(Ex2CudaBNativeError)
    CUDA_CATCH(Ex2CudaCNativeError) CUDA_CATCH(Ex2CudaD1NativeError) CUDA_CATCH(Ex2CudaENativeError)
    VK_CATCH(Ex2VulkanA1NativeError) VK_CATCH(Ex2VulkanA2NativeError) VK_CATCH(Ex2VulkanBNativeError)
    VK_CATCH(Ex2VulkanCNativeError) VK_CATCH(Ex2VulkanD1NativeError) VK_CATCH(Ex2VulkanENativeError)
#undef CUDA_CATCH
#undef VK_CATCH
}
struct LinearData { std::vector<std::uint32_t> input, expected; };
struct IndexedData { std::vector<std::uint32_t> input, indices, expected; };
struct ContentionData { std::vector<std::uint32_t> targets, zeros, expected; };
struct IterativeData { std::vector<std::uint32_t> input, expected; IterativeFinalBuffer finalBuffer; };
struct TransferData { std::vector<std::uint8_t> source, expected; };
using LogicalData = std::variant<LinearData, IndexedData, ContentionData, IterativeData, TransferData>;
LogicalData Generate(const WorkloadConfiguration& w)
{
    return std::visit([&](const auto& c) -> LogicalData {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
        { auto input = GenerateWordInput(w.common.seed, c.elementCount); auto expected = c.variant == LinearVariant::A1 ? ReferenceA1(input) : ReferenceA2(input); return LinearData{std::move(input), std::move(expected)}; }
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
        { auto input = GenerateWordInput(w.common.seed, c.elementCount); auto indices = c.indexPattern == IndexPattern::StructuredV1 ? GenerateStructuredPermutation(c.elementCount) : GenerateShuffledPermutation(w.common.seed, c.elementCount);
            auto expected = c.variant == IndexedVariant::B1 ? ReferenceB1Gather(input, indices) : ReferenceB2Scatter(input, indices); return IndexedData{std::move(input), std::move(indices), std::move(expected)}; }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        { auto r = ReferenceContention(c.elementCount, c.activeCounterCount);
            return ContentionData{std::move(r.targets), MakeZeroInitialCounterState(c.allocatedCounterCount), std::move(r.counters)}; }
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        { auto input = GenerateWordInput(w.common.seed, c.elementCount); auto r = ReferenceD1(input, c.iterationCount); return IterativeData{std::move(input), std::move(r.finalState), r.finalBuffer}; }
        else { auto r = ReferenceTransfer(w.common.seed, c.byteCount, c.direction); return TransferData{std::move(r.source), std::move(r.expectedDestination)}; }
    }, w.parameters);
}
ev::Foundation Foundation(const ev::FoundationRequest& r, const LogicalData& data)
{
    const auto& w = WorkloadForCell(r.slot.cellIndex);
    return std::visit([&](const auto& d) -> ev::Foundation {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, TransferData>) return ev::MakeByteFoundation(r, old::MakeI7ByteInputIdentity(w, d.source), d.expected);
        else if constexpr (std::is_same_v<T, IndexedData>) return ev::MakeWordFoundation(r, old::MakeI7IndexedInputIdentity(w, d.input, d.indices), d.expected);
        else if constexpr (std::is_same_v<T, ContentionData>) return ev::MakeWordFoundation(r, old::MakeI7ContentionInputIdentity(w, d.targets, d.zeros), d.expected);
        else return ev::MakeWordFoundation(r, old::MakeI7WordInputIdentity(w, d.input), d.expected);
    }, data);
}
struct ChildState
{
    const Configuration& config;
    const RuntimePaths& paths;
    PlannedSlot slot;
    Provenance provenance;
    SessionPaths session;
    AttemptObserver observer;
    std::optional<ev::Foundation> foundation;
    std::optional<ev::EnvironmentRecord> environment;
    std::vector<ev::InitializationRecord> initialization;
    std::vector<ev::SampleRecord> samples;
    std::optional<FailureContext> nativeFailure;
    std::optional<std::uint64_t> currentSample;
    ExitCode exit{ExitCode::Completed};
    bool ownsSession{}, canPackage{}, internalFailure{};
    std::string phase{"configuration"}, code{"configuration_failed"};
};
template<class O> void EstablishEnvironment(ChildState& s, O& o)
{
    const auto uuid = FormatUuid(o.SelectedDeviceUuid()); RequireGpuIdentity(s.config.expectedGpuUuid, s.config.expectedGpuUuid, uuid);
    ev::BackendDiagnostics d;
    auto route = std::visit([](const auto& c) -> std::string {
        if constexpr (requires { c.variant; }) return std::string(ToString(c.variant));
        else return "C";
    }, WorkloadForCell(s.slot.cellIndex).parameters);
    for (auto& c : route) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    d.implementation = "ex2-" + std::string(ToString(s.slot.process.backend)) + "-" + route + "-native";
    bool validationEnabled = false;
    if constexpr (requires { o.Diagnostics(); })
    {
        if constexpr (requires { o.LoadedSpirv(); })
        { if constexpr (requires { o.LoadedShaderName(); }) RequireLoadedShader(*s.foundation, o.LoadedSpirv(), o.LoadedShaderName());
            else RequireLoadedShader(*s.foundation, o.LoadedSpirv()); }
        const auto& n = o.Diagnostics(); d.queueFamilyIndex = n.queueFamilyIndex;
        const char* layers = std::getenv("VK_INSTANCE_LAYERS");
        validationEnabled = layers && std::string_view(layers).find("VK_LAYER_KHRONOS_validation") != std::string_view::npos;
        if constexpr (requires { n.validationEnabled; }) validationEnabled = validationEnabled || n.validationEnabled;
        d.queueFlags = n.queueFamily.queueFlags; d.queueCount = n.queueFamily.queueCount;
        d.uploadMemoryFlags = n.uploadMemoryFlags; d.readbackMemoryFlags = n.readbackMemoryFlags;
        if constexpr (requires { n.inputMemoryFlags; }) { d.inputMemoryFlags = n.inputMemoryFlags; d.outputMemoryFlags = n.outputMemoryFlags; }
        else if constexpr (requires { n.targetMemoryFlags; }) { d.inputMemoryFlags = n.targetMemoryFlags; d.outputMemoryFlags = n.counterMemoryFlags; }
        else if constexpr (requires { n.stateAMemoryFlags; }) { d.inputMemoryFlags = n.stateAMemoryFlags; d.outputMemoryFlags = n.stateBMemoryFlags; }
        else { d.inputMemoryFlags = n.deviceMemoryFlags; d.outputMemoryFlags = n.deviceMemoryFlags; }
        if constexpr (requires { n.timestampQueryPoolCreated; }) Require(!n.timestampQueryPoolCreated && !n.hostQueryResetEnabled, "H native query instrumentation");
    }
    else { d.streamFlags = "nonblocking"; Require(o.SelectedDeviceOrdinal() == s.config.cudaDevice, "native CUDA selector drift"); }
    const env::EnvironmentRunContext context{"EX-2", s.config.sessionId, Utc(), s.provenance.sourceRevision,
        s.provenance.dirty, s.config.machineId, validationEnabled, false};
    auto common = s.slot.process.backend == Backend::Cuda
        ? env::CollectCudaEnvironmentRecord(context, static_cast<std::uint32_t>(s.config.cudaDevice), o.SelectedDeviceUuid())
        : env::CollectVulkanEnvironmentRecord(context, s.config.vulkanDevice, o.SelectedDeviceUuid());
    common.schemaVersion = 2; s.environment = ev::MakeEnvironmentRecord(std::move(common), *s.foundation, std::move(d));
}
template<class Prepare, class Submit, class Wait, class Compare>
void Samples(ChildState& s, Prepare prepare, Submit submit, Wait wait, Compare compare)
{
    s.initialization.push_back(ev::MakeSetupCompleteInitialization(*s.foundation, "resources and immutable sources ready for Started sample 0"));
    for (std::uint64_t j = 0; j < PlannedSampleCount; ++j)
    {
        s.currentSample = j;
        s.phase = "attempt"; s.code = "unrepresentable_attempt";
        RetainObservedAttempt({s.slot.sequenceIndex, j}, s.observer, [&] {
            ev::CorrectnessProgress p{true, false, false, false, std::nullopt};
            std::optional<timing::HostTimePoint> t0, t1, t2;
            std::optional<FailureContext> failure;
            auto stage = ev::FailurePhase::BackendInitialization;
            try
            {
                NativeCall(stage, [&] { prepare(j); });
                stage = ev::FailurePhase::Submission;
                NativeCall(stage, [&] { t0 = timing::CaptureHostTime(); submit(); t1 = timing::CaptureHostTime(); });
                stage = ev::FailurePhase::CompletionWait;
                NativeCall(stage, [&] { wait(); t2 = timing::CaptureHostTime(); }); p.operationCompleted = true;
                stage = ev::FailurePhase::Readback;
                const bool passed = NativeCall(stage, [&] { return compare(); });
                p.outputObserved = true; p.comparisonPerformed = true; p.validationPassed = passed;
            }
            catch (const NativeFailure& e) { failure = e.context; s.nativeFailure = failure;
                s.phase = std::string(old::ToString(e.context.phase)); s.code = "native_operation_failed"; }
            catch (const std::bad_alloc&)
            { failure = FailureContext{stage, false, false, stage == ev::FailurePhase::BackendInitialization,
                stage == ev::FailurePhase::Submission || stage == ev::FailurePhase::CompletionWait}; s.nativeFailure = failure; }
            const auto h = p.operationCompleted ? CalculateHostTimingIntervals(HostTimingStatus::Ok, t0, t1, t2) : HostTimingIntervals{};
            return MapAttempt(*s.foundation, j, p, h, failure);
        }, s.samples);
        if (s.samples.back().status != ev::Status::Ok) break;
    }
    CompleteSampleSequence(s.currentSample, s.nativeFailure);
    s.canPackage = true;
}
template<class O> bool Executed(O& o)
{ if constexpr (requires { o.LastCompletionExecutedKernel(); }) return o.LastCompletionExecutedKernel(); else return o.LastCompletionExecutedShader(); }
template<class O> void LinearRoute(ChildState& s, O& o, const LinearData& d)
{
    EstablishEnvironment(s, o); NativeCall(ev::FailurePhase::BackendInitialization, [&] { o.Upload(d.input); });
    Samples(s, [&](std::uint64_t j) {
        if (j) { if constexpr (requires { o.PrepareNextA2WithoutUpload(); }) o.PrepareNextA2WithoutUpload(); else o.PrepareNextA1WithoutUpload(); }
        else if constexpr (requires { o.Prepare(); }) o.Prepare();
    }, [&] { if constexpr (requires { o.SubmitA2(); }) o.SubmitA2(); else o.SubmitA1(); }, [&] { o.WaitForCompletion(); }, [&] {
        const auto output = o.RetrieveOutput(); const auto input = o.RetrieveDeviceInput();
        return output == d.expected && input == d.input && Executed(o);
    });
}
template<class O> void IndexedRoute(ChildState& s, O& o, const IndexedData& d)
{
    EstablishEnvironment(s, o); NativeCall(ev::FailurePhase::BackendInitialization, [&] { o.Upload(d.input, d.indices); });
    Samples(s, [&](std::uint64_t j) { if (j) o.PrepareNextWithoutUpload(); else if constexpr (requires { o.Prepare(); }) o.Prepare(); },
        [&] { o.Submit(); }, [&] { o.WaitForCompletion(); }, [&] {
            const auto output = o.RetrieveOutput(); const auto input = o.RetrieveDeviceInput(); const auto indices = o.RetrieveDeviceIndices();
            return output == d.expected && input == d.input && indices == d.indices && Executed(o);
        });
}
template<class O> void ContentionRoute(ChildState& s, O& o, const ContentionData& d)
{
    EstablishEnvironment(s, o); NativeCall(ev::FailurePhase::BackendInitialization, [&] { o.UploadTargets(d.targets); });
    const auto& c = std::get<ContentionConfiguration>(WorkloadForCell(s.slot.cellIndex).parameters);
    Samples(s, [&](std::uint64_t) { o.PrepareReset(); }, [&] { o.SubmitAtomic(); }, [&] { o.WaitForCompletion(); }, [&] {
        const auto counters = o.RetrieveCounters(); const auto targets = o.RetrieveDeviceTargets();
        const auto total = std::accumulate(counters.begin(), counters.end(), std::uint64_t{});
        const bool inactive = counters.size() == c.allocatedCounterCount && std::ranges::all_of(counters | std::views::drop(static_cast<std::ptrdiff_t>(c.activeCounterCount)), [](auto n) { return n == 0; });
        return counters == d.expected && targets == d.targets && total == c.elementCount && inactive && Executed(o);
    });
}
template<class O> void IterativeRoute(ChildState& s, O& o, const IterativeData& d)
{
    EstablishEnvironment(s, o); const auto& c = std::get<IterativeConfiguration>(WorkloadForCell(s.slot.cellIndex).parameters);
    Samples(s, [&](std::uint64_t) { o.UploadInitialState(d.input); }, [&] { o.SubmitSequence(); }, [&] { o.WaitForCompletion(); }, [&] {
        const auto output = o.RetrieveFinalState();
        bool passed = output == d.expected && o.CompletedFinalBuffer() == d.finalBuffer && o.ExpectedFinalBuffer() == d.finalBuffer
            && o.LastCompletionNativeDispatchCount() == c.iterationCount && o.LastCompletionExecutedSequence();
        if constexpr (requires { o.LastCompletionInterPassBarrierCount(); }) passed = passed && o.LastCompletionInterPassBarrierCount() == c.iterationCount - 1
            && o.Diagnostics().validationErrorCount == 0 && !o.Diagnostics().timestampQueryPoolCreated;
        return passed;
    });
}
template<class O> void TransferRoute(ChildState& s, O& o, const TransferData& d)
{
    EstablishEnvironment(s, o); const auto& c = std::get<TransferConfiguration>(WorkloadForCell(s.slot.cellIndex).parameters);
    Samples(s, [&](std::uint64_t) { o.PrepareTransfer(d.source); }, [&] { o.SubmitTransfer(); }, [&] { o.WaitForCompletion(); }, [&] {
        const auto output = o.RetrieveDestinationForValidation();
        bool passed = output == d.expected && o.ByteCount() == c.byteCount && o.Direction() == c.direction
            && o.ExpectedNativeCopyCount() == 1 && o.LastCompletionNativeCopyCount() == 1 && o.LastCompletionExecutedCopy();
        if constexpr (requires { o.Diagnostics(); }) passed = passed && o.Diagnostics().preparedCommandRecordCount == 1
            && o.Diagnostics().preparedNativeCopyCount == 1 && o.Diagnostics().validationErrorCount == 0 && !o.Diagnostics().timestampQueryPoolCreated;
        return passed;
    });
}
void Dispatch(ChildState& s, const LogicalData& data)
{
    const auto& w = WorkloadForCell(s.slot.cellIndex); const auto& p = s.paths; const auto& c = s.config;
    NativeCall(ev::FailurePhase::BackendInitialization, [&] {
        std::visit([&](const auto& cfg) {
            using T = std::decay_t<decltype(cfg)>;
            if constexpr (std::is_same_v<T, LinearConfiguration>)
            {
                const auto& d = std::get<LinearData>(data);
                if (s.slot.process.backend == Backend::Cuda)
                { if (cfg.variant == LinearVariant::A1) { cu::Ex2CudaA1Operation o(c.cudaDevice, cfg); LinearRoute(s, o, d); }
                    else { cu::Ex2CudaA2Operation o(c.cudaDevice, cfg); LinearRoute(s, o, d); } }
                else { if (cfg.variant == LinearVariant::A1) { vk::Ex2VulkanA1Operation o(cfg, p.a1Shader, c.vulkanDevice); LinearRoute(s, o, d); }
                    else { vk::Ex2VulkanA2Operation o(cfg, p.a2Shader, c.vulkanDevice); LinearRoute(s, o, d); } }
            }
            else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            { const auto& d = std::get<IndexedData>(data);
                if (s.slot.process.backend == Backend::Cuda) { cu::Ex2CudaBOperation o(c.cudaDevice, cfg, w.common.seed); IndexedRoute(s, o, d); }
                else { vk::Ex2VulkanBOperation o(cfg, w.common.seed, p.b1Shader, p.b2Shader, c.vulkanDevice); IndexedRoute(s, o, d); } }
            else if constexpr (std::is_same_v<T, ContentionConfiguration>)
            { const auto& d = std::get<ContentionData>(data);
                if (s.slot.process.backend == Backend::Cuda) { cu::Ex2CudaCOperation o(c.cudaDevice, cfg); ContentionRoute(s, o, d); }
                else { vk::Ex2VulkanCOperation o(cfg, p.cShader, c.vulkanDevice); ContentionRoute(s, o, d); } }
            else if constexpr (std::is_same_v<T, IterativeConfiguration>)
            { const auto& d = std::get<IterativeData>(data);
                if (s.slot.process.backend == Backend::Cuda) { cu::Ex2CudaD1Operation o(c.cudaDevice, cfg); IterativeRoute(s, o, d); }
                else { vk::Ex2VulkanD1Operation o(cfg, p.d1Shader, c.vulkanDevice); IterativeRoute(s, o, d); } }
            else { const auto& d = std::get<TransferData>(data);
                if (s.slot.process.backend == Backend::Cuda) { cu::Ex2CudaEOperation o(c.cudaDevice, cfg); TransferRoute(s, o, d); }
                else { vk::Ex2VulkanEOperation o(cfg, c.vulkanDevice); TransferRoute(s, o, d); } }
        }, w.parameters);
    });
}
} // namespace

Configuration ParseConfiguration(std::span<const std::string_view> args)
{
    Require(args.size() == 14, "exactly seven option-value pairs required");
    std::map<std::string_view, std::string_view> values;
    const std::set<std::string_view> keys{"--cell-index", "--plan-index", "--cuda-device", "--vulkan-device", "--machine-id", "--session-id", "--expected-gpu-uuid"};
    for (std::size_t i = 0; i < args.size(); i += 2)
        Require(keys.contains(args[i]) && values.emplace(args[i], args[i + 1]).second, "unknown or duplicate option");
    Configuration c{Number<std::size_t>(values.at("--cell-index")), Number<std::size_t>(values.at("--plan-index")),
        Number<int>(values.at("--cuda-device")), Number<std::uint32_t>(values.at("--vulkan-device")),
        std::string(values.at("--machine-id")), std::string(values.at("--session-id")), std::string(values.at("--expected-gpu-uuid"))};
    Require(Anonymous(c.machineId) && Anonymous(c.sessionId) && Uuid(c.expectedGpuUuid), "invalid anonymous identity or UUID");
    (void)ResolveSlot(c); return c;
}
PlannedSlot ResolveSlot(const Configuration& c)
{
    Require(c.cellIndex < CoreCellCount && c.planIndex < ChildrenPerCell && c.cudaDevice >= 0, "invalid child coordinates");
    const auto slot = FrozenCampaignPlan()[c.cellIndex * ChildrenPerCell + c.planIndex]; ValidatePlannedSlot(slot);
    Require(slot.cellIndex == c.cellIndex && slot.process == FrozenCellProcessPlan()[c.planIndex], "frozen plan mismatch"); return slot;
}
std::optional<std::filesystem::path> ApplicableShader(const PlannedSlot& slot, const RuntimePaths& p)
{
    ValidatePlannedSlot(slot); if (slot.process.backend == Backend::Cuda) return {};
    return std::visit([&](const auto& c) -> std::optional<std::filesystem::path> {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>) return c.variant == LinearVariant::A1 ? p.a1Shader : p.a2Shader;
        else if constexpr (std::is_same_v<T, IndexedConfiguration>) return c.variant == IndexedVariant::B1 ? p.b1Shader : p.b2Shader;
        else if constexpr (std::is_same_v<T, ContentionConfiguration>) return p.cShader;
        else if constexpr (std::is_same_v<T, IterativeConfiguration>) return p.d1Shader;
        else return {};
    }, WorkloadForCell(slot.cellIndex).parameters);
}
Provenance ResolveProvenance(const RuntimePaths& p, const PlannedSlot& slot)
{
    auto revision = Git(p.repositoryRoot, "rev-parse HEAD"); while (!revision.empty() && (revision.back() == '\n' || revision.back() == '\r')) revision.pop_back();
    Require(Hex(revision, 40), "source revision unavailable");
    const bool dirty = !Git(p.repositoryRoot, "status --porcelain=v1 --untracked-files=all").empty();
    auto shader = ApplicableShader(slot, p);
    return {std::move(revision), dirty, Sha256File(p.executable), shader ? std::optional<std::string>{Sha256File(*shader)} : std::nullopt};
}
void RequireSameProvenance(const Provenance& a, const Provenance& b)
{ Require(a == b, "terminal provenance drift"); }
std::string FormatUuid(const std::array<std::uint8_t, 16>& uuid)
{
    constexpr char hex[] = "0123456789abcdef"; std::string s;
    for (std::size_t i = 0; i < uuid.size(); ++i) { if (i == 4 || i == 6 || i == 8 || i == 10) s += '-'; s += hex[uuid[i] >> 4]; s += hex[uuid[i] & 15]; }
    Require(Uuid(s), "native GPU UUID unavailable"); return s;
}
void RequireGpuIdentity(std::string_view expected, std::string_view preflight, std::string_view native)
{ Require(Uuid(expected) && expected == preflight && expected == native, "selected GPU identity mismatch"); }
void RequireLoadedShader(const ev::Foundation& f, std::span<const std::uint32_t> bytes, std::optional<std::string_view> name)
{
    const auto& hash = f.Identity().seriesIdentity.shaderSha256;
    Require(hash && !bytes.empty() && Sha256(std::as_bytes(bytes)) == *hash, "loaded SPIR-V provenance mismatch");
    if (name) Require(*name == ExpectedShaderName(f.Identity().slot), "loaded shader name mismatch");
}
void AttemptObserver::Report(const AttemptIdentity& i, AttemptEvent e) const noexcept
{ if (callback) callback(context, i, e); }
ExitCode DominantExit(ExitCode a, ExitCode b) noexcept
{
    const auto rank = [](ExitCode c) { switch(c) { case ExitCode::NativeStateUncertain: return 4; case ExitCode::PublicationFailure: return 3; case ExitCode::InternalFailure: return 2; case ExitCode::PreFoundationFailure: return 1; default: return 0; } };
    return rank(a) >= rank(b) ? a : b;
}
ev::SampleRecord MapAttempt(const ev::Foundation& f, std::uint64_t index, const ev::CorrectnessProgress& progress,
    const HostTimingIntervals& h, const std::optional<FailureContext>& failure)
{
    auto r = ev::MakeSampleRecord(f, index); r.correctness = progress;
    if (progress.operationCompleted)
    {
        Require(h.status == HostTimingStatus::Ok && h.hostSubmissionNanoseconds && h.hostWaitNanoseconds && h.hostCompletionNanoseconds, "known completion has invalid H captures");
        r.hostSubmissionNanoseconds = h.hostSubmissionNanoseconds; r.hostWaitNanoseconds = h.hostWaitNanoseconds; r.hostCompletionNanoseconds = h.hostCompletionNanoseconds;
    }
    if (!failure)
    {
        Require(progress.comparisonPerformed && progress.validationPassed.has_value(), "attempt lacks a complete comparison");
        r.status = *progress.validationPassed ? ev::Status::Ok : ev::Status::ValidationFailed;
        if (r.status != ev::Status::Ok) { r.failurePhase = ev::FailurePhase::Validation; r.errorCode = "output_mismatch"; }
    }
    else
    {
        const auto& c = *failure;
        Require(c.phase == ev::FailurePhase::BackendInitialization || c.phase == ev::FailurePhase::ResourceAllocation
            || c.phase == ev::FailurePhase::Submission || c.phase == ev::FailurePhase::CompletionWait
            || c.phase == ev::FailurePhase::Readback, "non-native failure cannot become a scientific attempt");
        r.failurePhase = c.phase;
        if (c.deviceLost) { r.status = ev::Status::DeviceLost; r.errorCode = "device_lost"; }
        else if (c.timeout && (c.phase == ev::FailurePhase::Submission || c.phase == ev::FailurePhase::CompletionWait)) { r.status = ev::Status::Timeout; r.errorCode = "operation_timeout"; }
        else if (c.phase == ev::FailurePhase::Submission) { r.status = ev::Status::SubmitFailed; r.errorCode = "submission_failed"; }
        else if (c.phase == ev::FailurePhase::CompletionWait) { r.status = ev::Status::WaitFailed; r.errorCode = "completion_failed"; }
        else if (c.phase == ev::FailurePhase::Readback) { r.status = ev::Status::Incomplete; r.errorCode = "readback_failed"; }
        else { r.status = ev::Status::Incomplete; r.failurePhase = c.allocation ? ev::FailurePhase::ResourceAllocation : ev::FailurePhase::BackendInitialization;
            r.errorCode = c.allocation ? "resource_allocation_failed" : "backend_initialization_failed"; }
    }
    ev::ValidateSampleRecord(r); return r;
}
void RetainObservedAttempt(const AttemptIdentity& i, const AttemptObserver& observer,
    const std::function<ev::SampleRecord()>& run, std::vector<ev::SampleRecord>& rows)
{
    Require(i.sampleIndex == rows.size() && i.sampleIndex < PlannedSampleCount, "attempt sequence drift");
    Require(rows.empty() || rows.back().status == ev::Status::Ok, "attempt after terminal failure");
    observer.Report(i, AttemptEvent::Started); auto row = run(); ev::ValidateSampleRecord(row);
    Require(row.plan.slot.sequenceIndex == i.slotSequenceIndex && row.sampleIndex == i.sampleIndex, "attempt row identity drift");
    rows.push_back(std::move(row)); observer.Report(i, AttemptEvent::Returned);
}
void CompleteSampleSequence(std::optional<std::uint64_t>& currentSample,
    const std::optional<FailureContext>& retainedNativeFailure) noexcept
{
    if (!retainedNativeFailure) currentSample.reset();
}
SessionPaths MakeSessionPaths(const RuntimePaths& p, std::string_view session)
{
    Require(p.repositoryRoot.is_absolute() && Anonymous(session), "invalid owned session path");
    const auto root = p.repositoryRoot / "results" / "local";
    return {root / std::string(session), root / (std::string(session) + ".incomplete"), root / (std::string(session) + ".failure.json")};
}
void RequireUnusedSession(const SessionPaths& p)
{
    PlainAncestors(p.finalDirectory.parent_path());
    Require(!Exists(p.finalDirectory) && !Exists(p.stagingDirectory) && !Exists(p.failureSidecar), "session collision");
}
void CreateStaging(const SessionPaths& p)
{
    RequireUnusedSession(p); std::filesystem::create_directories(p.stagingDirectory.parent_path()); PlainAncestors(p.stagingDirectory.parent_path());
    if (!CreateDirectoryW(p.stagingDirectory.c_str(), nullptr)) throw std::runtime_error("staging create failed"); Plain(p.stagingDirectory, true);
}
Artifacts SerializePackage(const ev::Foundation& f, const ev::EnvironmentRecord& e, std::span<const ev::InitializationRecord> init,
    std::span<const ev::SampleRecord> rows, const ev::SummaryRecord& summary)
{
    ev::ValidateBundle(f, e, init, rows, summary);
    return {{"environment.json", ev::SerializeEnvironmentJson(f, e)}, {"initialization.csv", ev::SerializeInitializationCsv(f.Identity(), init)},
        {"samples.csv", ev::SerializeSamplesCsv(f.Identity(), rows)}, {"summary.json", ev::SerializeSummaryJson(summary, rows)}};
}
void WriteStagedPackage(const SessionPaths& p, const Artifacts& a)
{
    Plain(p.stagingDirectory, true); Require(std::filesystem::is_empty(p.stagingDirectory), "staging must be empty");
    const std::set<std::string> expected{"environment.json", "initialization.csv", "samples.csv", "summary.json"}; std::set<std::string> keys;
    for (const auto& [name, bytes] : a) { keys.insert(name); Require(expected.contains(name) && !bytes.empty(), "invalid package artifact"); }
    Require(keys == expected, "four artifacts required"); for (const auto& [name, bytes] : a) WriteOnce(p.stagingDirectory / name, bytes);
}
void VerifyStagedPackage(const SessionPaths& p, const Artifacts& a)
{
    PlainAncestors(p.stagingDirectory); std::set<std::string> actual;
    for (const auto& file : std::filesystem::directory_iterator(p.stagingDirectory))
    { Plain(file.path(), false); actual.insert(file.path().filename().string()); }
    const std::set<std::string> expected{"environment.json", "initialization.csv", "samples.csv", "summary.json"};
    Require(actual == expected && a.size() == 4, "staging exact file set mismatch");
    for (const auto& name : expected) Require(a.contains(name) && ReadBytes(p.stagingDirectory / name) == a.at(name), "staging exact bytes mismatch");
}
void FinalizePackage(const SessionPaths& p)
{
    PlainAncestors(p.stagingDirectory); Require(!Exists(p.finalDirectory), "final destination occupied");
    if (!MoveFileExW(p.stagingDirectory.c_str(), p.finalDirectory.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("final rename failed");
}
std::string SerializeFailureSidecar(const FailureSidecar& f)
{
    using ev::detail::Json; using ev::detail::Field;
    Require(Anonymous(f.sessionId) && IsValidAnonymousIdentifier(f.failurePhase) && IsValidAnonymousIdentifier(f.errorCode), "invalid sidecar control identity");
    Require(f.foundationEstablished == (f.runId && f.seriesId && f.sourceRevision), "sidecar Foundation facts disagree");
    if (f.foundationEstablished) Require(*f.runId == f.sessionId && Hex(*f.seriesId, 64) && Hex(*f.sourceRevision, 40), "sidecar Foundation identity invalid");
    if (f.slotSequenceIndex) Require(*f.slotSequenceIndex < TotalChildCount, "sidecar slot invalid");
    if (f.sampleIndex) Require(*f.sampleIndex < PlannedSampleCount, "sidecar sample invalid");
    SafeText(f.nativePhase); SafeText(f.nativeCode); SafeText(f.nativeDetail);
    std::string s = "{\"record_version\":1,\"record_type\":\"ex2-stage6-child-failure\",\"session_id\":" + Json(f.sessionId);
    Field(s, "slot_sequence_index", f.slotSequenceIndex); Field(s, "foundation_established", f.foundationEstablished);
    Field(s, "run_id", f.runId); Field(s, "series_id", f.seriesId); Field(s, "source_revision", f.sourceRevision);
    Field(s, "exit_category", static_cast<std::uint64_t>(f.exitCategory)); Field(s, "failure_phase", f.failurePhase); Field(s, "error_code", f.errorCode);
    Field(s, "sample_index", f.sampleIndex); Field(s, "completion_uncertain", f.completionUncertain);
    Field(s, "native_phase", f.nativePhase); Field(s, "native_code", f.nativeCode); Field(s, "native_detail", f.nativeDetail); return s + "}\n";
}
void WriteFailureSidecar(const SessionPaths& p, const FailureSidecar& f)
{ WriteOnce(p.failureSidecar, SerializeFailureSidecar(f)); }
ExitCode PublishPreparedPackage(const SessionPaths& p, const Artifacts& a, FailureSidecar failure,
    bool required, ExitCode exit, const std::function<void()>& provenance)
{
    if (required)
    {
        failure.exitCategory = exit;
        try { WriteFailureSidecar(p, failure); }
        catch (...) { exit = DominantExit(exit, ExitCode::PublicationFailure); }
    }
    bool checkingProvenance = false;
    try
    {
        WriteStagedPackage(p, a); VerifyStagedPackage(p, a);
        checkingProvenance = true; provenance(); checkingProvenance = false;
        FinalizePackage(p);
    }
    catch (...)
    {
        exit = DominantExit(exit, checkingProvenance ? ExitCode::InternalFailure : ExitCode::PublicationFailure);
        if (!required)
        {
            failure.exitCategory = exit; failure.failurePhase = checkingProvenance ? "terminal-provenance" : "publication";
            failure.errorCode = checkingProvenance ? "terminal_provenance_drift" : "evidence_publication_failed";
            try { WriteFailureSidecar(p, failure); }
            catch (...) { exit = DominantExit(exit, ExitCode::PublicationFailure); }
        }
    }
    return exit;
}
FailureContext CudaFailureContext(ev::FailurePhase phase, std::string_view p, int code, std::string_view name)
{
    // CUDA's inherited scientific failure vocabulary is phase-specific. Sticky
    // execution errors imply unsafe native state, without inventing device_lost.
    const bool sticky = name == "cudaErrorIllegalAddress" || name == "cudaErrorLaunchFailure"
        || name == "cudaErrorAssert" || name == "cudaErrorContextIsDestroyed" || name == "cudaErrorECCUncorrectable";
    const bool timeout = name == "cudaErrorLaunchTimeout";
    const bool uncertain = sticky || timeout || phase == ev::FailurePhase::Submission || phase == ev::FailurePhase::CompletionWait
        || phase == ev::FailurePhase::Readback || p.find("completion") != p.npos || p.find("upload") != p.npos
        || p.find("reset") != p.npos || p.find("preparation") != p.npos || p == "output initialization";
    return {phase, timeout, false, name == "cudaErrorMemoryAllocation", uncertain, Bounded(p), std::to_string(code), Bounded(name)};
}
FailureContext VulkanFailureContext(ev::FailurePhase phase, std::string_view p, int code, std::string_view operation)
{
    const bool allocation = code == VK_ERROR_OUT_OF_DEVICE_MEMORY || code == VK_ERROR_OUT_OF_HOST_MEMORY;
    const bool uncertain = code == VK_ERROR_DEVICE_LOST || code == VK_TIMEOUT || operation.find("vkWaitForFences") != operation.npos
        || phase == ev::FailurePhase::CompletionWait || p.find("completion") != p.npos
        || (operation.find("vkQueueSubmit") != operation.npos && !allocation);
    return {phase, code == VK_TIMEOUT, code == VK_ERROR_DEVICE_LOST, allocation, uncertain,
        Bounded(p), std::to_string(code), Bounded(operation)};
}
ExitCode RunChild(const Configuration& c, const RuntimePaths& p, AttemptObserver observer)
{
    ChildState s{c, p, {}, {}, {}, observer};
    try
    {
        s.slot = ResolveSlot(c); Require(Anonymous(c.machineId) && Anonymous(c.sessionId) && Uuid(c.expectedGpuUuid), "invalid child identity");
        s.session = MakeSessionPaths(p, c.sessionId); RequireUnusedSession(s.session);
        s.phase = "provenance"; s.code = "provenance_failed"; s.provenance = ResolveProvenance(p, s.slot);
        s.phase = "gpu-preflight"; s.code = "gpu_identity_failed";
        if (s.slot.process.backend == Backend::Cuda)
        { const auto devices = env::EnumerateCudaDeviceMetadata(); Require(static_cast<std::size_t>(c.cudaDevice) < devices.size(), "CUDA selector unavailable");
            const auto id = FormatUuid(devices[c.cudaDevice].uuid); RequireGpuIdentity(c.expectedGpuUuid, id, id); }
        else { const auto devices = env::EnumerateVulkanDeviceMetadata(); Require(c.vulkanDevice < devices.size(), "Vulkan selector unavailable");
            const auto id = FormatUuid(devices[c.vulkanDevice].uuid); RequireGpuIdentity(c.expectedGpuUuid, id, id); }
        s.phase = "foundation"; s.code = "foundation_failed";
        const auto data = Generate(WorkloadForCell(s.slot.cellIndex));
        s.foundation = Foundation({s.slot, c.sessionId, c.machineId, {c.expectedGpuUuid, true}, s.provenance.sourceRevision,
            s.provenance.executableSha256, s.provenance.shaderSha256}, data);
        s.phase = "staging"; s.code = "staging_creation_failed";
        try { CreateStaging(s.session); s.ownsSession = true; }
        catch (...) { s.exit = ExitCode::PublicationFailure; throw; }
        s.phase = "backend-setup"; s.code = "backend_initialization_failed"; Dispatch(s, data);
    }
    catch (const NativeFailure& e)
    {
        s.nativeFailure = e.context;
        if (s.environment && s.samples.empty())
        { auto row = ev::MakeSetupCompleteInitialization(*s.foundation, "native immutable-source setup failed after environment collection"); row.metric = "initialization_failed"; s.initialization.push_back(std::move(row)); s.canPackage = true; }
        else s.exit = s.foundation ? ExitCode::InternalFailure : ExitCode::PreFoundationFailure;
    }
    catch (const std::bad_alloc&)
    {
        if (s.environment && !s.currentSample)
        {
            s.nativeFailure = FailureContext{ev::FailurePhase::ResourceAllocation, false, false, true, false};
            auto row = ev::MakeSetupCompleteInitialization(*s.foundation, "immutable-source setup allocation failed after environment collection");
            row.metric = "initialization_failed"; s.initialization.push_back(std::move(row)); s.canPackage = true;
        }
        else { s.internalFailure = true; s.exit = DominantExit(s.exit, s.foundation ? ExitCode::InternalFailure : ExitCode::PreFoundationFailure); }
    }
    catch (...)
    {
        s.internalFailure = true;
        s.exit = DominantExit(s.exit, s.foundation ? ExitCode::InternalFailure : ExitCode::PreFoundationFailure);
    }
    if (s.nativeFailure && (s.nativeFailure->completionUncertain || s.nativeFailure->deviceLost)) s.exit = ExitCode::NativeStateUncertain;
    std::optional<Artifacts> artifacts;
    if (s.canPackage && !s.internalFailure)
    {
        std::optional<ev::SummaryRecord> summary;
        try
        {
            auto status = ev::Status::Ok; std::optional<ev::FailurePhase> phase; std::optional<std::string> code;
            if (!s.samples.empty()) { status = s.samples.back().status; phase = s.samples.back().failurePhase; code = s.samples.back().errorCode; }
            else { const auto& f = *s.nativeFailure; status = f.deviceLost ? ev::Status::DeviceLost : ev::Status::Incomplete;
                phase = f.allocation && !f.deviceLost ? ev::FailurePhase::ResourceAllocation : ev::FailurePhase::BackendInitialization;
                code = f.deviceLost ? "device_lost" : f.allocation ? "resource_allocation_failed" : "backend_initialization_failed"; }
            summary = ev::SummarizeSamples(s.foundation->Identity(), s.samples, status, phase, code);
            ev::ValidateBundle(*s.foundation, *s.environment, s.initialization, s.samples, *summary);
        }
        catch (...) { summary.reset(); s.exit = DominantExit(s.exit, ExitCode::InternalFailure); s.phase = "bundle-validation"; s.code = "unrepresentable_bundle"; }
        if (summary)
        {
            try { artifacts = SerializePackage(*s.foundation, *s.environment, s.initialization, s.samples, *summary); }
            catch (...) { s.exit = DominantExit(s.exit, ExitCode::PublicationFailure); s.phase = "serialization"; s.code = "evidence_serialization_failed"; }
        }
    }
    const auto sidecar = [&] {
        FailureSidecar f{c.sessionId, s.slot.sequenceIndex, s.foundation.has_value()};
        if (s.foundation) { f.runId = s.foundation->Identity().runId; f.seriesId = s.foundation->Identity().seriesId; f.sourceRevision = s.provenance.sourceRevision; }
        f.exitCategory = s.exit; f.failurePhase = s.phase; f.errorCode = s.code; f.sampleIndex = s.currentSample;
        if (s.nativeFailure) { f.completionUncertain = s.nativeFailure->completionUncertain || s.nativeFailure->deviceLost;
            f.nativePhase = s.nativeFailure->nativePhase; f.nativeCode = s.nativeFailure->nativeCode; f.nativeDetail = s.nativeFailure->nativeDetail; }
        return f;
    };
    if (artifacts)
        return PublishPreparedPackage(s.session, *artifacts, sidecar(), s.nativeFailure.has_value()
            || s.internalFailure || s.exit != ExitCode::Completed, s.exit,
            [&] { RequireSameProvenance(s.provenance, ResolveProvenance(p, s.slot)); });
    if (s.ownsSession && (s.nativeFailure || s.internalFailure || s.exit != ExitCode::Completed))
    {
        try { WriteFailureSidecar(s.session, sidecar()); }
        catch (...) { s.exit = DominantExit(s.exit, ExitCode::PublicationFailure); }
    }
    return s.exit;
}
} // namespace computelab::ex2::stage6::execution
