#include "cuda/Ex2CudaA2.hpp"

#include <utility>

namespace computelab::cuda
{
namespace
{

Ex2CudaA2NativePhase ConvertPhase(Ex2CudaA1NativePhase phase) noexcept
{
    return static_cast<Ex2CudaA2NativePhase>(phase);
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

[[noreturn]] void RethrowA2(const Ex2CudaA1NativeError& error)
{
    throw Ex2CudaA2NativeError{
        ConvertPhase(error.Phase()),
        error.NativeErrorCode(),
        error.NativeErrorName(),
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
            "EX-2 CUDA A2 configuration violates the I2-C semantic contract");
    }
    if (configuration.variant != ex2::LinearVariant::A2)
    {
        throw std::invalid_argument(
            "EX-2 CUDA A2 operation requires the A2 linear variant");
    }
}

} // namespace

std::string_view ToString(Ex2CudaA2NativePhase phase) noexcept
{
    return ToString(static_cast<Ex2CudaA1NativePhase>(phase));
}

Ex2CudaA2NativeError::Ex2CudaA2NativeError(
    Ex2CudaA2NativePhase phase,
    int nativeErrorCode,
    std::string nativeErrorName,
    std::string message)
    : std::runtime_error{std::move(message)},
      phase_{phase},
      nativeErrorCode_{nativeErrorCode},
      nativeErrorName_{std::move(nativeErrorName)}
{
}

Ex2CudaA2NativePhase Ex2CudaA2NativeError::Phase() const noexcept
{
    return phase_;
}

int Ex2CudaA2NativeError::NativeErrorCode() const noexcept
{
    return nativeErrorCode_;
}

const std::string& Ex2CudaA2NativeError::NativeErrorName() const noexcept
{
    return nativeErrorName_;
}

detail::Ex2CudaA2LaunchShape detail::ValidateEx2CudaA2LaunchShape(
    const ex2::LinearConfiguration& configuration,
    std::uint64_t maximumGridDimensionX,
    std::uint32_t maximumThreadsPerBlock,
    std::uint32_t maximumThreadsDimensionX)
{
    ValidateA2Configuration(configuration);
    auto shared = configuration;
    shared.variant = ex2::LinearVariant::A1;
    const auto shape = ValidateEx2CudaA1LaunchShape(
        shared,
        maximumGridDimensionX,
        maximumThreadsPerBlock,
        maximumThreadsDimensionX);
    return {shape.blockCount, shape.threadsPerBlock};
}

std::size_t detail::CalculateEx2CudaA2BufferByteCount(
    std::uint64_t elementCount,
    std::size_t maximumAddressableByteCount)
{
    return CalculateEx2CudaA1BufferByteCount(
        elementCount, maximumAddressableByteCount);
}

void detail::ValidateEx2CudaA2MemoryFeasibility(
    std::size_t singleBufferByteCount,
    std::size_t totalGlobalMemoryBytes,
    std::size_t freeGlobalMemoryBytes)
{
    ValidateEx2CudaA1MemoryFeasibility(
        singleBufferByteCount,
        totalGlobalMemoryBytes,
        freeGlobalMemoryBytes);
}

Ex2CudaA2Operation::Ex2CudaA2Operation(
    int deviceOrdinal,
    const ex2::LinearConfiguration& configuration)
try
    : operation_{new Ex2CudaA1Operation(
          deviceOrdinal, configuration, ex2::LinearVariant::A2)}
{
    ValidateA2Configuration(configuration);
}
catch (const Ex2CudaA1NativeError& error)
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

Ex2CudaA2Operation::~Ex2CudaA2Operation() noexcept = default;

int Ex2CudaA2Operation::SelectedDeviceOrdinal() const noexcept
{
    return operation_->SelectedDeviceOrdinal();
}

const std::array<std::uint8_t, 16>&
Ex2CudaA2Operation::SelectedDeviceUuid() const noexcept
{
    return operation_->SelectedDeviceUuid();
}

std::uint64_t Ex2CudaA2Operation::ElementCount() const noexcept
{
    return operation_->ElementCount();
}

void Ex2CudaA2Operation::Upload(std::span<const std::uint32_t> input)
{
    try { operation_->Upload(input); }
    catch (const Ex2CudaA1NativeError& error) { RethrowA2(error); }
    catch (const std::invalid_argument& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

void Ex2CudaA2Operation::SubmitA2()
{
    try { operation_->SubmitA1(); }
    catch (const Ex2CudaA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

void Ex2CudaA2Operation::WaitForCompletion()
{
    try { operation_->WaitForCompletion(); }
    catch (const Ex2CudaA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

std::vector<std::uint32_t> Ex2CudaA2Operation::RetrieveOutput()
{
    try { return operation_->RetrieveOutput(); }
    catch (const Ex2CudaA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

std::vector<std::uint32_t> Ex2CudaA2Operation::RetrieveDeviceInput()
{
    try { return operation_->RetrieveDeviceInput(); }
    catch (const Ex2CudaA1NativeError& error) { RethrowA2(error); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

bool Ex2CudaA2Operation::LastCompletionExecutedKernel() const
{
    try { return operation_->LastCompletionExecutedKernel(); }
    catch (const std::logic_error& error) { RethrowA2(error); }
}

} // namespace computelab::cuda
