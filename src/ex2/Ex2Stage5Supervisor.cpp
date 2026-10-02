#include "ex2/Ex2Stage5Supervisor.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include "ex2/Ex2Sha256.hpp"
#include "environment/EnvironmentCollector.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace computelab::ex2::stage5::control
{
namespace
{
// Narrow parsing and Windows ownership mechanics adapted from I7/G0.
// Historical source/contracts remain independently owned and unchanged.
struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue
{
    using Storage = std::variant<std::nullptr_t, bool, std::uint64_t, std::int64_t, double,
        std::string, JsonArray, JsonObject>;
    Storage storage;
};

[[noreturn]] void InvalidManifest(std::string_view detail)
{
    throw std::invalid_argument(
        "invalid EX-2 Stage-5 control: " + std::string(detail));
}

bool IsIdentifierCharacter(char value) noexcept
{
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '.' || value == '_' || value == '-';
}

void RequireIdentifier(std::string_view value, std::string_view field)
{
    if (value.empty() || value.size() > 128U || value == "." || value == ".."
        || !std::all_of(value.begin(), value.end(), IsIdentifierCharacter))
    {
        InvalidManifest(std::string(field)
            + " must be a nonempty anonymous ASCII identifier");
    }
}

bool IsLowerHex(std::string_view value, std::size_t size) noexcept
{
    return value.size() == size
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

bool IsUuid(std::string_view value) noexcept
{
    if (value.size() != 36U) return false;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            if (value[index] != '-') return false;
        }
        else if (!((value[index] >= '0' && value[index] <= '9')
            || (value[index] >= 'a' && value[index] <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool ValidUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]); if (first < 0x80U) continue;
        const unsigned count = first >= 0xC2U && first <= 0xDFU ? 1U : first >= 0xE0U && first <= 0xEFU ? 2U
            : first >= 0xF0U && first <= 0xF4U ? 3U : 0U;
        if (!count || i + count > text.size()) return false;
        const auto next = static_cast<unsigned char>(text[i]);
        if ((first == 0xE0U && next < 0xA0U) || (first == 0xEDU && next >= 0xA0U)
            || (first == 0xF0U && next < 0x90U) || (first == 0xF4U && next > 0x8FU)) return false;
        for (unsigned n = 0; n < count; ++n)
        { const auto c = static_cast<unsigned char>(text[i++]); if (c < 0x80U || c > 0xBFU) return false; }
    }
    return true;
}
class JsonParser final
{
public:
    explicit JsonParser(std::string_view input) : input_{input} {}

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (position_ != input_.size()) Fail("trailing content");
        return value;
    }

private:
    [[noreturn]] void Fail(std::string_view detail) const
    {
        InvalidManifest("JSON parse error at byte " + std::to_string(position_)
            + ": " + std::string(detail));
    }

    void SkipWhitespace()
    {
        while (position_ < input_.size()
            && (input_[position_] == ' ' || input_[position_] == '\t'
                || input_[position_] == '\r' || input_[position_] == '\n'))
        {
            ++position_;
        }
    }

    bool Consume(char value)
    {
        SkipWhitespace();
        if (position_ < input_.size() && input_[position_] == value)
        {
            ++position_;
            return true;
        }
        return false;
    }

    void RequireLiteral(std::string_view literal)
    {
        if (input_.substr(position_, literal.size()) != literal)
            Fail("invalid literal");
        position_ += literal.size();
    }

    static unsigned HexDigit(char value)
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10U;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10U;
        return 16U;
    }

    void AppendCodePoint(std::string& output, std::uint32_t value)
    {
        if (value <= 0x7FU) output.push_back(static_cast<char>(value));
        else if (value <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
    }

    std::string ParseString()
    {
        SkipWhitespace();
        if (position_ >= input_.size() || input_[position_] != '"')
            Fail("expected string");
        ++position_;
        std::string output;
        while (position_ < input_.size())
        {
            const unsigned char character = input_[position_++];
            if (character == '"') { if (!ValidUtf8(output)) Fail("invalid UTF-8 string"); return output; }
            if (character < 0x20U) Fail("unescaped control character");
            if (character != '\\')
            {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) Fail("truncated escape");
            const char escaped = input_[position_++];
            switch (escaped)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                if (position_ + 4U > input_.size()) Fail("truncated Unicode escape");
                std::uint32_t value{};
                for (unsigned index = 0; index < 4U; ++index)
                {
                    const unsigned digit = HexDigit(input_[position_++]);
                    if (digit > 15U) Fail("invalid Unicode escape");
                    value = (value << 4U) | digit;
                }
                if (value >= 0xD800U && value <= 0xDFFFU)
                    Fail("surrogate Unicode escapes are not accepted");
                AppendCodePoint(output, value);
                break;
            }
            default: Fail("invalid escape");
            }
        }
        Fail("unterminated string");
    }

    JsonValue ParseNumber()
    {
        SkipWhitespace();
        const std::size_t start = position_;
        bool negative{};
        if (position_ < input_.size() && input_[position_] == '-')
        {
            negative = true;
            ++position_;
        }
        if (position_ >= input_.size() || input_[position_] < '0'
            || input_[position_] > '9')
            Fail("expected number");
        if (input_[position_] == '0' && position_ + 1U < input_.size()
            && input_[position_ + 1U] >= '0' && input_[position_ + 1U] <= '9')
            Fail("leading zero in number");
        while (position_ < input_.size()
            && input_[position_] >= '0' && input_[position_] <= '9')
            ++position_;
        bool floating{};
        if (position_ < input_.size() && input_[position_] == '.')
        {
            floating = true;
            ++position_;
            const std::size_t fractionStart = position_;
            while (position_ < input_.size()
                && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (position_ == fractionStart) Fail("fraction has no digits");
        }
        if (position_ < input_.size()
            && (input_[position_] == 'e' || input_[position_] == 'E'))
        {
            floating = true;
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-'))
                ++position_;
            const std::size_t exponentStart = position_;
            while (position_ < input_.size()
                && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
            if (position_ == exponentStart) Fail("exponent has no digits");
        }
        if (!negative && !floating)
        {
            std::uint64_t value{};
            const auto [end, error] = std::from_chars(
                input_.data() + start, input_.data() + position_, value);
            if (error != std::errc{} || end != input_.data() + position_)
                Fail("integer is outside uint64 range");
            return {{value}};
        }
        if (negative && !floating)
        {
            std::int64_t value{};
            const auto [end, error] = std::from_chars(input_.data() + start, input_.data() + position_, value);
            if (error != std::errc{} || end != input_.data() + position_) Fail("integer is outside int64 range");
            return {{value}};
        }
        double value{};
        const auto [end, error] = std::from_chars(
            input_.data() + start, input_.data() + position_, value,
            std::chars_format::general);
        if (error != std::errc{} || end != input_.data() + position_
            || !std::isfinite(value))
            Fail("number is outside finite binary64 range");
        return {{value}};
    }

    JsonArray ParseArray()
    {
        if (!Consume('[')) Fail("expected array");
        JsonArray output;
        if (Consume(']')) return output;
        for (;;)
        {
            output.push_back(ParseValue());
            if (Consume(']')) return output;
            if (!Consume(',')) Fail("expected comma in array");
        }
    }

    JsonObject ParseObject()
    {
        if (!Consume('{')) Fail("expected object");
        JsonObject output;
        if (Consume('}')) return output;
        for (;;)
        {
            const std::string key = ParseString();
            if (!Consume(':')) Fail("expected colon in object");
            if (!output.emplace(key, ParseValue()).second)
                Fail("duplicate object member");
            if (Consume('}')) return output;
            if (!Consume(',')) Fail("expected comma in object");
        }
    }

    JsonValue ParseValue()
    {
        if (++depth_ > 32U) Fail("JSON nesting exceeds control bound");
        struct Depth { std::size_t& value; ~Depth() { --value; } } depth{depth_};
        SkipWhitespace();
        if (position_ >= input_.size()) Fail("expected value");
        switch (input_[position_])
        {
        case 'n': RequireLiteral("null"); return {{nullptr}};
        case 't': RequireLiteral("true"); return {{true}};
        case 'f': RequireLiteral("false"); return {{false}};
        case '"': return {{ParseString()}};
        case '[': return {{ParseArray()}};
        case '{': return {{ParseObject()}};
        default: return ParseNumber();
        }
    }

    std::string_view input_;
    std::size_t position_{};
    std::size_t depth_{};
};

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            }
            else output.push_back(static_cast<char>(character));
            break;
        }
    }
    output.push_back('"');
}

void AppendUnsigned(std::string& output, std::uint64_t value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) throw std::runtime_error("integer serialization failed");
    output.append(buffer.data(), end);
}

void AppendCanonicalJson(std::string& output, const JsonValue& value)
{
    std::visit([&output](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) output += "null";
        else if constexpr (std::is_same_v<T, bool>) output += item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::uint64_t>) AppendUnsigned(output, item);
        else if constexpr (std::is_same_v<T, std::int64_t>)
        {
            std::array<char, 32> buffer{}; const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), item);
            if (error != std::errc{}) throw std::runtime_error("signed integer serialization failed"); output.append(buffer.data(), end);
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            std::array<char, 32> buffer{};
            const auto [end, error] = std::to_chars(
                buffer.data(), buffer.data() + buffer.size(), item);
            if (error != std::errc{})
                throw std::runtime_error("binary64 serialization failed");
            output.append(buffer.data(), end);
        }
        else if constexpr (std::is_same_v<T, std::string>) AppendJsonString(output, item);
        else if constexpr (std::is_same_v<T, JsonArray>)
        {
            output.push_back('[');
            for (std::size_t index = 0; index < item.size(); ++index)
            {
                if (index != 0U) output.push_back(',');
                AppendCanonicalJson(output, item[index]);
            }
            output.push_back(']');
        }
        else
        {
            output.push_back('{');
            std::size_t index{};
            for (const auto& [key, child] : item)
            {
                if (index++ != 0U) output.push_back(',');
                AppendJsonString(output, key);
                output.push_back(':');
                AppendCanonicalJson(output, child);
            }
            output.push_back('}');
        }
    }, value.storage);
}

template <typename T>
const T& RequireType(
    const JsonValue& value, std::string_view field)
{
    const T* result = std::get_if<T>(&value.storage);
    if (result == nullptr)
        InvalidManifest(std::string(field) + " has the wrong JSON type");
    return *result;
}

const JsonValue& RequireMember(
    const JsonObject& object, std::string_view field)
{
    const auto found = object.find(field);
    if (found == object.end())
        InvalidManifest("missing required field " + std::string(field));
    return found->second;
}

std::string RequireString(const JsonObject& object, std::string_view field)
{
    return RequireType<std::string>(RequireMember(object, field), field);
}

std::uint64_t RequireUnsigned(const JsonObject& object, std::string_view field)
{
    return RequireType<std::uint64_t>(RequireMember(object, field), field);
}

bool RequireBoolean(const JsonObject& object, std::string_view field)
{
    return RequireType<bool>(RequireMember(object, field), field);
}

void RequireExactKeys(
    const JsonObject& object,
    std::initializer_list<std::string_view> keys,
    std::string_view context = "record")
{
    std::set<std::string, std::less<>> expected;
    for (const auto key : keys) expected.emplace(key);
    if (object.size() != expected.size())
        InvalidManifest(std::string(context) + " contains missing or unknown fields");
    for (const auto& [key, unused] : object)
    {
        static_cast<void>(unused);
        if (!expected.contains(key))
            InvalidManifest(std::string(context) + " contains unknown field " + key);
    }
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
    std::string result{buffer.data()};
    result.push_back('.');
    const auto count = milliseconds.count();
    result.push_back(static_cast<char>('0' + (count / 100) % 10));
    result.push_back(static_cast<char>('0' + (count / 10) % 10));
    result.push_back(static_cast<char>('0' + count % 10));
    result.push_back('Z');
    return result;
}


std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::invalid_argument("argument is not valid UTF-8");
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), output.data(), size) != size)
        throw std::runtime_error("UTF-8 argument conversion failed");
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

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_{handle} {}
    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
                CloseHandle(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }

private:
    HANDLE handle_{};
};

void WriteDurableFile(const std::filesystem::path& path, std::string_view contents)
{
    const UniqueHandle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.Get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("unable to create supervisor execution record");
    std::size_t offset{};
    while (offset < contents.size())
    {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            contents.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(file.Get(), contents.data() + offset, chunk, &written, nullptr)
            || written != chunk)
            throw std::runtime_error("unable to write supervisor execution record");
        offset += written;
    }
    if (!FlushFileBuffers(file.Get()))
        throw std::runtime_error("unable to flush supervisor execution record");
}


std::vector<std::vector<std::string>> ParseCsv(std::string_view input)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, closed = false;
    for (std::size_t index = 0; index < input.size(); ++index)
    {
        const char value = input[index];
        if (quoted)
        {
            if (value == '"')
            {
                if (index + 1U < input.size() && input[index + 1U] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else { quoted = false; closed = true; }
            }
            else field.push_back(value);
            continue;
        }
        if (closed && value != ',' && value != '\r' && value != '\n')
            throw std::invalid_argument("CSV content after closing quote");
        if (value == '"')
        {
            if (!field.empty()) throw std::invalid_argument("CSV quote after field content");
            quoted = true;
        }
        else if (value == ',')
        {
            row.push_back(std::move(field));
            field.clear();
            closed = false;
        }
        else if (value == '\r' || value == '\n')
        {
            if (value == '\r' && index + 1U < input.size() && input[index + 1U] == '\n')
                ++index;
            row.push_back(std::move(field));
            field.clear();
            closed = false;
            rows.push_back(std::move(row));
            row.clear();
        }
        else field.push_back(value);
    }
    if (quoted) throw std::invalid_argument("unterminated CSV quote");
    if (!field.empty() || !row.empty())
    {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

std::uint64_t ParseCsvUnsigned(std::string_view value, std::string_view field)
{
    if (value.empty()) throw std::invalid_argument(std::string(field) + " is empty");
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::invalid_argument(std::string(field) + " is not uint64");
    return result;
}


std::string Canonical(const JsonValue& value)
{
    std::string result;
    AppendCanonicalJson(result, value);
    return result;
}
void Check(bool condition, std::string_view message)
{
    if (!condition) throw std::invalid_argument(std::string(message));
}
std::string ReadText(const std::filesystem::path& path)
{
    if (std::filesystem::file_size(path) > 16U * 1024U * 1024U)
        throw std::runtime_error("control artifact exceeds read bound");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("control artifact unavailable");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (in.bad() || text.size() > 16U * 1024U * 1024U)
        throw std::runtime_error("control artifact read failed or exceeds bound");
    return text;
}
std::string Lower(std::string value)
{
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string DeclaredPath(std::string value)
{
    const std::filesystem::path path(value);
    Check(!value.empty() && value.find('\\') == std::string::npos
        && value.find(':') == std::string::npos && !path.is_absolute()
        && value == path.lexically_normal().generic_string(),
        "path must be normalized repository relative");
    for (const auto& part : path)
        Check(IsValidAnonymousIdentifier(part.string()), "unsafe path component");
    return value;
}
bool Exists(const std::filesystem::path& path)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("filesystem state unavailable");
    return status.type() != std::filesystem::file_type::not_found;
}
void RejectReparsePath(const std::filesystem::path& path)
{
    auto current = std::filesystem::absolute(path);
    while (!current.empty())
    {
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            Check((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U, "reparse path is not supported");
        else Check(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND,
            "cannot verify path confinement");
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
}
void Absent(const std::filesystem::path& path)
{
    RejectReparsePath(path);
    Check(!Exists(path), "output path collision");
}
std::optional<std::string> OptionalString(const JsonObject& object,
    std::string_view field)
{
    const auto& value = RequireMember(object, field);
    if (std::holds_alternative<std::nullptr_t>(value.storage)) return std::nullopt;
    return RequireType<std::string>(value, field);
}

std::optional<std::uint64_t> OptionalUnsigned(const JsonObject& o, std::string_view key)
{
    const auto& v = RequireMember(o, key);
    if (std::holds_alternative<std::nullptr_t>(v.storage)) return {};
    return RequireUnsigned(o, key);
}
bool Anonymous(std::string_view id)
{
    if (!IsValidAnonymousIdentifier(id) || id.size() > 96U || id.find('.') != id.npos) return false;
    const auto lower = Lower(std::string(id));
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") return false;
    return !(lower.size() == 4U && (lower.starts_with("com") || lower.starts_with("lpt"))
        && lower[3] >= '1' && lower[3] <= '9');
}
std::string CaptureGit(const std::filesystem::path& root, const char* arguments)
{
    Check(root.string().find('"') == std::string::npos, "invalid repository path");
    const std::string command = "git -C \"" + root.string() + "\" " + arguments;
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("git preflight launch failed");
    std::string output; std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
    if (_pclose(pipe) != 0) throw std::runtime_error("git preflight failed");
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n')) output.pop_back();
    return output;
}
std::string Uuid(const environment::DeviceUuid& bytes)
{
    Check(std::ranges::any_of(bytes, [](auto b) { return b != 0U; }), "zero GPU UUID");
    constexpr char hex[] = "0123456789abcdef"; std::string result;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i == 4U || i == 6U || i == 8U || i == 10U) result += '-';
        result += hex[bytes[i] >> 4U]; result += hex[bytes[i] & 15U];
    }
    return result;
}
JsonValue J(std::string_view x) { return {{std::string(x)}}; }
JsonValue J(const char* x) { return J(std::string_view(x)); }
JsonValue J(std::uint64_t x) { return {{x}}; }
JsonValue J(std::uint32_t x) { return J(std::uint64_t(x)); }
JsonValue J(bool x) { return {{x}}; }
JsonValue J(double x) { Check(std::isfinite(x), "nonfinite analysis fact"); return {{x}}; }
JsonValue NullableText(std::string_view x) { return x.empty() ? JsonValue{{nullptr}} : J(x); }
template<class T> JsonValue J(const std::optional<T>& x) { return x ? J(*x) : JsonValue{{nullptr}}; }
JsonValue Strings(const std::vector<std::string>& values)
{ JsonArray a; for (const auto& v : values) a.push_back(J(v)); return {{a}}; }
JsonValue ProcessJson(const PlannedProcess& p)
{
    return {{JsonObject{{"backend", J(p.backend == Stage5Backend::Cuda ? "cuda" : "vulkan")},
        {"process_index", J(p.processIndex)}, {"block_index", J(p.blockIndex)}, {"order_slot", J(p.orderSlot)}}}};
}
JsonValue IdentityJson(const AttemptIdentity& x)
{
    return {{JsonObject{{"phase", J(PhaseName(x.phase))}, {"backend", J(x.backend == Stage5Backend::Cuda ? "cuda" : "vulkan")},
        {"kind", J(x.kind == AttemptKind::DiagnosticObservation ? "diagnostic_observation"
            : x.kind == AttemptKind::SelectedWarmupPreparation ? "selected_warmup_preparation" : "measured_observation")},
        {"plan_index", J(std::uint64_t(x.planIndex))}, {"attempt_index", J(std::uint64_t(x.attemptIndex))}}}};
}
JsonValue ArtifactsJson(const std::vector<Artifact>& artifacts)
{
    JsonArray a;
    for (const auto& v : artifacts) a.push_back({{JsonObject{{"relative_path", J(v.relativePath)},
        {"size_bytes", J(v.sizeBytes)}, {"sha256", J(v.sha256)}}}});
    return {{a}};
}
JsonValue AnchorJson(const AnalysisArtifact& a)
{ return {{JsonObject{{"relative_path", J(a.relativePath)}, {"sha256", J(a.sha256)}}}}; }
JsonValue MedianJson(MedianNanoseconds m)
{ return {{JsonObject{{"whole_ns", J(m.whole)}, {"half_ns", J(m.half)}}}}; }
JsonValue StateJson(const OrderedStateAssessment& s)
{
    JsonArray windows;
    if (s.Windows() && s.Medians()) for (std::size_t i = 0; i < 4U; ++i)
        windows.push_back({{JsonObject{{"begin", J(std::uint64_t((*s.Windows())[i].begin))},
            {"count", J(std::uint64_t((*s.Windows())[i].count))}, {"median", MedianJson((*s.Medians())[i])}}}});
    return {{JsonObject{{"input_valid", J(s.InputValid())}, {"windows", s.Windows() ? JsonValue{{windows}} : JsonValue{{nullptr}}},
        {"persistent_trend", J(s.PersistentTrend())}, {"abrupt_state_switch", J(s.AbruptStateSwitch())},
        {"state_structure", J(s.StateStructure())}}}};
}
JsonValue ClockJson(const ClockCalibrationAssessment& c, const ClockAdequacyAssessment& a)
{
    return {{JsonObject{{"calibration_valid", J(c.InputValid())}, {"effective_step_ns", J(c.EffectiveStep())},
        {"decision_scale", a.DecisionScale() ? MedianJson(*a.DecisionScale()) : JsonValue{{nullptr}}},
        {"adequate", a.InputValid() ? J(a.Adequate()) : JsonValue{{nullptr}}}}}};
}
} // namespace

std::string CanonicalManifestPayload(std::string_view json)
{
    auto object = RequireType<JsonObject>(JsonParser(json).Parse(), "manifest");
    Check(object.erase("manifest_sha256") == 1U, "missing manifest hash");
    return Canonical({{object}});
}
std::string CalculateManifestSha256(std::string_view json) { return Sha256(CanonicalManifestPayload(json)); }
std::string PhaseName(Stage5Phase phase)
{
    switch (phase) { case Stage5Phase::A1Sentinel: return "a1-sentinel";
        case Stage5Phase::D1Warmup: return "d1-warmup"; case Stage5Phase::D1Sample: return "d1-sample"; }
    throw std::invalid_argument("invalid Stage-5 phase");
}
Condition FrozenCondition(Stage5Phase phase, std::optional<std::uint64_t> w)
{
    const bool a1 = phase == Stage5Phase::A1Sentinel, sample = phase == Stage5Phase::D1Sample;
    Condition c{phase, a1 ? Stage5Workload::A1 : Stage5Workload::D1, a1 ? A1ElementCount : D1ElementCount,
        a1 ? std::nullopt : std::optional{D1IterationCount}, "H", sample ? 0U : DiagnosticObservationCount,
        w, sample ? SampleObservationCount : 0U};
    Check(ValidateCondition(c), "phase/W does not describe a frozen condition"); return c;
}
Manifest ParseManifest(std::string_view json)
{
    Check(json.size() <= 1024U * 1024U, "manifest exceeds read bound");
    const auto parsed = JsonParser(json).Parse(); const auto& o = RequireType<JsonObject>(parsed, "manifest");
    RequireExactKeys(o, {"manifest_version", "manifest_type", "manifest_id", "manifest_sha256", "protocol_version",
        "evidence_kind", "machine_id", "child_executable_path", "expected_source_revision", "expected_git_dirty",
        "expected_child_executable_sha256", "expected_supervisor_executable_sha256", "expected_gpu_uuid",
        "cuda_device_ordinal", "vulkan_physical_device_index", "expected_a1_vulkan_shader_sha256",
        "expected_d1_vulkan_shader_sha256", "operation_timeout_ms", "child_timeout_ms", "campaign_timeout_ms",
        "declared_max_child_count", "groups"});
    Check(RequireUnsigned(o, "manifest_version") == 1U && RequireString(o, "manifest_type") == "ex2-stage5-qualification"
        && RequireString(o, "protocol_version") == ProtocolVersion && RequireString(o, "evidence_kind") == EvidenceKind
        && !RequireBoolean(o, "expected_git_dirty") && RequireUnsigned(o, "declared_max_child_count") == 30U,
        "manifest constants differ from the frozen contract");
    Manifest m; m.id = RequireString(o, "manifest_id"); m.sha256 = RequireString(o, "manifest_sha256");
    m.machineId = RequireString(o, "machine_id"); Check(Anonymous(m.id) && Anonymous(m.machineId), "invalid anonymous identity");
    m.childExecutablePath = DeclaredPath(RequireString(o, "child_executable_path"));
    Check(std::filesystem::path(m.childExecutablePath).filename() == "ComputeLabEx2Stage5.exe", "unexpected child executable");
    m.sourceRevision = RequireString(o, "expected_source_revision");
    m.childExecutableSha256 = RequireString(o, "expected_child_executable_sha256");
    m.supervisorExecutableSha256 = RequireString(o, "expected_supervisor_executable_sha256");
    m.a1ShaderSha256 = RequireString(o, "expected_a1_vulkan_shader_sha256");
    m.d1ShaderSha256 = RequireString(o, "expected_d1_vulkan_shader_sha256"); m.gpuUuid = RequireString(o, "expected_gpu_uuid");
    Check(IsLowerHex(m.sourceRevision, 40U) && IsUuid(m.gpuUuid)
        && m.gpuUuid != "00000000-0000-0000-0000-000000000000", "invalid source or physical GPU identity");
    for (const auto* h : {&m.sha256, &m.childExecutableSha256, &m.supervisorExecutableSha256, &m.a1ShaderSha256, &m.d1ShaderSha256})
        Check(IsLowerHex(*h, 64U), "invalid SHA-256");
    const auto cuda = RequireUnsigned(o, "cuda_device_ordinal"), vk = RequireUnsigned(o, "vulkan_physical_device_index");
    Check(cuda <= INT_MAX && vk <= UINT32_MAX, "device selector overflow");
    m.cudaDeviceOrdinal = static_cast<int>(cuda); m.vulkanPhysicalDeviceIndex = static_cast<std::uint32_t>(vk);
    m.operationTimeoutMs = RequireUnsigned(o, "operation_timeout_ms"); m.childTimeoutMs = RequireUnsigned(o, "child_timeout_ms");
    m.campaignTimeoutMs = RequireUnsigned(o, "campaign_timeout_ms");
    Check(m.operationTimeoutMs >= 1U && m.operationTimeoutMs <= 60000U
        && m.childTimeoutMs >= m.operationTimeoutMs && m.childTimeoutMs <= 1200000U
        && m.campaignTimeoutMs >= m.childTimeoutMs && m.campaignTimeoutMs <= 86400000U, "invalid deadlines");
    const auto& groups = RequireType<JsonArray>(RequireMember(o, "groups"), "groups");
    Check(groups.size() == 3U, "exactly three groups required"); std::set<std::string> names;
    const std::array phases{Stage5Phase::A1Sentinel, Stage5Phase::D1Warmup, Stage5Phase::D1Sample};
    const std::array rules{"after-campaign-preflight", "after-a1-integrity-complete", "after-d1-warmup-qualified"};
    for (std::size_t g = 0; g < 3U; ++g)
    {
        const auto& group = RequireType<JsonObject>(groups[g], "group");
        RequireExactKeys(group, {"group_index", "phase", "launch_rule", "selected_w_policy", "declared_child_count", "children"});
        Check(RequireUnsigned(group, "group_index") == g && RequireString(group, "phase") == PhaseName(phases[g])
            && RequireString(group, "launch_rule") == rules[g]
            && RequireString(group, "selected_w_policy") == (g == 2U ? "max-qualified-d1-warmup-w" : "none")
            && RequireUnsigned(group, "declared_child_count") == 10U, "group topology differs from protocol");
        const auto& children = RequireType<JsonArray>(RequireMember(group, "children"), "children");
        Check(children.size() == 10U, "exactly ten predeclared children per group required"); m.groups[g].phase = phases[g];
        for (std::size_t p = 0; p < 10U; ++p)
        {
            const auto& child = RequireType<JsonObject>(children[p], "child");
            RequireExactKeys(child, {"sequence_index", "plan_index", "session_id"});
            ManifestChild c{RequireUnsigned(child, "sequence_index"), static_cast<std::size_t>(RequireUnsigned(child, "plan_index")),
                RequireString(child, "session_id")};
            Check(c.sequenceIndex == g * 10U + p && c.planIndex == p && Anonymous(c.sessionId), "invalid child identity/order");
            for (const auto& suffix : {"", ".incomplete", ".failure.json"})
                Check(names.insert(Lower("results/local/" + c.sessionId + suffix)).second, "case-insensitive namespace overlap");
            m.groups[g].children.push_back(c);
        }
    }
    for (const auto& suffix : {"-stage5-control.json", "-stage5-control.json.incomplete", "-stage5-control.json.incomplete.tmp", "-stage5-analysis"})
        Check(names.insert(Lower("results/local/" + m.id + suffix)).second, "control/child namespace overlap");
    Check(m.sha256 == CalculateManifestSha256(json), "manifest SHA-256 mismatch"); m.canonicalJson = Canonical(parsed); return m;
}
std::filesystem::path ParseSupervisorArguments(std::span<const std::string_view> args)
{
    Check(args.size() == 2U && args[0] == "--manifest", "exact --manifest interface required");
    auto path = DeclaredPath(std::string(args[1])); Check(std::filesystem::path(path).extension() == ".json", "manifest must be JSON"); return path;
}
Manifest LoadManifestFromRepository(const std::filesystem::path& root, const std::filesystem::path& relativePath)
{
    const auto text = relativePath.string();
    const std::array<std::string_view, 2> args{"--manifest", text};
    const auto path = root / ParseSupervisorArguments(args);
    RejectReparsePath(path);
    Check(std::filesystem::is_regular_file(path), "manifest must be a regular file");
    const auto size = std::filesystem::file_size(path);
    Check(size <= 1024U * 1024U, "manifest exceeds read bound");
    std::ifstream input(path, std::ios::binary);
    Check(static_cast<bool>(input), "manifest unavailable");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (size != 0U) input.read(bytes.data(), static_cast<std::streamsize>(size));
    Check(input.gcount() == static_cast<std::streamsize>(size) && !input.bad() && !input.fail(), "manifest read failed");
    Check(input.peek() == std::char_traits<char>::eof() && !input.bad(), "manifest changed or read failed");
    return ParseManifest(bytes);
}
std::filesystem::path ControlPath(const Manifest& m, const std::filesystem::path& root)
{ return root / "results/local" / (m.id + "-stage5-control.json"); }
std::filesystem::path AnalysisPath(const Manifest& m, const std::filesystem::path& root)
{ return root / "results/local" / (m.id + "-stage5-analysis"); }
std::vector<std::string> ChildArguments(const Manifest& m, Stage5Phase phase, const ManifestChild& child, std::optional<std::uint64_t> w)
{
    static_cast<void>(FrozenCondition(phase, w)); Check(child.planIndex < 10U, "invalid child plan");
    return {"--phase", PhaseName(phase), "--plan-index", std::to_string(child.planIndex), "--selected-w", w ? std::to_string(*w) : "none",
        "--cuda-device", std::to_string(m.cudaDeviceOrdinal), "--vulkan-device", std::to_string(m.vulkanPhysicalDeviceIndex),
        "--machine-id", m.machineId, "--session-id", child.sessionId, "--expected-gpu-uuid", m.gpuUuid};
}
void ValidatePreflightFacts(const Manifest& m, const PreflightFacts& f)
{
    Check(!f.branch.empty() && f.sourceRevision == m.sourceRevision && !f.gitDirty, "source provenance drift");
    Check(f.childSha256 == m.childExecutableSha256 && f.supervisorSha256 == m.supervisorExecutableSha256, "binary provenance drift");
    Check(f.cudaUuid == m.gpuUuid && f.vulkanUuid == m.gpuUuid, "physical GPU mismatch");
    Check(f.a1ShaderSha256 == m.a1ShaderSha256 && f.d1ShaderSha256 == m.d1ShaderSha256, "shader provenance drift");
}
void RejectOutputCollisions(const Manifest& m, const std::filesystem::path& root)
{
    for (const auto& group : m.groups) for (const auto& child : group.children)
        for (const auto& suffix : {"", ".incomplete", ".failure.json"}) Absent(root / "results/local" / (child.sessionId + suffix));
    const auto ledger = ControlPath(m, root);
    for (const auto& suffix : {L"", L".incomplete", L".incomplete.tmp"}) Absent(std::filesystem::path(ledger.wstring() + suffix));
    Absent(AnalysisPath(m, root));
}
PreflightFacts CollectPreflightFacts(const Manifest& m, const RuntimePaths& paths)
{
    for (const auto& path : {paths.repositoryRoot / m.childExecutablePath, paths.supervisorExecutable, paths.a1Shader, paths.d1Shader}) RejectReparsePath(path);
    std::wstring module(32768, L'\0'); const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    Check(length && length < module.size(), "supervisor module unavailable"); module.resize(length);
    Check(std::filesystem::equivalent(module, paths.supervisorExecutable), "supervisor hash must describe the running module");
    PreflightFacts f; f.sourceRevision = CaptureGit(paths.repositoryRoot, "rev-parse --verify HEAD");
    f.branch = CaptureGit(paths.repositoryRoot, "branch --show-current");
    f.gitDirty = !CaptureGit(paths.repositoryRoot, "status --porcelain=v1 --untracked-files=all").empty();
    f.childSha256 = Sha256File(paths.repositoryRoot / m.childExecutablePath); f.supervisorSha256 = Sha256File(paths.supervisorExecutable);
    f.a1ShaderSha256 = Sha256File(paths.a1Shader); f.d1ShaderSha256 = Sha256File(paths.d1Shader);
    const auto cuda = environment::EnumerateCudaDeviceMetadata();
    const auto vulkan = environment::EnumerateVulkanDeviceMetadata();
    Check(m.cudaDeviceOrdinal >= 0 && static_cast<std::size_t>(m.cudaDeviceOrdinal) < cuda.size()
        && m.vulkanPhysicalDeviceIndex < vulkan.size(), "device unavailable");
    f.cudaUuid = Uuid(cuda[static_cast<std::size_t>(m.cudaDeviceOrdinal)].uuid); f.vulkanUuid = Uuid(vulkan[m.vulkanPhysicalDeviceIndex].uuid); return f;
}

ProgressReporter::ProgressReporter(std::uintptr_t handle) : handle_(handle)
{ if (handle_) Check(GetFileType(reinterpret_cast<HANDLE>(handle_)) == FILE_TYPE_PIPE, "invalid progress pipe handle"); }
AttemptObserver ProgressReporter::Observer() noexcept { return handle_ ? AttemptObserver{this, Publish} : AttemptObserver{}; }
void ProgressReporter::Publish(void* context, const AttemptIdentity& i, AttemptEvent event) noexcept
{
    const auto handle = reinterpret_cast<HANDLE>(static_cast<ProgressReporter*>(context)->handle_);
    LARGE_INTEGER now{}; DWORD written{};
    ProgressEvent e{ProgressMagic, ProgressVersion, event, static_cast<std::uint32_t>(i.phase),
        static_cast<std::uint32_t>(i.backend), i.kind, i.planIndex, i.attemptIndex, 0};
    if (QueryPerformanceCounter(&now)) e.performanceCounter = now.QuadPart;
    if (e.performanceCounter <= 0 || !::WriteFile(handle, &e, sizeof(e), &written, nullptr) || written != sizeof(e))
    { TerminateProcess(GetCurrentProcess(), 4U); ExitProcess(4U); }
}
ProgressReporter ExtractProgressReporter(std::vector<std::string_view>& args)
{
    std::uintptr_t handle{}; bool found = false;
    for (std::size_t i = 0; i < args.size();)
    {
        if (args[i] != "--supervisor-progress-handle") { ++i; continue; }
        Check(!found && i + 1U < args.size(), "duplicate or missing progress handle"); found = true;
        const auto v = args[i + 1U]; const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), handle);
        Check(error == std::errc{} && end == v.data() + v.size() && handle != 0U, "invalid progress handle");
        args.erase(args.begin() + i, args.begin() + i + 2U);
    }
    return ProgressReporter(handle);
}
ProgressState::ProgressState(ProgressExpectation expectation) : expectation_(expectation)
{ Check(expectation.planIndex < 10U, "invalid progress plan index"); static_cast<void>(FrozenCondition(expectation.phase, expectation.selectedW)); }
std::uint32_t ProgressState::ExpectedEventCount() const noexcept
{ return 2U * (expectation_.phase == Stage5Phase::D1Sample ? 200U + static_cast<std::uint32_t>(*expectation_.selectedW) : 48U); }
bool ProgressState::Full() const noexcept { return !invalid && eventCount == ExpectedEventCount(); }
AttemptIdentity ProgressState::NextIdentity() const
{
    Check(eventCount < ExpectedEventCount(), "progress already complete");
    const auto index = eventCount / 2U; const bool sample = expectation_.phase == Stage5Phase::D1Sample;
    const auto w = static_cast<std::uint32_t>(expectation_.selectedW.value_or(0U));
    return {expectation_.phase, FrozenProcessPlan()[expectation_.planIndex].backend,
        !sample ? AttemptKind::DiagnosticObservation : index < w ? AttemptKind::SelectedWarmupPreparation : AttemptKind::MeasuredObservation,
        static_cast<std::uint32_t>(expectation_.planIndex), sample && index >= w ? index - w : index};
}
void ProgressState::Accept(const ProgressEvent& e, std::int64_t now, std::int64_t timeoutTicks)
{
    if (invalid) return;
    if (eventCount >= ExpectedEventCount()) { invalid = true; return; }
    const auto i = NextIdentity(); const auto type = eventCount % 2U ? AttemptEvent::Returned : AttemptEvent::Started;
    if (e.magic != ProgressMagic || e.version != ProgressVersion || e.event != type
        || e.phase != static_cast<std::uint32_t>(i.phase) || e.backend != static_cast<std::uint32_t>(i.backend)
        || e.kind != i.kind || e.planIndex != i.planIndex || e.attemptIndex != i.attemptIndex
        || e.performanceCounter <= 0 || e.performanceCounter > now || e.performanceCounter < lastCounter || timeoutTicks <= 0)
    { invalid = true; return; }
    lastCounter = e.performanceCounter;
    if (type == AttemptEvent::Started)
    {
        outstandingStart = e.performanceCounter; activeAttempt = i;
        if (now - e.performanceCounter >= timeoutTicks) operationTimedOut = true;
    }
    else
    {
        if (e.performanceCounter - *outstandingStart >= timeoutTicks) operationTimedOut = true;
        outstandingStart.reset(); activeAttempt.reset(); lastCompletedAttempt = i;
    }
    ++eventCount;
}

ProcessResult RunSupervisedProcess(const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const ProgressExpectation& expectation,
    std::chrono::milliseconds operationTimeout, std::chrono::milliseconds childTimeout,
    std::chrono::steady_clock::time_point campaignDeadline)
{
    Check(executable.is_absolute() && operationTimeout.count() > 0
        && operationTimeout.count() <= 60000 && childTimeout >= operationTimeout
        && childTimeout.count() <= 1200000, "invalid process request");
    ProcessResult result;
    if (std::chrono::steady_clock::now() >= campaignDeadline)
    {
        result.terminationReason = "campaign_timeout";
        result.progressValidation = "not_started";
        return result;
    }
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        throw std::runtime_error("QPC frequency unavailable");
    const auto ticks = static_cast<std::int64_t>(
        static_cast<long double>(operationTimeout.count()) * frequency.QuadPart / 1000.0L);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE rawRead{}, rawWrite{};
    if (!CreatePipe(&rawRead, &rawWrite, &security, 4096U))
        throw std::runtime_error("progress pipe creation failed");
    UniqueHandle reader(rawRead), writer(rawWrite);
    if (!SetHandleInformation(reader.Get(), HANDLE_FLAG_INHERIT, 0U))
        throw std::runtime_error("progress inheritance configuration failed");
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.Get() || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation,
            &limits, sizeof(limits))) throw std::runtime_error("child ownership job failed");
    SIZE_T bytes{};
    InitializeProcThreadAttributeList(nullptr, 1U, 0U, &bytes);
    std::vector<std::byte> storage(bytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1U, 0U, &bytes))
        throw std::runtime_error("process attributes failed");
    struct Attributes { PPROC_THREAD_ATTRIBUTE_LIST value;
        ~Attributes() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE inherited[] = {writer.Get()};
    if (!UpdateProcThreadAttribute(attributes, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("restricted handle list failed");
    std::wstring command = QuoteWindowsArgument(executable.native());
    for (const auto& arg : arguments) command += L" " + QuoteWindowsArgument(Utf8ToWide(arg));
    command += L" --supervisor-progress-handle " + QuoteWindowsArgument(
        std::to_wstring(reinterpret_cast<std::uintptr_t>(writer.Get())));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, nullptr, &startup.StartupInfo, &info))
        throw std::runtime_error("Stage-5 child CreateProcessW failed");
    UniqueHandle process(info.hProcess), thread(info.hThread);
    result.processId = info.dwProcessId;
    result.launchTimeUtc = TimestampUtc();
    struct OwnedChild
    {
        HANDLE process;
        HANDLE job;
        ~OwnedChild()
        {
            if (WaitForSingleObject(process, 0U) != WAIT_OBJECT_0)
            {
                TerminateJobObject(job, 4U);
                TerminateProcess(process, 4U);
                WaitForSingleObject(process, 5000U);
            }
        }
    } owner{process.Get(), job.Get()};
    if (!AssignProcessToJobObject(job.Get(), process.Get()))
        throw std::runtime_error("child job assignment failed");
    if (std::chrono::steady_clock::now() >= campaignDeadline)
    {
        result.terminationReason = "campaign_timeout";
        result.progressValidation = "not_started";
        if (!TerminateJobObject(job.Get(), 4U) || WaitForSingleObject(process.Get(), 5000U) != WAIT_OBJECT_0)
            throw std::runtime_error("owned suspended child termination failed");
        DWORD exit{}; if (!GetExitCodeProcess(process.Get(), &exit) || exit == STILL_ACTIVE)
            throw std::runtime_error("suspended child exit unavailable");
        result.exitCode = exit; result.exitTimeUtc = TimestampUtc();
        return result;
    }
    const auto childDeadline = std::chrono::steady_clock::now() + childTimeout;
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
        throw std::runtime_error("child resume failed");
    thread = UniqueHandle{};
    writer = UniqueHandle{};
    ProgressState progress(expectation);
    std::vector<std::byte> pending;
    const auto drain = [&] {
        DWORD available{};
        if (!PeekNamedPipe(reader.Get(), nullptr, 0U, nullptr, &available, nullptr))
        {
            if (GetLastError() == ERROR_BROKEN_PIPE) return;
            throw std::runtime_error("progress pipe inspection failed");
        }
        if (available > (progress.ExpectedEventCount() + 1U) * sizeof(ProgressEvent)) { progress.invalid = true; return; }
        const auto old = pending.size();
        pending.resize(old + available);
        DWORD read{};
        if (available && !::ReadFile(reader.Get(), pending.data() + old, available, &read, nullptr))
            throw std::runtime_error("progress read failed");
        pending.resize(old + read);
        while (pending.size() >= sizeof(ProgressEvent))
        {
            ProgressEvent event;
            std::memcpy(&event, pending.data(), sizeof(event));
            pending.erase(pending.begin(), pending.begin() + sizeof(event));
            LARGE_INTEGER now{};
            if (!QueryPerformanceCounter(&now)) throw std::runtime_error("QPC unavailable");
            progress.Accept(event, now.QuadPart, ticks);
        }
    };
    for (;;)
    {
        const DWORD wait = WaitForSingleObject(process.Get(), 10U);
        if (wait != WAIT_TIMEOUT && wait != WAIT_OBJECT_0)
            throw std::runtime_error("child wait failed");
        drain();
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) throw std::runtime_error("QPC unavailable");
        if (progress.outstandingStart && now.QuadPart - *progress.outstandingStart >= ticks)
            progress.operationTimedOut = true;
        if (progress.invalid) result.terminationReason = "progress_protocol_error";
        else if (progress.operationTimedOut)
            result.terminationReason = FrozenProcessPlan()[expectation.planIndex].backend == Stage5Backend::Cuda
                ? "cuda_attempt_timeout" : "vulkan_attempt_timeout";
        else if (std::chrono::steady_clock::now() >= campaignDeadline)
            result.terminationReason = "campaign_timeout";
        else if (std::chrono::steady_clock::now() >= childDeadline)
            result.terminationReason = "child_timeout";
        if (wait == WAIT_OBJECT_0) break;
        if (result.terminationReason)
        {
            if (!TerminateJobObject(job.Get(), 4U)
                || WaitForSingleObject(process.Get(), 5000U) != WAIT_OBJECT_0)
                throw std::runtime_error("owned child termination failed");
            break;
        }
    }
    drain();
    DWORD exit{};
    if (!GetExitCodeProcess(process.Get(), &exit) || exit == STILL_ACTIVE)
        throw std::runtime_error("child exit unavailable");
    result.exitCode = exit;
    result.exitTimeUtc = TimestampUtc();
    result.eventCount = progress.eventCount;
    result.activeAttempt = progress.activeAttempt; result.lastCompletedAttempt = progress.lastCompletedAttempt;
    if (progress.invalid || !pending.empty() || (exit == 0U && !progress.Full()))
    {
        result.progressValidation = "invalid";
        if (!result.terminationReason) result.terminationReason = "progress_protocol_error";
    }
    else result.progressValidation = progress.Full() ? "complete" : "valid_prefix";
    return result;
}


namespace
{
results::EnvironmentRecord ReadCommon(const JsonObject& o)
{
    results::EnvironmentRecord r;
    Check(RequireUnsigned(o, "schema_version") == 2U, "schema version");
    r.schemaVersion = 2U;
    r.experimentId = RequireString(o, "experiment_id");
    r.runId = RequireString(o, "run_id");
    r.timestampUtc = RequireString(o, "timestamp_utc");
    r.gitCommit = RequireString(o, "git_commit");
    r.gitDirty = RequireBoolean(o, "git_dirty");
    r.machineId = RequireString(o, "machine_id");
    r.osName = RequireString(o, "os_name");
    r.osVersion = RequireString(o, "os_version");
    r.cpuName = RequireString(o, "cpu_name");
    r.systemMemoryBytes = RequireUnsigned(o, "system_memory_bytes");
    r.gpuName = OptionalString(o, "gpu_name");
    r.gpuVendor = OptionalString(o, "gpu_vendor");
    r.gpuDeviceId = OptionalString(o, "gpu_device_id");
    r.gpuMemoryBytes = OptionalUnsigned(o, "gpu_memory_bytes");
    r.nvidiaDriverVersion = OptionalString(o, "nvidia_driver_version");
    r.cudaToolkitVersion = OptionalString(o, "cuda_toolkit_version");
    r.cudaRuntimeVersion = OptionalString(o, "cuda_runtime_version");
    r.cudaComputeCapability = OptionalString(o, "cuda_compute_capability");
    r.vulkanSdkVersion = OptionalString(o, "vulkan_sdk_version");
    r.vulkanDeviceApiVersion = OptionalString(o, "vulkan_device_api_version");
    r.compilerName = RequireString(o, "compiler_name");
    r.compilerVersion = RequireString(o, "compiler_version");
    r.cmakeVersion = RequireString(o, "cmake_version");
    r.ninjaVersion = RequireString(o, "ninja_version");
    r.configurePreset = RequireString(o, "configure_preset");
    r.buildType = RequireString(o, "build_type");
    r.validationEnabled = RequireBoolean(o, "validation_enabled");
    r.diagnosticInstrumentation = RequireBoolean(o, "diagnostic_instrumentation");
    return r;
}

evidence::BackendDiagnostics ReadDiagnostics(const JsonObject& o)
{
    evidence::BackendDiagnostics d;
    d.implementation = RequireString(o, "implementation");
    d.streamFlags = OptionalString(o, "stream_flags");
    d.queueFamilyIndex = OptionalUnsigned(o, "queue_family_index");
    d.queueFlags = OptionalUnsigned(o, "queue_flags");
    d.queueCount = OptionalUnsigned(o, "queue_count");
    d.inputMemoryFlags = OptionalUnsigned(o, "input_memory_flags");
    d.outputMemoryFlags = OptionalUnsigned(o, "output_memory_flags");
    d.uploadMemoryFlags = OptionalUnsigned(o, "upload_memory_flags");
    d.readbackMemoryFlags = OptionalUnsigned(o, "readback_memory_flags");
    d.nativeMarkersEnabled = RequireBoolean(o, "native_markers_enabled");
    // All remaining diagnostic timing fields must be null under H. Exact
    // comparison against the public serializer below also rejects unknown keys.
    return d;
}

std::optional<std::uint64_t> CellUnsigned(const std::string& cell)
{ return cell.empty() ? std::nullopt : std::optional{ParseCsvUnsigned(cell, "numeric CSV field")}; }
std::optional<std::string> CellString(const std::string& cell)
{ return cell.empty() ? std::nullopt : std::optional{cell}; }
using Csv = std::vector<std::vector<std::string>>;
Csv ReadCsv(const std::filesystem::path& path, std::string_view header, std::size_t count)
{
    auto rows = ParseCsv(ReadText(path)); const auto expected = ParseCsv(header);
    Check(!rows.empty() && rows[0] == expected[0] && rows.size() == count + 1U, "CSV header/row count mismatch");
    for (const auto& row : rows) Check(row.size() == rows[0].size(), "CSV row width mismatch"); return rows;
}
void SameJson(std::string_view actual, std::string_view expected)
{ Check(Canonical(JsonParser(actual).Parse()) == Canonical(JsonParser(expected).Parse()), "JSON disagrees with reconstructed raw evidence"); }
const std::vector<std::uint32_t>& Oracle(Stage5Phase phase)
{
    // Only the two frozen workloads; immutable CPU state, never package assertions.
    if (phase == Stage5Phase::A1Sentinel)
    { static const auto a1 = ReferenceA1(GenerateWordInput(CoreInputSeed, A1ElementCount)); return a1; }
    static const auto d1 = ReferenceD1(CoreInputSeed, D1ElementCount, D1IterationCount).finalState; return d1;
}
void InspectComplete(const Manifest& m, Stage5Phase phase, const ManifestChild& child,
    const std::filesystem::path& path, std::optional<std::uint64_t> w, PackageInspection& result)
{
    namespace ev = evidence;
    const auto condition = FrozenCondition(phase, w); const auto process = FrozenProcessPlan()[child.planIndex];
    const auto input = GenerateWordInput(CoreInputSeed, condition.elementCount);
    const auto shader = process.backend == Stage5Backend::Cuda ? std::nullopt
        : std::optional{phase == Stage5Phase::A1Sentinel ? m.a1ShaderSha256 : m.d1ShaderSha256};
    const auto f = ev::MakeWordFoundation({condition, process, child.sessionId, m.machineId,
        {m.gpuUuid, true}, m.sourceRevision, m.childExecutableSha256, shader}, input, Oracle(phase));
    const auto environmentBytes = ReadText(path / "environment.json");
    const auto environmentObject = RequireType<JsonObject>(JsonParser(environmentBytes).Parse(), "environment");
    auto env = ev::MakeEnvironmentRecord(ReadCommon(environmentObject), f,
        ReadDiagnostics(RequireType<JsonObject>(RequireMember(environmentObject, "backend_native"), "backend diagnostics")));
    SameJson(environmentBytes, ev::SerializeEnvironmentJson(f, env));
    const auto ir = ParseCsv(ReadText(path / "initialization.csv"));
    Check(ir.size() >= 2U && ir[0] == ParseCsv(ev::InitializationCsvHeader())[0], "initialization missing/header drift");
    std::vector<ev::InitializationRecord> initialization;
    for (std::size_t i = 1U; i < ir.size(); ++i)
    {
        const auto& r = ir[i]; Check(r.size() == 13U, "initialization width");
        Check(r[3] == "cuda" || r[3] == "vulkan", "invalid initialization backend");
        initialization.push_back({r[1], r[3] == "cuda" ? Backend::Cuda : Backend::Vulkan,
            ParseCsvUnsigned(r[4], "process index"), ParseCsvUnsigned(r[5], "sequence index"), r[6],
            CellString(r[7]), CellString(r[8]), CellUnsigned(r[9]), r[10], CellUnsigned(r[11]), CellString(r[12])});
    }
    // Serializer comparison includes every cell, including schema and diagnostic fields.
    Check(ir == ParseCsv(ev::SerializeInitializationCsv(f, initialization)), "initialization identity/facts drift");
    const auto cr = ReadCsv(path / "host-clock.csv", ev::HostClockHeader(), ClockDeltaCount);
    std::vector<ev::HostClockRecord> clock; ProcessInput reconstructed{condition, process,
        phase == Stage5Phase::D1Sample ? SampleObservationCount : DiagnosticObservationCount, {}, {}};
    for (std::size_t i = 1U; i < cr.size(); ++i)
    {
        const auto& r = cr[i]; const auto delta = CellUnsigned(r[5]);
        clock.push_back({r[1], r[2], ParseCsvUnsigned(r[3], "process index"), ParseCsvUnsigned(r[4], "clock sequence"), delta});
        reconstructed.clockCalibration.deltasNanoseconds.push_back(delta);
    }
    Check(cr == ParseCsv(ev::SerializeHostClockCsv(f, clock)), "clock identity/order drift");
    const auto wr = ReadCsv(path / "warmup.csv", ev::WarmupCsvHeader(), condition.diagnosticCount);
    const auto sr = ReadCsv(path / "samples.csv", ev::SamplesCsvHeader(), condition.measuredSampleCount);
    std::vector<ev::WarmupRecord> warmup; std::vector<ev::SampleRecord> samples;
    const auto fill = [&](ev::OperationRecord& r, const std::vector<std::string>& cells, bool sample) {
        r.sequenceIndex = ParseCsvUnsigned(cells[sample ? 22U : 4U], "observation index");
        Check(cells[sample ? 24U : 8U] == "ok", "completed package contains failed operation");
        if (sample) Check(cells[23] == "true" && cells[25].empty() && cells[26].empty() && cells[30].empty(), "sample correctness/native/failure drift");
        r.status = ev::Status::Ok; r.correctness = {true, true, true, true, true};
        r.hostSubmissionNanoseconds = CellUnsigned(cells[sample ? 27U : 5U]);
        r.hostWaitNanoseconds = CellUnsigned(cells[sample ? 28U : 6U]);
        r.hostCompletionNanoseconds = CellUnsigned(cells[sample ? 29U : 7U]);
        ev::ValidateOperationRecord(f, r);
        reconstructed.observations.push_back({r.sequenceIndex, OperationStatus::Ok, true, r.hostCompletionNanoseconds});
    };
    for (std::size_t i = 1U; i < wr.size(); ++i)
    { auto r = ev::MakeWarmupRecord(f, i - 1U); fill(r, wr[i], false); warmup.push_back(r); }
    for (std::size_t i = 1U; i < sr.size(); ++i)
    { auto r = ev::MakeSampleRecord(f, i - 1U); fill(r, sr[i], true); samples.push_back(r); }
    Check(wr == ParseCsv(ev::SerializeWarmupCsv(f, warmup)) && sr == ParseCsv(ev::SerializeSamplesCsv(f, samples)), "operation identity/order drift");
    const auto summary = ev::SummarizeSamples(f, samples, ev::Status::Ok);
    SameJson(ReadText(path / "summary.json"), ev::SerializeSummaryJson(f, summary, samples));
    ev::ValidateBundle(f, env, initialization, clock, warmup, samples, summary);
    result.input = std::move(reconstructed);
}
void InspectFailure(const Manifest& m, Stage5Phase phase, const ManifestChild& child,
    const std::filesystem::path& sidecar, bool staging, std::optional<std::uint64_t> w,
    std::optional<std::uint32_t> exit)
{
    const auto o = RequireType<JsonObject>(JsonParser(ReadText(sidecar)).Parse(), "failure record");
    RequireExactKeys(o, {"record_version", "record_type", "session_id", "phase", "plan_index", "backend", "process_index",
        "block_index", "order_slot", "exit_category", "failure_phase", "error_code", "foundation_established", "source_revision",
        "run_id", "series_id", "staging_retained", "completion_uncertain", "native_phase", "native_code", "native_detail",
        "attempt_index", "preparation_attempt", "successful_wait", "validation_passed", "host_submission_ns", "host_wait_ns", "host_completion_ns", "detail"});
    Check(RequireUnsigned(o, "record_version") == 1U && RequireString(o, "record_type") == "ex2-stage5-execution-failure"
        && RequireString(o, "session_id") == child.sessionId, "failure context identity mismatch");
    const auto declaredPhase = OptionalString(o, "phase"); const auto plan = OptionalUnsigned(o, "plan_index");
    Check(!declaredPhase || *declaredPhase == PhaseName(phase), "failure phase mismatch");
    Check(!plan || *plan == child.planIndex, "failure plan mismatch");
    const auto category = RequireUnsigned(o, "exit_category"); Check(category >= 2U && category <= 4U, "invalid failure category");
    if (exit && *exit <= 4U) Check(*exit == category, "failure category/exit mismatch");
    Check(RequireBoolean(o, "staging_retained") == staging, "failure retained-state mismatch");
    Check(!RequireString(o, "failure_phase").empty() && !RequireString(o, "error_code").empty(), "missing failure context");
    const auto source = OptionalString(o, "source_revision"); Check(!source || *source == m.sourceRevision, "failure source mismatch");
    if (RequireBoolean(o, "foundation_established"))
    {
        const auto process = FrozenProcessPlan()[child.planIndex];
        Check(RequireString(o, "run_id") == child.sessionId && RequireString(o, "backend") == (process.backend == Stage5Backend::Cuda ? "cuda" : "vulkan")
            && RequireUnsigned(o, "process_index") == process.processIndex && RequireUnsigned(o, "block_index") == process.blockIndex
            && RequireUnsigned(o, "order_slot") == process.orderSlot, "failure foundation mismatch");
        const auto condition = FrozenCondition(phase, w); const auto input = GenerateWordInput(CoreInputSeed, condition.elementCount);
        const auto f = evidence::MakeWordFoundation({condition, process, child.sessionId, m.machineId, {m.gpuUuid, true}, m.sourceRevision,
            m.childExecutableSha256, process.backend == Stage5Backend::Cuda ? std::nullopt : std::optional{phase == Stage5Phase::A1Sentinel ? m.a1ShaderSha256 : m.d1ShaderSha256}}, input, Oracle(phase));
        Check(RequireString(o, "series_id") == f.Identity().seriesId, "failure series mismatch");
    }
    else Check(!OptionalString(o, "run_id") && !OptionalString(o, "series_id"), "failure fabricates foundation");
    const auto wait = RequireMember(o, "successful_wait");
    Check(std::holds_alternative<std::nullptr_t>(wait.storage) || std::holds_alternative<bool>(wait.storage), "invalid successful-wait fact");
    if (!std::holds_alternative<bool>(wait.storage) || !std::get<bool>(wait.storage))
        Check(!OptionalUnsigned(o, "host_wait_ns") && !OptionalUnsigned(o, "host_completion_ns"), "failure fabricates successful t2");
    const auto& native = RequireMember(o, "native_code");
    Check(std::holds_alternative<std::nullptr_t>(native.storage) || std::holds_alternative<std::int64_t>(native.storage)
        || (std::holds_alternative<std::uint64_t>(native.storage) && std::get<std::uint64_t>(native.storage) <= INT64_MAX), "invalid native error code");
    for (const auto& key : {"validation_passed", "successful_wait"})
    { const auto& v = RequireMember(o, key); Check(std::holds_alternative<std::nullptr_t>(v.storage) || std::holds_alternative<bool>(v.storage), "invalid optional failure fact"); }
    static_cast<void>(RequireBoolean(o, "completion_uncertain")); const bool preparation = RequireBoolean(o, "preparation_attempt");
    const auto attempt = OptionalUnsigned(o, "attempt_index");
    Check(!preparation || (phase == Stage5Phase::D1Sample && w && attempt && *attempt < *w), "failure preparation context drift");
    Check(!attempt || preparation || *attempt < (phase == Stage5Phase::D1Sample ? 200U : 48U), "failure observation context drift");
    for (const auto& key : {"native_phase", "native_detail"}) static_cast<void>(OptionalString(o, key));
    static_cast<void>(RequireString(o, "detail")); static_cast<void>(OptionalUnsigned(o, "host_submission_ns"));
}
} // namespace

PackageInspection InspectPackage(const Manifest& m, Stage5Phase phase, const ManifestChild& child,
    const std::filesystem::path& root, std::optional<std::uint64_t> w, std::optional<std::uint32_t> exit)
{
    PackageInspection r;
    try
    {
        Check(child.planIndex < 10U && Anonymous(child.sessionId), "unsafe inspector identity"); static_cast<void>(FrozenCondition(phase, w));
        const auto final = root / "results/local" / child.sessionId;
        const auto staging = root / "results/local" / (child.sessionId + ".incomplete");
        const auto sidecar = root / "results/local" / (child.sessionId + ".failure.json");
        const bool f = Exists(final), s = Exists(staging), side = Exists(sidecar);
        r.state = f && (s || side) ? "contradictory" : f ? "complete" : s && side ? "incomplete-retained"
            : side ? "sidecar-only" : s ? "partial-retained" : "missing";
        const std::set<std::string> expected{"environment.json", "initialization.csv", "host-clock.csv", "warmup.csv", "samples.csv", "summary.json"};
        for (const auto& dir : {final, staging}) if (Exists(dir))
        {
            RejectReparsePath(dir); Check(std::filesystem::is_directory(dir), "package path is not a directory"); std::set<std::string> names;
            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                names.insert(entry.path().filename().string());
                try
                {
                    RejectReparsePath(entry.path()); Check(entry.is_regular_file(), "package contains non-regular artifact");
                    r.artifacts.push_back({entry.path().lexically_relative(root).generic_string(), entry.file_size(), Sha256File(entry.path())});
                }
                catch (const std::exception& e) { r.errors.emplace_back(e.what()); }
            }
            if (dir == final && names != expected) r.errors.emplace_back("completed package must contain exactly six standard files");
        }
        if (side)
        {
            RejectReparsePath(sidecar); Check(std::filesystem::is_regular_file(sidecar), "failure sidecar is not regular");
            r.artifacts.push_back({sidecar.lexically_relative(root).generic_string(), std::filesystem::file_size(sidecar), Sha256File(sidecar)});
            r.failureContextJson = Canonical(JsonParser(ReadText(sidecar)).Parse());
            InspectFailure(m, phase, child, sidecar, s, w, exit);
        }
        Check(r.errors.empty(), "package artifact inventory invalid");
        Check(r.state != "contradictory", "contradictory final/failure package states");
        if (r.state == "complete")
        { Check(exit && *exit == 0U, "completed package requires successful child exit"); InspectComplete(m, phase, child, final, w, r); }
        else Check(!exit || *exit != 0U, "exit zero without completed package");
        for (const auto& a : r.artifacts)
            Check(std::filesystem::file_size(root / a.relativePath) == a.sizeBytes && Sha256File(root / a.relativePath) == a.sha256, "package changed during inspection");
        r.structurallyValid = r.state == "complete" && r.input.has_value();
    }
    catch (const std::exception& e) { r.errors.emplace_back(e.what()); r.structurallyValid = false; r.input.reset(); }
    std::ranges::sort(r.artifacts, {}, &Artifact::relativePath); return r;
}

namespace
{
JsonValue FactsJson(const PreflightFacts& f)
{
    return {{JsonObject{{"source_revision", J(f.sourceRevision)}, {"branch", J(f.branch)}, {"git_dirty", J(f.gitDirty)},
        {"child_executable_sha256", J(f.childSha256)}, {"supervisor_executable_sha256", J(f.supervisorSha256)},
        {"cuda_gpu_uuid", J(f.cudaUuid)}, {"vulkan_gpu_uuid", J(f.vulkanUuid)},
        {"a1_shader_sha256", J(f.a1ShaderSha256)}, {"d1_shader_sha256", J(f.d1ShaderSha256)}}}};
}
JsonValue InventoryJson(const ExecutionRecord& r, std::size_t group)
{
    JsonArray a;
    for (const auto& child : r.executions) if (child.groupIndex == group)
        a.push_back({{JsonObject{{"sequence_index", J(child.child.sequenceIndex)}, {"session_id", J(child.child.sessionId)},
            {"artifacts", ArtifactsJson(child.package.artifacts)}}}});
    return {{a}};
}
JsonObject AnalysisEnvelope(const ExecutionRecord& r, std::size_t group)
{
    return {{"control_analysis_version", J(std::uint64_t(1))}, {"analysis_schema_version", J(AnalysisSchemaVersion)},
        {"protocol_version", J(ProtocolVersion)}, {"evidence_kind", J(EvidenceKind)},
        {"analysis_kind", J(group == 0U ? "a1-sentinel-diagnostic" : group == 1U ? "d1-warmup-qualification-facts" : "d1-sample-qualification-facts")},
        {"group_index", J(std::uint64_t(group))}, {"phase", J(PhaseName(r.manifest.groups[group].phase))},
        {"manifest_id", J(r.manifest.id)}, {"manifest_sha256", J(r.manifest.sha256)},
        {"source_revision", J(r.manifest.sourceRevision)}, {"ordered_packages", InventoryJson(r, group)}};
}
std::string A1Analysis(const ExecutionRecord& r, const A1SentinelAssessment& a)
{
    auto o = AnalysisEnvelope(r, 0U); JsonArray processes;
    for (const auto& p : a.processes)
        processes.push_back({{JsonObject{{"process", ProcessJson(p.Process())}, {"input_valid", J(p.InputValid())},
            {"median48", p.Median48() ? MedianJson(*p.Median48()) : JsonValue{{nullptr}}},
            {"clock", ClockJson(p.ClockCalibration(), p.ClockAdequacy())}, {"ordered_state", StateJson(p.OrderedState())},
            {"reasons", Strings(p.Reasons())}}}});
    const auto backend = [](const A1BackendDescription& b) -> JsonValue { return {{JsonObject{{"input_valid", J(b.inputValid)},
        {"r_a1_process", J(b.rA1Process)}, {"exceeds_one_point_ten", J(b.exceedsOnePointTen)}}}}; };
    o["assessment"] = {{JsonObject{{"input_valid", J(a.inputValid)}, {"processes", {{processes}}},
        {"cuda", backend(a.cuda)}, {"vulkan", backend(a.vulkan)}, {"reasons", Strings(a.reasons)}}}};
    return Canonical({{o}}) + "\n";
}
std::string D1Analysis(const ExecutionRecord& r, const D1QualificationAssessment& a, std::size_t group)
{
    auto o = AnalysisEnvelope(r, group); o["assessment"] = JsonParser(SerializeAnalysisJson(a)).Parse();
    JsonArray diagnostics;
    if (group == 1U) for (const auto& p : a.warmupProcesses)
    {
        JsonArray candidates;
        for (const auto& c : p.Candidates()) candidates.push_back({{JsonObject{{"w", J(c.w)}, {"early_a", MedianJson(c.earlyA)},
            {"early_b", MedianJson(c.earlyB)}, {"reference", MedianJson(c.reference)}, {"late_a", MedianJson(c.lateA)},
            {"early_a_relative_difference", J(c.earlyARelativeDifference)}, {"early_b_relative_difference", J(c.earlyBRelativeDifference)},
            {"late_relative_difference", J(c.lateRelativeDifference)}}}});
        diagnostics.push_back({{JsonObject{{"process", ProcessJson(p.Process())}, {"candidate_medians", {{candidates}}}}}});
    }
    else if (a.sampleProcesses) for (const auto& p : *a.sampleProcesses)
    {
        JsonValue d{{nullptr}};
        if (p.Diagnostics())
        {
            const auto& v = *p.Diagnostics(); JsonArray windows; for (const auto& m : v.windows) windows.push_back(MedianJson(m));
            d = {{JsonObject{{"median50", MedianJson(v.median50)}, {"median100", MedianJson(v.median100)},
                {"median200", MedianJson(v.median200)}, {"windows", {{windows}}},
                {"prefix100_relative_difference", J(v.prefix100RelativeDifference)}, {"prefix50_relative_difference", J(v.prefix50RelativeDifference)},
                {"first_last_relative_difference", J(v.firstLastRelativeDifference)}}}};
        }
        diagnostics.push_back({{JsonObject{{"process", ProcessJson(p.Process())}, {"sample_diagnostics", d}}}});
    }
    o["process_diagnostics"] = {{diagnostics}}; return Canonical({{o}}) + "\n";
}
std::string CampaignAnalysis(const ExecutionRecord& r, const std::optional<D1QualificationAssessment>& a)
{
    JsonArray groups; for (const auto& anchor : r.analyses) groups.push_back(anchor ? AnchorJson(*anchor) : JsonValue{{nullptr}});
    return Canonical({{JsonObject{{"control_analysis_version", J(std::uint64_t(1))}, {"analysis_schema_version", J(AnalysisSchemaVersion)},
        {"analysis_kind", J("stage5-campaign-qualification-facts")}, {"protocol_version", J(ProtocolVersion)}, {"evidence_kind", J(EvidenceKind)},
        {"manifest_id", J(r.manifest.id)}, {"manifest_sha256", J(r.manifest.sha256)}, {"source_revision", J(r.manifest.sourceRevision)},
        {"campaign_complete", J(r.campaignComplete)}, {"terminal_control_state", J(r.status)}, {"failure_reason", J(r.failureReason)},
        {"group_analyses", {{groups}}}, {"selected_common_w", J(r.selectedCommonW)},
        {"d1_scope_qualified", r.campaignComplete && a ? J(a->d1ScopeQualified) : JsonValue{{nullptr}}},
        {"qualification_reasons", a ? Strings(a->reasons) : JsonValue{{nullptr}}},
        {"gate0_verdict", J("pending_human_review")}, {"stage2_authorization", J("not_granted_by_this_artifact")}}}}) + "\n";
}
std::vector<ProcessInput> ReinspectGroup(ExecutionRecord& r, std::size_t g, const std::filesystem::path& root)
{
    std::vector<ProcessInput> inputs;
    for (auto& c : r.executions) if (c.groupIndex == g)
    {
        auto disk = InspectPackage(r.manifest, r.manifest.groups[g].phase, c.child, root, c.selectedW, c.process.exitCode);
        Check(disk.structurallyValid && Canonical(ArtifactsJson(disk.artifacts)) == Canonical(ArtifactsJson(c.package.artifacts)), "group evidence changed after child inspection");
        c.package = std::move(disk); inputs.push_back(*c.package.input);
    }
    Check(inputs.size() == ChildrenPerGroup, "group barrier requires all ten fresh processes"); return inputs;
}
void RecheckArtifacts(const ExecutionRecord& r, const std::filesystem::path& root)
{
    for (const auto& c : r.executions) for (const auto& a : c.package.artifacts)
    {
        RejectReparsePath(root / a.relativePath);
        Check(std::filesystem::file_size(root / a.relativePath) == a.sizeBytes && Sha256File(root / a.relativePath) == a.sha256, "retained evidence changed");
    }
    for (const auto& a : r.analyses) if (a)
    { RejectReparsePath(root / a->relativePath); Check(Sha256File(root / a->relativePath) == a->sha256, "finalized group analysis changed"); }
}
} // namespace

std::string SerializeExecutionRecord(const ExecutionRecord& r)
{
    JsonArray schedule, executions, groups;
    for (std::size_t g = 0; g < 3U; ++g)
    {
        for (const auto& child : r.manifest.groups[g].children)
        {
            auto o = RequireType<JsonObject>(ProcessJson(FrozenProcessPlan()[child.planIndex]), "process");
            o["sequence_index"] = J(child.sequenceIndex); o["plan_index"] = J(std::uint64_t(child.planIndex)); o["session_id"] = J(child.sessionId);
            o["group_index"] = J(std::uint64_t(g)); o["phase"] = J(PhaseName(r.manifest.groups[g].phase));
            schedule.push_back({{o}});
        }
        groups.push_back({{JsonObject{{"group_index", J(std::uint64_t(g))}, {"phase", J(PhaseName(r.manifest.groups[g].phase))},
            {"state", J(r.groupStates[g])}, {"analysis", r.analyses[g] ? AnchorJson(*r.analyses[g]) : JsonValue{{nullptr}}}}}});
    }
    for (const auto& c : r.executions)
    {
        executions.push_back({{JsonObject{{"sequence_index", J(c.child.sequenceIndex)}, {"group_index", J(std::uint64_t(c.groupIndex))},
            {"plan_index", J(std::uint64_t(c.child.planIndex))}, {"session_id", J(c.child.sessionId)}, {"selected_w", J(c.selectedW)},
            {"process", ProcessJson(FrozenProcessPlan()[c.child.planIndex])}, {"launch_time_utc", NullableText(c.process.launchTimeUtc)},
            {"exit_time_utc", NullableText(c.process.exitTimeUtc)}, {"process_id", J(c.process.processId)}, {"exit_code", J(c.process.exitCode)},
            {"termination_reason", J(c.process.terminationReason)}, {"progress_validation", J(c.process.progressValidation)},
            {"progress_event_count", J(c.process.eventCount)},
            {"active_attempt", c.process.activeAttempt ? IdentityJson(*c.process.activeAttempt) : JsonValue{{nullptr}}},
            {"last_completed_attempt", c.process.lastCompletedAttempt ? IdentityJson(*c.process.lastCompletedAttempt) : JsonValue{{nullptr}}},
            {"package_state", J(c.package.state)}, {"package_structurally_valid", J(c.package.structurallyValid)},
            {"failure_context", c.package.failureContextJson ? JsonParser(*c.package.failureContextJson).Parse() : JsonValue{{nullptr}}},
            {"arguments", Strings(ChildArguments(r.manifest, r.manifest.groups[c.groupIndex].phase, c.child, c.selectedW))},
            {"inspection_errors", Strings(c.package.errors)}, {"artifacts", ArtifactsJson(c.package.artifacts)}, {"continuation", J(c.continuation)}}}});
    }
    return Canonical({{JsonObject{{"record_version", J(std::uint64_t(1))}, {"record_type", J("ex2-stage5-qualification-supervisor-execution")},
        {"manifest_id", J(r.manifest.id)}, {"manifest_sha256", J(r.manifest.sha256)}, {"manifest", JsonParser(r.manifest.canonicalJson).Parse()},
        {"observed_provenance", FactsJson(r.observed)}, {"start_time_utc", NullableText(r.startTimeUtc)}, {"end_time_utc", NullableText(r.endTimeUtc)},
        {"status", J(r.status)}, {"campaign_complete", J(r.campaignComplete)}, {"failure_reason", J(r.failureReason)},
        {"declared_schedule", {{schedule}}}, {"executions", {{executions}}}, {"groups", {{groups}}},
        {"selected_common_w", J(r.selectedCommonW)}, {"d1_sample_authorized_by_warmup", J(r.selectedCommonW.has_value() && r.analyses[1].has_value())},
        {"campaign_analysis", r.campaignAnalysis ? AnchorJson(*r.campaignAnalysis) : JsonValue{{nullptr}}}}}}) + "\n";
}
void WriteExecutionRecord(const std::filesystem::path& path, const ExecutionRecord& r, bool complete, bool replaceOwned)
{
    const auto staging = std::filesystem::path(path.wstring() + L".incomplete");
    const auto temporary = std::filesystem::path(path.wstring() + L".incomplete.tmp");
    Absent(path); RejectReparsePath(staging); Absent(temporary);
    if (!replaceOwned) Absent(staging);
    else
    {
        const auto old = RequireType<JsonObject>(JsonParser(ReadText(staging)).Parse(), "owned ledger");
        Check(RequireString(old, "manifest_sha256") == r.manifest.sha256 && RequireString(old, "start_time_utc") == r.startTimeUtc,
            "refusing replacement of unowned control state");
    }
    const auto bytes = SerializeExecutionRecord(r); WriteDurableFile(temporary, bytes);
    Check(ReadText(temporary) == bytes, "ledger readback differs from durable bytes");
    if (!MoveFileExW(temporary.c_str(), staging.c_str(), MOVEFILE_WRITE_THROUGH | (replaceOwned ? MOVEFILE_REPLACE_EXISTING : 0U)))
        throw std::runtime_error("ledger staging publication failed");
    if (complete && !MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("ledger final publication failed");
}
AnalysisArtifact PublishAnalysis(const std::filesystem::path& root, const std::filesystem::path& path, std::string_view bytes)
{
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp"); Absent(path); Absent(temporary);
    RejectReparsePath(path.parent_path()); Check(std::filesystem::is_directory(path.parent_path()), "analysis directory unavailable");
    WriteDurableFile(temporary, bytes); Check(ReadText(temporary) == bytes, "analysis durable readback mismatch");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("analysis final publication failed");
    return {path.lexically_relative(root).generic_string(), Sha256File(path)};
}
void RequireWarmupAnchor(const ExecutionRecord& r, const std::filesystem::path& root)
{
    Check(r.selectedCommonW && IsCandidateWarmup(*r.selectedCommonW) && r.analyses[1], "D1-S lacks a qualified current-campaign W anchor");
    const auto path = AnalysisPath(r.manifest, root) / "d1-warmup-analysis.json";
    Check(r.analyses[1]->relativePath == path.lexically_relative(root).generic_string(), "warmup anchor path mismatch"); RejectReparsePath(path);
    Check(Sha256File(path) == r.analyses[1]->sha256, "warmup final disk hash mismatch");
    const auto analysis = RequireType<JsonObject>(JsonParser(ReadText(path)).Parse(), "warmup analysis");
    const auto& a = RequireType<JsonObject>(RequireMember(analysis, "assessment"), "warmup assessment");
    Check(RequireString(analysis, "manifest_sha256") == r.manifest.sha256 && RequireBoolean(a, "d1_warmup_qualified")
        && OptionalUnsigned(a, "selected_common_w") == r.selectedCommonW, "warmup analysis does not authorize current W");
    const auto ledgerPath = std::filesystem::path(ControlPath(r.manifest, root).wstring() + L".incomplete"); RejectReparsePath(ledgerPath);
    const auto ledger = RequireType<JsonObject>(JsonParser(ReadText(ledgerPath)).Parse(), "current ledger");
    const auto& groups = RequireType<JsonArray>(RequireMember(ledger, "groups"), "ledger groups");
    Check(groups.size() == 3U && OptionalUnsigned(ledger, "selected_common_w") == r.selectedCommonW
        && RequireString(ledger, "manifest_sha256") == r.manifest.sha256
        && Canonical(RequireMember(RequireType<JsonObject>(groups[1], "warmup group"), "analysis")) == Canonical(AnchorJson(*r.analyses[1])),
        "warmup anchor is not durably recorded in the current ledger");
}
ExitCode ExecuteManifestWithServices(const Manifest& supplied, const RuntimePaths& paths, const RuntimeServices& services)
{
    Manifest m; PreflightFacts facts;
    try
    {
        m = ParseManifest(supplied.canonicalJson); Check(services.collectFacts && services.runProcess, "missing control services");
        RejectOutputCollisions(m, paths.repositoryRoot); facts = services.collectFacts(m, paths); ValidatePreflightFacts(m, facts);
    }
    catch (const std::exception&) { return ExitCode::PreflightOrControlIncomplete; }
    ExecutionRecord r; r.manifest = m; r.observed = facts; r.startTimeUtc = TimestampUtc();
    const auto ledger = ControlPath(m, paths.repositoryRoot), analysisRoot = AnalysisPath(m, paths.repositoryRoot);
    bool owned = false, publishing = false; std::optional<D1QualificationAssessment> d1; std::vector<ProcessInput> warmup;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m.campaignTimeoutMs);
    const auto update = [&] {
        publishing = true;
        WriteExecutionRecord(ledger, r, false, owned); owned = true;
        publishing = false;
        if (services.afterDurableUpdate) services.afterDurableUpdate(r);
    };
    const auto publish = [&](const std::filesystem::path& path, std::string_view bytes) {
        publishing = true; const auto anchor = PublishAnalysis(paths.repositoryRoot, path, bytes); publishing = false; return anchor;
    };
    ExitCode outcome = ExitCode::CompletedQualificationAnalysis;
    try
    {
        std::filesystem::create_directories(ledger.parent_path()); RejectReparsePath(ledger.parent_path());
        Check(std::filesystem::create_directory(analysisRoot), "analysis directory collision"); update();
        for (std::size_t g = 0; g < 3U; ++g)
        {
            if (g == 2U && !r.selectedCommonW) { r.groupStates[g] = "skipped_by_protocol"; update(); break; }
            r.groupStates[g] = "running"; update();
            for (const auto& child : m.groups[g].children)
            {
                Check(std::chrono::steady_clock::now() < deadline, "campaign_timeout");
                ValidatePreflightFacts(m, services.collectFacts(m, paths));
                for (const auto& suffix : {"", ".incomplete", ".failure.json"}) Absent(paths.repositoryRoot / "results/local" / (child.sessionId + suffix));
                if (g == 2U) { RecheckArtifacts(r, paths.repositoryRoot); RequireWarmupAnchor(r, paths.repositoryRoot); }
                r.executions.push_back({g, child, g == 2U ? r.selectedCommonW : std::nullopt, {}, {}, "launch_pending"}); update();
                // Recheck after durable launch_pending publication too; a missing
                // or changed anchor must prevent this physical sample launch.
                if (g == 2U) RequireWarmupAnchor(r, paths.repositoryRoot);
                auto& c = r.executions.back(); const ProgressExpectation expectation{m.groups[g].phase, child.planIndex, c.selectedW};
                bool processControlFailure = false;
                try
                {
                    c.process = services.runProcess(std::filesystem::absolute(paths.repositoryRoot / m.childExecutablePath),
                        ChildArguments(m, m.groups[g].phase, child, c.selectedW), expectation,
                        std::chrono::milliseconds(m.operationTimeoutMs), std::chrono::milliseconds(m.childTimeoutMs), deadline);
                }
                catch (const std::exception&) { processControlFailure = true; c.process.terminationReason = "process_control_failure"; }
                c.continuation = "child_terminated"; update();
                c.package = InspectPackage(m, m.groups[g].phase, child, paths.repositoryRoot, c.selectedW, c.process.exitCode);
                bool validInput = false;
                if (c.package.input)
                {
                    validInput = g == 0U ? AssessA1Process(*c.package.input).InputValid()
                        : g == 1U ? AssessWarmupProcess(*c.package.input).InputValid() : AssessSampleCountProcess(*c.package.input).InputValid();
                    if (!validInput) c.package.errors.emplace_back("operational analysis input invalid");
                }
                const bool complete = c.process.exitCode == 0U && !c.process.terminationReason && c.process.progressValidation == "complete"
                    && c.process.eventCount == ProgressState(expectation).ExpectedEventCount() && c.package.structurallyValid && validInput;
                c.continuation = complete ? "continue_group" : "stop_incomplete"; update();
                if (!complete)
                {
                    outcome = processControlFailure ? ExitCode::PreflightOrControlIncomplete : ExitCode::ChildOrEvidenceIncomplete;
                    throw std::runtime_error(processControlFailure ? "process_control_failure" : "child/progress/evidence integrity incomplete");
                }
            }
            r.groupStates[g] = "integrity_complete"; update();
            const auto inputs = ReinspectGroup(r, g, paths.repositoryRoot);
            if (g == 0U)
            {
                const auto a = AssessA1Sentinel(inputs); Check(a.inputValid, "A1 group input invalid");
                r.analyses[g] = publish(analysisRoot / "a1-analysis.json", A1Analysis(r, a));
            }
            else if (g == 1U)
            {
                warmup = inputs; d1 = AssessD1Qualification(warmup); Check(d1->warmupInputValid, "D1-W group input invalid");
                r.analyses[g] = publish(analysisRoot / "d1-warmup-analysis.json", D1Analysis(r, *d1, g));
                r.selectedCommonW = d1->selectedCommonW;
            }
            else
            {
                warmup = ReinspectGroup(r, 1U, paths.repositoryRoot);
                d1 = AssessD1Qualification(warmup, std::span<const ProcessInput>(inputs)); Check(d1->sampleInputValid == true, "D1-S group input invalid");
                r.analyses[g] = publish(analysisRoot / "d1-sample-analysis.json", D1Analysis(r, *d1, g));
            }
            r.groupStates[g] = "analysis_finalized"; update();
            Check(std::chrono::steady_clock::now() < deadline, "campaign_timeout");
        }
        RecheckArtifacts(r, paths.repositoryRoot); ValidatePreflightFacts(m, services.collectFacts(m, paths));
        Check(std::chrono::steady_clock::now() < deadline, "campaign_timeout");
        r.campaignComplete = true; r.status = "completed_control";
    }
    catch (const std::exception& e)
    {
        r.campaignComplete = false; r.status = "incomplete_control"; r.failureReason = e.what();
        if (publishing) outcome = ExitCode::SupervisorPublicationFailure;
        if (outcome == ExitCode::CompletedQualificationAnalysis) outcome = ExitCode::PreflightOrControlIncomplete;
        if (!r.executions.empty() && r.groupStates[r.executions.back().groupIndex] == "running")
            r.groupStates[r.executions.back().groupIndex] = "incomplete";
    }
    try
    {
        if (!owned) return ExitCode::SupervisorPublicationFailure;
        r.endTimeUtc = TimestampUtc(); update();
        r.campaignAnalysis = PublishAnalysis(paths.repositoryRoot, analysisRoot / "campaign-analysis.json", CampaignAnalysis(r, d1)); update();
        WriteExecutionRecord(ledger, r, true, true);
    }
    catch (const std::exception&) { return ExitCode::SupervisorPublicationFailure; }
    return outcome;
}
ExitCode ExecuteManifest(const Manifest& m, const RuntimePaths& paths)
{ return ExecuteManifestWithServices(m, paths, {CollectPreflightFacts, RunSupervisedProcess, {}}); }
} // namespace computelab::ex2::stage5::control
