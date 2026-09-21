#include "app/Ex2A1Integration.hpp"

#include "ex2/Ex2Input.hpp"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace a1 = computelab::ex2::a1;
namespace environment = computelab::environment;
namespace ex2 = computelab::ex2;

constexpr std::uint64_t kMaximumCorrectnessElementCount = 16'777'216U;

struct Configuration
{
    std::uint64_t elementCount{};
    std::uint64_t seed{ex2::CoreInputSeed};
    int cudaDeviceOrdinal{};
    std::uint32_t vulkanPhysicalDeviceIndex{};
    std::string machineId;
    std::string sessionId;
};

struct GitState
{
    std::string revision;
    bool dirty{};
    std::string workingTreeState;
};

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
        throw std::runtime_error(
            "source Git revision is not a full SHA-1 commit identity");
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

std::uint64_t ParseUnsigned(std::string_view value, std::string_view name)
{
    int base = 10;
    if (value.size() > 2U && value[0] == '0'
        && (value[1] == 'x' || value[1] == 'X'))
    {
        value.remove_prefix(2U);
        base = 16;
    }
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), result, base);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::invalid_argument(std::string(name) + " is not a valid integer");
    return result;
}

Configuration ParseArguments(int argc, char** argv)
{
    Configuration result;
    bool hasElementCount = false;
    bool hasCudaDevice = false;
    bool hasVulkanDevice = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view key = argv[index];
        if (index + 1 >= argc)
            throw std::invalid_argument("every EX-2 A1 option requires a value");
        const std::string_view value = argv[++index];
        if (key == "--element-count")
        {
            result.elementCount = ParseUnsigned(value, "element-count");
            hasElementCount = true;
        }
        else if (key == "--seed")
        {
            result.seed = ParseUnsigned(value, "seed");
        }
        else if (key == "--cuda-device")
        {
            const auto parsed = ParseUnsigned(value, "cuda-device");
            if (parsed > static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max()))
            {
                throw std::invalid_argument("cuda-device is out of range");
            }
            result.cudaDeviceOrdinal = static_cast<int>(parsed);
            hasCudaDevice = true;
        }
        else if (key == "--vulkan-device")
        {
            const auto parsed = ParseUnsigned(value, "vulkan-device");
            if (parsed > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("vulkan-device is out of range");
            result.vulkanPhysicalDeviceIndex = static_cast<std::uint32_t>(parsed);
            hasVulkanDevice = true;
        }
        else if (key == "--machine-id")
        {
            result.machineId = value;
        }
        else if (key == "--session-id")
        {
            result.sessionId = value;
        }
        else
        {
            throw std::invalid_argument("unknown EX-2 A1 option: " + std::string(key));
        }
    }
    if (!hasElementCount || !hasCudaDevice || !hasVulkanDevice
        || result.machineId.empty() || result.sessionId.empty())
    {
        throw std::invalid_argument(
            "element-count, CUDA/Vulkan selectors, machine-id and session-id are required");
    }
    if (result.elementCount > kMaximumCorrectnessElementCount)
        throw std::invalid_argument(
            "element-count exceeds the bounded A1 correctness limit");
    if (result.seed != ex2::CoreInputSeed)
        throw std::invalid_argument(
            "the minimal A1 correctness path requires seed 0x0123456789ABCDEF");
    if (!ex2::IsValidAnonymousIdentifier(result.machineId)
        || !ex2::IsValidAnonymousIdentifier(result.sessionId))
    {
        throw std::invalid_argument(
            "machine-id and session-id must be anonymous identifiers");
    }
    return result;
}

void PrintUsage()
{
    std::cout
        << "ComputeLabEx2A1 --element-count N --cuda-device N --vulkan-device N "
           "--machine-id ID --session-id ID [--seed 0x0123456789ABCDEF]\n"
           "Runs one sequential, correctness-only native A1 operation per backend.\n"
           "No performance timings or qualification evidence are collected.\n";
}

std::array<std::filesystem::path, 2> ValidateOutputPlan(
    const Configuration& configuration,
    const std::string& cudaRunId,
    const std::string& vulkanRunId)
{
    const auto localRoot = std::filesystem::path(COMPUTELAB_REPOSITORY_ROOT)
        / "results" / "local";
    const std::array destinations{
        localRoot / cudaRunId,
        localRoot / vulkanRunId};
    const std::array<ex2::PlannedRun, 2> runs{{
        {cudaRunId, destinations[0]},
        {vulkanRunId, destinations[1]}}};
    if (!ex2::ValidateRunPlan(runs, localRoot).IsValid())
        throw std::invalid_argument("EX-2 A1 output run plan is invalid");
    for (const auto& destination : destinations)
    {
        std::error_code error;
        if (std::filesystem::exists(destination, error) || error)
            throw std::invalid_argument(
                "EX-2 A1 output destination exists or cannot be checked");
    }
    return destinations;
}

void WriteText(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(
            content.data(), static_cast<std::streamsize>(content.size())))
    {
        throw std::runtime_error("EX-2 A1 evidence file write failed");
    }
}

void PublishSeries(
    const std::filesystem::path& destination,
    const a1::SerializedSeries& series)
{
    if (!std::filesystem::create_directories(destination))
        throw std::runtime_error("EX-2 A1 evidence directory creation failed");
    WriteText(destination / "environment.json", series.environmentJson);
    WriteText(destination / "initialization.csv", series.initializationCsv);
    WriteText(destination / "samples.csv", series.samplesCsv);
    WriteText(destination / "summary.json", series.summaryJson);
}

int Run(const Configuration& configuration)
{
    const std::string cudaRunId = configuration.sessionId + "-cuda";
    const std::string vulkanRunId = configuration.sessionId + "-vulkan";
    const auto destinations = ValidateOutputPlan(
        configuration, cudaRunId, vulkanRunId);
    const GitState gitBefore = ReadGitState();
    const auto executable = RunningExecutablePath();
    const std::string executableHash = ex2::Sha256File(executable);
    const std::filesystem::path spirv = COMPUTELAB_EX2_A1_SPIRV_PATH;
    const std::string shaderHash = ex2::Sha256File(spirv);
    const std::string timestamp = TimestampUtc();

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
    const auto& preflightCudaUuid = cudaDevices[
        static_cast<std::size_t>(configuration.cudaDeviceOrdinal)].uuid;
    const auto& preflightVulkanUuid = vulkanDevices[
        configuration.vulkanPhysicalDeviceIndex].uuid;
    static_cast<void>(a1::VerifySamePhysicalDevice(
        preflightCudaUuid, preflightVulkanUuid));

    const environment::EnvironmentRunContext cudaContext{
        "EX-2", cudaRunId, timestamp, gitBefore.revision, gitBefore.dirty,
        configuration.machineId, true, true};
    const environment::EnvironmentRunContext vulkanContext{
        "EX-2", vulkanRunId, timestamp, gitBefore.revision, gitBefore.dirty,
        configuration.machineId, true, true};
    auto cudaEnvironment = environment::CollectCudaEnvironmentRecord(
        cudaContext,
        static_cast<std::uint32_t>(configuration.cudaDeviceOrdinal),
        preflightCudaUuid);
    auto vulkanEnvironment = environment::CollectVulkanEnvironmentRecord(
        vulkanContext,
        configuration.vulkanPhysicalDeviceIndex,
        preflightVulkanUuid);

    const auto observation = a1::RunCrossBackendCorrectness(
        configuration.elementCount,
        configuration.seed,
        configuration.cudaDeviceOrdinal,
        configuration.vulkanPhysicalDeviceIndex,
        spirv);
    if (observation.verifiedDeviceUuid != preflightCudaUuid
        || observation.verifiedDeviceUuid != preflightVulkanUuid)
    {
        throw std::runtime_error(
            "constructed native operations selected UUIDs different from preflight");
    }

    const GitState gitAfter = ReadGitState();
    if (gitAfter.revision != gitBefore.revision
        || gitAfter.workingTreeState != gitBefore.workingTreeState)
    {
        throw std::runtime_error(
            "source revision or working-tree state changed during A1 correctness execution");
    }

    const auto evidence = a1::BuildEvidence(
        observation,
        {configuration.machineId,
            gitBefore.revision,
            executableHash,
            shaderHash,
            cudaRunId,
            vulkanRunId,
            std::move(cudaEnvironment),
            std::move(vulkanEnvironment)});
    PublishSeries(destinations[0], evidence.cuda);
    PublishSeries(destinations[1], evidence.vulkan);

    if (!observation.Passed())
    {
        std::cerr << "EX-2 A1 correctness failed; exploratory failure evidence was retained.\n";
        return 3;
    }
    std::cout << "EX-2 A1 correctness passed on verified GPU "
        << observation.verifiedDeviceUuidText << "\n"
        << "CUDA series: " << evidence.cuda.environment.plan.seriesId << "\n"
        << "Vulkan series: " << evidence.vulkan.environment.plan.seriesId << "\n"
        << "Exploratory evidence: " << destinations[0].string()
        << " and " << destinations[1].string() << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage();
        return 0;
    }
    try
    {
        return Run(ParseArguments(argc, argv));
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "EX-2 A1 configuration/identity failure: "
            << error.what() << '\n';
        return 2;
    }
    catch (const computelab::cuda::Ex2CudaA1NativeError& error)
    {
        std::cerr << "EX-2 A1 CUDA initialization failure at "
            << computelab::cuda::ToString(error.Phase()) << ": "
            << error.NativeErrorName() << '\n';
        return 4;
    }
    catch (const computelab::vulkan::Ex2VulkanA1NativeError& error)
    {
        std::cerr << "EX-2 A1 Vulkan initialization failure at "
            << computelab::vulkan::ToString(error.Phase()) << ": VkResult "
            << static_cast<int>(error.NativeResult()) << '\n';
        return 4;
    }
    catch (const std::exception& error)
    {
        std::cerr << "EX-2 A1 evidence/provenance failure: "
            << error.what() << '\n';
        return 5;
    }
}
