#include "cuda/Ex2CudaA2.hpp"

#include <utility>

namespace computelab::cuda
{
namespace
{

[[noreturn]] void RethrowA2(const Ex2CudaA1NativeError& error)
{
    throw Ex2CudaA2NativeError{
        detail::ConvertEx2CudaA1PhaseForA2(error.Phase()),
        error.NativeErrorCode(),
        error.NativeErrorName(),
        detail::RelabelEx2CudaA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::logic_error& error)
{
    throw std::logic_error{
        detail::RelabelEx2CudaA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::invalid_argument& error)
{
    throw std::invalid_argument{
        detail::RelabelEx2CudaA1DiagnosticForA2(error.what())};
}

[[noreturn]] void RethrowA2(const std::length_error& error)
{
    throw std::length_error{
        detail::RelabelEx2CudaA1DiagnosticForA2(error.what())};
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

Ex2CudaA2NativePhase detail::ConvertEx2CudaA1PhaseForA2(
    Ex2CudaA1NativePhase phase)
{
    using A1 = Ex2CudaA1NativePhase;
    using A2 = Ex2CudaA2NativePhase;
    switch (phase)
    {
    case A1::DeviceSelection: return A2::DeviceSelection;
    case A1::RuntimeInitialization: return A2::RuntimeInitialization;
    case A1::DeviceProperties: return A2::DeviceProperties;
    case A1::ResourcePreflight: return A2::ResourcePreflight;
    case A1::StreamCreation: return A2::StreamCreation;
    case A1::InputAllocation: return A2::InputAllocation;
    case A1::OutputAllocation: return A2::OutputAllocation;
    case A1::Upload: return A2::Upload;
    case A1::UploadCompletion: return A2::UploadCompletion;
    case A1::Submission: return A2::Submission;
    case A1::CompletionWait: return A2::CompletionWait;
    case A1::OutputReadback: return A2::OutputReadback;
    case A1::InputDiagnosticReadback: return A2::InputDiagnosticReadback;
    }
    throw std::invalid_argument("unknown EX-2 CUDA A1 native phase");
}

Ex2CudaA1NativePhase detail::ConvertEx2CudaA2PhaseForA1(
    Ex2CudaA2NativePhase phase)
{
    using A1 = Ex2CudaA1NativePhase;
    using A2 = Ex2CudaA2NativePhase;
    switch (phase)
    {
    case A2::DeviceSelection: return A1::DeviceSelection;
    case A2::RuntimeInitialization: return A1::RuntimeInitialization;
    case A2::DeviceProperties: return A1::DeviceProperties;
    case A2::ResourcePreflight: return A1::ResourcePreflight;
    case A2::StreamCreation: return A1::StreamCreation;
    case A2::InputAllocation: return A1::InputAllocation;
    case A2::OutputAllocation: return A1::OutputAllocation;
    case A2::Upload: return A1::Upload;
    case A2::UploadCompletion: return A1::UploadCompletion;
    case A2::Submission: return A1::Submission;
    case A2::CompletionWait: return A1::CompletionWait;
    case A2::OutputReadback: return A1::OutputReadback;
    case A2::InputDiagnosticReadback: return A1::InputDiagnosticReadback;
    }
    throw std::invalid_argument("unknown EX-2 CUDA A2 native phase");
}

std::string detail::RelabelEx2CudaA1DiagnosticForA2(std::string text)
{
    constexpr std::string_view from{"EX-2 CUDA A1"};
    constexpr std::string_view to{"EX-2 CUDA A2"};
    for (std::size_t offset = 0U;
        (offset = text.find(from, offset)) != std::string::npos;
        offset += to.size())
    {
        text.replace(offset, from.size(), to);
    }
    return text;
}

std::string_view ToString(Ex2CudaA2NativePhase phase) noexcept
{
    try
    {
        return ToString(detail::ConvertEx2CudaA2PhaseForA1(phase));
    }
    catch (...)
    {
        return "invalid";
    }
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
