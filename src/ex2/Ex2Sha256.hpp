#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace computelab::ex2
{

[[nodiscard]] std::string Sha256(std::span<const std::byte> bytes);
[[nodiscard]] std::string Sha256(std::string_view bytes);
[[nodiscard]] std::string Sha256File(const std::filesystem::path& path);

} // namespace computelab::ex2
