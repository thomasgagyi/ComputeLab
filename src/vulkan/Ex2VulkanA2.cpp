#include "vulkan/Ex2VulkanA2.hpp"

#include <utility>

namespace computelab::vulkan
{
namespace
{

[[noreturn]] void RethrowA2(const Ex2VulkanA1NativeError& error)
{
    throw Ex2VulkanA2NativeError{
        detail::ConvertEx2VulkanA1PhaseForA2(error.Phase()),
        error.NativeResult(),
        detail::RelabelEx2VulkanA1DiagnosticForA2(error.Operation()),
        detail::RelabelEx2VulkanA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::logic_error& error)
{
    throw std::logic_error{
        detail::RelabelEx2VulkanA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::invalid_argument& error)
{
    throw std::invalid_argument{
        detail::RelabelEx2VulkanA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::length_error& error)
{
    throw std::length_error{
        detail::RelabelEx2VulkanA1DiagnosticForA2(error.what())};
}

void ValidateA2Configuration(const ex2::LinearConfiguration& configuration)
{
    const auto semantic = ex2::MakeConfiguration(configuration);
    if (!ex2::ValidateSemanticConfiguration(semantic).IsValid())
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A2 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != ex2::LinearVariant::A2)
    {
        throw std::invalid_argument(
            "EX-2 Vulkan A2 operation requires the A2 linear variant");
    }
}

} // namespace

Ex2VulkanA2NativePhase detail::ConvertEx2VulkanA1PhaseForA2(
    Ex2VulkanA1NativePhase phase)
{
    using A1 = Ex2VulkanA1NativePhase;
    using A2 = Ex2VulkanA2NativePhase;
    switch (phase)
    {
    case A1::InstanceCreation: return A2::InstanceCreation;
    case A1::DeviceEnumeration: return A2::DeviceEnumeration;
    case A1::DeviceSelection: return A2::DeviceSelection;
    case A1::DeviceProperties: return A2::DeviceProperties;
    case A1::QueueSelection: return A2::QueueSelection;
    case A1::LogicalDeviceCreation: return A2::LogicalDeviceCreation;
    case A1::ResourcePreflight: return A2::ResourcePreflight;
    case A1::BufferCreation: return A2::BufferCreation;
    case A1::MemoryAllocation: return A2::MemoryAllocation;
    case A1::MemoryBinding: return A2::MemoryBinding;
    case A1::MemoryMapping: return A2::MemoryMapping;
    case A1::DescriptorCreation: return A2::DescriptorCreation;
    case A1::PipelineCreation: return A2::PipelineCreation;
    case A1::CommandCreation: return A2::CommandCreation;
    case A1::Upload: return A2::Upload;
    case A1::Preparation: return A2::Preparation;
    case A1::Submission: return A2::Submission;
    case A1::CompletionWait: return A2::CompletionWait;
    case A1::OutputReadback: return A2::OutputReadback;
    case A1::InputDiagnosticReadback: return A2::InputDiagnosticReadback;
    }
    throw std::invalid_argument("unknown EX-2 Vulkan A1 native phase");
}

Ex2VulkanA1NativePhase detail::ConvertEx2VulkanA2PhaseForA1(
    Ex2VulkanA2NativePhase phase)
{
    using A1 = Ex2VulkanA1NativePhase;
    using A2 = Ex2VulkanA2NativePhase;
    switch (phase)
    {
    case A2::InstanceCreation: return A1::InstanceCreation;
    case A2::DeviceEnumeration: return A1::DeviceEnumeration;
    case A2::DeviceSelection: return A1::DeviceSelection;
    case A2::DeviceProperties: return A1::DeviceProperties;
    case A2::QueueSelection: return A1::QueueSelection;
    case A2::LogicalDeviceCreation: return A1::LogicalDeviceCreation;
    case A2::ResourcePreflight: return A1::ResourcePreflight;
    case A2::BufferCreation: return A1::BufferCreation;
    case A2::MemoryAllocation: return A1::MemoryAllocation;
    case A2::MemoryBinding: return A1::MemoryBinding;
    case A2::MemoryMapping: return A1::MemoryMapping;
    case A2::DescriptorCreation: return A1::DescriptorCreation;
    case A2::PipelineCreation: return A1::PipelineCreation;
    case A2::CommandCreation: return A1::CommandCreation;
    case A2::Upload: return A1::Upload;
    case A2::Preparation: return A1::Preparation;
    case A2::Submission: return A1::Submission;
    case A2::CompletionWait: return A1::CompletionWait;
    case A2::OutputReadback: return A1::OutputReadback;
    case A2::InputDiagnosticReadback: return A1::InputDiagnosticReadback;
    }
    throw std::invalid_argument("unknown EX-2 Vulkan A2 native phase");
}

std::string detail::RelabelEx2VulkanA1DiagnosticForA2(std::string text)
{
    constexpr std::string_view from{"EX-2 Vulkan A1"};
    constexpr std::string_view to{"EX-2 Vulkan A2"};
    for (std::size_t offset = 0U;
        (offset = text.find(from, offset)) != std::string::npos;
        offset += to.size())
    {
        text.replace(offset, from.size(), to);
    }
    return text;
}

std::string_view ToString(Ex2VulkanA2NativePhase phase) noexcept
{
    try
    {
        return ToString(detail::ConvertEx2VulkanA2PhaseForA1(phase));
    }
    catch (...)
    {
        return "invalid";
    }
}

Ex2VulkanA2NativeError::Ex2VulkanA2NativeError(
    Ex2VulkanA2NativePhase phase,
    VkResult nativeResult,
    std::string operation,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeResult_{nativeResult},
      operation_{std::move(operation)}
{
}

Ex2VulkanA2NativePhase Ex2VulkanA2NativeError::Phase() const noexcept
{
    return phase_;
}

VkResult Ex2VulkanA2NativeError::NativeResult() const noexcept
{
    return nativeResult_;
}

const std::string& Ex2VulkanA2NativeError::Operation() const noexcept
{
    return operation_;
}

detail::Ex2VulkanA2DispatchShape detail::ValidateEx2VulkanA2DispatchShape(
    const ex2::LinearConfiguration& configuration,
    const VkPhysicalDeviceLimits& limits,
    VkDeviceSize maximumBufferSize)
{
    ValidateA2Configuration(configuration);
    auto shared = configuration;
    shared.variant = ex2::LinearVariant::A1;
    const auto shape = ValidateEx2VulkanA1DispatchShape(
        shared, limits, maximumBufferSize);
    return {shape.groupCountX, shape.localSizeX};
}

VkDeviceSize detail::CalculateEx2VulkanA2BufferByteCount(
    std::uint64_t elementCount,
    VkDeviceSize maximumAddressableByteCount)
{
    return CalculateEx2VulkanA1BufferByteCount(
        elementCount, maximumAddressableByteCount);
}

std::uint32_t detail::SelectEx2VulkanA2Queue(
    std::span<const VkQueueFamilyProperties> families)
{
    return SelectEx2VulkanA1Queue(families);
}

std::uint32_t detail::SelectEx2VulkanA2MemoryType(
    std::uint32_t compatibleTypeBits,
    VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred,
    const VkPhysicalDeviceMemoryProperties& properties)
{
    return SelectEx2VulkanA1MemoryType(
        compatibleTypeBits, required, preferred, properties);
}

detail::Ex2VulkanA2MappedRange detail::AlignEx2VulkanA2NoncoherentRange(
    VkDeviceSize offset,
    VkDeviceSize size,
    VkDeviceSize allocationSize,
    VkDeviceSize nonCoherentAtomSize)
{
    return AlignEx2VulkanA1NoncoherentRange(
        offset, size, allocationSize, nonCoherentAtomSize);
}

Ex2VulkanA2Operation::Ex2VulkanA2Operation(
    const ex2::LinearConfiguration& configuration,
    const std::filesystem::path& spirvPath,
    std::uint32_t physicalDeviceIndex)
try
    : operation_{new Ex2VulkanA1Operation(
          configuration,
          spirvPath,
          physicalDeviceIndex,
          ex2::LinearVariant::A2)}
{
    ValidateA2Configuration(configuration);
}
catch (const Ex2VulkanA1NativeError& error)
{
    RethrowA2(error);
}
catch (const std::invalid_argument& error)
{
    RethrowA2(error);
}
catch (const std::length_error& error)
{
    RethrowA2(error);
}
catch (const std::logic_error& error)
{
    RethrowA2(error);
}

Ex2VulkanA2Operation::~Ex2VulkanA2Operation() noexcept = default;

std::uint64_t Ex2VulkanA2Operation::ElementCount() const noexcept
{
    return operation_->ElementCount();
}

const std::array<std::uint8_t, VK_UUID_SIZE>&
Ex2VulkanA2Operation::SelectedDeviceUuid() const noexcept
{
    return operation_->SelectedDeviceUuid();
}

const Ex2VulkanA2Diagnostics&
Ex2VulkanA2Operation::Diagnostics() const noexcept
{
    return operation_->Diagnostics();
}

std::span<const std::uint32_t>
Ex2VulkanA2Operation::LoadedSpirv() const noexcept
{
    return operation_->LoadedSpirv();
}

void Ex2VulkanA2Operation::Upload(std::span<const std::uint32_t> input)
{
    try { operation_->Upload(input); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::invalid_argument& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

void Ex2VulkanA2Operation::Prepare()
{
    try { operation_->Prepare(); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

void Ex2VulkanA2Operation::SubmitA2()
{
    try { operation_->SubmitA1(); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

void Ex2VulkanA2Operation::WaitForCompletion(
    std::uint64_t timeoutNanoseconds)
{
    try { operation_->WaitForCompletion(timeoutNanoseconds); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

std::vector<std::uint32_t> Ex2VulkanA2Operation::RetrieveOutput()
{
    try { return operation_->RetrieveOutput(); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

std::vector<std::uint32_t> Ex2VulkanA2Operation::RetrieveDeviceInput()
{
    try { return operation_->RetrieveDeviceInput(); }
    catch (const Ex2VulkanA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

bool Ex2VulkanA2Operation::LastCompletionExecutedShader() const
{
    try { return operation_->LastCompletionExecutedShader(); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

} // namespace computelab::vulkan
