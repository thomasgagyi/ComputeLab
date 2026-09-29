#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace computelab::ex2
{

// Narrow incremental interface for canonical encodings and file hashing.
// Finish consumes the state; Update or a second Finish then throws logic_error.
class Sha256Hasher final
{
public:
    Sha256Hasher();
    ~Sha256Hasher();

    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    Sha256Hasher(Sha256Hasher&&) noexcept;
    Sha256Hasher& operator=(Sha256Hasher&&) noexcept;

    void Update(std::span<const std::byte> bytes);
    [[nodiscard]] std::string Finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string Sha256(std::span<const std::byte> bytes);
[[nodiscard]] std::string Sha256(std::string_view bytes);
[[nodiscard]] std::string Sha256File(const std::filesystem::path& path);

} // namespace computelab::ex2
