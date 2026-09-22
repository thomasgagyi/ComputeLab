#pragma once

#include "app/Ex2A1Integration.hpp"
#include "cuda/Ex2CudaA2.hpp"
#include "vulkan/Ex2VulkanA2.hpp"

namespace computelab::ex2::a2
{

using a1::BackendObservation;
using a1::BufferValidation;
using a1::CrossBackendObservation;
using a1::DeviceUuid;
using a1::EvidenceBuildContext;
using a1::FailedSessionResourceDisposition;
using a1::FailureClassification;
using a1::SerializedEvidencePair;
using a1::SerializedSeries;
using a1::BuildEvidence;
using a1::ClassifyFailedSessionResourceDisposition;
using a1::CompletedObservation;
using a1::FormatDeviceUuid;
using a1::IsZeroDeviceUuid;
using a1::ReconcileCrossBackendOutputs;
using a1::ValidateBuffers;
using a1::VerifySamePhysicalDevice;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaA2NativePhase phase,
    int nativeErrorCode);
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanA2NativePhase phase,
    VkResult nativeResult);

[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& spirvPath);

} // namespace computelab::ex2::a2
