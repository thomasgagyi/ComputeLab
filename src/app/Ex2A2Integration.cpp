#include "app/Ex2A2Integration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::a2
{
namespace
{

constexpr std::array<std::uint32_t, 4> kLiteralInput{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4> kLiteralExpected{
    0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U};

void VerifyLiteralFixture(
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    const std::size_t applicable = std::min(input.size(), kLiteralInput.size());
    if (expected.size() < applicable
        || !std::equal(input.begin(), input.begin() + applicable,
            kLiteralInput.begin())
        || !std::equal(expected.begin(), expected.begin() + applicable,
            kLiteralExpected.begin()))
    {
        throw std::runtime_error(
            "EX-2 A2 generated input or CPU oracle disagrees with the literal fixture");
    }
}

BackendObservation FailureObservation(
    const FailureClassification& failure,
    bool operationCompleted,
    bool outputObserved)
{
    BackendObservation result;
    result.operationCompleted = operationCompleted;
    result.outputObserved = outputObserved;
    result.status = failure.status;
    result.failurePhase = failure.phase;
    result.errorCode = failure.errorCode;
    result.safeForFurtherGpuCalls = false;
    return result;
}

BackendObservation InterruptedObservation()
{
    return FailureObservation(
        {evidence::OperationStatus::Incomplete,
            evidence::FailurePhase::Interrupted,
            std::string(evidence::error_code::Interrupted)},
        false,
        false);
}

BackendObservation ExecuteCuda(
    cuda::Ex2CudaA2Operation& operation,
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    bool completed = false;
    bool outputObserved = false;
    std::vector<std::uint32_t> output;
    try
    {
        operation.Upload(input);
        operation.SubmitA2();
        operation.WaitForCompletion();
        completed = true;
        output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        return CompletedObservation(
            expected,
            std::move(output),
            input,
            std::move(deviceInput),
            operation.LastCompletionExecutedKernel());
    }
    catch (const cuda::Ex2CudaA2NativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {evidence::OperationStatus::Incomplete,
                evidence::FailurePhase::ResourceAllocation,
                std::string(evidence::error_code::ResourceAllocationFailed)},
            completed,
            outputObserved);
    }
}

BackendObservation ExecuteVulkan(
    vulkan::Ex2VulkanA2Operation& operation,
    std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    bool completed = false;
    bool outputObserved = false;
    std::vector<std::uint32_t> output;
    try
    {
        operation.Upload(input);
        operation.Prepare();
        operation.SubmitA2();
        operation.WaitForCompletion();
        completed = true;
        output = operation.RetrieveOutput();
        outputObserved = true;
        auto deviceInput = operation.RetrieveDeviceInput();
        return CompletedObservation(
            expected,
            std::move(output),
            input,
            std::move(deviceInput),
            operation.LastCompletionExecutedShader());
    }
    catch (const vulkan::Ex2VulkanA2NativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            completed,
            outputObserved);
    }
    catch (const std::bad_alloc&)
    {
        return FailureObservation(
            {evidence::OperationStatus::Incomplete,
                evidence::FailurePhase::ResourceAllocation,
                std::string(evidence::error_code::ResourceAllocationFailed)},
            completed,
            outputObserved);
    }
}

} // namespace

FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaA2NativePhase phase,
    int nativeErrorCode)
{
    return a1::ClassifyCudaFailure(
        cuda::detail::ConvertEx2CudaA2PhaseForA1(phase), nativeErrorCode);
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanA2NativePhase phase,
    VkResult nativeResult)
{
    return a1::ClassifyVulkanFailure(
        vulkan::detail::ConvertEx2VulkanA2PhaseForA1(phase), nativeResult);
}

CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& spirvPath)
{
    if (seed != CoreInputSeed)
        throw std::invalid_argument(
            "EX-2 A2 minimal correctness path requires the approved core seed");
    const LinearConfiguration linear{LinearVariant::A2, elementCount};
    const WorkloadConfiguration configuration = MakeConfiguration(linear, seed);
    if (!ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration))
    {
        throw std::invalid_argument(
            "EX-2 A2 request is not eligible for correctness execution");
    }

    auto cudaOperation = std::make_unique<cuda::Ex2CudaA2Operation>(
        cudaDeviceOrdinal, linear);
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanA2Operation>(
        linear, spirvPath, vulkanPhysicalDeviceIndex);
    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const std::string uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);

    auto input = GenerateWordInput(seed, elementCount);
    auto expected = ReferenceA2(input);
    VerifyLiteralFixture(input, expected);

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.inputSha256 = WordInputSha256(input);
    result.expectedOutputSha256 = WordInputSha256(expected);
    result.vulkanShaderSha256 = Sha256(
        std::as_bytes(vulkanOperation->LoadedSpirv()));
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    result.input = std::move(input);
    result.expectedOutput = std::move(expected);

    result.cuda = ExecuteCuda(
        *cudaOperation, result.input, result.expectedOutput);
    if (result.cuda.status != evidence::OperationStatus::Ok)
    {
        result.vulkan = InterruptedObservation();
        if (ClassifyFailedSessionResourceDisposition(result.cuda)
            == FailedSessionResourceDisposition::PreserveForProcessTeardown)
        {
            static_cast<void>(vulkanOperation.release());
            static_cast<void>(cudaOperation.release());
        }
        return result;
    }

    result.vulkan = ExecuteVulkan(
        *vulkanOperation, result.input, result.expectedOutput);
    ReconcileCrossBackendOutputs(result);
    if (!result.vulkan.safeForFurtherGpuCalls)
    {
        static_cast<void>(vulkanOperation.release());
        static_cast<void>(cudaOperation.release());
    }
    return result;
}

} // namespace computelab::ex2::a2
