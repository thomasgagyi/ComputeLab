#include "ex2/Ex2CorrectnessSupervisor.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2LogicalInput.hpp"
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

namespace computelab::ex2::correctness::control
{
namespace
{
// Narrow parser and Windows ownership helpers adapted from the frozen G0
// instrument. G0 source and qualification semantics remain independently owned.
struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue
{
    using Storage = std::variant<std::nullptr_t, bool, std::uint64_t, double,
        std::string, JsonArray, JsonObject>;
    Storage storage;
};

[[noreturn]] void InvalidManifest(std::string_view detail)
{
    throw std::invalid_argument(
        "invalid EX-2 I7 correctness control: " + std::string(detail));
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
            if (character == '"') return output;
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
    std::string_view context)
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
    bool quoted = false;
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
                else quoted = false;
            }
            else field.push_back(value);
            continue;
        }
        if (value == '"')
        {
            if (!field.empty()) throw std::invalid_argument("CSV quote after field content");
            quoted = true;
        }
        else if (value == ',')
        {
            row.push_back(std::move(field));
            field.clear();
        }
        else if (value == '\r' || value == '\n')
        {
            if (value == '\r' && index + 1U < input.size() && input[index + 1U] == '\n')
                ++index;
            row.push_back(std::move(field));
            field.clear();
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


} // namespace
namespace
{
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
    const bool result = std::filesystem::exists(path, error);
    if (error) throw std::runtime_error("filesystem state unavailable");
    return result;
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
std::optional<std::size_t> ShaderIndex(std::size_t cell)
{
    Check(cell < ApprovedCoreCells().size(), "core cell out of range");
    return std::visit([](const auto& p) -> std::optional<std::size_t> {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
            return p.variant == LinearVariant::A1 ? 0U : 1U;
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            return p.variant == IndexedVariant::B1 ? 2U : 3U;
        else if constexpr (std::is_same_v<T, ContentionConfiguration>) return 4U;
        else if constexpr (std::is_same_v<T, IterativeConfiguration>) return 5U;
        else return std::nullopt;
    }, ApprovedCoreCells()[cell].parameters);
}
std::string CaptureGit(const std::filesystem::path& root, const char* arguments)
{
    Check(root.string().find('"') == std::string::npos, "invalid repository path");
    const std::string command = "git -C \"" + root.string() + "\" " + arguments;
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("git preflight launch failed");
    std::string output;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        output += buffer.data();
    if (_pclose(pipe) != 0) throw std::runtime_error("git preflight failed");
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n'))
        output.pop_back();
    return output;
}
std::string Uuid(const environment::DeviceUuid& bytes)
{
    Check(std::ranges::any_of(bytes, [](auto b) { return b != 0U; }), "zero GPU UUID");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i == 4U || i == 6U || i == 8U || i == 10U) result += '-';
        result += hex[bytes[i] >> 4U]; result += hex[bytes[i] & 15U];
    }
    return result;
}
} // namespace

std::string CanonicalManifestPayload(std::string_view json)
{
    auto value = JsonParser(json).Parse();
    auto object = RequireType<JsonObject>(value, "manifest");
    Check(object.erase("manifest_sha256") == 1U, "missing manifest hash");
    return Canonical(JsonValue{{object}});
}
std::string CalculateManifestSha256(std::string_view json)
{
    const auto payload = CanonicalManifestPayload(json);
    return Sha256(std::as_bytes(std::span(payload.data(), payload.size())));
}
Manifest ParseManifest(std::string_view json)
{
    const auto value = JsonParser(json).Parse();
    const auto& o = RequireType<JsonObject>(value, "manifest");
    RequireExactKeys(o, {"manifest_version", "manifest_type", "manifest_id",
        "manifest_sha256", "protocol_version", "machine_id", "child_executable_path",
        "supervisor_record_path", "expected_source_revision", "expected_git_dirty",
        "expected_child_executable_sha256", "expected_supervisor_executable_sha256",
        "expected_gpu_uuid", "cuda_device_ordinal", "vulkan_physical_device_index",
        "operation_timeout_ms", "child_timeout_ms", "campaign_timeout_ms",
        "continuation_policy", "declared_child_count", "children"}, "manifest");
    Check(RequireUnsigned(o, "manifest_version") == 1U, "manifest version");
    Check(RequireString(o, "manifest_type") == "i7-correctness", "manifest type");
    Check(RequireString(o, "protocol_version") == "1.1", "protocol version");
    Check(RequireString(o, "continuation_policy") == "completed-validation-only",
        "continuation policy");
    Manifest m;
    m.id = RequireString(o, "manifest_id");
    m.machineId = RequireString(o, "machine_id");
    Check(IsValidAnonymousIdentifier(m.id) && IsValidAnonymousIdentifier(m.machineId),
        "invalid anonymous identity");
    m.sha256 = RequireString(o, "manifest_sha256");
    Check(IsLowerHex(m.sha256, 64U) && m.sha256 == CalculateManifestSha256(json),
        "manifest hash mismatch");
    m.childExecutablePath = DeclaredPath(RequireString(o, "child_executable_path"));
    Check(std::filesystem::path(m.childExecutablePath).filename()
        == "ComputeLabEx2Correctness.exe", "wrong child executable name");
    m.supervisorRecordPath = DeclaredPath(RequireString(o, "supervisor_record_path"));
    Check(std::filesystem::path(m.supervisorRecordPath).parent_path().generic_string()
        == "results/local", "ledger must be directly under results/local");
    m.sourceRevision = RequireString(o, "expected_source_revision");
    m.gitDirty = RequireBoolean(o, "expected_git_dirty");
    m.childExecutableSha256 = RequireString(o, "expected_child_executable_sha256");
    m.supervisorExecutableSha256 = RequireString(o, "expected_supervisor_executable_sha256");
    m.gpuUuid = RequireString(o, "expected_gpu_uuid");
    Check(IsLowerHex(m.sourceRevision, 40U) && IsLowerHex(m.childExecutableSha256, 64U)
        && IsLowerHex(m.supervisorExecutableSha256, 64U) && IsUuid(m.gpuUuid)
        && m.gpuUuid != "00000000-0000-0000-0000-000000000000", "invalid provenance identity");
    const auto cuda = RequireUnsigned(o, "cuda_device_ordinal");
    const auto vulkan = RequireUnsigned(o, "vulkan_physical_device_index");
    Check(cuda <= INT_MAX && vulkan <= UINT32_MAX, "device index overflow");
    m.cudaDeviceOrdinal = static_cast<int>(cuda);
    m.vulkanPhysicalDeviceIndex = static_cast<std::uint32_t>(vulkan);
    m.operationTimeoutMs = RequireUnsigned(o, "operation_timeout_ms");
    m.childTimeoutMs = RequireUnsigned(o, "child_timeout_ms");
    m.campaignTimeoutMs = RequireUnsigned(o, "campaign_timeout_ms");
    Check(m.operationTimeoutMs >= 1U && m.operationTimeoutMs <= 60000U
        && m.childTimeoutMs >= m.operationTimeoutMs && m.childTimeoutMs <= 1200000U
        && m.campaignTimeoutMs >= m.childTimeoutMs && m.campaignTimeoutMs <= 86400000U,
        "deadline bounds");
    const auto& children = RequireType<JsonArray>(RequireMember(o, "children"), "children");
    Check(!children.empty() && children.size() <= 22U
        && children.size() == RequireUnsigned(o, "declared_child_count"), "child count");
    std::set<std::size_t> cells;
    std::set<std::string> paths;
    for (const auto& path : {m.supervisorRecordPath,
            m.supervisorRecordPath + ".incomplete", m.supervisorRecordPath + ".incomplete.tmp"})
        Check(paths.insert(Lower(path)).second, "ledger collision");
    for (const auto& childValue : children)
    {
        const auto& c = RequireType<JsonObject>(childValue, "child");
        RequireExactKeys(c, {"sequence_index", "core_cell_index", "session_id",
            "expected_vulkan_shader_sha256"}, "child");
        ManifestChild child;
        child.sequenceIndex = RequireUnsigned(c, "sequence_index");
        const auto index = RequireUnsigned(c, "core_cell_index");
        Check(child.sequenceIndex == m.children.size() && index < ApprovedCoreCells().size(),
            "sequence or core cell");
        child.coreCellIndex = static_cast<std::size_t>(index);
        Check(cells.insert(child.coreCellIndex).second, "duplicate core cell");
        child.sessionId = RequireString(c, "session_id");
        Check(IsValidAnonymousIdentifier(child.sessionId)
            && IsValidAnonymousIdentifier(child.sessionId + "-cuda")
            && IsValidAnonymousIdentifier(child.sessionId + "-vulkan"), "invalid session");
        for (const auto& suffix : {"", ".incomplete", ".failure.json"})
            Check(paths.insert(Lower("results/local/" + child.sessionId + suffix)).second,
                "session path collision");
        child.vulkanShaderSha256 = OptionalString(c, "expected_vulkan_shader_sha256");
        Check(ShaderIndex(child.coreCellIndex).has_value() == child.vulkanShaderSha256.has_value(),
            "shader nullability");
        if (child.vulkanShaderSha256)
            Check(IsLowerHex(*child.vulkanShaderSha256, 64U), "shader hash");
        m.children.push_back(std::move(child));
    }
    m.canonicalJson = Canonical(value);
    return m;
}

std::vector<std::string> ChildArguments(const Manifest& m, const ManifestChild& child)
{
    return {"--core-cell-index", std::to_string(child.coreCellIndex),
        "--cuda-device", std::to_string(m.cudaDeviceOrdinal),
        "--vulkan-device", std::to_string(m.vulkanPhysicalDeviceIndex),
        "--machine-id", m.machineId, "--session-id", child.sessionId};
}
void ValidatePreflightFacts(const Manifest& m, const PreflightFacts& facts)
{
    Check(facts.sourceRevision == m.sourceRevision && facts.gitDirty == m.gitDirty,
        "source preflight mismatch");
    Check(facts.childSha256 == m.childExecutableSha256
        && facts.supervisorSha256 == m.supervisorExecutableSha256, "binary preflight mismatch");
    Check(facts.cudaUuid == m.gpuUuid && facts.vulkanUuid == m.gpuUuid,
        "physical GPU identity mismatch");
    Check(facts.shaderSha256.size() == m.children.size(), "shader inventory mismatch");
    for (std::size_t i = 0; i < m.children.size(); ++i)
        Check(facts.shaderSha256[i] == m.children[i].vulkanShaderSha256, "shader preflight mismatch");
}
void RejectOutputCollisions(const Manifest& m, const std::filesystem::path& root)
{
    for (const auto& suffix : {"", ".incomplete", ".incomplete.tmp"})
        Absent(root / (m.supervisorRecordPath + suffix));
    for (const auto& child : m.children)
        for (const auto& suffix : {"", ".incomplete", ".failure.json"})
            Absent(root / "results/local" / (child.sessionId + suffix));
}
PreflightFacts CollectPreflightFacts(const Manifest& m, const RuntimePaths& paths)
{
    RejectReparsePath(paths.repositoryRoot / m.childExecutablePath);
    RejectReparsePath(paths.supervisorExecutable);
    PreflightFacts facts;
    facts.sourceRevision = CaptureGit(paths.repositoryRoot, "rev-parse --verify HEAD");
    facts.gitDirty = !CaptureGit(paths.repositoryRoot,
        "status --porcelain=v1 --untracked-files=all").empty();
    facts.childSha256 = Sha256File(paths.repositoryRoot / m.childExecutablePath);
    facts.supervisorSha256 = Sha256File(paths.supervisorExecutable);
    const auto cuda = environment::EnumerateCudaDeviceMetadata();
    const auto vulkan = environment::EnumerateVulkanDeviceMetadata();
    Check(m.cudaDeviceOrdinal >= 0 && static_cast<std::size_t>(m.cudaDeviceOrdinal) < cuda.size()
        && m.vulkanPhysicalDeviceIndex < vulkan.size(), "device unavailable");
    facts.cudaUuid = Uuid(cuda[static_cast<std::size_t>(m.cudaDeviceOrdinal)].uuid);
    facts.vulkanUuid = Uuid(vulkan[m.vulkanPhysicalDeviceIndex].uuid);
    for (const auto& child : m.children)
    {
        const auto index = ShaderIndex(child.coreCellIndex);
        facts.shaderSha256.push_back(index
            ? std::optional<std::string>(Sha256File(paths.shaders[*index])) : std::nullopt);
    }
    return facts;
}

void ProgressState::Accept(const ProgressEvent& e, std::int64_t now,
    std::int64_t timeoutTicks)
{
    if (invalid) return;
    const auto backend = eventCount < 2U ? AttemptBackend::Cuda : AttemptBackend::Vulkan;
    const auto type = eventCount % 2U == 0U ? AttemptEvent::Started : AttemptEvent::Returned;
    if (eventCount >= 4U || e.magic != ProgressMagic || e.version != ProgressVersion
        || e.backend != backend || e.event != type || e.performanceCounter <= 0
        || e.performanceCounter > now || e.performanceCounter < lastCounter
        || timeoutTicks <= 0)
    {
        invalid = true;
        return;
    }
    lastCounter = e.performanceCounter;
    if (type == AttemptEvent::Started)
    {
        outstandingStart = e.performanceCounter;
        if (now - e.performanceCounter >= timeoutTicks) timedOutBackend = backend;
    }
    else
    {
        if (!outstandingStart) { invalid = true; return; }
        if (e.performanceCounter - *outstandingStart >= timeoutTicks)
            timedOutBackend = backend;
        outstandingStart.reset();
    }
    ++eventCount;
}

ProgressReporter::ProgressReporter(std::uintptr_t handle) : handle_(handle)
{
    if (handle_ != 0U)
        Check(GetFileType(reinterpret_cast<HANDLE>(handle_)) == FILE_TYPE_PIPE,
            "progress handle is not an inherited pipe");
}
AttemptObserver ProgressReporter::Observer() noexcept
{
    return handle_ == 0U ? AttemptObserver{} : AttemptObserver{this, Publish};
}
void ProgressReporter::Publish(void* context, AttemptBackend backend,
    AttemptEvent type) noexcept
{
    const auto& reporter = *static_cast<ProgressReporter*>(context);
    LARGE_INTEGER counter{};
    DWORD written{};
    ProgressEvent event{ProgressMagic, ProgressVersion, type, backend, 0};
    const bool counterOk = QueryPerformanceCounter(&counter) != FALSE;
    event.performanceCounter = counter.QuadPart;
    if (!counterOk || !WriteFile(reinterpret_cast<HANDLE>(reporter.handle_),
            &event, sizeof(event), &written, nullptr) || written != sizeof(event))
    {
        // Fail closed without unwinding potentially unsafe native resources.
        TerminateProcess(GetCurrentProcess(), 4U);
        ExitProcess(4U);
    }
}
ProgressReporter ExtractProgressReporter(std::vector<std::string_view>& arguments)
{
    std::optional<std::uintptr_t> handle;
    for (std::size_t i = 0; i < arguments.size();)
    {
        if (arguments[i] != "--supervisor-progress-handle") { ++i; continue; }
        Check(!handle && i + 1U < arguments.size(), "duplicate or missing progress handle");
        std::uintptr_t parsed{};
        const auto value = arguments[i + 1U];
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        Check(error == std::errc{} && end == value.data() + value.size() && parsed != 0U,
            "invalid progress handle");
        handle = parsed;
        arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(i),
            arguments.begin() + static_cast<std::ptrdiff_t>(i + 2U));
    }
    return ProgressReporter(handle.value_or(0U));
}

ProcessResult RunSupervisedProcess(const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
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
        throw std::runtime_error("correctness child CreateProcessW failed");
    UniqueHandle process(info.hProcess), thread(info.hThread);
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
        return result;
    }
    result.launchTimeUtc = TimestampUtc();
    const auto childDeadline = std::chrono::steady_clock::now() + childTimeout;
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
        throw std::runtime_error("child resume failed");
    thread = UniqueHandle{};
    writer = UniqueHandle{};
    ProgressState progress;
    std::vector<std::byte> pending;
    const auto drain = [&] {
        DWORD available{};
        if (!PeekNamedPipe(reader.Get(), nullptr, 0U, nullptr, &available, nullptr))
        {
            if (GetLastError() == ERROR_BROKEN_PIPE) return;
            throw std::runtime_error("progress pipe inspection failed");
        }
        if (available > 5U * sizeof(ProgressEvent)) { progress.invalid = true; return; }
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
            progress.timedOutBackend = progress.eventCount < 3U
                ? AttemptBackend::Cuda : AttemptBackend::Vulkan;
        if (progress.invalid) result.terminationReason = "progress_protocol_error";
        else if (progress.timedOutBackend)
            result.terminationReason = *progress.timedOutBackend == AttemptBackend::Cuda
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
std::optional<std::uint64_t> OptionalUnsigned(const JsonObject& object, std::string_view key)
{
    const auto& value = RequireMember(object, key);
    if (std::holds_alternative<std::nullptr_t>(value.storage)) return std::nullopt;
    return RequireType<std::uint64_t>(value, key);
}

std::pair<std::string, std::string> ExpectedDigests(const WorkloadConfiguration& workload)
{
    return std::visit([](const auto& p) -> std::pair<std::string, std::string> {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, TransferConfiguration>)
        {
            const auto reference = ReferenceTransfer(CoreInputSeed, p.byteCount, p.direction);
            return {ByteInputSha256(reference.source), ByteInputSha256(reference.expectedDestination)};
        }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        {
            const auto targets = GenerateContentionTargets(p.elementCount, p.activeCounterCount);
            const auto zero = MakeZeroInitialCounterState(p.elementCount);
            return {ContentionLogicalInputSha256(targets, zero),
                WordInputSha256(ReferenceContentionHistogram(targets, p.elementCount))};
        }
        else
        {
            const auto input = GenerateWordInput(CoreInputSeed, p.elementCount);
            if constexpr (std::is_same_v<T, LinearConfiguration>)
                return {WordInputSha256(input), WordInputSha256(p.variant == LinearVariant::A1
                    ? ReferenceA1(input) : ReferenceA2(input))};
            else if constexpr (std::is_same_v<T, IndexedConfiguration>)
            {
                const auto permutation = p.indexPattern == IndexPattern::StructuredV1
                    ? GenerateStructuredPermutation(p.elementCount)
                    : GenerateShuffledPermutation(CoreInputSeed, p.elementCount);
                return {IndexedLogicalInputSha256(input, permutation),
                    WordInputSha256(p.variant == IndexedVariant::B1
                        ? ReferenceB1Gather(input, permutation) : ReferenceB2Scatter(input, permutation))};
            }
            else return {WordInputSha256(input), WordInputSha256(ReferenceD1(input, p.iterationCount).finalState)};
        }
    }, workload.parameters);
}

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
    // All remaining diagnostic timing fields must be null under P. Exact
    // comparison against the public serializer below also rejects unknown keys.
    return d;
}

template<class E>
E ParseEnum(std::string_view text, E last)
{
    for (int i = 0; i <= static_cast<int>(last); ++i)
        if (evidence::ToString(static_cast<E>(i)) == text) return static_cast<E>(i);
    throw std::invalid_argument("unknown evidence enum");
}

struct SeriesState { bool passed{}; bool completed{}; };

SeriesState InspectSeries(const Manifest& manifest, const ManifestChild& child,
    Backend backend, const std::filesystem::path& directory,
    const std::pair<std::string, std::string>& digests)
{
    std::set<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        Check(entry.is_regular_file() && !entry.is_symlink(), "non-regular series artifact");
        names.insert(entry.path().filename().string());
    }
    Check(names == std::set<std::string>{"environment.json", "initialization.csv", "samples.csv", "summary.json"},
        "four-file series membership");
    const auto& workload = ApprovedCoreCells().at(child.coreCellIndex);
    const auto plan = evidence::MakeCorrectnessPlan(child.sessionId + "-" + std::string(ToString(backend)),
        {{"1.1", manifest.machineId, {manifest.gpuUuid, true}, workload, InstrumentMode::P},
            backend, 0U, 0U, backend == Backend::Cuda ? 0U : 1U, 0U, 1U,
            manifest.sourceRevision, manifest.childExecutableSha256,
            backend == Backend::Cuda ? std::nullopt : child.vulkanShaderSha256});
    const auto parsed = JsonParser(ReadText(directory / "environment.json")).Parse();
    const auto& env = RequireType<JsonObject>(parsed, "environment");
    const auto common = ReadCommon(env);
    Check(common.gitDirty == manifest.gitDirty && common.gitCommit == manifest.sourceRevision
        && common.machineId == manifest.machineId, "environment provenance");
    const evidence::EnvironmentRecord environment{common, plan, digests.first, digests.second,
        ReadDiagnostics(RequireType<JsonObject>(RequireMember(env, "backend_native"), "backend_native"))};
    const auto regeneratedEnv = JsonParser(evidence::SerializeEnvironmentJson(environment)).Parse();
    Check(Canonical(parsed) == Canonical(regeneratedEnv), "environment independent identity/shape mismatch");

    const auto init = ParseCsv(ReadText(directory / "initialization.csv"));
    Check(init.size() == 2U && init[1].size() == 13U, "initialization row count/width");
    const auto& ir = init[1];
    Check(!ir[12].empty(), "setup observation absent");
    const evidence::InitializationRecord initialization{plan.runId, backend, 0U, 0U,
        "backend_setup", RequireString(env, "workload"), RequireString(env, "variant"),
        OptionalUnsigned(env, "element_count"), "setup_complete", std::nullopt, ir[12]};
    const std::array initRecords{initialization};
    Check(init == ParseCsv(evidence::SerializeInitializationCsv(plan, initRecords)),
        "initialization identity/setup/timing mismatch");

    const auto rows = ParseCsv(ReadText(directory / "samples.csv"));
    Check(rows.size() == 2U && rows[1].size() == 31U, "sample row count/width");
    const auto& row = rows[1];
    auto sample = evidence::MakeSampleRecord(plan, 0U);
    sample.status = ParseEnum(row[24], evidence::OperationStatus::Incomplete);
    if (!row[25].empty()) sample.failurePhase = ParseEnum(row[25], evidence::FailurePhase::Interrupted);
    if (!row[26].empty()) sample.errorCode = row[26];
    sample.correctness.expectedOutputGenerated = true;
    const bool completed = sample.status == evidence::OperationStatus::Ok
        || sample.status == evidence::OperationStatus::ValidationFailed;
    if (completed)
    {
        Check(row[23] == "true" || row[23] == "false", "missing sample comparison result");
        sample.correctness = {true, true, true, true, row[23] == "true"};
    }
    else
    {
        Check(row[23].empty(), "native failure fabricated validation");
        // Disk schema does not retain detailed progress flags. These values
        // establish only the minimal representable status, not GPU execution.
        sample.correctness.operationCompleted = sample.failurePhase == evidence::FailurePhase::Readback;
        Check(sample.failurePhase != evidence::FailurePhase::BackendInitialization
            && sample.failurePhase != evidence::FailurePhase::ResourceAllocation
            && sample.failurePhase != evidence::FailurePhase::Configuration
            && sample.failurePhase != evidence::FailurePhase::InputGeneration,
            "pre-setup failure cannot carry setup_complete");
    }
    const std::array samples{sample};
    Check(rows == ParseCsv(evidence::SerializeSamplesCsv(plan, samples)),
        "sample independent identity/status/timing mismatch");
    const auto summary = evidence::SummarizeSamples(plan, samples, sample.status, sample.failurePhase, sample.errorCode);
    Check(Canonical(JsonParser(ReadText(directory / "summary.json")).Parse())
        == Canonical(JsonParser(evidence::SerializeSummaryJson(summary, samples)).Parse()),
        "summary differs from raw sample reconstruction");
    evidence::ValidateEvidenceBundle(environment, initRecords, samples, summary);
    return {sample.status == evidence::OperationStatus::Ok, completed};
}

void HashArtifacts(const std::filesystem::path& path, const std::filesystem::path& root,
    PackageInspection& result)
{
    RejectReparsePath(path);
    Check(!std::filesystem::is_symlink(path), "symlink artifact");
    if (std::filesystem::is_regular_file(path))
    {
        result.artifacts.push_back({path.lexically_relative(root).generic_string(),
            std::filesystem::file_size(path), Sha256File(path)});
        return;
    }
    Check(std::filesystem::is_directory(path), "unsupported artifact type");
    for (const auto& item : std::filesystem::directory_iterator(path))
        HashArtifacts(item.path(), root, result);
}

bool SafeText(std::string_view text, std::size_t max, bool token)
{
    return !text.empty() && text.size() <= max && std::ranges::all_of(text, [token](char c) {
        const bool identifier = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        return identifier || (!token && (c == ' ' || c == ',' || c == '(' || c == ')'));
    });
}
} // namespace

PackageInspection InspectPackage(const Manifest& manifest, const ManifestChild& child,
    const std::filesystem::path& repositoryRoot, std::optional<std::uint32_t> exitCode)
{
    PackageInspection result;
    try
    {
        const auto local = repositoryRoot / "results/local";
        const auto final = local / child.sessionId;
        const auto staging = local / (child.sessionId + ".incomplete");
        const auto sidecar = local / (child.sessionId + ".failure.json");
        const bool f = Exists(final), s = Exists(staging), x = Exists(sidecar);
        for (const auto& path : {final, staging, sidecar})
            if (Exists(path)) HashArtifacts(path, repositoryRoot, result);
        std::ranges::sort(result.artifacts, {}, &Artifact::relativePath);
        if (f && s) { result.state = "collision"; throw std::invalid_argument("final/staging collision"); }
        Check(!(f && x), "final/sidecar contradiction");
        bool foundation = false;
        if (x)
        {
            const auto parsed = JsonParser(ReadText(sidecar)).Parse();
            const auto& o = RequireType<JsonObject>(parsed, "failure sidecar");
            RequireExactKeys(o, {"record_version", "record_type", "session_id", "failure_phase",
                "error_code", "foundation_established", "source_revision", "cuda_run_id",
                "vulkan_run_id", "staging_retained", "detail"}, "failure sidecar");
            Check(RequireUnsigned(o, "record_version") == 1U
                && RequireString(o, "record_type") == "ex2-i7-correctness-child-failure"
                && RequireString(o, "session_id") == child.sessionId, "sidecar identity");
            const auto phase = RequireString(o, "failure_phase");
            const std::set<std::string> phases{"configuration", "input_generation", "provenance",
                "device_identity", "interrupted", "backend_execution", "validation",
                "evidence_serialization", "evidence_publication", "package_verification"};
            Check(phases.contains(phase), "sidecar phase");
            result.sidecarPhase = phase;
            Check(SafeText(RequireString(o, "error_code"), 96U, true), "sidecar error token");
            const auto revision = OptionalString(o, "source_revision");
            Check(!revision || *revision == manifest.sourceRevision, "sidecar revision");
            foundation = RequireBoolean(o, "foundation_established");
            const auto cuda = OptionalString(o, "cuda_run_id"), vulkan = OptionalString(o, "vulkan_run_id");
            if (foundation)
                Check(cuda == child.sessionId + "-cuda" && vulkan == child.sessionId + "-vulkan", "sidecar run identities");
            else Check(!cuda && !vulkan, "pre-foundation sidecar fabricated run identity");
            Check(RequireBoolean(o, "staging_retained") == s, "sidecar staging truth");
            const auto detail = OptionalString(o, "detail");
            Check(!detail || SafeText(*detail, 256U, false), "sidecar detail");
        }
        Check(exitCode != 0U || (f && !s && !x), "exit-zero package contradiction");
        Check(!f || exitCode == 0U, "nonzero exit with finalized package");
        if (!f && !s) { result.state = "missing"; return result; }
        const auto session = f ? final : staging;
        Check(std::filesystem::is_directory(session), "session is not directory");
        std::set<std::string> expected{child.sessionId + "-cuda", child.sessionId + "-vulkan"};
        const auto digests = ExpectedDigests(ApprovedCoreCells().at(child.coreCellIndex));
        std::size_t count = 0U;
        bool allPassed = true, allCompleted = true;
        for (const auto& entry : std::filesystem::directory_iterator(session))
        {
            Check(entry.is_directory() && expected.erase(entry.path().filename().string()) == 1U,
                "unexpected session member");
            const auto backend = entry.path().filename() == child.sessionId + "-cuda" ? Backend::Cuda : Backend::Vulkan;
            // Prompt 2 creates the two directories before attempting setup.
            // Empty retained directories are absence of standard evidence,
            // not fabricated setup rows and not a completed backend.
            if (!f && std::filesystem::is_empty(entry.path())) continue;
            const auto state = InspectSeries(manifest, child, backend, entry.path(), digests);
            allPassed &= state.passed;
            allCompleted &= state.completed;
            ++count;
        }
        Check(!f || (count == 2U && allPassed), "final package does not pass");
        Check(!x || foundation || count == 0U, "standard rows without foundation");
        result.structurallyValid = true;
        result.bothCompleted = count == 2U && allCompleted;
        if (f) result.state = "complete-pass";
        else if (exitCode == 3U && x && result.sidecarPhase == "validation"
            && foundation && result.bothCompleted) result.state = "validation-failure";
        else result.state = "incomplete";
    }
    catch (const std::exception& e)
    {
        if (result.state != "collision") result.state = "contradictory";
        result.structurallyValid = false;
        result.errors.emplace_back(e.what());
    }
    return result;
}

std::string ContinuationDecision(const ProcessResult& process, const PackageInspection& package)
{
    if (package.state == "collision" || package.state == "contradictory" || !package.errors.empty())
        return "stop_evidence_or_control";
    if (process.terminationReason)
        return "stop_incomplete_or_unsafe";
    if (process.exitCode == 4U || package.sidecarPhase == "provenance"
        || package.sidecarPhase == "package_verification"
        || package.sidecarPhase == "evidence_publication"
        || package.sidecarPhase == "evidence_serialization") return "stop_evidence_or_control";
    if (process.progressValidation != "complete" || process.eventCount != 4U)
        return "stop_incomplete_or_unsafe";
    if (process.exitCode == 0U && package.state == "complete-pass" && package.structurallyValid && package.bothCompleted)
        return "continue_complete_pass";
    if (process.exitCode == 3U && package.state == "validation-failure" && package.structurallyValid
        && package.bothCompleted && package.sidecarPhase == "validation") return "continue_validation_failure";
    return "stop_incomplete_or_unsafe";
}

namespace
{
JsonValue String(std::string_view value) { return JsonValue{std::string(value)}; }
JsonValue Number(std::uint64_t value) { return JsonValue{value}; }
JsonValue Optional(const std::optional<std::string>& value)
{
    return value ? String(*value) : JsonValue{nullptr};
}
JsonValue ScheduleChild(const ManifestChild& c)
{
    return JsonValue{JsonObject{{"sequence_index", Number(c.sequenceIndex)},
        {"core_cell_index", Number(c.coreCellIndex)}, {"session_id", String(c.sessionId)},
        {"expected_vulkan_shader_sha256", Optional(c.vulkanShaderSha256)}}};
}
void RejectChildCollisions(const ManifestChild& child, const std::filesystem::path& root)
{
    for (const auto& suffix : {"", ".incomplete", ".failure.json"})
        Absent(root / "results/local" / (child.sessionId + suffix));
}
} // namespace

std::string SerializeExecutionRecord(const ExecutionRecord& r)
{
    const auto& m = r.manifest;
    JsonArray scheduled, executions;
    for (const auto& c : m.children) scheduled.push_back(ScheduleChild(c));
    for (const auto& c : r.executions)
    {
        auto object = std::get<JsonObject>(ScheduleChild(c.child).storage);
        JsonArray artifacts, errors;
        for (const auto& a : c.package.artifacts)
            artifacts.push_back(JsonValue{JsonObject{{"relative_path", String(a.relativePath)},
                {"size_bytes", Number(a.sizeBytes)}, {"sha256", String(a.sha256)}}});
        for (const auto& error : c.package.errors) errors.push_back(String(error));
        object.emplace("launch_time_utc", c.process.launchTimeUtc.empty() ? JsonValue{nullptr} : String(c.process.launchTimeUtc));
        object.emplace("exit_time_utc", c.process.exitTimeUtc.empty() ? JsonValue{nullptr} : String(c.process.exitTimeUtc));
        object.emplace("exit_code", c.process.exitCode ? Number(*c.process.exitCode) : JsonValue{nullptr});
        object.emplace("termination_reason", Optional(c.process.terminationReason));
        object.emplace("progress_validation", String(c.process.progressValidation));
        object.emplace("cuda_attempt_started", JsonValue{c.process.eventCount >= 1U});
        object.emplace("cuda_attempt_returned", JsonValue{c.process.eventCount >= 2U});
        object.emplace("vulkan_attempt_started", JsonValue{c.process.eventCount >= 3U});
        object.emplace("vulkan_attempt_returned", JsonValue{c.process.eventCount >= 4U});
        object.emplace("package_state", String(c.package.state));
        object.emplace("package_structurally_valid", JsonValue{c.package.structurallyValid});
        object.emplace("integrity_errors", JsonValue{errors});
        object.emplace("artifacts", JsonValue{artifacts});
        object.emplace("continuation_decision", String(c.continuation));
        executions.push_back(JsonValue{object});
    }
    const JsonObject object{
        {"record_version", Number(1U)}, {"record_kind", String("ex2-i7-correctness-supervisor-execution")},
        {"scope", String("correctness_control_only_no_stage4_verdict")},
        {"manifest_id", String(m.id)}, {"manifest_sha256", String(m.sha256)},
        {"manifest_type", String("i7-correctness")}, {"protocol_version", String("1.1")},
        {"machine_id", String(m.machineId)}, {"continuation_policy", String("completed-validation-only")},
        {"expected_source_revision", String(m.sourceRevision)}, {"observed_source_revision", String(r.observed.sourceRevision)},
        {"expected_git_dirty", JsonValue{m.gitDirty}}, {"observed_git_dirty", JsonValue{r.observed.gitDirty}},
        {"expected_child_executable_sha256", String(m.childExecutableSha256)}, {"actual_child_executable_sha256", String(r.observed.childSha256)},
        {"expected_supervisor_executable_sha256", String(m.supervisorExecutableSha256)}, {"actual_supervisor_executable_sha256", String(r.observed.supervisorSha256)},
        {"expected_gpu_uuid", String(m.gpuUuid)}, {"verified_cuda_gpu_uuid", String(r.observed.cudaUuid)},
        {"verified_vulkan_gpu_uuid", String(r.observed.vulkanUuid)},
        {"operation_timeout_ms", Number(m.operationTimeoutMs)}, {"child_timeout_ms", Number(m.childTimeoutMs)},
        {"campaign_timeout_ms", Number(m.campaignTimeoutMs)}, {"start_time_utc", String(r.startTimeUtc)},
        {"end_time_utc", r.endTimeUtc.empty() ? JsonValue{nullptr} : String(r.endTimeUtc)},
        {"scheduled_children", JsonValue{scheduled}}, {"executions", JsonValue{executions}},
        {"overall_execution_status", String(r.status)}, {"failure_reason", Optional(r.failureReason)}};
    return Canonical(JsonValue{object}) + "\n";
}

void WriteExecutionRecord(const std::filesystem::path& path, const ExecutionRecord& record,
    bool complete, bool replaceOwnedIncomplete)
{
    const auto staging = std::filesystem::path(path.wstring() + L".incomplete");
    const auto temporary = std::filesystem::path(path.wstring() + L".incomplete.tmp");
    Absent(path);
    if (!replaceOwnedIncomplete) Absent(staging);
    // The caller owns staging only after the collision-free initial write.
    // CREATE_NEW preserves any unexpected temporary file, including a partial
    // durable write left by a previous crash. No cleanup hides that evidence.
    WriteDurableFile(temporary, SerializeExecutionRecord(record));
    if (!MoveFileExW(temporary.c_str(), staging.c_str(), MOVEFILE_WRITE_THROUGH
            | (replaceOwnedIncomplete ? MOVEFILE_REPLACE_EXISTING : 0U)))
        throw std::runtime_error("ledger staging publication failed");
    if (complete && !MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("ledger final publication failed");
}

ExitCode ExecuteManifest(const Manifest& supplied, const RuntimePaths& paths)
{
    // Only strict parsed manifests enter execution; caller mutation of a
    // convenient public value cannot bypass the signed canonical schedule.
    Manifest m;
    PreflightFacts facts;
    try
    {
        m = ParseManifest(supplied.canonicalJson);
        RejectOutputCollisions(m, paths.repositoryRoot);
        facts = CollectPreflightFacts(m, paths);
        ValidatePreflightFacts(m, facts);
    }
    catch (...) { return ExitCode::ConfigurationOrPreflightError; }
    ExecutionRecord record{m, facts, TimestampUtc()};
    const auto recordPath = paths.repositoryRoot / m.supervisorRecordPath;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m.campaignTimeoutMs);
    ExitCode outcome = ExitCode::Success;
    bool ledgerOwned = false;
    try
    {
        // Recheck immediately before obtaining ownership; never overwrite an
        // existing ledger from a previous execution.
        RejectOutputCollisions(m, paths.repositoryRoot);
        WriteExecutionRecord(recordPath, record, false);
        ledgerOwned = true;
        for (const auto& child : m.children)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                outcome = ExitCode::IncompleteOrTimeout;
                record.failureReason = "campaign_timeout_before_launch";
                break;
            }
            const auto current = CollectPreflightFacts(m, paths);
            ValidatePreflightFacts(m, current);
            RejectChildCollisions(child, paths.repositoryRoot);
            record.executions.push_back(ChildRecord{child});
            WriteExecutionRecord(recordPath, record, false, true);
            auto& execution = record.executions.back();
            execution.process = RunSupervisedProcess(
                std::filesystem::absolute(paths.repositoryRoot / m.childExecutablePath), ChildArguments(m, child),
                std::chrono::milliseconds(m.operationTimeoutMs), std::chrono::milliseconds(m.childTimeoutMs), deadline);
            execution.package = InspectPackage(m, child, paths.repositoryRoot, execution.process.exitCode);
            execution.continuation = "inspection_complete";
            WriteExecutionRecord(recordPath, record, false, true);
            execution.continuation = ContinuationDecision(execution.process, execution.package);
            WriteExecutionRecord(recordPath, record, false, true);
            if (execution.continuation == "continue_complete_pass") continue;
            if (execution.continuation == "continue_validation_failure")
            {
                outcome = ExitCode::CorrectnessFailure;
                continue;
            }
            record.failureReason = execution.continuation;
            outcome = execution.continuation == "stop_evidence_or_control"
                ? ExitCode::EvidenceOrControlFailure : ExitCode::IncompleteOrTimeout;
            break;
        }
    }
    catch (const std::exception& error)
    {
        record.failureReason = error.what();
        outcome = ExitCode::EvidenceOrControlFailure;
    }
    record.endTimeUtc = TimestampUtc();
    record.status = outcome == ExitCode::Success ? "execution_complete"
        : outcome == ExitCode::CorrectnessFailure ? "correctness_failed"
        : outcome == ExitCode::IncompleteOrTimeout ? "stopped_incomplete_or_unsafe" : "control_or_evidence_failed";
    if (ledgerOwned)
    {
        try { WriteExecutionRecord(recordPath, record, true, true); }
        catch (...) { return ExitCode::EvidenceOrControlFailure; }
    }
    return outcome;
}

} // namespace computelab::ex2::correctness::control
