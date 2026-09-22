#include "vulkan/Ex2VulkanA2.hpp"

#include <utility>

namespace computelab::vulkan
{
namespace
{

Ex2VulkanA2NativePhase ConvertPhase(
    Ex2VulkanA1NativePhase phase) noexcept
{
    return static_cast<Ex2VulkanA2NativePhase>(phase);
}

std::string A2Text(std::string text)
{
    for (std::size_t offset = 0U;
        (offset = text.find("A1", offset)) != std::string::npos;
        offset += 2U)
    {
        text.replace(offset, 2U, "A2");
    }
    return text;
}

[[noreturn]] void RethrowA2(const Ex2VulkanA1NativeError& error)
{
    throw Ex2VulkanA2NativeError{
        ConvertPhase(error.Phase()),
        error.NativeResult(),
        A2Text(error.Operation()),
        A2Text(error.what())};
}

[[noreturn]] void RethrowA2(const std::logic_error& error)
{
    throw std::logic_error{A2Text(error.what())};
}

[[noreturn]] void RethrowA2(const std::invalid_argument& error)
{
    throw std::invalid_argument{A2Text(error.what())};
}

[[noreturn]] void RethrowA2(const std::length_error& error)
{
    throw std::length_error{A2Text(error.what())};
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

std::string_view ToString(Ex2VulkanA2NativePhase phase) noexcept
{
    return ToString(static_cast<Ex2VulkanA1NativePhase>(phase));
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
