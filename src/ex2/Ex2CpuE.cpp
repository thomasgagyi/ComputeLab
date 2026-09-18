#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace computelab::ex2
{
namespace
{

bool IsValidDirection(TransferDirection direction) noexcept
{
    return direction == TransferDirection::HostToDevice
        || direction == TransferDirection::DeviceToHost;
}

} // namespace

TransferReference ReferenceTransfer(
    std::uint64_t seed,
    std::uint64_t byteCount,
    TransferDirection direction)
{
    if (!IsValidDirection(direction))
    {
        throw std::invalid_argument("EX-2 E transfer direction is invalid");
    }
    auto source = GenerateByteInput(seed, byteCount);
    return TransferReference{direction, source, std::move(source)};
}

bool ValidateTransferOutput(
    const TransferReference& reference,
    std::span<const std::uint8_t> actualDestination) noexcept
{
    return IsValidDirection(reference.direction)
        && reference.source.size() == reference.expectedDestination.size()
        && actualDestination.size() == reference.expectedDestination.size()
        && std::equal(reference.source.begin(), reference.source.end(),
            reference.expectedDestination.begin())
        && std::equal(actualDestination.begin(), actualDestination.end(),
            reference.expectedDestination.begin());
}

} // namespace computelab::ex2
