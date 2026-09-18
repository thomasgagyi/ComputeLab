#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2Gate0.hpp"
#include "ex2/Ex2Gate0Supervisor.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace gate0 = computelab::ex2::gate0;
namespace supervisor = computelab::ex2::gate0::supervisor;
namespace environment = computelab::environment;

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : value_{value} {}
    ~UniqueHandle()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : value_{std::exchange(other.value_, nullptr)} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }

private:
    HANDLE value_{};
};

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read manifest " + path.string());
    return {std::istreambuf_iterator<char>{stream}, {}};
}

std::string Trim(std::string value)
{
    while (!value.empty()
        && (value.back() == '\r' || value.back() == '\n'
            || value.back() == ' ' || value.back() == '\t'))
        value.pop_back();
    return value;
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
    std::string output{buffer.data()};
    output += ".";
    const auto count = milliseconds.count();
    output.push_back(static_cast<char>('0' + (count / 100) % 10));
    output.push_back(static_cast<char>('0' + (count / 10) % 10));
    output.push_back(static_cast<char>('0' + count % 10));
    output.push_back('Z');
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

std::string ReadCurrentRevision(const std::filesystem::path& repositoryRoot)
{
    std::vector<wchar_t> gitPath(MAX_PATH);
    DWORD length = SearchPathW(nullptr, L"git.exe", nullptr,
        static_cast<DWORD>(gitPath.size()), gitPath.data(), nullptr);
    if (length == 0U) throw std::runtime_error("git.exe is unavailable for source preflight");
    if (length >= gitPath.size())
    {
        gitPath.resize(static_cast<std::size_t>(length) + 1U);
        length = SearchPathW(nullptr, L"git.exe", nullptr,
            static_cast<DWORD>(gitPath.size()), gitPath.data(), nullptr);
        if (length == 0U || length >= gitPath.size())
            throw std::runtime_error("git.exe path resolution failed");
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE rawRead{};
    HANDLE rawWrite{};
    if (!CreatePipe(&rawRead, &rawWrite, &security, 1024U))
        throw std::runtime_error("unable to create git output pipe");
    UniqueHandle readPipe{rawRead};
    UniqueHandle writePipe{rawWrite};
    if (!SetHandleInformation(readPipe.Get(), HANDLE_FLAG_INHERIT, 0U))
        throw std::runtime_error("unable to restrict git output pipe inheritance");

    const std::filesystem::path gitExecutable{gitPath.data(), gitPath.data() + length};
    std::wstring command = QuoteWindowsArgument(gitExecutable.native())
        + L" -C " + QuoteWindowsArgument(repositoryRoot.native())
        + L" rev-parse --verify HEAD";
    command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe.Get();
    startup.hStdError = writePipe.Get();
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(gitExecutable.c_str(), command.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, repositoryRoot.c_str(),
            &startup, &processInfo))
        throw std::runtime_error("unable to start git source preflight");
    UniqueHandle process{processInfo.hProcess};
    UniqueHandle thread{processInfo.hThread};
    writePipe = UniqueHandle{};
    if (WaitForSingleObject(process.Get(), 30'000U) != WAIT_OBJECT_0)
        throw std::runtime_error("git source preflight did not terminate");
    DWORD exitCode{};
    if (!GetExitCodeProcess(process.Get(), &exitCode) || exitCode != 0U)
        throw std::runtime_error("git source preflight failed");
    std::string output;
    std::array<char, 256> buffer{};
    for (;;)
    {
        DWORD read{};
        if (!::ReadFile(readPipe.Get(), buffer.data(), static_cast<DWORD>(buffer.size()),
                &read, nullptr))
        {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;
            throw std::runtime_error("unable to read git source preflight output");
        }
        if (read == 0U) break;
        output.append(buffer.data(), read);
    }
    output = Trim(std::move(output));
    if (output.size() != 40U
        || !std::all_of(output.begin(), output.end(), [](char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        }))
        throw std::runtime_error("git source preflight did not return a full revision");
    return output;
}

std::string FormatUuid(const environment::DeviceUuid& uuid)
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

std::filesystem::path Resolve(
    const std::filesystem::path& root,
    const std::string& declared)
{
    return std::filesystem::absolute(root / std::filesystem::u8path(declared))
        .lexically_normal();
}

void RejectCollision(const std::filesystem::path& path, std::string_view description)
{
    std::error_code error;
    if (std::filesystem::exists(path, error) || error)
        throw std::invalid_argument(std::string(description) + " path collision: "
            + path.string());
}

void Preflight(
    const supervisor::QualificationManifest& manifest,
    const std::filesystem::path& repositoryRoot)
{
    const auto executable = Resolve(repositoryRoot, manifest.childExecutablePath);
    const auto shader = Resolve(repositoryRoot, manifest.vulkanShaderPath);
    if (!std::filesystem::is_regular_file(executable))
        throw std::invalid_argument("qualification child executable does not exist");
    if (!std::filesystem::is_regular_file(shader))
        throw std::invalid_argument("required Vulkan shader does not exist");
    const std::string executableHash = gate0::Sha256File(executable);
    const std::string shaderHash = gate0::Sha256File(shader);
    const std::string sourceRevision = ReadCurrentRevision(repositoryRoot);

    bool needsCuda{};
    bool needsVulkan{};
    for (const auto& child : manifest.children)
    {
        needsCuda |= child.configuration.backend == gate0::Backend::Cuda;
        needsVulkan |= child.configuration.backend == gate0::Backend::Vulkan;
        if (child.expectedSourceRevision != sourceRevision)
            throw std::invalid_argument("manifest source revision disagrees with current HEAD");
        if (child.expectedExecutableSha256 != executableHash)
            throw std::invalid_argument("manifest executable identity disagrees with child file");
        if (child.configuration.backend == gate0::Backend::Vulkan
            && child.expectedShaderSha256 != shaderHash)
            throw std::invalid_argument("manifest shader identity disagrees with shader file");
        RejectCollision(child.configuration.outputPackageDirectory,
            "completed qualification package");
        RejectCollision(
            std::filesystem::path(child.configuration.outputPackageDirectory.string()
                + ".incomplete"),
            "incomplete qualification package");
    }

    std::vector<environment::CudaDeviceMetadata> cudaDevices;
    std::vector<environment::VulkanDeviceMetadata> vulkanDevices;
    if (needsCuda) cudaDevices = environment::EnumerateCudaDeviceMetadata();
    if (needsVulkan) vulkanDevices = environment::EnumerateVulkanDeviceMetadata();
    for (const auto& child : manifest.children)
    {
        if (child.configuration.backend == gate0::Backend::Cuda)
        {
            if (child.configuration.deviceIndex >= cudaDevices.size())
                throw std::invalid_argument("selected CUDA device index is unavailable");
            if (FormatUuid(cudaDevices[child.configuration.deviceIndex].uuid)
                != child.expectedGpuUuid)
                throw std::invalid_argument("selected CUDA device UUID disagrees with manifest");
        }
        else
        {
            if (child.configuration.deviceIndex >= vulkanDevices.size())
                throw std::invalid_argument("selected Vulkan device index is unavailable");
            if (FormatUuid(vulkanDevices[child.configuration.deviceIndex].uuid)
                != child.expectedGpuUuid)
                throw std::invalid_argument("selected Vulkan device UUID disagrees with manifest");
        }
    }

    const auto recordPath = Resolve(repositoryRoot, manifest.supervisorRecordPath);
    RejectCollision(recordPath, "supervisor record");
    RejectCollision(std::filesystem::path(recordPath.string() + ".incomplete"),
        "incomplete supervisor record");
    RejectCollision(std::filesystem::path(recordPath.string() + ".incomplete.tmp"),
        "temporary supervisor record");
}

int ExecuteManifest(
    supervisor::QualificationManifest manifest,
    const std::filesystem::path& repositoryRoot)
{
    Preflight(manifest, repositoryRoot);
    const auto executable = Resolve(repositoryRoot, manifest.childExecutablePath);
    const auto recordPath = Resolve(repositoryRoot, manifest.supervisorRecordPath);
    supervisor::SupervisorExecutionRecord record;
    record.manifest = manifest;
    record.startTimeUtc = TimestampUtc();
    record.endTimeUtc = "";
    supervisor::WriteExecutionRecord(recordPath, record, false);

    const auto campaignDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(manifest.campaignTimeoutMilliseconds);
    bool failed{};
    for (const auto& child : manifest.children)
    {
        supervisor::ChildExecutionRecord childRecord;
        childRecord.sequenceIndex = child.sequenceIndex;
        childRecord.runId = child.configuration.runId;
        childRecord.expectedOutputPath = child.declaredOutputPath;
        childRecord.launchTimeUtc = TimestampUtc();
        childRecord.progressValidation = "launch_pending";
        record.actualExecutions.push_back(childRecord);
        supervisor::WriteExecutionRecord(recordPath, record, false);
        try
        {
            const auto execution = supervisor::RunSupervisedProcess(
                executable,
                supervisor::ChildArguments(child),
                std::chrono::milliseconds(manifest.operationTimeoutMilliseconds),
                std::chrono::milliseconds(manifest.childTimeoutMilliseconds),
                campaignDeadline);
            auto& current = record.actualExecutions.back();
            current.launchTimeUtc = execution.launchTimeUtc;
            current.exitTimeUtc = execution.exitTimeUtc;
            current.exitCode = execution.exitCode;
            current.terminationReason = execution.terminationReason;
            current.operationStarts = execution.operationStarts;
            current.operationCompletions = execution.operationCompletions;
            current.progressValidation = execution.progressValidation;
            current.package = supervisor::InspectPackage(child, execution.exitCode);
            const auto expectedOperations = supervisor::ExpectedOperationCount(child);
            supervisor::ApplySupervisorAdmission(
                current.package, execution, expectedOperations);
            const bool childFailed = execution.exitCode != 0U
                || execution.terminationReason.has_value()
                || !current.package.admitted;
            failed |= childFailed;
            if (childFailed)
            {
                record.failureReason = "qualification child failed or was not admitted";
                record.overallExecutionStatus = "failed";
            }
            supervisor::WriteExecutionRecord(recordPath, record, false);
            if (childFailed && !manifest.continueAfterFatalFailure) break;
        }
        catch (const std::exception& error)
        {
            failed = true;
            auto& current = record.actualExecutions.back();
            current.exitTimeUtc = TimestampUtc();
            current.terminationReason = "supervisor_launch_or_wait_failure";
            current.progressValidation = "failed";
            current.package = supervisor::InspectPackage(child, std::nullopt);
            current.package.admissionReasons.push_back(
                "supervisor_launch_or_wait_failure:" + std::string(error.what()));
            record.failureReason = error.what();
            record.overallExecutionStatus = "failed";
            supervisor::WriteExecutionRecord(recordPath, record, false);
            break;
        }
    }
    record.endTimeUtc = TimestampUtc();
    if (!failed)
    {
        record.overallExecutionStatus = "qualification_execution_complete";
        record.failureReason.reset();
    }
    supervisor::WriteExecutionRecord(recordPath, record, true);
    return failed
        ? static_cast<int>(gate0::ExitCode::IncompleteOrTimeout)
        : static_cast<int>(gate0::ExitCode::Success);
}

void PrintUsage()
{
    std::cout
        << "ComputeLabEx2Gate0Supervisor --manifest <manifest.json>\n"
           "ComputeLabEx2Gate0Supervisor --print-manifest-hash <manifest.json>\n\n"
           "The supervisor executes only the frozen qualification entries in the manifest. "
           "It does not analyze measurements or issue a Gate-0 verdict.\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage();
        return 0;
    }
    if (argc != 3)
    {
        PrintUsage();
        return static_cast<int>(computelab::ex2::gate0::ExitCode::ConfigurationError);
    }
    try
    {
        const std::string json = ReadFile(argv[2]);
        if (std::string_view(argv[1]) == "--print-manifest-hash")
        {
            std::cout << supervisor::CalculateManifestSha256(json) << '\n';
            return 0;
        }
        if (std::string_view(argv[1]) != "--manifest")
            throw std::invalid_argument("unknown supervisor option");
        const std::filesystem::path repositoryRoot{COMPUTELAB_REPOSITORY_ROOT};
        return ExecuteManifest(
            supervisor::ParseManifest(json, repositoryRoot), repositoryRoot);
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "ComputeLabEx2Gate0Supervisor preflight rejection: "
            << error.what() << '\n';
        return static_cast<int>(computelab::ex2::gate0::ExitCode::ConfigurationError);
    }
    catch (const std::exception& error)
    {
        std::cerr << "ComputeLabEx2Gate0Supervisor failure: " << error.what() << '\n';
        return static_cast<int>(computelab::ex2::gate0::ExitCode::EvidenceOrProvenanceFailure);
    }
}
