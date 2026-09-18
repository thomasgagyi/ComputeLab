#include "cuda/CudaQualification.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2Gate0.hpp"
#include "ex2/Ex2Gate0Supervisor.hpp"
#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"
#include "vulkan/VulkanQualification.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;
namespace supervisor = computelab::ex2::gate0::supervisor;
namespace cuda = computelab::cuda;
namespace vk = computelab::vulkan;

struct OperationObservation
{
    std::string status;
    std::optional<bool> validationPassed;
    std::optional<std::string> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
    gate0::ExitCode exitCode{gate0::ExitCode::Success};
    bool safeForFurtherGpuCalls{true};
};

struct ExecutionState
{
    std::vector<gate0::SampleRecord> samples;
    std::string processStatus{"ok"};
    std::optional<std::string> failurePhase;
    std::optional<std::string> errorCode;
    gate0::ExitCode exitCode{gate0::ExitCode::Success};
    bool safeForFurtherGpuCalls{true};
};

struct GitState
{
    std::string revision;
    bool dirty{};
    std::string workingTreeState;
};

class NativeExecutionException final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

template <typename Function>
decltype(auto) NativeCall(Function&& function)
{
    try
    {
        return std::forward<Function>(function)();
    }
    catch (const std::exception& error)
    {
        throw NativeExecutionException(error.what());
    }
}

std::filesystem::path RunningExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0U)
            throw std::runtime_error(
                "unable to resolve the running executable for provenance");
        if (length < buffer.size())
            return std::filesystem::path(
                buffer.data(), buffer.data() + length);
        if (buffer.size() >= 32768U)
            throw std::runtime_error(
                "running executable path exceeds the supported Windows path length");
        buffer.resize(std::min<std::size_t>(buffer.size() * 2U, 32768U));
    }
}

std::string Trim(std::string value)
{
    while (!value.empty()
        && (value.back() == '\r' || value.back() == '\n'
            || value.back() == ' ' || value.back() == '\t'))
    {
        value.pop_back();
    }
    return value;
}

std::string CaptureCommand(const std::string& command)
{
    std::unique_ptr<FILE, decltype(&_pclose)> pipe{
        _popen(command.c_str(), "r"), &_pclose};
    if (!pipe) throw std::runtime_error("required provenance command could not start");
    std::array<char, 4096> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()))
    {
        output += buffer.data();
    }
    const int result = _pclose(pipe.release());
    if (result != 0)
        throw std::runtime_error("required provenance command failed");
    return Trim(std::move(output));
}

GitState ReadGitState()
{
    const std::string root = COMPUTELAB_REPOSITORY_ROOT;
    const std::string prefix = "git -C \"" + root + "\" ";
    const std::string revision = CaptureCommand(
        prefix + "rev-parse --verify HEAD 2>NUL");
    if (revision.size() != 40U
        || !std::all_of(revision.begin(), revision.end(), [](char value) {
            return (value >= '0' && value <= '9')
                || (value >= 'a' && value <= 'f');
        }))
    {
        throw std::runtime_error("source Git revision is not a full SHA-1 commit identity");
    }
    const std::string workingTreeState = CaptureCommand(
        prefix + "status --porcelain=v1 --untracked-files=all 2>NUL");
    return {revision, !workingTreeState.empty(), workingTreeState};
}

std::string TimestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    if (gmtime_s(&utc, &time) != 0)
        throw std::runtime_error("UTC timestamp acquisition failed");
    std::array<char, 32> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U)
        throw std::runtime_error("UTC timestamp formatting failed");
    return buffer.data();
}

std::string FormatUuid(const std::array<std::uint8_t, 16>& uuid)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(36U);
    for (std::size_t index = 0; index < uuid.size(); ++index)
    {
        if (index == 4U || index == 6U || index == 8U || index == 10U)
            output.push_back('-');
        output.push_back(hex[uuid[index] >> 4U]);
        output.push_back(hex[uuid[index] & 0x0FU]);
    }
    return output;
}

std::string FailurePhase(cuda::CudaQualificationFailurePhase value)
{
    using Phase = cuda::CudaQualificationFailurePhase;
    switch (value)
    {
    case Phase::None: return {};
    case Phase::Submission: return "submission";
    case Phase::CompletionWait: return "completion_wait";
    case Phase::Readback: return "readback";
    case Phase::Validation: return "validation";
    case Phase::HostTiming: return "host_timing";
    case Phase::StartMarker: return "start_marker";
    case Phase::StopMarker: return "stop_marker";
    case Phase::NativeTimingRetrieval: return "native_timing_retrieval";
    case Phase::NativeTimingConversion: return "native_timing_conversion";
    }
    return "unknown";
}

std::string FailurePhase(vk::VulkanQualificationFailurePhase value)
{
    using Phase = vk::VulkanQualificationFailurePhase;
    switch (value)
    {
    case Phase::None: return {};
    case Phase::Submission: return "submission";
    case Phase::CompletionWait: return "completion_wait";
    case Phase::Readback: return "readback";
    case Phase::Validation: return "validation";
    case Phase::HostTiming: return "host_timing";
    case Phase::NativeTiming: return "native_timing";
    }
    return "unknown";
}

OperationObservation Observe(const cuda::CudaQualificationExecution& execution)
{
    using Status = cuda::CudaQualificationStatus;
    OperationObservation result;
    result.hostSubmissionNanoseconds = execution.hostTiming.hostSubmissionNanoseconds;
    result.hostWaitNanoseconds = execution.hostTiming.hostWaitNanoseconds;
    result.hostCompletionNanoseconds = execution.hostTiming.hostCompletionNanoseconds;
    result.nativeDeviceIntervalNanoseconds = execution.nativeDeviceIntervalNanoseconds;
    const std::string phase = FailurePhase(execution.failurePhase);
    if (!phase.empty()) result.failurePhase = phase;
    if (execution.nativeErrorCode.has_value())
    {
        result.errorCode = std::to_string(*execution.nativeErrorCode);
        if (!execution.nativeErrorName.empty())
            *result.errorCode += ":" + execution.nativeErrorName;
    }
    switch (execution.status)
    {
    case Status::Ok:
        result.status = "ok";
        result.validationPassed = true;
        break;
    case Status::ValidationFailed:
        result.status = "validation_failed";
        result.validationPassed = false;
        result.exitCode = gate0::ExitCode::CorrectnessFailure;
        break;
    case Status::SubmitFailed:
        result.status = execution.nativeErrorName == "cudaErrorDeviceLost"
            ? "device_lost" : "submit_failed";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::WaitFailed:
        result.status = execution.nativeErrorName == "cudaErrorDeviceLost"
            ? "device_lost" : "wait_failed";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::ReadbackFailed:
        result.status = execution.nativeErrorName == "cudaErrorDeviceLost"
            ? "device_lost" : "incomplete";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::TimingInvalid:
        result.status = "timestamp_invalid";
        if (execution.validationPassed) result.validationPassed = true;
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        break;
    }
    return result;
}

OperationObservation Observe(const vk::VulkanQualificationExecution& execution)
{
    using Status = vk::VulkanQualificationStatus;
    OperationObservation result;
    result.hostSubmissionNanoseconds = execution.hostTiming.hostSubmissionNanoseconds;
    result.hostWaitNanoseconds = execution.hostTiming.hostWaitNanoseconds;
    result.hostCompletionNanoseconds = execution.hostTiming.hostCompletionNanoseconds;
    result.nativeDeviceIntervalNanoseconds = execution.nativeDeviceIntervalNanoseconds;
    const std::string phase = FailurePhase(execution.failurePhase);
    if (!phase.empty()) result.failurePhase = phase;
    if (execution.nativeResult.has_value())
        result.errorCode = std::to_string(static_cast<int>(*execution.nativeResult));
    switch (execution.status)
    {
    case Status::Ok:
        result.status = "ok";
        result.validationPassed = true;
        break;
    case Status::ValidationFailed:
        result.status = "validation_failed";
        result.validationPassed = false;
        result.exitCode = gate0::ExitCode::CorrectnessFailure;
        break;
    case Status::SubmitFailed:
        result.status = execution.nativeResult == VK_ERROR_DEVICE_LOST
            ? "device_lost" : "submit_failed";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::WaitFailed:
        result.status = execution.nativeResult == VK_ERROR_DEVICE_LOST
            ? "device_lost" : "wait_failed";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::Timeout:
        result.status = "timeout";
        result.exitCode = gate0::ExitCode::IncompleteOrTimeout;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::ReadbackFailed:
        result.status = execution.nativeResult == VK_ERROR_DEVICE_LOST
            ? "device_lost" : "incomplete";
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        result.safeForFurtherGpuCalls = false;
        break;
    case Status::TimingInvalid:
        result.status = "timestamp_invalid";
        if (execution.validationPassed) result.validationPassed = true;
        result.exitCode = gate0::ExitCode::NativeExecutionFailure;
        break;
    }
    return result;
}

gate0::SampleRecord MakeSample(
    const gate0::Configuration& configuration,
    std::string_view comparisonConditionId,
    std::string_view seriesId,
    std::uint64_t sampleIndex,
    const OperationObservation& observation)
{
    return {
        configuration.runId,
        std::string(comparisonConditionId),
        std::string(seriesId),
        configuration.backend,
        configuration.seed,
        configuration.elementCount,
        configuration.instrumentMode,
        configuration.warmupCount,
        configuration.plannedSampleCount,
        configuration.blockIndex,
        configuration.orderSlot,
        configuration.processIndex,
        sampleIndex,
        observation.validationPassed,
        observation.status,
        observation.failurePhase,
        observation.errorCode,
        observation.hostSubmissionNanoseconds,
        observation.hostWaitNanoseconds,
        observation.hostCompletionNanoseconds,
        observation.nativeDeviceIntervalNanoseconds};
}

void RetainFailure(ExecutionState& state, const OperationObservation& observation)
{
    if (observation.exitCode == gate0::ExitCode::Success) return;
    state.processStatus = observation.status;
    state.failurePhase = observation.failurePhase;
    state.errorCode = observation.errorCode;
    state.exitCode = observation.exitCode;
    state.safeForFurtherGpuCalls = observation.safeForFurtherGpuCalls;
}

template <typename Execute>
ExecutionState ExecuteCondition(
    const gate0::Configuration& configuration,
    gate0::QualificationPackage& package,
    std::string_view comparisonConditionId,
    std::string_view seriesId,
    const std::optional<supervisor::ProgressReporter>& progress,
    Execute execute)
{
    ExecutionState state;
    std::uint64_t operationIndex{};
    const auto executeOne = [&]() {
        if (progress.has_value()) progress->OperationStarted(operationIndex);
        OperationObservation observation = execute();
        if (progress.has_value()) progress->OperationCompleted(operationIndex);
        ++operationIndex;
        return observation;
    };
    if (configuration.phase == gate0::QualificationPhase::WarmupCharacterization)
    {
        for (std::uint64_t index = 0;
             index < gate0::WarmupCharacterizationCount; ++index)
        {
            const OperationObservation observation = executeOne();
            package.AppendWarmup({
                configuration.runId,
                std::string(seriesId),
                configuration.processIndex,
                index,
                observation.hostSubmissionNanoseconds,
                observation.hostWaitNanoseconds,
                observation.hostCompletionNanoseconds,
                observation.status});
            RetainFailure(state, observation);
            if (state.exitCode != gate0::ExitCode::Success) break;
        }
        return state;
    }

    for (std::uint64_t index = 0; index < configuration.warmupCount; ++index)
    {
        const OperationObservation observation = executeOne();
        RetainFailure(state, observation);
        if (state.exitCode != gate0::ExitCode::Success) return state;
    }

    state.samples.reserve(static_cast<std::size_t>(configuration.plannedSampleCount));
    for (std::uint64_t index = 0;
         index < configuration.plannedSampleCount; ++index)
    {
        const OperationObservation observation = executeOne();
        state.samples.push_back(MakeSample(
            configuration, comparisonConditionId, seriesId, index, observation));
        package.AppendSample(state.samples.back());
        RetainFailure(state, observation);
        if (state.exitCode != gate0::ExitCode::Success) break;
    }
    return state;
}

std::vector<gate0::InitializationRecord> SetupInitialization(
    const gate0::Configuration& configuration)
{
    return {{
        configuration.runId,
        configuration.backend,
        configuration.processIndex,
        0U,
        "backend_setup",
        std::string(gate0::WorkloadId),
        std::string(gate0::VariantId),
        configuration.elementCount,
        "setup_complete",
        std::nullopt,
        "true"}};
}

void AddWarmupOutcome(
    std::vector<gate0::InitializationRecord>& initialization,
    const gate0::Configuration& configuration,
    const ExecutionState& state)
{
    if (configuration.phase == gate0::QualificationPhase::WarmupCharacterization)
        return;
    initialization.push_back({
        configuration.runId,
        configuration.backend,
        configuration.processIndex,
        1U,
        "selected_warmup",
        std::string(gate0::WorkloadId),
        std::string(gate0::VariantId),
        configuration.elementCount,
        state.exitCode == gate0::ExitCode::Success
            ? "warmup_complete" : "warmup_failed",
        std::nullopt,
        state.exitCode == gate0::ExitCode::Success ? "true" : state.processStatus});
}

gate0::BackendDiagnostics CudaDiagnostics(gate0::InstrumentMode mode)
{
    gate0::BackendDiagnostics result;
    result.implementation = "cuda-qualification-operation";
    result.streamFlags = "cudaStreamNonBlocking";
    result.nativeMarkersEnabled = mode == gate0::InstrumentMode::N;
    if (result.nativeMarkersEnabled)
    {
        const cuda::CudaNativeTimingMetadata metadata;
        result.nativeTimingMethod = std::string(metadata.method);
        result.nativeTimingResolutionNanoseconds =
            metadata.approximateResolutionNanoseconds;
    }
    return result;
}

gate0::BackendDiagnostics VulkanDiagnostics(
    const vk::VulkanQualificationDiagnostics& diagnostics,
    gate0::InstrumentMode mode)
{
    gate0::BackendDiagnostics result;
    result.implementation = "vulkan-qualification-operation";
    result.queueFamilyIndex = diagnostics.queueFamilyIndex;
    result.queueFlags = diagnostics.queueFamily.queueFlags;
    result.queueCount = diagnostics.queueFamily.queueCount;
    result.timestampValidBits = diagnostics.queueFamily.timestampValidBits;
    result.timestampPeriodNanoseconds =
        static_cast<double>(diagnostics.properties.limits.timestampPeriod);
    result.inputMemoryFlags = diagnostics.inputMemoryFlags;
    result.outputMemoryFlags = diagnostics.outputMemoryFlags;
    result.uploadMemoryFlags = diagnostics.uploadMemoryFlags;
    result.readbackMemoryFlags = diagnostics.readbackMemoryFlags;
    result.nativeMarkersEnabled = mode == gate0::InstrumentMode::N;
    if (result.nativeMarkersEnabled)
    {
        const vk::VulkanNativeTimingMetadata metadata;
        result.nativeTimingMethod = std::string(metadata.method);
        result.nativeTimingStartStage = metadata.startStage;
        result.nativeTimingStopStage = metadata.stopStage;
        result.nativeDurationEnvelopeNanoseconds = metadata.durationEnvelopeNanoseconds;
    }
    return result;
}

void PrintUsage()
{
    std::cout
        << "ComputeLabEx2Gate0 --phase warmup-characterization|sample-count-qualification|instrumentation-control "
           "--backend cuda|vulkan --instrument-mode H|N --device-index N --element-count N --seed N "
           "--warmup-count N --planned-sample-count N --process-index 0..4 --block-index 0..4 "
           "--order-slot 0|1 --run-id ID --output-package-directory results/local/ID "
           "--machine-id ID --protocol-version 1.0\n\n"
           "Exit classes: 0 success; 2 configuration; 3 correctness; 4 native execution; "
           "5 incomplete/timeout (including external supervision); 6 evidence/provenance.\n";
}

void ValidateInvocationConfiguration(const gate0::Configuration& configuration)
{
    const auto localRoot = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local";
    gate0::ValidateOutputPackageDirectory(configuration, localRoot);

    std::error_code error;
    const bool finalExists = std::filesystem::exists(
        configuration.outputPackageDirectory, error);
    if (error)
        throw std::runtime_error("qualification package collision check failed");
    const bool stagingExists = std::filesystem::exists(
        configuration.outputPackageDirectory.string() + ".incomplete", error);
    if (error)
        throw std::runtime_error("qualification staging collision check failed");
    if (finalExists || stagingExists)
        throw std::invalid_argument(
            "qualification package path collision; refusing to overwrite");
}

int Run(
    const gate0::Configuration& configuration,
    const std::filesystem::path& executable,
    const std::optional<supervisor::ProgressReporter>& progress)
{
    const GitState gitBefore = ReadGitState();
    const std::string executableHash = gate0::Sha256File(executable);
    const std::optional<std::string> shaderHash =
        configuration.backend == gate0::Backend::Vulkan
            ? std::optional<std::string>{gate0::Sha256File(COMPUTELAB_EX1_SPIRV_PATH)}
            : std::nullopt;
    const std::string timestampUtc = TimestampUtc();

    if (configuration.elementCount > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument("element-count exceeds this process's address space");
    const auto input = computelab::GenerateSeededInput(
        configuration.seed, static_cast<std::size_t>(configuration.elementCount));
    const auto expected = computelab::TransformSequence(input);
    const std::string inputHash = gate0::Sha256(std::as_bytes(std::span{input}));
    const std::string expectedHash = gate0::Sha256(std::as_bytes(std::span{expected}));

    std::array<std::uint8_t, 16> selectedUuid{};
    gate0::BackendDiagnostics diagnostics;
    ExecutionState execution;
    std::vector<gate0::InitializationRecord> initialization;
    std::string comparisonConditionId;
    std::string seriesId;
    std::unique_ptr<gate0::QualificationPackage> package;

    const auto establishIdentities = [&](const std::array<std::uint8_t, 16>& uuid) {
        const gate0::IdentityContext condition{
            configuration.protocolVersion,
            configuration.machineId,
            FormatUuid(uuid),
            configuration.elementCount,
            configuration.seed,
            configuration.instrumentMode};
        comparisonConditionId = gate0::ComparisonConditionId(condition);
        seriesId = gate0::SeriesId({
            condition,
            configuration.backend,
            configuration.processIndex,
            configuration.blockIndex,
            configuration.orderSlot,
            configuration.warmupCount,
            configuration.plannedSampleCount,
            gitBefore.revision,
            executableHash,
            shaderHash});
    };

    if (configuration.backend == gate0::Backend::Cuda
        && configuration.instrumentMode == gate0::InstrumentMode::H)
    {
        auto operation = NativeCall([&] {
            return std::make_unique<cuda::CudaQualificationOperation>(
                static_cast<int>(configuration.deviceIndex), input.size());
        });
        NativeCall([&] { operation->Upload(input); });
        selectedUuid = operation->SelectedDeviceUuid();
        establishIdentities(selectedUuid);
        package = std::make_unique<gate0::QualificationPackage>(configuration);
        initialization = SetupInitialization(configuration);
        package->WriteInitialization(initialization);
        diagnostics = CudaDiagnostics(configuration.instrumentMode);
        execution = ExecuteCondition(
            configuration, *package, comparisonConditionId, seriesId, progress,
            [&operation] {
                return Observe(NativeCall(
                    [&operation] { return operation->ExecuteHostOnly(); }));
            });
    }
    else if (configuration.backend == gate0::Backend::Cuda)
    {
        auto operation = NativeCall([&] {
            return std::make_unique<cuda::CudaDeviceTimedQualificationOperation>(
                static_cast<int>(configuration.deviceIndex), input.size());
        });
        NativeCall([&] { operation->Upload(input); });
        selectedUuid = operation->SelectedDeviceUuid();
        establishIdentities(selectedUuid);
        package = std::make_unique<gate0::QualificationPackage>(configuration);
        initialization = SetupInitialization(configuration);
        package->WriteInitialization(initialization);
        diagnostics = CudaDiagnostics(configuration.instrumentMode);
        execution = ExecuteCondition(
            configuration, *package, comparisonConditionId, seriesId, progress,
            [&operation] {
                return Observe(NativeCall(
                    [&operation] { return operation->ExecuteDeviceTimed(); }));
            });
    }
    else if (configuration.instrumentMode == gate0::InstrumentMode::H)
    {
        auto operation = NativeCall([&] {
            return std::make_unique<vk::VulkanQualificationOperation>(
                input.size(), COMPUTELAB_EX1_SPIRV_PATH, configuration.deviceIndex);
        });
        NativeCall([&] { operation->Upload(input); });
        selectedUuid = operation->Diagnostics().deviceUuid;
        establishIdentities(selectedUuid);
        package = std::make_unique<gate0::QualificationPackage>(configuration);
        initialization = SetupInitialization(configuration);
        package->WriteInitialization(initialization);
        diagnostics = VulkanDiagnostics(operation->Diagnostics(), configuration.instrumentMode);
        execution = ExecuteCondition(
            configuration, *package, comparisonConditionId, seriesId, progress,
            [&operation] {
                NativeCall([&operation] { operation->PrepareHostOnly(); });
                return Observe(NativeCall(
                    [&operation] { return operation->ExecuteHostOnly(); }));
            });
    }
    else
    {
        auto operation = NativeCall([&] {
            return std::make_unique<vk::VulkanDeviceTimedQualificationOperation>(
                input.size(), COMPUTELAB_EX1_SPIRV_PATH, configuration.deviceIndex);
        });
        NativeCall([&] { operation->Upload(input); });
        selectedUuid = operation->Diagnostics().deviceUuid;
        establishIdentities(selectedUuid);
        package = std::make_unique<gate0::QualificationPackage>(configuration);
        initialization = SetupInitialization(configuration);
        package->WriteInitialization(initialization);
        diagnostics = VulkanDiagnostics(operation->Diagnostics(), configuration.instrumentMode);
        execution = ExecuteCondition(
            configuration, *package, comparisonConditionId, seriesId, progress,
            [&operation] {
                NativeCall([&operation] { operation->PrepareDeviceTimed(); });
                return Observe(NativeCall(
                    [&operation] { return operation->ExecuteDeviceTimed(); }));
            });
    }

    AddWarmupOutcome(initialization, configuration, execution);
    package->WriteInitialization(initialization);

    // A failed submit/wait/readback or timeout may leave native work or
    // resources uncertain. Do not initialize/query either API again. The
    // already-flushed .incomplete directory is the deliberately incomplete
    // partial package for an external supervisor to retain.
    if (!execution.safeForFurtherGpuCalls)
        return static_cast<int>(execution.exitCode);

    const GitState gitAfter = ReadGitState();
    if (gitAfter.revision != gitBefore.revision
        || gitAfter.workingTreeState != gitBefore.workingTreeState)
        throw std::runtime_error(
            "source Git revision or working-tree state changed during qualification child execution");

    computelab::environment::EnvironmentRunContext context{
        std::string(gate0::ExperimentId),
        configuration.runId,
        timestampUtc,
        gitBefore.revision,
        gitBefore.dirty,
        configuration.machineId,
        false,
        configuration.instrumentMode == gate0::InstrumentMode::N};
    auto common = configuration.backend == gate0::Backend::Cuda
        ? computelab::environment::CollectCudaEnvironmentRecord(
            context, configuration.deviceIndex, selectedUuid)
        : computelab::environment::CollectVulkanEnvironmentRecord(
            context, configuration.deviceIndex, selectedUuid);
    common.schemaVersion = gate0::SchemaVersion;

    const gate0::EnvironmentRecord environment{
        std::move(common),
        configuration,
        comparisonConditionId,
        seriesId,
        gitBefore.revision,
        executableHash,
        shaderHash,
        inputHash,
        expectedHash,
        FormatUuid(selectedUuid),
        diagnostics};
    const gate0::SummaryRecord summary = gate0::SummarizeSamples(
        configuration,
        comparisonConditionId,
        seriesId,
        execution.samples,
        execution.processStatus,
        execution.failurePhase,
        execution.errorCode);
    package->Complete(environment, summary);

    std::cout << "EX-2 Gate-0 qualification package: "
        << configuration.outputPackageDirectory.string() << '\n';
    return static_cast<int>(execution.exitCode);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage();
        return static_cast<int>(gate0::ExitCode::Success);
    }

    std::optional<gate0::Configuration> configuration;
    std::optional<supervisor::ProgressReporter> progress;
    try
    {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        progress = supervisor::ExtractProgressReporter(arguments);
        configuration = gate0::ParseArguments(arguments);
        ValidateInvocationConfiguration(*configuration);
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "ComputeLabEx2Gate0 configuration error: " << error.what() << '\n';
        return static_cast<int>(gate0::ExitCode::ConfigurationError);
    }
    catch (const std::exception& error)
    {
        std::cerr << "ComputeLabEx2Gate0 evidence/provenance failure: "
            << error.what() << '\n';
        return static_cast<int>(gate0::ExitCode::EvidenceOrProvenanceFailure);
    }

    try
    {
        return Run(*configuration, RunningExecutablePath(), progress);
    }
    catch (const NativeExecutionException& error)
    {
        std::cerr << "ComputeLabEx2Gate0 native execution failure: "
            << error.what() << '\n';
        return static_cast<int>(gate0::ExitCode::NativeExecutionFailure);
    }
    catch (const std::exception& error)
    {
        // All exceptions after validated configuration are evidence/provenance
        // failures unless a native operation returned a classified row. The
        // latter path is completed and returned explicitly by Run().
        std::cerr << "ComputeLabEx2Gate0 evidence/provenance failure: "
            << error.what() << '\n';
        return static_cast<int>(gate0::ExitCode::EvidenceOrProvenanceFailure);
    }
}
