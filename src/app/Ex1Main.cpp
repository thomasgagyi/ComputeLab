#include "cuda/CudaTransform.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex1/Ex1Support.hpp"
#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"
#include "timing/HostTiming.hpp"
#include "vulkan/VulkanTransform.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace ex1 = computelab::ex1;
namespace results = computelab::results;
namespace timing = computelab::timing;

struct ExecutionResult
{
    std::vector<results::InitializationRecord> initialization;
    std::vector<results::SampleRecord> samples;
    bool validationPassed{true};
};

std::size_t ElementCount(const ex1::Configuration& configuration)
{
    if (configuration.elementCount > std::numeric_limits<std::size_t>::max())
    {
        throw std::invalid_argument("element-count exceeds this process's addressable size");
    }
    return static_cast<std::size_t>(configuration.elementCount);
}

std::uint64_t Elapsed(timing::HostTimePoint begin)
{
    return timing::ElapsedNanoseconds(begin, timing::CaptureHostTime());
}

results::InitializationRecord InitializationDuration(
    const ex1::Configuration& configuration,
    std::uint64_t sequenceIndex,
    std::string category,
    std::string metric,
    std::uint64_t durationNanoseconds)
{
    return {
        ex1::SchemaVersion,
        configuration.runId,
        std::string(ex1::ExperimentId),
        std::string(ex1::ToString(configuration.backend)),
        0U,
        sequenceIndex,
        std::move(category),
        std::string(ex1::WorkloadId),
        configuration.variant,
        configuration.elementCount,
        std::move(metric),
        durationNanoseconds,
        std::nullopt};
}

results::InitializationRecord InitializationObservation(
    const ex1::Configuration& configuration,
    std::uint64_t sequenceIndex,
    std::string category,
    std::string metric,
    std::string observation)
{
    return {
        ex1::SchemaVersion,
        configuration.runId,
        std::string(ex1::ExperimentId),
        std::string(ex1::ToString(configuration.backend)),
        0U,
        sequenceIndex,
        std::move(category),
        std::string(ex1::WorkloadId),
        configuration.variant,
        configuration.elementCount,
        std::move(metric),
        std::nullopt,
        std::move(observation)};
}

ExecutionResult RunCudaInitialization(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    ExecutionResult result;
    result.initialization.reserve(7U);

    const auto constructionBegin = timing::CaptureHostTime();
    auto operation = std::make_unique<computelab::cuda::CudaTransformOperation>(
        0, input.size());
    result.initialization.push_back(InitializationDuration(
        configuration, 0U, "backend_setup", "host_duration_ns",
        Elapsed(constructionBegin)));

    const auto uploadBegin = timing::CaptureHostTime();
    operation->Upload(input);
    result.initialization.push_back(InitializationDuration(
        configuration, 1U, "first_upload", "host_visible_duration_ns",
        Elapsed(uploadBegin)));

    operation->RecordDeviceStart();
    const auto endToEndBegin = timing::CaptureHostTime();
    const auto submissionBegin = timing::CaptureHostTime();
    operation->SubmitTransform();
    const auto submissionEnd = timing::CaptureHostTime();
    operation->RecordDeviceStop();
    operation->WaitForCompletion();
    const auto endToEndEnd = timing::CaptureHostTime();

    result.initialization.push_back(InitializationDuration(
        configuration, 2U, "first_submission", "host_submission_ns",
        timing::ElapsedNanoseconds(submissionBegin, submissionEnd)));
    result.initialization.push_back(InitializationDuration(
        configuration, 3U, "first_execution", "device_execution_ns",
        operation->DeviceElapsedNanoseconds()));
    result.initialization.push_back(InitializationDuration(
        configuration, 4U, "first_completion", "end_to_end_ns",
        timing::ElapsedNanoseconds(endToEndBegin, endToEndEnd)));

    const auto readbackBegin = timing::CaptureHostTime();
    const auto output = operation->RetrieveOutput();
    result.initialization.push_back(InitializationDuration(
        configuration, 5U, "first_readback", "host_visible_duration_ns",
        Elapsed(readbackBegin)));

    result.validationPassed = output == expected;
    result.initialization.push_back(InitializationObservation(
        configuration, 6U, "first_correctness", "validation_passed",
        result.validationPassed ? "true" : "false"));
    return result;
}

ExecutionResult RunVulkanInitialization(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    ExecutionResult result;
    result.initialization.reserve(7U);

    const auto constructionBegin = timing::CaptureHostTime();
    auto operation = std::make_unique<computelab::vulkan::TransformDispatch>(
        input.size(), COMPUTELAB_EX1_SPIRV_PATH);
    result.initialization.push_back(InitializationDuration(
        configuration, 0U, "backend_setup", "host_duration_ns",
        Elapsed(constructionBegin)));

    const auto uploadBegin = timing::CaptureHostTime();
    operation->Upload(input);
    result.initialization.push_back(InitializationDuration(
        configuration, 1U, "first_upload", "host_visible_duration_ns",
        Elapsed(uploadBegin)));

    operation->PrepareMeasurement();
    const auto endToEndBegin = timing::CaptureHostTime();
    const auto submissionBegin = timing::CaptureHostTime();
    operation->SubmitTransform();
    const auto submissionEnd = timing::CaptureHostTime();
    operation->WaitForCompletion();
    const auto endToEndEnd = timing::CaptureHostTime();

    result.initialization.push_back(InitializationDuration(
        configuration, 2U, "first_submission", "host_submission_ns",
        timing::ElapsedNanoseconds(submissionBegin, submissionEnd)));
    result.initialization.push_back(InitializationDuration(
        configuration, 3U, "first_execution", "device_execution_ns",
        operation->DeviceElapsedNanoseconds()));
    result.initialization.push_back(InitializationDuration(
        configuration, 4U, "first_completion", "end_to_end_ns",
        timing::ElapsedNanoseconds(endToEndBegin, endToEndEnd)));

    const auto readbackBegin = timing::CaptureHostTime();
    const auto output = operation->RetrieveOutput();
    result.initialization.push_back(InitializationDuration(
        configuration, 5U, "first_readback", "host_visible_duration_ns",
        Elapsed(readbackBegin)));

    result.validationPassed = output == expected;
    result.initialization.push_back(InitializationObservation(
        configuration, 6U, "first_correctness", "validation_passed",
        result.validationPassed ? "true" : "false"));
    return result;
}

ExecutionResult RunInitialization(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    switch (configuration.backend)
    {
    case ex1::Backend::Cuda:
        return RunCudaInitialization(configuration, input, expected);
    case ex1::Backend::Vulkan:
        return RunVulkanInitialization(configuration, input, expected);
    case ex1::Backend::Cpu:
        throw std::invalid_argument("CPU has no EX-1 GPU initialization mode");
    }
    throw std::invalid_argument("unsupported EX-1 backend");
}

void RunCpuWarmups(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    for (std::uint64_t index = 0U; index < *configuration.warmupCount; ++index)
    {
        if (computelab::TransformSequence(input) != expected)
        {
            throw std::runtime_error("CPU warm-up correctness validation failed");
        }
    }
}

ExecutionResult RunCpuSeries(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    RunCpuWarmups(configuration, input, expected);
    ExecutionResult result;
    result.samples.reserve(static_cast<std::size_t>(*configuration.plannedSampleCount));
    for (std::uint64_t index = 0U; index < *configuration.plannedSampleCount; ++index)
    {
        const auto begin = timing::CaptureHostTime();
        const auto output = computelab::TransformSequence(input);
        const auto end = timing::CaptureHostTime();
        const bool valid = output == expected;
        result.samples.push_back(ex1::MakeSampleRecord(
            configuration, index, valid,
            {std::nullopt, std::nullopt, std::nullopt,
                timing::ElapsedNanoseconds(begin, end), std::nullopt}));
        if (!valid)
        {
            result.validationPassed = false;
            break;
        }
    }
    return result;
}

void ExecuteCudaUntimed(
    computelab::cuda::CudaTransformOperation& operation,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    operation.Upload(input);
    operation.RecordDeviceStart();
    operation.SubmitTransform();
    operation.RecordDeviceStop();
    operation.WaitForCompletion();
    if (operation.RetrieveOutput() != expected)
    {
        throw std::runtime_error("CUDA warm-up correctness validation failed");
    }
}

ExecutionResult RunCudaSeries(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    computelab::cuda::CudaTransformOperation operation{0, input.size()};
    for (std::uint64_t index = 0U; index < *configuration.warmupCount; ++index)
    {
        ExecuteCudaUntimed(operation, input, expected);
    }

    ExecutionResult result;
    result.samples.reserve(static_cast<std::size_t>(*configuration.plannedSampleCount));
    for (std::uint64_t index = 0U; index < *configuration.plannedSampleCount; ++index)
    {
        operation.Upload(input);
        operation.RecordDeviceStart();
        const auto endToEndBegin = timing::CaptureHostTime();
        const auto submissionBegin = timing::CaptureHostTime();
        operation.SubmitTransform();
        const auto submissionEnd = timing::CaptureHostTime();
        operation.RecordDeviceStop();
        operation.WaitForCompletion();
        const auto endToEndEnd = timing::CaptureHostTime();
        const std::uint64_t deviceNanoseconds = operation.DeviceElapsedNanoseconds();
        const bool valid = operation.RetrieveOutput() == expected;

        result.samples.push_back(ex1::MakeSampleRecord(
            configuration, index, valid,
            {std::nullopt,
                timing::ElapsedNanoseconds(submissionBegin, submissionEnd),
                deviceNanoseconds,
                timing::ElapsedNanoseconds(endToEndBegin, endToEndEnd),
                std::nullopt}));
        if (!valid)
        {
            result.validationPassed = false;
            break;
        }
    }
    return result;
}

void ExecuteVulkanUntimed(
    computelab::vulkan::TransformDispatch& operation,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    operation.Upload(input);
    operation.PrepareMeasurement();
    operation.SubmitTransform();
    operation.WaitForCompletion();
    if (operation.RetrieveOutput() != expected)
    {
        throw std::runtime_error("Vulkan warm-up correctness validation failed");
    }
}

ExecutionResult RunVulkanSeries(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    computelab::vulkan::TransformDispatch operation{
        input.size(), COMPUTELAB_EX1_SPIRV_PATH};
    for (std::uint64_t index = 0U; index < *configuration.warmupCount; ++index)
    {
        ExecuteVulkanUntimed(operation, input, expected);
    }

    ExecutionResult result;
    result.samples.reserve(static_cast<std::size_t>(*configuration.plannedSampleCount));
    for (std::uint64_t index = 0U; index < *configuration.plannedSampleCount; ++index)
    {
        operation.Upload(input);
        operation.PrepareMeasurement();
        const auto endToEndBegin = timing::CaptureHostTime();
        const auto submissionBegin = timing::CaptureHostTime();
        operation.SubmitTransform();
        const auto submissionEnd = timing::CaptureHostTime();
        operation.WaitForCompletion();
        const auto endToEndEnd = timing::CaptureHostTime();
        const std::uint64_t deviceNanoseconds = operation.DeviceElapsedNanoseconds();
        const bool valid = operation.RetrieveOutput() == expected;

        result.samples.push_back(ex1::MakeSampleRecord(
            configuration, index, valid,
            {std::nullopt,
                timing::ElapsedNanoseconds(submissionBegin, submissionEnd),
                deviceNanoseconds,
                timing::ElapsedNanoseconds(endToEndBegin, endToEndEnd),
                std::nullopt}));
        if (!valid)
        {
            result.validationPassed = false;
            break;
        }
    }
    return result;
}

ExecutionResult RunSeries(
    const ex1::Configuration& configuration,
    const std::vector<std::uint32_t>& input,
    const std::vector<std::uint32_t>& expected)
{
    switch (configuration.backend)
    {
    case ex1::Backend::Cpu:
        return RunCpuSeries(configuration, input, expected);
    case ex1::Backend::Cuda:
        return RunCudaSeries(configuration, input, expected);
    case ex1::Backend::Vulkan:
        return RunVulkanSeries(configuration, input, expected);
    }
    throw std::invalid_argument("unsupported EX-1 backend");
}

void PrintUsage()
{
    std::cout
        << "ComputeLabEx1 --mode initialization|series --backend cpu|cuda|vulkan "
           "--element-count N --seed N --variant ID --run-id ID --timestamp-utc UTC "
           "--git-commit COMMIT --git-dirty true|false --machine-id ID "
           "--validation-enabled true|false --diagnostic-instrumentation true|false "
           "[--warmup-count N --planned-sample-count N]\n";
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            PrintUsage();
            return 0;
        }

        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index)
        {
            arguments.emplace_back(argv[index]);
        }
        const ex1::Configuration configuration = ex1::ParseArguments(arguments);

        // A2 input and A3 expected output are each computed exactly once and
        // remain outside every measured interval.
        const auto input = computelab::GenerateSeededInput(
            configuration.seed, ElementCount(configuration));
        const auto expected = computelab::TransformSequence(input);

        ExecutionResult execution = configuration.mode == ex1::Mode::Initialization
            ? RunInitialization(configuration, input, expected)
            : RunSeries(configuration, input, expected);

        // Environment acquisition, statistics, serialization, and file I/O are
        // deliberately after measurement. In initialization mode this also
        // avoids acquiring GPU metadata before the first backend operation.
        const results::EnvironmentRecord environment =
            computelab::environment::CollectEnvironmentRecord(
                ex1::EnvironmentContext(configuration));
        const results::SummaryRecord summary = ex1::SummarizeSamples(
            ex1::SchemaVersion,
            configuration.runId,
            std::string(ex1::ExperimentId),
            execution.samples);
        ex1::WriteResultPackage(
            std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT) / "results" / "local",
            environment,
            execution.initialization,
            execution.samples,
            summary);

        const std::filesystem::path package = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
            / "results" / "local" / configuration.runId;
        std::cout << "EX-1 local result package: " << package.string() << '\n';
        if (!execution.validationPassed)
        {
            std::cerr << "EX-1 correctness validation failed; rejected evidence was preserved.\n";
            return 2;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ComputeLabEx1 failed: " << error.what() << '\n';
        return 1;
    }
}
