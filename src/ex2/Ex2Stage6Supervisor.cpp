#include "ex2/Ex2Stage6Supervisor.hpp"
#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2ContentionTargets.hpp"

#include "ex2/Ex2Sha256.hpp"
#include "environment/EnvironmentCollector.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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

namespace computelab::ex2::stage6::control
{
namespace
{
// Narrow parsing and Windows ownership mechanics adapted from I7/G0.
// Historical source/contracts remain independently owned and unchanged.
struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;
// The ledger's fixed numeric transcript has no arbitrary JSON values. Keep its
// validated canonical bytes compact instead of allocating eight map nodes per
// frame at every durable barrier. The on-disk schema is unchanged.
struct EncodedTranscript { std::string bytes; std::size_t count{}; };

struct JsonValue
{
    using Storage = std::variant<std::nullptr_t, bool, std::uint64_t, std::int64_t, double,
        std::string, JsonArray, JsonObject, EncodedTranscript>;
    Storage storage;
};

[[noreturn]] void InvalidManifest(std::string_view detail)
{
    throw std::invalid_argument(
        "invalid EX-2 Stage-6 control: " + std::string(detail));
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
    explicit JsonParser(std::string_view input, bool compactTranscripts = false)
        : input_{input}, compactTranscripts_{compactTranscripts} {}

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
        const char* const begin = input_.data() + position_;
        const char* cursor = begin; const char* const limit = input_.data() + input_.size(); bool nonAscii{};
        while (cursor != limit && *cursor != '"' && *cursor != '\\' && static_cast<unsigned char>(*cursor) >= 0x20U) {
            nonAscii |= static_cast<unsigned char>(*cursor) >= 0x80U; ++cursor;
        }
        if (cursor != limit && *cursor == '"') {
            std::string output(begin, cursor);
            if (nonAscii && !ValidUtf8(output)) Fail("invalid UTF-8 string");
            position_ = static_cast<std::size_t>(cursor - input_.data()) + 1; return output;
        }
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
        if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            std::uint64_t integer{};
            const auto [end, error] = std::from_chars(input_.data() + position_, input_.data() + input_.size(), integer);
            if (end == input_.data() + input_.size() || (*end != '.' && *end != 'e' && *end != 'E')) {
                if (error != std::errc{}) Fail("integer is outside uint64 range");
                if (input_[position_] == '0' && end - (input_.data() + position_) > 1) Fail("leading zero in number");
                position_ = static_cast<std::size_t>(end - input_.data()); return {{integer}};
            }
        }
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
            if (!output.emplace(key, compactTranscripts_ && key == "progress_transcript"
                    ? JsonValue{{ParseTranscript()}} : ParseValue()).second)
                Fail("duplicate object member");
            if (Consume('}')) return output;
            if (!Consume(',')) Fail("expected comma in object");
        }
    }

    EncodedTranscript ParseTranscript()
    {
        const auto start = position_;
        RequireLiteral("["); std::size_t count{};
        if (input_.substr(position_, 1) != "]") for (;;) {
            if (++count > 201) Fail("progress transcript bound");
            RequireLiteral("{"); std::size_t field{};
            for (const auto key : {"child_qpc", "event", "magic", "receive_qpc", "reserved", "sample_index", "slot_sequence_index", "version"}) {
                if (field++) RequireLiteral(","); RequireLiteral("\""); RequireLiteral(key); RequireLiteral("\":");
                const auto numberStart = position_;
                const auto value = ParseNumber(); std::string canonical;
                const bool counter = std::string_view(key) == "child_qpc" || std::string_view(key) == "receive_qpc";
                if (const auto u = std::get_if<std::uint64_t>(&value.storage)) {
                    if (counter && *u > INT64_MAX) Fail("QPC outside signed range");
                    if ((field == 2 || field == 3 || field == 5 || field == 8) && *u > UINT32_MAX) Fail("wire word outside uint32 range");
                    canonical = std::to_string(*u);
                } else if (const auto i = std::get_if<std::int64_t>(&value.storage); i && counter) canonical = std::to_string(*i);
                else Fail("progress field is not a wire integer");
                if (input_.substr(numberStart, position_ - numberStart) != canonical) Fail("noncanonical progress integer");
            }
            RequireLiteral("}");
            if (input_.substr(position_, 1) == "]") break;
            RequireLiteral(",");
        }
        RequireLiteral("]"); return {std::string(input_.substr(start, position_ - start)), count};
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
    bool compactTranscripts_{};
};

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    bool escape{};
    for (const char* cursor = value.data(), *end = value.data() + value.size(); cursor != end; ++cursor)
        if (*cursor == '"' || *cursor == '\\' || static_cast<unsigned char>(*cursor) < 0x20U) { escape = true; break; }
    if (!escape) { output += '"'; output.append(value); output += '"'; return; }
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
        else if constexpr (std::is_same_v<T, EncodedTranscript>) output += item.bytes;
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
void RejectReparsePath(const std::filesystem::path&);
std::string ReadText(const std::filesystem::path& path, std::uint64_t limit = 16U * 1024U * 1024U)
{
    RejectReparsePath(path);
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    Check(file.Get() != INVALID_HANDLE_VALUE, "artifact open failed");
    BY_HANDLE_FILE_INFORMATION information{}; LARGE_INTEGER size{};
    Check(GetFileInformationByHandle(file.Get(), &information) && GetFileSizeEx(file.Get(), &size)
        && GetFileType(file.Get()) == FILE_TYPE_DISK
        && !(information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= limit, "artifact type or size invalid");
    std::wstring actual(32768, L'\0');
    const auto n = GetFinalPathNameByHandleW(file.Get(), actual.data(), static_cast<DWORD>(actual.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    Check(n && n < actual.size(), "artifact physical path unavailable"); actual.resize(n);
    if (actual.starts_with(L"\\\\?\\")) actual.erase(0, 4);
    const auto expected = std::filesystem::absolute(path).lexically_normal().native();
    Check(CompareStringOrdinal(actual.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL, "artifact escaped expected path");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD read{};
    Check(bytes.empty() || (ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
        && read == bytes.size()), "artifact short read");
    char extra{}; Check(ReadFile(file.Get(), &extra, 1, &read, nullptr) && !read, "artifact changed while reading");
    RejectReparsePath(path); return bytes;
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
JsonValue J(std::int64_t x) { return {{x}}; }
JsonValue J(bool x) { return {{x}}; }
JsonValue J(double x) { Check(std::isfinite(x), "nonfinite analysis fact"); return {{x}}; }
JsonValue NullableText(std::string_view x) { return x.empty() ? JsonValue{{nullptr}} : J(x); }
template<class T> JsonValue J(const std::optional<T>& x) { return x ? J(*x) : JsonValue{{nullptr}}; }
constexpr std::array<std::string_view, 6> ShaderNames{"a1", "a2", "b1", "b2", "c", "d1"};
constexpr std::array<std::string_view, 4> PackageNames{"environment.json", "initialization.csv", "samples.csv", "summary.json"};
std::string SlotSession(std::string_view id, std::uint64_t slot)
{ auto number = std::to_string(slot); return std::string(id) + "-slot-" + std::string(3 - number.size(), '0') + number; }
const ManifestChild& Child(const Manifest& m, std::uint64_t slot)
{
    Check(slot < TotalChildCount && Anonymous(m.id), "invalid slot");
    const auto& child = m.groups[slot / ChildrenPerCell].children[slot % ChildrenPerCell];
    Check(child.sequenceIndex == slot && child.sessionId == SlotSession(m.id, slot) && Anonymous(child.sessionId), "unsafe child identity"); return child;
}
std::int64_t NowQpc()
{ LARGE_INTEGER v{}; Check(QueryPerformanceCounter(&v) && v.QuadPart > 0, "QPC unavailable"); return v.QuadPart; }
std::int64_t Frequency()
{ LARGE_INTEGER v{}; Check(QueryPerformanceFrequency(&v) && v.QuadPart > 0, "QPC frequency unavailable"); return v.QuadPart; }
std::filesystem::path ManifestPath(const Manifest& m, const std::filesystem::path& root)
{ return root / "results/local" / (m.id + "-stage6-manifest.json"); }
void RejectSlotCollision(const Manifest& m, std::uint64_t slot, const std::filesystem::path& root)
{ for (const auto* suffix : {"", ".incomplete", ".failure.json"}) Absent(root / "results/local" / (Child(m, slot).sessionId + suffix)); }
} // namespace

std::string CanonicalManifestPayload(std::string_view bytes)
{
    Check(bytes.size() <= 1024U * 1024U, "manifest exceeds read bound");
    auto object = RequireType<JsonObject>(JsonParser(bytes).Parse(), "manifest");
    Check(object.erase("manifest_sha256") == 1, "manifest hash missing"); return Canonical({{object}});
}
std::string CalculateManifestSha256(std::string_view bytes) { return Sha256(CanonicalManifestPayload(bytes)); }
Manifest ParseManifest(std::string_view bytes)
{
    Check(bytes.size() <= 1024U * 1024U, "manifest exceeds read bound");
    const auto object = RequireType<JsonObject>(JsonParser(bytes).Parse(), "manifest");
    RequireExactKeys(object, {"manifest_version", "manifest_type", "manifest_id", "manifest_sha256", "protocol_version",
        "evidence_schema_version", "evidence_kind", "instrument_mode", "machine_id", "child_executable_path", "expected_source_revision",
        "expected_git_dirty", "expected_child_executable_sha256", "expected_supervisor_executable_sha256", "expected_gpu_uuid",
        "cuda_device_ordinal", "vulkan_physical_device_index", "expected_vulkan_shader_sha256", "operation_timeout_ms", "child_timeout_ms",
        "campaign_timeout_ms", "continuation_policy", "declared_cell_group_count", "declared_child_count", "groups"}, "manifest");
    Check(RequireUnsigned(object, "manifest_version") == 1 && RequireString(object, "manifest_type") == "ex2-stage6-diagnostic"
        && RequireString(object, "protocol_version") == "1.3" && RequireUnsigned(object, "evidence_schema_version") == 2
        && RequireString(object, "evidence_kind") == "diagnostic" && RequireString(object, "instrument_mode") == "H"
        && RequireString(object, "continuation_policy") == "resolved-only-no-retry" && !RequireBoolean(object, "expected_git_dirty")
        && RequireUnsigned(object, "declared_cell_group_count") == CoreCellCount && RequireUnsigned(object, "declared_child_count") == TotalChildCount,
        "manifest frozen assertions differ");
    Manifest m; m.id = RequireString(object, "manifest_id"); m.machineId = RequireString(object, "machine_id");
    Check(Anonymous(m.id) && m.id.size() <= 87 && Anonymous(m.machineId), "unsafe manifest identity");
    m.sha256 = RequireString(object, "manifest_sha256"); m.sourceRevision = RequireString(object, "expected_source_revision");
    m.childExecutablePath = DeclaredPath(RequireString(object, "child_executable_path"));
    m.childExecutableSha256 = RequireString(object, "expected_child_executable_sha256");
    m.supervisorExecutableSha256 = RequireString(object, "expected_supervisor_executable_sha256");
    m.gpuUuid = RequireString(object, "expected_gpu_uuid");
    Check(IsLowerHex(m.sourceRevision, 40) && IsLowerHex(m.sha256, 64) && IsLowerHex(m.childExecutableSha256, 64)
        && IsLowerHex(m.supervisorExecutableSha256, 64) && IsUuid(m.gpuUuid)
        && m.gpuUuid != "00000000-0000-0000-0000-000000000000", "invalid provenance identity");
    const auto cuda = RequireUnsigned(object, "cuda_device_ordinal"), vulkan = RequireUnsigned(object, "vulkan_physical_device_index");
    Check(cuda <= INT_MAX && vulkan <= UINT32_MAX, "device selector overflow");
    m.cudaDeviceOrdinal = static_cast<int>(cuda); m.vulkanPhysicalDeviceIndex = static_cast<std::uint32_t>(vulkan);
    const auto& shaders = RequireType<JsonObject>(RequireMember(object, "expected_vulkan_shader_sha256"), "shaders");
    RequireExactKeys(shaders, {"a1", "a2", "b1", "b2", "c", "d1"});
    for (std::size_t i = 0; i < 6; ++i) { m.shaderSha256[i] = RequireString(shaders, ShaderNames[i]); Check(IsLowerHex(m.shaderSha256[i], 64), "invalid shader hash"); }
    m.operationTimeoutMs = RequireUnsigned(object, "operation_timeout_ms"); m.childTimeoutMs = RequireUnsigned(object, "child_timeout_ms");
    m.campaignTimeoutMs = RequireUnsigned(object, "campaign_timeout_ms");
    Check(m.operationTimeoutMs >= 1 && m.operationTimeoutMs <= 60000 && m.childTimeoutMs >= m.operationTimeoutMs
        && m.childTimeoutMs <= 1200000 && m.campaignTimeoutMs >= m.childTimeoutMs && m.campaignTimeoutMs <= 86400000, "invalid deadlines");
    const auto& groups = RequireType<JsonArray>(RequireMember(object, "groups"), "groups"); Check(groups.size() == CoreCellCount, "requires 22 groups");
    std::set<std::string> names;
    for (std::size_t g = 0; g < CoreCellCount; ++g) {
        const auto& group = RequireType<JsonObject>(groups[g], "group"); RequireExactKeys(group, {"cell_index", "declared_child_count", "children"});
        Check(RequireUnsigned(group, "cell_index") == g && RequireUnsigned(group, "declared_child_count") == ChildrenPerCell, "invalid group");
        m.groups[g].cellIndex = g; const auto& children = RequireType<JsonArray>(RequireMember(group, "children"), "children");
        Check(children.size() == ChildrenPerCell, "requires ten children");
        for (std::size_t p = 0; p < ChildrenPerCell; ++p) {
            const auto& item = RequireType<JsonObject>(children[p], "child"); RequireExactKeys(item, {"sequence_index", "session_id"});
            auto& child = m.groups[g].children[p]; child.sequenceIndex = RequireUnsigned(item, "sequence_index"); child.sessionId = RequireString(item, "session_id");
            Check(child.sequenceIndex == g * ChildrenPerCell + p && child.sessionId == SlotSession(m.id, child.sequenceIndex)
                && Anonymous(child.sessionId) && names.insert(Lower(child.sessionId)).second, "invalid frozen child identity");
            const auto slot = FrozenCampaignPlan()[child.sequenceIndex]; ValidatePlannedSlot(slot);
            Check(slot.cellIndex == g && slot.process == FrozenCellProcessPlan()[p], "frozen schedule mismatch");
        }
    }
    m.canonicalJson = Canonical({{object}}); Check(bytes == m.canonicalJson + "\n", "manifest bytes are not canonical");
    Check(CalculateManifestSha256(bytes) == m.sha256, "manifest hash mismatch"); m.fileSha256 = Sha256(bytes); return m;
}
std::filesystem::path ParseSupervisorArguments(std::span<const std::string_view> args)
{ Check(args.size() == 2 && args[0] == "--manifest", "only --manifest is supported"); return DeclaredPath(std::string(args[1])); }
Manifest LoadManifestFromRepository(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    const auto normalized = DeclaredPath(relative.generic_string());
    const auto m = ParseManifest(ReadText(root / normalized, 1024U * 1024U));
    Check(normalized == ManifestPath(m, {}).generic_string(), "manifest path differs from identity"); return m;
}
std::filesystem::path ControlPath(const Manifest& m, const std::filesystem::path& root) { return root / "results/local" / (m.id + "-stage6-control.json"); }
std::filesystem::path AnalysisPath(const Manifest& m, const std::filesystem::path& root) { return root / "results/local" / (m.id + "-stage6-analysis"); }
std::vector<std::string> ChildArguments(const Manifest& m, std::uint64_t sequence)
{
    const auto& child = Child(m, sequence); const auto slot = FrozenCampaignPlan()[sequence];
    return {"--cell-index", std::to_string(slot.cellIndex), "--plan-index", std::to_string(sequence % ChildrenPerCell),
        "--cuda-device", std::to_string(m.cudaDeviceOrdinal), "--vulkan-device", std::to_string(m.vulkanPhysicalDeviceIndex),
        "--machine-id", m.machineId, "--session-id", child.sessionId, "--expected-gpu-uuid", m.gpuUuid};
}
void ValidatePreflightFacts(const Manifest& m, const PreflightFacts& f)
{
    Check(f.sourceRevision == m.sourceRevision && !f.gitDirty, "source_provenance_drift");
    Check(f.childSha256 == m.childExecutableSha256 && f.supervisorSha256 == m.supervisorExecutableSha256, "binary_provenance_drift");
    Check(f.shaderSha256 == m.shaderSha256, "shader_provenance_drift");
    Check(f.cudaUuid == m.gpuUuid && f.vulkanUuid == m.gpuUuid, "gpu_identity_drift");
}
void RejectOutputCollisions(const Manifest& m, const std::filesystem::path& root)
{
    std::set<std::string> reserved;
    const auto reserve = [&](const std::filesystem::path& path) { Check(reserved.insert(Lower(path.generic_string())).second, "namespace alias"); Absent(path); };
    for (std::uint64_t i = 0; i < TotalChildCount; ++i)
        for (const auto* suffix : {"", ".incomplete", ".failure.json"}) reserve(root / "results/local" / (Child(m, i).sessionId + suffix));
    for (const auto* suffix : {L"", L".incomplete", L".incomplete.tmp"}) reserve(std::filesystem::path(ControlPath(m, root).wstring() + suffix));
    reserve(AnalysisPath(m, root));
}
PreflightFacts CollectPreflightFacts(const Manifest& m, const RuntimePaths& paths)
{
    std::wstring module(32768, L'\0'); const auto n = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    Check(n && n < module.size(), "supervisor module unavailable"); module.resize(n);
    Check(std::filesystem::equivalent(module, paths.supervisorExecutable), "supervisor must hash running module");
    PreflightFacts f; f.sourceRevision = CaptureGit(paths.repositoryRoot, "rev-parse --verify HEAD");
    f.gitDirty = !CaptureGit(paths.repositoryRoot, "status --porcelain=v1 --untracked-files=all").empty();
    const auto hash = [](const auto& path) { return Sha256(ReadText(path, 256U * 1024U * 1024U)); };
    f.childSha256 = hash(paths.repositoryRoot / m.childExecutablePath); f.supervisorSha256 = hash(std::filesystem::path(module));
    for (std::size_t i = 0; i < 6; ++i) f.shaderSha256[i] = hash(paths.shaders[i]);
    const auto cuda = environment::EnumerateCudaDeviceMetadata(); const auto vulkan = environment::EnumerateVulkanDeviceMetadata();
    Check(m.cudaDeviceOrdinal >= 0 && static_cast<std::size_t>(m.cudaDeviceOrdinal) < cuda.size() && m.vulkanPhysicalDeviceIndex < vulkan.size(), "device unavailable");
    f.cudaUuid = Uuid(cuda[static_cast<std::size_t>(m.cudaDeviceOrdinal)].uuid); f.vulkanUuid = Uuid(vulkan[m.vulkanPhysicalDeviceIndex].uuid); return f;
}
std::int64_t DeadlineTicks(std::uint64_t ms, std::int64_t frequency)
{
    Check(ms > 0 && frequency > 0, "invalid clock conversion");
    const auto f = static_cast<std::uint64_t>(frequency), whole = f / 1000, remainder = f % 1000;
    Check(ms <= static_cast<std::uint64_t>(INT64_MAX) / std::max<std::uint64_t>(whole, 1)
        && ms <= (UINT64_MAX - 999) / std::max<std::uint64_t>(remainder, 1), "deadline conversion overflow");
    const auto partial = (ms * remainder + 999) / 1000, base = ms * whole;
    Check(base <= static_cast<std::uint64_t>(INT64_MAX) - partial, "deadline overflow"); return static_cast<std::int64_t>(base + partial);
}
std::int64_t AddDeadline(std::int64_t start, std::int64_t ticks)
{ Check(start > 0 && ticks > 0 && start <= INT64_MAX - ticks, "deadline addition overflow"); return start + ticks; }

ProcessResult RunSupervisedProcess(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
    std::uint64_t slot, std::uint64_t operationMs, std::uint64_t childMs, std::int64_t frequency, std::int64_t campaignDeadline, ProcessTestFaults faults)
{
    Check(executable.is_absolute() && slot < TotalChildCount && operationMs >= 1 && operationMs <= 60000
        && childMs >= operationMs && childMs <= 1200000 && frequency > 0 && campaignDeadline > 0, "invalid supervised request");
    ProcessResult result; progress::State state(slot);
    UniqueHandle job, process, primaryThread, pipeRead, pipeWrite;
    std::unique_ptr<progress::BlockingReader> reader;
    const auto operationTicks = DeadlineTicks(operationMs, frequency), childTicks = DeadlineTicks(childMs, frequency);
    bool readerFinished{};
    const auto consume = [&] {
        if (!reader) return;
        const auto batch = reader->Drain(); readerFinished = batch.finished;
        result.cleanEof = batch.cleanEof; result.trailingBytes = batch.trailingBytes;
        result.progressTransportFailed |= batch.transportFailed;
        for (const auto& observation : batch.observations) {
            const auto start = state.outstandingStart;
            state.Accept(observation);
            if (!state.invalid && observation.record.event == execution::AttemptEvent::Returned && start
                && observation.record.performanceCounter - *start >= operationTicks) {
                result.operationTimedOut = true;
                if (!result.timedOutSampleIndex) result.timedOutSampleIndex = observation.record.sampleIndex;
            }
        }
        state.invalid |= batch.protocolInvalid || batch.overflow || batch.trailingBytes != 0;
    };
    const auto force = [&] {
        result.supervisorTerminationRequested = true;
        result.terminateJobSucceeded = job.Get() && TerminateJobObject(job.Get(), SupervisorForcedExitCode);
        if (!result.terminateJobSucceeded || !result.containmentAssigned) {
            result.containmentVerificationFailed = true;
            if (process.Get()) TerminateProcess(process.Get(), SupervisorForcedExitCode);
        }
    };
    try {
        if (NowQpc() >= campaignDeadline) { result.campaignTimedOut = true; return result; }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE r{}, w{};
        Check(CreatePipe(&r, &w, &security, 4096), "progress pipe creation failed"); pipeRead = UniqueHandle(r); pipeWrite = UniqueHandle(w);
        Check(SetHandleInformation(pipeRead.Get(), HANDLE_FLAG_INHERIT, 0), "progress inheritance failed");
        job = UniqueHandle(CreateJobObjectW(nullptr, nullptr)); Check(job.Get(), "job creation failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "job limit failed");
        SIZE_T size{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &size); std::vector<std::byte> storage(size);
        auto attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        Check(InitializeProcThreadAttributeList(attributes, 1, 0, &size), "attribute list creation failed");
        struct AttributeGuard { PPROC_THREAD_ATTRIBUTE_LIST p; ~AttributeGuard() { DeleteProcThreadAttributeList(p); } } guard{attributes};
        HANDLE inherited[] = {pipeWrite.Get()};
        Check(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr), "restricted inheritance failed");
        auto command = QuoteWindowsArgument(executable.native());
        for (const auto& argument : arguments) command += L" " + QuoteWindowsArgument(Utf8ToWide(argument));
        command += L" --supervisor-progress-handle " + std::to_wstring(reinterpret_cast<std::uintptr_t>(pipeWrite.Get()));
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes; PROCESS_INFORMATION info{};
        Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &info), "child creation failed");
        process = UniqueHandle(info.hProcess); primaryThread = UniqueHandle(info.hThread);
        result.processCreated = true; result.processId = info.dwProcessId; result.launchTimeUtc = TimestampUtc();
        Check(AssignProcessToJobObject(job.Get(), process.Get()), "job assignment failed"); result.containmentAssigned = true;
        BOOL inside{}; Check(IsProcessInJob(process.Get(), job.Get(), &inside) && inside, "job membership verification failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION actual{};
        Check(QueryInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &actual, sizeof(actual), nullptr)
            && (actual.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
            && !(actual.BasicLimitInformation.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)), "job containment limits changed");
        result.containmentVerified = true;
        reader = std::make_unique<progress::BlockingReader>(reinterpret_cast<std::uintptr_t>(pipeRead.Release()), slot);
        if (NowQpc() >= campaignDeadline) { result.campaignTimedOut = true; force(); }
        else {
            Check(ResumeThread(primaryThread.Get()) != static_cast<DWORD>(-1), "child resume failed");
            result.processResumed = true; const auto childDeadline = AddDeadline(NowQpc(), childTicks);
            primaryThread = UniqueHandle{}; pipeWrite = UniqueHandle{};
            for (;;) {
                consume();
                const auto now = NowQpc();
                const bool terminated = WaitForSingleObject(process.Get(), 0) == WAIT_OBJECT_0;
                if (state.outstandingStart && now - *state.outstandingStart >= operationTicks) {
                    result.operationTimedOut = true;
                    if (!result.timedOutSampleIndex) result.timedOutSampleIndex = state.activeAttempt->sampleIndex;
                }
                result.campaignTimedOut |= now >= campaignDeadline;
                result.childTimedOut |= !terminated && now >= childDeadline;
                if (terminated) break;
                if (state.invalid || result.progressTransportFailed || result.operationTimedOut || result.campaignTimedOut || result.childTimedOut) { force(); break; }
                auto deadline = std::min(childDeadline, campaignDeadline);
                if (state.outstandingStart) deadline = std::min(deadline, AddDeadline(*state.outstandingStart, operationTicks));
                const auto remaining = deadline - now;
                const auto waitMs = static_cast<DWORD>(std::clamp<long double>(std::ceil(static_cast<long double>(remaining) * 1000 / frequency), 1, MAXDWORD - 1));
                HANDLE handles[] = {process.Get(), reinterpret_cast<HANDLE>(reader->ReadyHandle())};
                const auto wait = WaitForMultipleObjects(2, handles, FALSE, waitMs);
                Check(wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1 || wait == WAIT_TIMEOUT, "event wait failed");
            }
        }
    } catch (...) { result.controlError = "process_control_failure"; if (process.Get()) force(); }
    // The process/thread handles must be released before final Job accounting.
    primaryThread = UniqueHandle{}; pipeWrite = UniqueHandle{};
    if (process.Get()) {
        if (WaitForSingleObject(process.Get(), static_cast<DWORD>(ContainmentGraceMs)) == WAIT_OBJECT_0) {
            result.primaryTerminationConfirmed = true; DWORD exit{};
            if (GetExitCodeProcess(process.Get(), &exit)) result.exitCode = exit; else result.controlError = "exit_code_unavailable";
            result.exitTimeUtc = TimestampUtc();
        } else { result.containmentVerificationFailed = true; force(); TerminateProcess(process.Get(), SupervisorForcedExitCode); }
        process = UniqueHandle{};
    }
    if (job.Get()) {
        const auto accounting = [&] {
            if (faults.finalAccountingUnavailableAfterSurvival && result.descendantSurvivalObserved) return false;
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION a{};
            if (!QueryInformationJobObject(job.Get(), JobObjectBasicAccountingInformation, &a, sizeof(a), nullptr)) return false;
            std::uint32_t total = a.TotalProcesses, active = a.ActiveProcesses;
            if (result.primaryTerminationConfirmed && !result.supervisorTerminationRequested && faults.naturalAccountingSample)
                faults.naturalAccountingSample(total, active);
            result.jobTotalProcesses = total; result.jobActiveProcesses = active; return true;
        };
        const auto liveDescendant = [&] {
            // A residual accounting count is not liveness evidence. Opened handles
            // pin process identity; membership verification rejects reused foreign PIDs.
            std::vector<ULONG_PTR> storage(66);
            for (;;) {
                auto* ids = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
                if (QueryInformationJobObject(job.Get(), JobObjectBasicProcessIdList, ids,
                    static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR)), nullptr)) {
                    for (DWORD i = 0; i < ids->NumberOfProcessIdsInList; ++i) {
                        const auto pid = ids->ProcessIdList[i];
                        if (pid == result.processId.value_or(0) || pid > MAXDWORD) continue;
                        UniqueHandle member(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid)));
                        BOOL inside{};
                        if (member.Get() && IsProcessInJob(member.Get(), job.Get(), &inside) && inside
                            && WaitForSingleObject(member.Get(), 0) == WAIT_TIMEOUT) return true;
                    }
                    return false;
                }
                if (GetLastError() != ERROR_MORE_DATA || storage.size() >= 65538) return false;
                storage.resize(std::min<std::size_t>(65538, std::max(storage.size() * 2,
                    static_cast<std::size_t>(ids->NumberOfAssignedProcesses) + 2)));
            }
        };
        if (result.primaryTerminationConfirmed && !result.supervisorTerminationRequested) {
            const auto until = GetTickCount64() + ContainmentGraceMs;
            for (;;) {
                if (accounting() && result.jobActiveProcesses == 0) { result.jobEmptyConfirmed = true; break; }
                if (liveDescendant()) { result.descendantSurvivalObserved = true; force(); break; }
                if (GetTickCount64() >= until) { result.containmentVerificationFailed = true; force(); break; }
                Sleep(5);
            }
        }
        if (!result.jobEmptyConfirmed) {
            if (!result.supervisorTerminationRequested) { result.containmentVerificationFailed = true; force(); }
            const auto until = GetTickCount64() + ContainmentGraceMs;
            for (;;) {
                if (accounting() && result.jobActiveProcesses == 0) { result.jobEmptyConfirmed = true; break; }
                if (GetTickCount64() >= until) break;
                Sleep(5);
            }
        }
        if (!result.jobEmptyConfirmed) result.containmentVerificationFailed = true;
        if (result.containmentAssigned && result.jobTotalProcesses.value_or(0) < 1) result.containmentVerificationFailed = true;
    }
    if (reader) {
        const auto until = GetTickCount64() + DrainGraceMs;
        do {
            consume(); if (readerFinished) break;
            const auto now = GetTickCount64(); if (now >= until) break;
            WaitForSingleObject(reinterpret_cast<HANDLE>(reader->ReadyHandle()), static_cast<DWORD>(until - now));
        } while (!readerFinished);
        if (!readerFinished) { result.progressTransportFailed = true; reader->Cancel(); }
    }
    result.campaignTimedOut |= NowQpc() >= campaignDeadline;
    result.progressInvalid = state.invalid; result.progressForm = state.Form(result.processResumed);
    result.observations = std::move(state.observations); result.activeAttempt = state.activeAttempt; result.lastReturnedAttempt = state.lastReturnedAttempt;
    if (result.supervisorTerminationRequested) result.exitKind = ExitKind::SupervisorForced;
    else if (result.exitCode == progress::ProgressTransportAbortExitCode) result.exitKind = ExitKind::ProgressTransportAbort;
    else if (result.exitCode) {
        const auto e = *result.exitCode; result.exitKind = e == 0 || e == 2 || e == 3 || e == 4 || e == 5 ? ExitKind::VoluntaryStage6 : ExitKind::Abnormal;
    }
    return result;
}

evidence::Foundation ReconstructFoundation(const Manifest& m, std::uint64_t sequence)
{
    namespace old = computelab::ex2::evidence;
    const auto slot = FrozenCampaignPlan().at(static_cast<std::size_t>(sequence)); const auto& w = WorkloadForCell(slot.cellIndex);
    std::optional<std::string> shader;
    if (slot.process.backend == Backend::Vulkan) std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>) shader = m.shaderSha256[c.variant == LinearVariant::A1 ? 0 : 1];
        else if constexpr (std::is_same_v<T, IndexedConfiguration>) shader = m.shaderSha256[c.variant == IndexedVariant::B1 ? 2 : 3];
        else if constexpr (std::is_same_v<T, ContentionConfiguration>) shader = m.shaderSha256[4];
        else if constexpr (std::is_same_v<T, IterativeConfiguration>) shader = m.shaderSha256[5];
    }, w.parameters);
    const evidence::FoundationRequest request{slot, Child(m, sequence).sessionId, m.machineId, {m.gpuUuid, true}, m.sourceRevision, m.childExecutableSha256, shader};
    return std::visit([&](const auto& c) -> evidence::Foundation {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, TransferConfiguration>) {
            const auto data = ReferenceTransfer(w.common.seed, c.byteCount, c.direction);
            return evidence::MakeByteFoundation(request, old::MakeI7ByteInputIdentity(w, data.source), data.expectedDestination);
        } else if constexpr (std::is_same_v<T, ContentionConfiguration>) {
            const auto data = ReferenceContention(c.elementCount, c.activeCounterCount);
            return evidence::MakeWordFoundation(request, old::MakeI7ContentionInputIdentity(w, data.targets, MakeZeroInitialCounterState(c.allocatedCounterCount)), data.counters);
        } else {
            const auto input = GenerateWordInput(w.common.seed, c.elementCount);
            if constexpr (std::is_same_v<T, IndexedConfiguration>) {
                const auto indices = c.indexPattern == IndexPattern::StructuredV1 ? GenerateStructuredPermutation(c.elementCount) : GenerateShuffledPermutation(w.common.seed, c.elementCount);
                const auto expected = c.variant == IndexedVariant::B1 ? ReferenceB1Gather(input, indices) : ReferenceB2Scatter(input, indices);
                return evidence::MakeWordFoundation(request, old::MakeI7IndexedInputIdentity(w, input, indices), expected);
            } else if constexpr (std::is_same_v<T, IterativeConfiguration>)
                return evidence::MakeWordFoundation(request, old::MakeI7WordInputIdentity(w, input), ReferenceD1(input, c.iterationCount).finalState);
            else return evidence::MakeWordFoundation(request, old::MakeI7WordInputIdentity(w, input), c.variant == LinearVariant::A1 ? ReferenceA1(input) : ReferenceA2(input));
        }
    }, w.parameters);
}
std::string PackageHash(std::span<const Artifact> artifacts)
{
    Check(artifacts.size() == 4, "package hash requires four artifacts"); JsonArray inventory;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& a = artifacts[i]; Check(a.name == PackageNames[i] && IsLowerHex(a.sha256, 64), "package hash inventory invalid");
        inventory.push_back({{JsonObject{{"name", J(a.name)}, {"size_bytes", J(a.sizeBytes)}, {"sha256", J(a.sha256)}}}});
    }
    return Sha256(Canonical({{JsonObject{{"package_hash_version", J(std::uint64_t{1})}, {"artifacts", {{inventory}}}}}}));
}
namespace
{
namespace ev = computelab::ex2::stage6::evidence;
namespace old = computelab::ex2::evidence;
results::EnvironmentRecord ReadCommon(const JsonObject& o)
{
    results::EnvironmentRecord r; Check(RequireUnsigned(o, "schema_version") == 2, "environment schema"); r.schemaVersion = 2;
    r.experimentId = RequireString(o, "experiment_id"); r.runId = RequireString(o, "run_id"); r.timestampUtc = RequireString(o, "timestamp_utc");
    r.gitCommit = RequireString(o, "git_commit"); r.gitDirty = RequireBoolean(o, "git_dirty"); r.machineId = RequireString(o, "machine_id");
    r.osName = RequireString(o, "os_name"); r.osVersion = RequireString(o, "os_version"); r.cpuName = RequireString(o, "cpu_name");
    r.systemMemoryBytes = RequireUnsigned(o, "system_memory_bytes"); r.gpuName = OptionalString(o, "gpu_name"); r.gpuVendor = OptionalString(o, "gpu_vendor");
    r.gpuDeviceId = OptionalString(o, "gpu_device_id"); r.gpuMemoryBytes = OptionalUnsigned(o, "gpu_memory_bytes");
    r.nvidiaDriverVersion = OptionalString(o, "nvidia_driver_version"); r.cudaToolkitVersion = OptionalString(o, "cuda_toolkit_version");
    r.cudaRuntimeVersion = OptionalString(o, "cuda_runtime_version"); r.cudaComputeCapability = OptionalString(o, "cuda_compute_capability");
    r.vulkanSdkVersion = OptionalString(o, "vulkan_sdk_version"); r.vulkanDeviceApiVersion = OptionalString(o, "vulkan_device_api_version");
    r.compilerName = RequireString(o, "compiler_name"); r.compilerVersion = RequireString(o, "compiler_version"); r.cmakeVersion = RequireString(o, "cmake_version");
    r.ninjaVersion = RequireString(o, "ninja_version"); r.configurePreset = RequireString(o, "configure_preset"); r.buildType = RequireString(o, "build_type");
    r.validationEnabled = RequireBoolean(o, "validation_enabled"); r.diagnosticInstrumentation = RequireBoolean(o, "diagnostic_instrumentation"); return r;
}
ev::BackendDiagnostics ReadDiagnostics(const JsonObject& o)
{
    ev::BackendDiagnostics d; d.implementation = RequireString(o, "implementation"); d.streamFlags = OptionalString(o, "stream_flags");
    d.queueFamilyIndex = OptionalUnsigned(o, "queue_family_index"); d.queueFlags = OptionalUnsigned(o, "queue_flags"); d.queueCount = OptionalUnsigned(o, "queue_count");
    d.inputMemoryFlags = OptionalUnsigned(o, "input_memory_flags"); d.outputMemoryFlags = OptionalUnsigned(o, "output_memory_flags");
    d.uploadMemoryFlags = OptionalUnsigned(o, "upload_memory_flags"); d.readbackMemoryFlags = OptionalUnsigned(o, "readback_memory_flags");
    d.nativeMarkersEnabled = RequireBoolean(o, "native_markers_enabled"); return d;
}
std::optional<std::uint64_t> CellUnsigned(const std::string& s) { return s.empty() ? std::nullopt : std::optional{ParseCsvUnsigned(s, "CSV number")}; }
std::optional<std::string> CellString(const std::string& s) { return s.empty() ? std::nullopt : std::optional{s}; }
std::optional<bool> CellBoolean(const std::string& s)
{ if (s.empty()) return {}; Check(s == "true" || s == "false", "invalid CSV boolean"); return s == "true"; }
template<class T> T ReadEnum(std::string_view text, T last)
{ for (int i = 0; i <= static_cast<int>(last); ++i) if (old::ToString(static_cast<T>(i)) == text) return static_cast<T>(i); throw std::invalid_argument("invalid evidence enum"); }
std::optional<ev::FailurePhase> ReadPhase(const std::optional<std::string>& text)
{ return text ? std::optional{ReadEnum(*text, ev::FailurePhase::Interrupted)} : std::nullopt; }
std::optional<double> ReadNumber(const JsonObject& o, std::string_view key)
{
    const auto& value = RequireMember(o, key); if (std::holds_alternative<std::nullptr_t>(value.storage)) return {};
    if (const auto* v = std::get_if<double>(&value.storage)) return *v;
    if (const auto* v = std::get_if<std::uint64_t>(&value.storage)) return static_cast<double>(*v);
    if (const auto* v = std::get_if<std::int64_t>(&value.storage)) return static_cast<double>(*v);
    throw std::invalid_argument("invalid metric number");
}
results::MetricSummary ReadMetric(const JsonValue& v)
{
    const auto& o = RequireType<JsonObject>(v, "metric");
    RequireExactKeys(o, {"sample_count", "minimum", "median", "mean", "standard_deviation", "coefficient_of_variation", "p95"});
    return {OptionalUnsigned(o, "sample_count"), ReadNumber(o, "minimum"), ReadNumber(o, "median"), ReadNumber(o, "mean"),
        ReadNumber(o, "standard_deviation"), ReadNumber(o, "coefficient_of_variation"), ReadNumber(o, "p95")};
}
// This recursively checks all schema keys and scalar types against the typed I1
// serialization, including identity fields which are never package-selected.
void ExactShape(const JsonValue& actual, const JsonValue& expected)
{
    Check(actual.storage.index() == expected.storage.index(), "JSON type mismatch");
    if (const auto* object = std::get_if<JsonObject>(&expected.storage)) {
        const auto& a = std::get<JsonObject>(actual.storage); Check(a.size() == object->size(), "JSON key set mismatch");
        for (const auto& [key, value] : *object) ExactShape(RequireMember(a, key), value);
    } else if (const auto* array = std::get_if<JsonArray>(&expected.storage)) {
        const auto& a = std::get<JsonArray>(actual.storage); Check(a.size() == array->size(), "JSON array shape mismatch");
        for (std::size_t i = 0; i < a.size(); ++i) ExactShape(a[i], (*array)[i]);
    }
}
void ParseBundle(const ev::Foundation& f, const std::map<std::string, std::string>& bytes, InspectionPurpose purpose, PackageInspection& result)
{
    const auto envJson = JsonParser(bytes.at("environment.json")).Parse(); const auto& eo = RequireType<JsonObject>(envJson, "environment");
    auto common = ReadCommon(eo);
    Check((purpose == InspectionPurpose::DisposableSmoke || !common.gitDirty) && common.configurePreset == "x64-release"
        && common.buildType == "Release" && !common.validationEnabled && !common.diagnosticInstrumentation, "supervised environment requirements");
    auto environment = ev::MakeEnvironmentRecord(std::move(common), f, ReadDiagnostics(RequireType<JsonObject>(RequireMember(eo, "backend_native"), "backend diagnostics")));
    const auto ir = ParseCsv(bytes.at("initialization.csv")); Check(ir.size() >= 2 && ir[0] == ParseCsv(ev::InitializationCsvHeader())[0], "initialization header");
    std::vector<ev::InitializationRecord> init;
    for (std::size_t i = 1; i < ir.size(); ++i) {
        const auto& r = ir[i]; Check(r.size() == 13 && (r[3] == "cuda" || r[3] == "vulkan"), "initialization row shape");
        init.push_back({r[1], r[3] == "cuda" ? Backend::Cuda : Backend::Vulkan, ParseCsvUnsigned(r[4], "process"), ParseCsvUnsigned(r[5], "sequence"), r[6],
            CellString(r[7]), CellString(r[8]), CellUnsigned(r[9]), r[10], CellUnsigned(r[11]), CellString(r[12])});
    }
    ev::ValidateInitializationRecords(f.Identity(), init);
    const auto sr = ParseCsv(bytes.at("samples.csv")); Check(!sr.empty() && sr.size() <= 101 && sr[0] == ParseCsv(ev::SamplesCsvHeader())[0], "samples header/count");
    std::vector<ev::SampleRecord> samples;
    for (std::size_t i = 1; i < sr.size(); ++i) {
        const auto& r = sr[i]; Check(r.size() == 31, "sample width");
        auto sample = ev::MakeSampleRecord(f, ParseCsvUnsigned(r[22], "sample index"));
        sample.status = ReadEnum(r[24], ev::Status::Incomplete); sample.failurePhase = ReadPhase(CellString(r[25])); sample.errorCode = CellString(r[26]);
        sample.hostSubmissionNanoseconds = CellUnsigned(r[27]); sample.hostWaitNanoseconds = CellUnsigned(r[28]); sample.hostCompletionNanoseconds = CellUnsigned(r[29]);
        sample.nativeDeviceIntervalNanoseconds = CellUnsigned(r[30]);
        const auto passed = CellBoolean(r[23]); sample.correctness = {true,
            sample.hostSubmissionNanoseconds && sample.hostWaitNanoseconds && sample.hostCompletionNanoseconds, passed.has_value(), passed.has_value(), passed};
        ev::ValidateSampleRecord(sample); samples.push_back(std::move(sample));
    }
    ev::ValidateSamples(f.Identity(), samples);
    const auto summaryJson = JsonParser(bytes.at("summary.json")).Parse(); const auto& so = RequireType<JsonObject>(summaryJson, "summary");
    ev::SummaryRecord summary; summary.plan = f.Identity(); summary.processStatus = ReadEnum(RequireString(so, "process_status"), ev::Status::Incomplete);
    summary.failurePhase = ReadPhase(OptionalString(so, "failure_phase")); summary.errorCode = OptionalString(so, "error_code");
    const auto& groups = RequireType<JsonArray>(RequireMember(so, "sample_groups"), "sample groups"); Check(groups.size() == 1, "summary group count");
    const auto& group = RequireType<JsonObject>(groups[0], "sample group");
    summary.recordedSampleCount = RequireUnsigned(group, "recorded_sample_count"); summary.successfulSampleCount = RequireUnsigned(group, "successful_sample_count");
    summary.validationFailures = RequireUnsigned(group, "validation_failures"); summary.failedSampleCount = RequireUnsigned(group, "failed_sample_count");
    const auto& metrics = RequireType<JsonObject>(RequireMember(group, "metrics"), "metrics");
    RequireExactKeys(metrics, {"host_submission_ns", "host_wait_ns", "host_completion_ns", "native_device_interval_ns"});
    Check(std::holds_alternative<std::nullptr_t>(RequireMember(metrics, "native_device_interval_ns").storage), "native metric in H");
    summary.hostSubmissionNanoseconds = ReadMetric(RequireMember(metrics, "host_submission_ns")); summary.hostWaitNanoseconds = ReadMetric(RequireMember(metrics, "host_wait_ns"));
    summary.hostCompletionNanoseconds = ReadMetric(RequireMember(metrics, "host_completion_ns"));
    result.scientificBundleParsed = true;
    ev::ValidateSummary(summary, samples); ev::ValidateBundle(f, environment, init, samples, summary);
    const auto canonicalEnvironment = ev::SerializeEnvironmentJson(f, environment), canonicalSummary = ev::SerializeSummaryJson(summary, samples);
    ExactShape(envJson, JsonParser(canonicalEnvironment).Parse()); ExactShape(summaryJson, JsonParser(canonicalSummary).Parse());
    Check(bytes.at("environment.json") == canonicalEnvironment && bytes.at("initialization.csv") == ev::SerializeInitializationCsv(f.Identity(), init)
        && bytes.at("samples.csv") == ev::SerializeSamplesCsv(f.Identity(), samples) && bytes.at("summary.json") == canonicalSummary, "scientific bytes not canonical or identity drift");
    result.scientificBundleValid = true; result.scientificBytesCanonical = true;
    result.input = analysis::ProcessInput{*result.packageSha256, f.Identity(), std::move(samples), std::move(summary)};
}
Sidecar ParseSidecar(std::string_view bytes, const Manifest& m, std::uint64_t slot, const ev::Foundation& f)
{
    const auto o = RequireType<JsonObject>(JsonParser(bytes).Parse(), "failure sidecar");
    RequireExactKeys(o, {"record_version", "record_type", "session_id", "slot_sequence_index", "foundation_established", "run_id", "series_id", "source_revision",
        "exit_category", "failure_phase", "error_code", "sample_index", "completion_uncertain", "native_phase", "native_code", "native_detail"});
    Check(RequireUnsigned(o, "record_version") == 1 && RequireString(o, "record_type") == "ex2-stage6-child-failure", "sidecar type");
    Sidecar s; s.sessionId = RequireString(o, "session_id"); s.slotSequenceIndex = RequireUnsigned(o, "slot_sequence_index");
    s.foundationEstablished = RequireBoolean(o, "foundation_established"); s.runId = RequireString(o, "run_id"); s.seriesId = RequireString(o, "series_id");
    s.sourceRevision = RequireString(o, "source_revision"); s.exitCategory = RequireUnsigned(o, "exit_category");
    Check(s.foundationEstablished && s.slotSequenceIndex == slot && s.sessionId == Child(m, slot).sessionId && s.runId == f.Identity().runId
        && s.seriesId == f.Identity().seriesId && s.sourceRevision == m.sourceRevision, "sidecar identity");
    Check(s.exitCategory == 0 || s.exitCategory == 2 || s.exitCategory == 3 || s.exitCategory == 4 || s.exitCategory == 5, "sidecar exit category");
    s.failurePhase = RequireString(o, "failure_phase"); s.errorCode = RequireString(o, "error_code"); s.sampleIndex = OptionalUnsigned(o, "sample_index");
    s.completionUncertain = RequireBoolean(o, "completion_uncertain"); s.nativePhase = OptionalString(o, "native_phase");
    s.nativeCode = OptionalString(o, "native_code"); s.nativeDetail = OptionalString(o, "native_detail");
    const auto safe = [](const std::string& text) { Check(!text.empty() && text.size() <= 512 && text.find_first_of("\\/\r\n") == text.npos && text.find('\0') == text.npos, "unsafe sidecar text"); };
    safe(s.failurePhase); safe(s.errorCode); for (const auto* text : {&s.nativePhase, &s.nativeCode, &s.nativeDetail}) if (*text) safe(**text);
    Check(!s.sampleIndex || *s.sampleIndex < PlannedSampleCount, "sidecar sample index"); return s;
}
struct Inventory
{
    Topology topology{Topology::Missing}; PackageLocation location{PackageLocation::None}; bool sidecar{};
    std::vector<Artifact> artifacts; std::optional<Artifact> sidecarArtifact;
    std::map<std::string, std::string> bytes; std::string sidecarBytes;
};
Inventory InventoryPackage(const Manifest& m, std::uint64_t sequence, const std::filesystem::path& root)
{
    Inventory r; const auto base = root / "results/local" / Child(m, sequence).sessionId;
    const auto staging = std::filesystem::path(base.wstring() + L".incomplete"), side = std::filesystem::path(base.wstring() + L".failure.json");
    for (const auto& path : {base, staging, side}) RejectReparsePath(path);
    const bool f = Exists(base), s = Exists(staging); r.sidecar = Exists(side);
    r.topology = f && s ? Topology::Contradictory : f ? (r.sidecar ? Topology::FinalWithSidecar : Topology::FinalOnly)
        : s ? (r.sidecar ? Topology::StagingWithSidecar : Topology::StagingOnly) : r.sidecar ? Topology::SidecarOnly : Topology::Missing;
    if (r.topology == Topology::Contradictory) return r;
    if (f || s) {
        const auto directory = f ? base : staging; r.location = f ? PackageLocation::Final : PackageLocation::Staging;
        Check(std::filesystem::is_directory(directory), "package is not a directory");
        std::set<std::string> entries;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            Check(std::ranges::find(PackageNames, name) != PackageNames.end() && entries.insert(name).second, "unexpected package entry");
            r.bytes.emplace(name, ReadText(entry.path()));
        }
        for (const auto name : PackageNames) if (const auto it = r.bytes.find(std::string(name)); it != r.bytes.end())
            r.artifacts.push_back({std::string(name), (directory / name).lexically_relative(root).generic_string(), it->second.size(), Sha256(it->second)});
    }
    if (r.sidecar) { r.sidecarBytes = ReadText(side); r.sidecarArtifact = Artifact{"failure.json", side.lexically_relative(root).generic_string(), r.sidecarBytes.size(), Sha256(r.sidecarBytes)}; }
    return r;
}
} // namespace
PackageInspection InspectPackage(const Manifest& m, std::uint64_t sequence, const std::filesystem::path& root, InspectionPurpose purpose, const InspectionTestHooks& hooks)
{
    PackageInspection r; r.attempted = true;
    try {
        const auto first = InventoryPackage(m, sequence, root); r.topology = first.topology; r.location = first.location;
        r.sidecarPresent = first.sidecar; r.sidecarArtifact = first.sidecarArtifact; r.artifacts = first.artifacts;
        Check(r.topology != Topology::Contradictory, "contradictory topology"); r.packageFinalized = first.location == PackageLocation::Final;
        r.exactFileSet = first.artifacts.size() == 4; Check(!r.packageFinalized || r.exactFileSet, "final package incomplete");
        std::optional<ev::Foundation> foundation; if (r.exactFileSet || r.sidecarPresent) foundation = ReconstructFoundation(m, sequence);
        if (r.exactFileSet) { r.packageSha256 = PackageHash(r.artifacts); ParseBundle(*foundation, first.bytes, purpose, r); }
        if (r.sidecarPresent) {
            r.sidecar = ParseSidecar(first.sidecarBytes, m, sequence, *foundation); r.sidecarValid = true;
            if (r.input && r.sidecar->sampleIndex) Check(!r.input->samples.empty() && *r.sidecar->sampleIndex == r.input->samples.back().sampleIndex, "sidecar terminal row mismatch");
        }
        try {
            if (hooks.beforeSecondPass) hooks.beforeSecondPass();
            const auto second = InventoryPackage(m, sequence, root);
            r.changedDuringInspection = first.topology != second.topology || first.artifacts != second.artifacts || first.sidecarArtifact != second.sidecarArtifact;
        } catch (...) { r.changedDuringInspection = true; }
        Check(!r.changedDuringInspection, "package changed during inspection"); r.structurallyValid = true;
    } catch (...) { r.structurallyValid = false; r.input.reset(); r.errors.push_back(r.changedDuringInspection ? "package_changed_during_inspection" : "package_inspection_invalid"); }
    return r;
}

Reconciliation ReconcileSlot(const ProcessResult& p, const PackageInspection& i)
{
    const auto fatal = [](std::string reason) { return Reconciliation{analysis::SlotDisposition::UnresolvedCampaignFatal, std::move(reason), {}}; };
    if (p.progressInvalid || p.progressForm == progress::TerminalForm::Invalid) return fatal("progress_protocol_invalid");
    if (p.operationTimedOut) return fatal("operation_timeout");
    if (p.campaignTimedOut) return fatal("campaign_timeout");
    if (p.childTimedOut) return fatal("child_timeout");
    if (!p.processCreated || !p.processResumed || !p.primaryTerminationConfirmed || !p.containmentAssigned || !p.containmentVerified
        || !p.jobEmptyConfirmed || p.jobActiveProcesses != 0 || (p.jobTotalProcesses && *p.jobTotalProcesses < 1)
        || p.descendantSurvivalObserved || p.containmentVerificationFailed || p.supervisorTerminationRequested || p.controlError)
        return fatal("unsafe_process_control");
    if (p.exitKind == ExitKind::VoluntaryStage6 && p.exitCode && i.sidecarPresent && i.sidecarValid && i.sidecar) {
        const auto actual = *p.exitCode; const auto historical = i.sidecar->exitCategory;
        const bool compatible = actual == 0 ? historical == 0 : actual == 3 ? historical == 3 : actual == 4 ? historical == 0 || historical == 4 || historical == 5
            : actual == 5 ? historical == 0 || historical == 5 : false;
        if (!compatible) return fatal("sidecar_exit_incompatible");
    }
    if (p.exitKind != ExitKind::VoluntaryStage6 || !p.exitCode || (*p.exitCode != 0 && *p.exitCode != 4)) return fatal("unsafe_child_exit");
    if (p.progressTransportFailed || !p.cleanEof || p.trailingBytes || p.activeAttempt
        || p.progressForm == progress::TerminalForm::NotStarted || p.progressForm == progress::TerminalForm::OutstandingStartedPrefix)
        return fatal("unsafe_progress_terminal");
    if (!i.attempted || !i.structurallyValid || !i.exactFileSet || !i.scientificBundleParsed || !i.scientificBundleValid
        || !i.scientificBytesCanonical || i.changedDuringInspection || !i.packageSha256 || !i.input)
        return fatal("invalid_scientific_package");
    try {
        if (i.input->packageSha256 != *i.packageSha256 || PackageHash(i.artifacts) != *i.packageSha256) return fatal("package_hash_mismatch");
        progress::State transcript(i.input->plan.slot.sequenceIndex); for (const auto& observation : p.observations) transcript.Accept(observation);
        const auto rows = i.input->samples.size();
        if (transcript.invalid || transcript.Form(true) != p.progressForm || p.activeAttempt != transcript.activeAttempt
            || p.lastReturnedAttempt != transcript.lastReturnedAttempt || p.observations.size() != rows * 2
            || (rows == 0 ? p.progressForm != progress::TerminalForm::Empty : rows == 100 ? p.progressForm != progress::TerminalForm::Full
                : p.progressForm != progress::TerminalForm::ReturnedPrefix)) return fatal("progress_sample_mismatch");
        if (i.input->summary.processStatus == ev::Status::DeviceLost
            || std::ranges::any_of(i.input->samples, [](const auto& row) { return row.status == ev::Status::DeviceLost; })) return fatal("device_lost");
        if (i.sidecarPresent && (!i.sidecarValid || !i.sidecar || !i.sidecarArtifact)) return fatal("invalid_failure_sidecar");
        if (i.sidecar && (i.sidecar->completionUncertain || i.sidecar->errorCode == "device_lost")) return fatal("native_completion_uncertain");
        const auto description = analysis::DescribeProcess(*i.input);
        const bool success = description.TerminalState() == analysis::ProcessTerminalState::Success;
        if (*p.exitCode == 0) {
            if (!i.packageFinalized || i.location != PackageLocation::Final
                || (i.topology != Topology::FinalOnly && i.topology != Topology::FinalWithSidecar)) return fatal("exit_zero_without_final_package");
            if ((success && i.sidecarPresent) || (i.sidecarPresent && i.sidecar->exitCategory != 0)) return fatal("final_sidecar_inconsistent");
        } else if (i.packageFinalized || i.location != PackageLocation::Staging || i.topology != Topology::StagingWithSidecar
            || !i.sidecarPresent || !i.sidecarValid || !i.sidecar || (i.sidecar->exitCategory != 0 && i.sidecar->exitCategory != 4))
            return fatal("unrecoverable_publication_failure");
        return {success ? analysis::SlotDisposition::ResolvedSuccess : analysis::SlotDisposition::ResolvedDiagnosticFailure,
            *p.exitCode == 4 ? "resolved_complete_staging" : success ? "resolved_success" : "resolved_diagnostic_failure", description};
    } catch (...) { return fatal("reconciliation_input_invalid"); }
}
namespace
{
constexpr std::uint64_t MaxStage6LedgerBytes = 32U * 1024U * 1024U;
std::string_view DispositionName(analysis::SlotDisposition d)
{
    switch (d) { case analysis::SlotDisposition::ResolvedSuccess: return "resolved_success";
    case analysis::SlotDisposition::ResolvedDiagnosticFailure: return "resolved_diagnostic_failure";
    case analysis::SlotDisposition::UnresolvedCampaignFatal: return "unresolved_campaign_fatal";
    default: return "not_launched"; }
}
analysis::SlotDisposition ReadDisposition(std::string_view text)
{
    for (const auto d : {analysis::SlotDisposition::ResolvedSuccess, analysis::SlotDisposition::ResolvedDiagnosticFailure,
        analysis::SlotDisposition::UnresolvedCampaignFatal, analysis::SlotDisposition::NotLaunched}) if (DispositionName(d) == text) return d;
    throw std::invalid_argument("invalid disposition");
}
bool Resolved(const std::optional<analysis::SlotDisposition>& d)
{ return d == analysis::SlotDisposition::ResolvedSuccess || d == analysis::SlotDisposition::ResolvedDiagnosticFailure; }
std::string_view TopologyName(Topology t)
{
    switch (t) { case Topology::Missing: return "Missing"; case Topology::SidecarOnly: return "SidecarOnly";
    case Topology::StagingOnly: return "StagingOnly"; case Topology::StagingWithSidecar: return "StagingWithSidecar";
    case Topology::FinalOnly: return "FinalOnly"; case Topology::FinalWithSidecar: return "FinalWithSidecar"; default: return "Contradictory"; }
}
std::string_view ExitKindName(ExitKind kind)
{
    switch (kind) { case ExitKind::VoluntaryStage6: return "VoluntaryStage6"; case ExitKind::ProgressTransportAbort: return "ProgressTransportAbort";
    case ExitKind::SupervisorForced: return "SupervisorForced"; case ExitKind::Abnormal: return "Abnormal"; default: return "NotAvailable"; }
}
JsonValue IdentityJson(const std::optional<execution::AttemptIdentity>& i)
{ return i ? JsonValue{{JsonObject{{"slot_sequence_index", J(i->slotSequenceIndex)}, {"sample_index", J(i->sampleIndex)}}}} : JsonValue{{nullptr}}; }
JsonValue ArtifactJson(const Artifact& a)
{ return {{JsonObject{{"relative_path", J(a.relativePath)}, {"size_bytes", J(a.sizeBytes)}, {"sha256", J(a.sha256)}}}}; }
JsonValue AnchorJson(const std::optional<Artifact>& a) { return a ? ArtifactJson(*a) : JsonValue{{nullptr}}; }
JsonValue ArtifactInventoryJson(const std::vector<Artifact>& artifacts)
{
    JsonArray out; for (const auto& a : artifacts) { auto o = std::get<JsonObject>(ArtifactJson(a).storage); o["name"] = J(a.name); out.push_back({{o}}); } return {{out}};
}
JsonValue FactsJson(const PreflightFacts& f)
{
    JsonObject shaders; for (std::size_t n = 0; n < 6; ++n) shaders[std::string(ShaderNames[n])] = J(f.shaderSha256[n]);
    return {{JsonObject{{"source_revision", J(f.sourceRevision)}, {"git_dirty", J(f.gitDirty)}, {"child_executable_sha256", J(f.childSha256)},
        {"supervisor_executable_sha256", J(f.supervisorSha256)}, {"cuda_uuid", J(f.cudaUuid)}, {"vulkan_uuid", J(f.vulkanUuid)}, {"vulkan_shader_sha256", {{shaders}}}}}};
}
JsonValue ProcessJson(const ProcessResult& p)
{
    EncodedTranscript observations{"[", p.observations.size()};
    for (const auto& o : p.observations) {
        if (observations.bytes.size() > 1) observations.bytes += ',';
        observations.bytes += "{\"child_qpc\":" + std::to_string(o.record.performanceCounter)
            + ",\"event\":" + std::to_string(static_cast<std::uint32_t>(o.record.event)) + ",\"magic\":" + std::to_string(o.record.magic)
            + ",\"receive_qpc\":" + std::to_string(o.receiveCounter) + ",\"reserved\":" + std::to_string(o.record.reserved)
            + ",\"sample_index\":" + std::to_string(o.record.sampleIndex) + ",\"slot_sequence_index\":" + std::to_string(o.record.slotSequenceIndex)
            + ",\"version\":" + std::to_string(o.record.version) + "}";
    }
    observations.bytes += ']';
    return {{JsonObject{{"process_created", J(p.processCreated)}, {"containment_assigned", J(p.containmentAssigned)}, {"containment_verified", J(p.containmentVerified)},
        {"process_resumed", J(p.processResumed)}, {"process_id", J(p.processId)}, {"exit_code", J(p.exitCode)}, {"launch_time_utc", NullableText(p.launchTimeUtc)},
        {"exit_time_utc", NullableText(p.exitTimeUtc)}, {"exit_kind", J(ExitKindName(p.exitKind))}, {"progress_form", J(progress::ToString(p.progressForm))},
        {"progress_transcript", {{observations}}}, {"active_attempt", IdentityJson(p.activeAttempt)}, {"last_returned_attempt", IdentityJson(p.lastReturnedAttempt)},
        {"progress_invalid", J(p.progressInvalid)}, {"progress_transport_failed", J(p.progressTransportFailed)}, {"clean_eof", J(p.cleanEof)}, {"trailing_bytes", J(p.trailingBytes)},
        {"operation_timed_out", J(p.operationTimedOut)}, {"timed_out_sample_index", J(p.timedOutSampleIndex)}, {"child_timed_out", J(p.childTimedOut)}, {"campaign_timed_out", J(p.campaignTimedOut)},
        {"supervisor_termination_requested", J(p.supervisorTerminationRequested)}, {"terminate_job_succeeded", J(p.terminateJobSucceeded)},
        {"primary_termination_confirmed", J(p.primaryTerminationConfirmed)}, {"job_total_processes", J(p.jobTotalProcesses)}, {"job_active_processes", J(p.jobActiveProcesses)},
        {"descendant_survival_observed", J(p.descendantSurvivalObserved)}, {"job_empty_confirmed", J(p.jobEmptyConfirmed)},
        {"containment_verification_failed", J(p.containmentVerificationFailed)}, {"control_error", J(p.controlError)}}}};
}
JsonValue InspectionJson(const PackageInspection& i)
{
    JsonArray errors; for (const auto& e : i.errors) { Check(e.size() <= 128, "inspection error bound"); errors.push_back(J(e)); } Check(errors.size() <= 16, "inspection error count");
    JsonValue side{{nullptr}};
    if (i.sidecar) { const auto& s = *i.sidecar; side = {{JsonObject{{"session_id", J(s.sessionId)}, {"slot_sequence_index", J(s.slotSequenceIndex)},
        {"foundation_established", J(s.foundationEstablished)}, {"run_id", J(s.runId)}, {"series_id", J(s.seriesId)}, {"source_revision", J(s.sourceRevision)},
        {"exit_category", J(s.exitCategory)}, {"failure_phase", J(s.failurePhase)}, {"error_code", J(s.errorCode)}, {"sample_index", J(s.sampleIndex)},
        {"completion_uncertain", J(s.completionUncertain)}, {"native_phase", J(s.nativePhase)}, {"native_code", J(s.nativeCode)}, {"native_detail", J(s.nativeDetail)}}}}; }
    JsonValue science{{nullptr}};
    if (i.input) { const auto& s = i.input->summary; science = {{JsonObject{{"terminal_state", J(s.processStatus == ev::Status::Ok ? "Success" : "DiagnosticFailure")},
        {"recorded_sample_count", J(s.recordedSampleCount)}, {"process_status", J(old::ToString(s.processStatus))},
        {"failure_phase", s.failurePhase ? J(old::ToString(*s.failurePhase)) : JsonValue{{nullptr}}}, {"error_code", J(s.errorCode)}}}}; }
    return {{JsonObject{{"attempted", J(i.attempted)}, {"topology", J(TopologyName(i.topology))}, {"structurally_valid", J(i.structurallyValid)},
        {"package_finalized", J(i.packageFinalized)}, {"location", J(i.location == PackageLocation::Final ? "Final" : i.location == PackageLocation::Staging ? "Staging" : "None")},
        {"exact_file_set", J(i.exactFileSet)}, {"scientific_bundle_parsed", J(i.scientificBundleParsed)}, {"scientific_bundle_valid", J(i.scientificBundleValid)},
        {"scientific_bytes_canonical", J(i.scientificBytesCanonical)}, {"changed_during_inspection", J(i.changedDuringInspection)},
        {"artifacts", ArtifactInventoryJson(i.artifacts)}, {"package_sha256", J(i.packageSha256)}, {"science", science},
        {"sidecar_present", J(i.sidecarPresent)}, {"sidecar_valid", J(i.sidecarValid)}, {"sidecar_artifact", AnchorJson(i.sidecarArtifact)},
        {"sidecar", side}, {"errors", {{errors}}}}}};
}
JsonValue LedgerJson(const Ledger& l)
{
    JsonArray slots, cells;
    for (std::size_t n = 0; n < TotalChildCount; ++n) { const auto& s = l.slots[n];
        slots.push_back({{JsonObject{{"sequence_index", J(static_cast<std::uint64_t>(n))}, {"session_id", J(Child(l.manifest, n).sessionId)}, {"control_phase", J(s.phase)},
            {"entered_revision", J(s.enteredRevision)}, {"process_revision", J(s.processRevision)}, {"inspection_revision", J(s.inspectionRevision)}, {"terminal_revision", J(s.terminalRevision)},
            {"disposition", s.disposition ? J(DispositionName(*s.disposition)) : JsonValue{{nullptr}}}, {"process", ProcessJson(s.process)}, {"inspection", InspectionJson(s.inspection)},
            {"reconciliation_reason", NullableText(s.reason)}, {"continuation", J(s.continuation)}}}});
    }
    for (const auto& a : l.cellAnalyses) cells.push_back(AnchorJson(a));
    return {{JsonObject{{"record_version", J(std::uint64_t{1})}, {"record_type", J("ex2-stage6-supervisor-control")}, {"protocol_version", J("1.3")},
        {"manifest_id", J(l.manifest.id)}, {"manifest_sha256", J(l.manifest.sha256)}, {"manifest_file_sha256", J(l.manifest.fileSha256)},
        {"manifest", JsonParser(l.manifest.canonicalJson).Parse()}, {"resume_policy", J("forbidden")}, {"ledger_revision", J(l.revision)}, {"control_state", J(l.state)},
        {"control_start_time_utc", J(l.controlStartUtc)}, {"campaign_start_time_utc", NullableText(l.campaignStartUtc)}, {"campaign_end_time_utc", NullableText(l.campaignEndUtc)},
        {"qpc_frequency", J(l.qpcFrequency)}, {"campaign_start_counter", J(l.campaignStartQpc)}, {"campaign_deadline_counter", J(l.campaignDeadlineQpc)},
        {"observed_preflight", FactsJson(l.observed)}, {"fatal_sequence_index", J(l.fatalSequence)}, {"fatal_reason", J(l.fatalReason)},
        {"slots", {{slots}}}, {"cell_analysis", {{cells}}}, {"campaign_analysis", AnchorJson(l.campaignAnalysis)}}}};
}
void ValidateAnchor(const JsonValue& v)
{
    if (std::holds_alternative<std::nullptr_t>(v.storage)) return;
    const auto& o = RequireType<JsonObject>(v, "anchor"); RequireExactKeys(o, {"relative_path", "size_bytes", "sha256"});
    (void)DeclaredPath(RequireString(o, "relative_path")); (void)RequireUnsigned(o, "size_bytes"); Check(IsLowerHex(RequireString(o, "sha256"), 64), "invalid anchor hash");
}
} // namespace
std::string SerializeLedger(const Ledger& l)
{ const auto bytes = Canonical(LedgerJson(l)) + "\n"; ValidateLedgerBytes(bytes); return bytes; }
void ValidateLedgerBytes(std::string_view bytes)
{
    Check(bytes.size() <= MaxStage6LedgerBytes, "ledger exceeds fixed bound"); const auto value = JsonParser(bytes, true).Parse();
    Check(bytes == Canonical(value) + "\n", "ledger bytes not canonical"); const auto& o = RequireType<JsonObject>(value, "ledger");
    RequireExactKeys(o, {"record_version", "record_type", "protocol_version", "manifest_id", "manifest_sha256", "manifest_file_sha256", "manifest", "resume_policy",
        "ledger_revision", "control_state", "control_start_time_utc", "campaign_start_time_utc", "campaign_end_time_utc", "qpc_frequency", "campaign_start_counter",
        "campaign_deadline_counter", "observed_preflight", "fatal_sequence_index", "fatal_reason", "slots", "cell_analysis", "campaign_analysis"});
    Check(RequireUnsigned(o, "record_version") == 1 && RequireString(o, "record_type") == "ex2-stage6-supervisor-control"
        && RequireString(o, "protocol_version") == "1.3" && RequireString(o, "resume_policy") == "forbidden", "ledger identity");
    const auto m = ParseManifest(Canonical(RequireMember(o, "manifest")) + "\n");
    const PreflightFacts expectedFacts{m.sourceRevision, false, m.childExecutableSha256, m.supervisorExecutableSha256, m.gpuUuid, m.gpuUuid, m.shaderSha256};
    Check(Canonical(RequireMember(o, "observed_preflight")) == Canonical(FactsJson(expectedFacts)), "ledger observed preflight differs from manifest");
    Check(RequireString(o, "manifest_id") == m.id && RequireString(o, "manifest_sha256") == m.sha256 && RequireString(o, "manifest_file_sha256") == m.fileSha256, "ledger manifest mismatch");
    const auto revision = RequireUnsigned(o, "ledger_revision"); const auto state = RequireString(o, "control_state");
    Check(state == "owned" || state == "running" || state == "completed" || state == "incomplete", "ledger state");
    Check(!RequireString(o, "control_start_time_utc").empty(), "ledger ownership time missing");
    const auto start = OptionalString(o, "campaign_start_time_utc"), end = OptionalString(o, "campaign_end_time_utc");
    if (revision == 0) Check(state == "owned" && !start && !end && !OptionalUnsigned(o, "qpc_frequency")
        && !OptionalUnsigned(o, "campaign_start_counter") && !OptionalUnsigned(o, "campaign_deadline_counter"), "revision zero state");
    else {
        const auto frequency = RequireUnsigned(o, "qpc_frequency"), counter = RequireUnsigned(o, "campaign_start_counter"), deadline = RequireUnsigned(o, "campaign_deadline_counter");
        Check(start && frequency > 0 && frequency <= INT64_MAX && counter > 0 && counter <= INT64_MAX && deadline <= INT64_MAX
            && deadline == static_cast<std::uint64_t>(AddDeadline(static_cast<std::int64_t>(counter), DeadlineTicks(m.campaignTimeoutMs, static_cast<std::int64_t>(frequency)))), "campaign clock identity");
    }
    const bool terminal = state == "completed" || state == "incomplete"; Check(terminal == end.has_value(), "terminal timestamp mismatch");
    const auto fatal = OptionalUnsigned(o, "fatal_sequence_index"); Check(!fatal || *fatal < TotalChildCount, "fatal slot out of range");
    const auto reason = OptionalString(o, "fatal_reason"); Check(!reason || (!reason->empty() && reason->size() <= 128), "fatal reason bound");
    const auto& slots = RequireType<JsonArray>(RequireMember(o, "slots"), "slots"); Check(slots.size() == TotalChildCount, "ledger requires 220 slots");
    bool pendingSeen{}, fatalSeen{}; std::optional<std::uint64_t> fatalRevision;
    for (std::size_t n = 0; n < slots.size(); ++n) {
        const auto& s = RequireType<JsonObject>(slots[n], "slot");
        RequireExactKeys(s, {"sequence_index", "session_id", "control_phase", "entered_revision", "process_revision", "inspection_revision", "terminal_revision",
            "disposition", "process", "inspection", "reconciliation_reason", "continuation"});
        Check(RequireUnsigned(s, "sequence_index") == n && RequireString(s, "session_id") == Child(m, n).sessionId, "ledger slot identity");
        const auto phase = RequireString(s, "control_phase"), continuation = RequireString(s, "continuation");
        const auto entered = OptionalUnsigned(s, "entered_revision"), process = OptionalUnsigned(s, "process_revision"), inspection = OptionalUnsigned(s, "inspection_revision"), done = OptionalUnsigned(s, "terminal_revision");
        const auto disposition = OptionalString(s, "disposition");
        for (const auto r : {entered, process, inspection, done}) Check(!r || (*r >= 2 && *r <= revision), "invalid revision anchor");
        Check(!process || (entered && *process > *entered), "process anchor order"); Check(!inspection || (process && *inspection > *process), "inspection anchor order");
        Check(!done || (!entered || *done >= *entered), "terminal anchor order");
        if (phase == "pending") { Check(!entered && !process && !inspection && !done && !disposition && continuation == "pending" && !terminal, "pending slot state"); pendingSeen = true; }
        else if (phase == "not_launched") { Check(fatalSeen && !entered && !process && !inspection && done == fatalRevision && disposition == "not_launched" && continuation == "not_launched", "fatal suffix state"); }
        else {
            Check(!pendingSeen && !fatalSeen && entered, "nonsequential slot entry");
            if (phase == "resolved") {
                Check(process && inspection && done && *done > *inspection && (disposition == "resolved_success" || disposition == "resolved_diagnostic_failure")
                    && continuation == (n + 1 == TotalChildCount ? "schedule_complete" : "launch_next"), "resolved slot state");
            } else if (phase == "fatal") {
                Check(done && disposition == "unresolved_campaign_fatal" && continuation == "stop_campaign" && fatal == n, "fatal slot state"); fatalSeen = true; fatalRevision = done;
            } else {
                Check(!terminal && !done && !disposition && continuation == "pending", "transient slot state");
                Check(phase == "launch_intent" ? !process && !inspection : phase == "process_terminated" ? process && !inspection
                    : phase == "inspection_complete" && process && inspection, "unknown or inconsistent control phase"); pendingSeen = true;
            }
        }
        const auto& p = RequireType<JsonObject>(RequireMember(s, "process"), "process");
        static const auto expectedProcess = std::get<JsonObject>(ProcessJson({}).storage);
        Check(p.size() == expectedProcess.size(), "process key set");
        for (const auto& [key, expected] : expectedProcess) { const auto& v = RequireMember(p, key);
            if (std::holds_alternative<bool>(expected.storage)) (void)RequireType<bool>(v, key);
            else if (std::holds_alternative<std::string>(expected.storage)) (void)RequireType<std::string>(v, key);
        }
        for (const auto key : {"process_id", "exit_code", "job_total_processes", "job_active_processes"}) {
            const auto v = OptionalUnsigned(p, key); Check(!v || *v <= UINT32_MAX, "process DWORD outside range");
        }
        Check(RequireUnsigned(p, "trailing_bytes") < sizeof(progress::ProgressEvent), "trailing frame byte bound");
        const auto timedOut = OptionalUnsigned(p, "timed_out_sample_index"); Check(!timedOut || *timedOut < PlannedSampleCount, "timeout sample range");
        for (const auto key : {"launch_time_utc", "exit_time_utc", "control_error"}) { const auto v = OptionalString(p, key); Check(!v || (!v->empty() && v->size() <= 128), "process text bound"); }
        const auto kind = RequireString(p, "exit_kind"), form = RequireString(p, "progress_form");
        Check(kind == "NotAvailable" || kind == "VoluntaryStage6" || kind == "ProgressTransportAbort" || kind == "SupervisorForced" || kind == "Abnormal", "exit kind");
        Check(form == "NotStarted" || form == "Empty" || form == "ReturnedPrefix" || form == "Full" || form == "OutstandingStartedPrefix" || form == "Invalid", "progress terminal form");
        for (const auto key : {"active_attempt", "last_returned_attempt"}) {
            const auto& v = RequireMember(p, key); if (std::holds_alternative<std::nullptr_t>(v.storage)) continue;
            const auto& identity = RequireType<JsonObject>(v, key); RequireExactKeys(identity, {"slot_sequence_index", "sample_index"});
            Check(RequireUnsigned(identity, "slot_sequence_index") == n && RequireUnsigned(identity, "sample_index") < PlannedSampleCount, "progress identity");
        }
        (void)RequireType<EncodedTranscript>(RequireMember(p, "progress_transcript"), "transcript");
        const auto& in = RequireType<JsonObject>(RequireMember(s, "inspection"), "inspection"); static const auto expectedInspection = std::get<JsonObject>(InspectionJson({}).storage);
        Check(in.size() == expectedInspection.size(), "inspection key set");
        for (const auto& [key, expected] : expectedInspection) { const auto& v = RequireMember(in, key);
            if (std::holds_alternative<bool>(expected.storage)) (void)RequireType<bool>(v, key);
            else if (std::holds_alternative<std::string>(expected.storage)) (void)RequireType<std::string>(v, key);
        }
        const auto package = OptionalString(in, "package_sha256"); Check(!package || IsLowerHex(*package, 64), "package hash type");
        const auto topology = RequireString(in, "topology"), location = RequireString(in, "location");
        Check(topology == "Missing" || topology == "SidecarOnly" || topology == "StagingOnly" || topology == "StagingWithSidecar"
            || topology == "FinalOnly" || topology == "FinalWithSidecar" || topology == "Contradictory", "inspection topology");
        Check(location == "None" || location == "Final" || location == "Staging", "inspection location");
        const auto& errors = RequireType<JsonArray>(RequireMember(in, "errors"), "errors"); Check(errors.size() <= 16, "inspection error count");
        for (const auto& error : errors) { const auto& text = RequireType<std::string>(error, "error"); Check(!text.empty() && text.size() <= 128, "inspection error bound"); }
        const auto& science = RequireMember(in, "science");
        if (!std::holds_alternative<std::nullptr_t>(science.storage)) {
            const auto& facts = RequireType<JsonObject>(science, "science");
            RequireExactKeys(facts, {"terminal_state", "recorded_sample_count", "process_status", "failure_phase", "error_code"});
            const auto status = RequireString(facts, "terminal_state"); Check(status == "Success" || status == "DiagnosticFailure", "scientific terminal state");
            Check(RequireUnsigned(facts, "recorded_sample_count") <= PlannedSampleCount, "scientific sample count");
            (void)ReadEnum(RequireString(facts, "process_status"), ev::Status::Incomplete); (void)ReadPhase(OptionalString(facts, "failure_phase"));
            const auto error = OptionalString(facts, "error_code"); Check(!error || error->size() <= 128, "science error bound");
        }
        const auto& sidecar = RequireMember(in, "sidecar");
        if (!std::holds_alternative<std::nullptr_t>(sidecar.storage)) {
            const auto& facts = RequireType<JsonObject>(sidecar, "sidecar");
            RequireExactKeys(facts, {"session_id", "slot_sequence_index", "foundation_established", "run_id", "series_id", "source_revision", "exit_category",
                "failure_phase", "error_code", "sample_index", "completion_uncertain", "native_phase", "native_code", "native_detail"});
            Check(RequireString(facts, "session_id") == Child(m, n).sessionId && RequireUnsigned(facts, "slot_sequence_index") == n
                && RequireString(facts, "run_id") == Child(m, n).sessionId && RequireString(facts, "source_revision") == m.sourceRevision, "sidecar ledger identity");
            (void)RequireBoolean(facts, "foundation_established"); (void)RequireBoolean(facts, "completion_uncertain");
            Check(RequireUnsigned(facts, "exit_category") <= 5 && IsLowerHex(RequireString(facts, "series_id"), 64), "sidecar control identity");
            const auto sample = OptionalUnsigned(facts, "sample_index"); Check(!sample || *sample < PlannedSampleCount, "sidecar sample range");
            for (const auto key : {"failure_phase", "error_code"}) Check(RequireString(facts, key).size() <= 128, "sidecar identifier bound");
            for (const auto key : {"native_phase", "native_code", "native_detail"}) { const auto v = OptionalString(facts, key); Check(!v || v->size() <= 512, "sidecar text bound"); }
        }
        const auto& artifacts = RequireType<JsonArray>(RequireMember(in, "artifacts"), "artifacts"); Check(artifacts.size() <= 4, "artifact bound");
        for (const auto& a : artifacts) { auto anchor = RequireType<JsonObject>(a, "artifact"); const auto name = RequireString(anchor, "name");
            Check(std::ranges::find(PackageNames, name) != PackageNames.end(), "artifact name"); anchor.erase("name"); ValidateAnchor({{anchor}}); }
        ValidateAnchor(RequireMember(in, "sidecar_artifact"));
    }
    Check(fatal.has_value() == fatalSeen && (!fatalSeen || reason), "fatal identity mismatch");
    const auto& cells = RequireType<JsonArray>(RequireMember(o, "cell_analysis"), "cell analysis"); Check(cells.size() == CoreCellCount, "cell anchor count");
    for (std::size_t g = 0; g < cells.size(); ++g) {
        ValidateAnchor(cells[g]); if (!std::holds_alternative<std::nullptr_t>(cells[g].storage))
            for (std::size_t p = 0; p < ChildrenPerCell; ++p) Check(RequireString(RequireType<JsonObject>(slots[g * ChildrenPerCell + p], "slot"), "control_phase") == "resolved", "analysis for incomplete cell");
        if (state == "completed") Check(!std::holds_alternative<std::nullptr_t>(cells[g].storage), "completed control missing cell analysis");
    }
    ValidateAnchor(RequireMember(o, "campaign_analysis"));
    if (state == "completed") Check(!fatal && !reason && !std::holds_alternative<std::nullptr_t>(RequireMember(o, "campaign_analysis").storage), "completed ledger incomplete");
    if (state == "incomplete") Check(reason.has_value(), "incomplete ledger missing reason");
}
Artifact PublishAnalysis(const std::filesystem::path& root, const std::filesystem::path& path, std::string_view bytes)
{
    const auto relative = DeclaredPath(path.lexically_relative(root).generic_string());
    const auto temporary = std::filesystem::path(path.wstring() + L".tmp"); Absent(path); Absent(temporary); RejectReparsePath(path.parent_path());
    Check(std::filesystem::is_directory(path.parent_path()), "analysis directory unavailable");
    WriteDurableFile(temporary, bytes); Check(ReadText(temporary) == bytes, "analysis temporary readback mismatch");
    Check(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH), "analysis publication failed");
    const auto final = ReadText(path); Check(final == bytes, "analysis final readback mismatch"); return {path.filename().string(), relative, final.size(), Sha256(final)};
}
namespace
{
struct LedgerPublicationError : std::runtime_error { LedgerPublicationError() : std::runtime_error("ledger publication failed") {} };
class LedgerWriter final
{
public:
    LedgerWriter(const Manifest& m, const RuntimePaths& paths, const RuntimeServices& services)
        : final_(ControlPath(m, paths.repositoryRoot)), staging_(final_.wstring() + L".incomplete"), temporary_(final_.wstring() + L".incomplete.tmp"), services_(services) {}
    void Save(const Ledger& l)
    {
        const auto bytes = SerializeLedger(l);
        try {
            Absent(final_); Absent(temporary_); RejectReparsePath(staging_);
            Check(l.revision == (owned_ ? revision_ + 1 : 0), "nonconsecutive ledger revision");
            if (owned_) {
                const auto previous = ReadText(staging_, MaxStage6LedgerBytes); ValidateLedgerBytes(previous);
                Check(previous == durableBytes_, "owned ledger changed");
                const auto before = RequireType<JsonObject>(JsonParser(previous, true).Parse(), "prior ledger");
                Check(RequireUnsigned(before, "ledger_revision") == l.revision - 1 && RequireString(before, "control_start_time_utc") == l.controlStartUtc
                    && RequireString(before, "manifest_id") == l.manifest.id && RequireString(before, "manifest_sha256") == l.manifest.sha256
                    && RequireString(before, "manifest_file_sha256") == l.manifest.fileSha256 && RequireString(before, "resume_policy") == "forbidden", "ledger ownership mismatch");
            } else Absent(staging_);
            Fault("before_create", l); WriteDurableFile(temporary_, bytes); Fault("after_flush", l);
            const auto temporary = ReadText(temporary_, MaxStage6LedgerBytes); Check(temporary == bytes, "ledger temporary readback mismatch"); ValidateLedgerBytes(temporary);
            Fault("before_replace", l);
            Check(MoveFileExW(temporary_.c_str(), staging_.c_str(), MOVEFILE_WRITE_THROUGH | (owned_ ? MOVEFILE_REPLACE_EXISTING : 0)), "ledger staging publication failed");
            Fault("after_replace", l);
            const auto retained = ReadText(staging_, MaxStage6LedgerBytes); Check(retained == bytes, "ledger staging readback mismatch"); ValidateLedgerBytes(retained);
            durableBytes_ = retained; revision_ = l.revision; owned_ = true;
            Fault("after_reopen", l); if (services_.afterDurableUpdate) services_.afterDurableUpdate(l);
        } catch (...) { throw LedgerPublicationError(); }
    }
    JsonObject Reopen(const Ledger& l) const
    {
        try { const auto bytes = ReadText(staging_, MaxStage6LedgerBytes); Check(owned_ && bytes == durableBytes_ && bytes == SerializeLedger(l), "durable ledger trust lost");
            ValidateLedgerBytes(bytes); return RequireType<JsonObject>(JsonParser(bytes, true).Parse(), "durable ledger"); }
        catch (...) { throw LedgerPublicationError(); }
    }
    bool CurrentSnapshotRepresentable(const Ledger& l) const noexcept
    {
        try { return owned_ && l.revision == revision_ && SerializeLedger(l) == durableBytes_; }
        catch (...) { return false; }
    }
    void Finalize(const Ledger& l)
    {
        try {
            (void)Reopen(l); Absent(final_); Fault("before_final_rename", l);
            Check(MoveFileExW(staging_.c_str(), final_.c_str(), MOVEFILE_WRITE_THROUGH), "terminal ledger publication failed");
            // No hooks or mutations are permitted after this rename.
            const auto bytes = ReadText(final_, MaxStage6LedgerBytes); Check(bytes == durableBytes_, "terminal ledger readback mismatch"); ValidateLedgerBytes(bytes);
        } catch (...) { throw LedgerPublicationError(); }
    }
private:
    void Fault(std::string_view phase, const Ledger& l) const { if (services_.publicationFault) services_.publicationFault(phase, l); }
    std::filesystem::path final_, staging_, temporary_; const RuntimeServices& services_;
    bool owned_{}; std::uint64_t revision_{}; std::string durableBytes_;
};
void Advance(Ledger& l, LedgerWriter& writer) { Check(l.revision != UINT64_MAX, "revision exhausted"); ++l.revision; writer.Save(l); }
void Enter(Ledger& l, LedgerWriter& writer, std::uint64_t sequence)
{ auto& s = l.slots[sequence]; Check(s.phase == "pending", "slot reentry forbidden"); s.phase = "launch_intent"; s.enteredRevision = l.revision + 1; Advance(l, writer); }
void Fatal(Ledger& l, LedgerWriter& writer, std::uint64_t sequence, std::string reason)
{
    Check(l.slots[sequence].enteredRevision && !l.slots[sequence].disposition, "fatal slot must be entered and unresolved");
    l.fatalSequence = sequence; l.fatalReason = reason;
    for (std::uint64_t n = sequence; n < TotalChildCount; ++n) {
        auto& s = l.slots[n]; s.phase = n == sequence ? "fatal" : "not_launched"; s.terminalRevision = l.revision + 1;
        s.disposition = n == sequence ? analysis::SlotDisposition::UnresolvedCampaignFatal : analysis::SlotDisposition::NotLaunched;
        s.reason = n == sequence ? reason : "prior_campaign_fatal"; s.continuation = n == sequence ? "stop_campaign" : "not_launched";
    }
    Advance(l, writer);
}
void GlobalFailure(Ledger& l, std::string reason) { if (!l.fatalReason) { l.fatalReason = std::move(reason); l.fatalSequence.reset(); } }
void VerifyArtifact(const Artifact& a, const std::filesystem::path& root)
{
    (void)DeclaredPath(a.relativePath); const auto bytes = ReadText(root / a.relativePath);
    Check(bytes.size() == a.sizeBytes && Sha256(bytes) == a.sha256, "anchored artifact changed");
}
void TerminalIntegrity(const Ledger& l, const RuntimePaths& paths, const RuntimeServices& services)
{
    const auto manifest = LoadManifestFromRepository(paths.repositoryRoot, ManifestPath(l.manifest, {}));
    Check(manifest.sha256 == l.manifest.sha256 && manifest.fileSha256 == l.manifest.fileSha256 && manifest.canonicalJson == l.manifest.canonicalJson, "manifest drift");
    for (std::size_t n = 0; n < TotalChildCount; ++n) { const auto& slot = l.slots[n];
        if (slot.inspection.attempted) {
            const auto inventory = InventoryPackage(l.manifest, n, paths.repositoryRoot);
            Check(inventory.topology == slot.inspection.topology && inventory.artifacts == slot.inspection.artifacts
                && inventory.sidecarArtifact == slot.inspection.sidecarArtifact, "terminal package topology or inventory changed");
        }
        for (const auto& artifact : slot.inspection.artifacts) VerifyArtifact(artifact, paths.repositoryRoot);
        if (slot.inspection.sidecarArtifact) VerifyArtifact(*slot.inspection.sidecarArtifact, paths.repositoryRoot);
        if (slot.inspection.packageSha256) Check(PackageHash(slot.inspection.artifacts) == *slot.inspection.packageSha256, "package inventory relationship changed");
    }
    for (const auto& a : l.cellAnalyses) if (a) VerifyArtifact(*a, paths.repositoryRoot);
    if (l.campaignAnalysis) VerifyArtifact(*l.campaignAnalysis, paths.repositoryRoot);
    ValidatePreflightFacts(l.manifest, services.collectFacts(l.manifest, paths));
}
analysis::CampaignInput CampaignFromLedger(const JsonObject& o)
{
    analysis::CampaignInput input; const auto& slots = RequireType<JsonArray>(RequireMember(o, "slots"), "slots");
    for (std::size_t n = 0; n < TotalChildCount; ++n) input.slots[n] = ReadDisposition(RequireString(RequireType<JsonObject>(slots[n], "slot"), "disposition"));
    const auto& cells = RequireType<JsonArray>(RequireMember(o, "cell_analysis"), "cells");
    for (std::size_t n = 0; n < CoreCellCount; ++n) if (!std::holds_alternative<std::nullptr_t>(cells[n].storage))
        input.cellAnalysisSha256[n] = RequireString(RequireType<JsonObject>(cells[n], "anchor"), "sha256");
    return input;
}
void RequireAnalysisDirectory(const Manifest& m, const std::filesystem::path& root, bool& created)
{
    const auto path = AnalysisPath(m, root); RejectReparsePath(path);
    if (!created) { Absent(path); Check(std::filesystem::create_directory(path), "analysis directory creation failed"); created = true; }
    Check(std::filesystem::is_directory(path), "analysis directory changed");
}
} // namespace
ExitCode ExecuteManifestWithServices(const Manifest& supplied, const RuntimePaths& paths, const RuntimeServices& services)
{
    auto ownedLedger = std::make_unique<Ledger>(); auto& ledger = *ownedLedger;
    try {
        Check(services.collectFacts && services.runProcess && services.inspectPackage && services.publishAnalysis, "missing supervisor service");
        ledger.manifest = ParseManifest(supplied.canonicalJson + "\n");
        const auto disk = LoadManifestFromRepository(paths.repositoryRoot, ManifestPath(ledger.manifest, {}));
        Check(disk.canonicalJson == ledger.manifest.canonicalJson, "manifest disk mismatch");
        ledger.observed = services.collectFacts(ledger.manifest, paths); ValidatePreflightFacts(ledger.manifest, ledger.observed);
        RejectOutputCollisions(ledger.manifest, paths.repositoryRoot); ledger.controlStartUtc = TimestampUtc();
    } catch (...) { return ExitCode::ConfigurationOrPreflightRejected; }
    LedgerWriter writer(ledger.manifest, paths, services); bool directoryCreated{};
    try {
        writer.Save(ledger);
        ledger.qpcFrequency = Frequency(); ledger.campaignStartQpc = NowQpc(); ledger.campaignStartUtc = TimestampUtc();
        ledger.campaignDeadlineQpc = AddDeadline(*ledger.campaignStartQpc, DeadlineTicks(ledger.manifest.campaignTimeoutMs, *ledger.qpcFrequency));
        ledger.state = "running"; Advance(ledger, writer);
        for (std::uint64_t sequence = 0; sequence < TotalChildCount; ++sequence) {
            Enter(ledger, writer, sequence); auto& slot = ledger.slots[sequence];
            try {
                ValidatePreflightFacts(ledger.manifest, services.collectFacts(ledger.manifest, paths));
                RejectSlotCollision(ledger.manifest, sequence, paths.repositoryRoot);
                Check(NowQpc() < *ledger.campaignDeadlineQpc, "campaign deadline");
            } catch (...) { Fatal(ledger, writer, sequence, NowQpc() >= *ledger.campaignDeadlineQpc ? "campaign_timeout" : "prelaunch_drift"); break; }
            try { slot.process = services.runProcess(ledger.manifest, paths, sequence, *ledger.qpcFrequency, *ledger.campaignDeadlineQpc); }
            catch (...) { slot.process.controlError = "process_control_exception"; }
            slot.phase = "process_terminated"; slot.processRevision = ledger.revision + 1; Advance(ledger, writer);
            if (slot.process.primaryTerminationConfirmed && slot.process.jobEmptyConfirmed) {
                try { slot.inspection = services.inspectPackage(ledger.manifest, sequence, paths.repositoryRoot); }
                catch (...) { slot.inspection.attempted = true; slot.inspection.errors = {"inspection_exception"}; }
            }
            slot.phase = "inspection_complete"; slot.inspectionRevision = ledger.revision + 1; Advance(ledger, writer);
            const auto reconciliation = ReconcileSlot(slot.process, slot.inspection);
            if (!Resolved(reconciliation.disposition)) { Fatal(ledger, writer, sequence, reconciliation.reason); break; }
            slot.phase = "resolved"; slot.disposition = reconciliation.disposition; slot.reason = reconciliation.reason;
            slot.continuation = sequence + 1 == TotalChildCount ? "schedule_complete" : "launch_next"; slot.terminalRevision = ledger.revision + 1; Advance(ledger, writer);
            if ((sequence + 1) % ChildrenPerCell != 0) continue;
            const auto cell = sequence / ChildrenPerCell;
            try {
                (void)writer.Reopen(ledger); Check(NowQpc() < *ledger.campaignDeadlineQpc, "campaign deadline");
                analysis::CellInput input; input.cellIndex = cell;
                for (std::uint64_t p = 0; p < ChildrenPerCell; ++p) {
                    const auto n = cell * ChildrenPerCell + p; const auto& retained = ledger.slots[n];
                    const auto fresh = services.inspectPackage(ledger.manifest, n, paths.repositoryRoot);
                    Check(Canonical(InspectionJson(fresh)) == Canonical(InspectionJson(retained.inspection)), "cell evidence changed");
                    const auto resolved = ReconcileSlot(retained.process, fresh);
                    Check(resolved.disposition == retained.disposition && resolved.process, "cell terminal state changed");
                    input.slots[p] = {*retained.disposition, resolved.process};
                }
                const auto bytes = analysis::SerializeCellJson(analysis::DescribeCell(input));
                RequireAnalysisDirectory(ledger.manifest, paths.repositoryRoot, directoryCreated);
                const auto number = std::to_string(cell); const auto path = AnalysisPath(ledger.manifest, paths.repositoryRoot) / ("cell-" + std::string(2 - number.size(), '0') + number + "-analysis.json");
                const auto anchor = services.publishAnalysis(paths.repositoryRoot, path, bytes); VerifyArtifact(anchor, paths.repositoryRoot);
                Check(anchor.relativePath == path.lexically_relative(paths.repositoryRoot).generic_string() && anchor.sha256 == Sha256(bytes) && anchor.sizeBytes == bytes.size(), "analysis publisher anchor mismatch");
                ledger.cellAnalyses[cell] = anchor; Advance(ledger, writer);
            } catch (const LedgerPublicationError&) { throw; }
            catch (...) {
                if (sequence + 1 < TotalChildCount) { Enter(ledger, writer, sequence + 1); Fatal(ledger, writer, sequence + 1, "cell_analysis_publication_failure"); }
                else GlobalFailure(ledger, "cell_analysis_publication_failure");
                break;
            }
        }
        const bool allResolved = std::ranges::all_of(ledger.slots, [](const auto& s) { return Resolved(s.disposition); });
        if (NowQpc() >= *ledger.campaignDeadlineQpc) GlobalFailure(ledger, "campaign_timeout");
        if (!ledger.fatalReason || (ledger.fatalSequence && NowQpc() < *ledger.campaignDeadlineQpc)) {
            try {
                const auto input = CampaignFromLedger(writer.Reopen(ledger)); const auto description = analysis::DescribeCampaign(input);
                Check(!allResolved || description.Status() == analysis::CoverageStatus::Complete, "complete campaign description required");
                const auto bytes = analysis::SerializeCampaignJson(description);
                RequireAnalysisDirectory(ledger.manifest, paths.repositoryRoot, directoryCreated);
                Check(NowQpc() < *ledger.campaignDeadlineQpc, "campaign deadline");
                const auto path = AnalysisPath(ledger.manifest, paths.repositoryRoot) / "campaign-analysis.json";
                const auto anchor = services.publishAnalysis(paths.repositoryRoot, path, bytes); VerifyArtifact(anchor, paths.repositoryRoot);
                Check(anchor.relativePath == path.lexically_relative(paths.repositoryRoot).generic_string() && anchor.sha256 == Sha256(bytes) && anchor.sizeBytes == bytes.size(), "campaign anchor mismatch");
                ledger.campaignAnalysis = anchor; Advance(ledger, writer);
            } catch (const LedgerPublicationError&) { throw; }
            catch (...) { if (allResolved) GlobalFailure(ledger, "campaign_analysis_publication_failure"); }
        }
        try { TerminalIntegrity(ledger, paths, services); } catch (...) { GlobalFailure(ledger, "terminal_integrity_failure"); }
        if (NowQpc() >= *ledger.campaignDeadlineQpc) GlobalFailure(ledger, "campaign_timeout");
        ledger.state = ledger.fatalReason ? "incomplete" : "completed"; ledger.campaignEndUtc = TimestampUtc(); Advance(ledger, writer);
        if (ledger.state == "completed" && NowQpc() >= *ledger.campaignDeadlineQpc) {
            GlobalFailure(ledger, "campaign_timeout"); ledger.state = "incomplete"; ledger.campaignEndUtc = TimestampUtc(); Advance(ledger, writer);
        }
        writer.Finalize(ledger); return ledger.state == "completed" ? ExitCode::Completed : ExitCode::CampaignIncomplete;
    } catch (const LedgerPublicationError&) { return ExitCode::ControlPublicationFailure; }
    catch (...) {
        // An unexpected exception is not automatically an unrepresentable
        // campaign. Recover only from the exact still-owned durable snapshot;
        // never retry a failed ledger publication or invent missing facts.
        if (!writer.CurrentSnapshotRepresentable(ledger)) return ExitCode::InternalControlFailure;
        try {
            if (!ledger.campaignStartQpc) {
                ledger.qpcFrequency = Frequency(); ledger.campaignStartQpc = NowQpc(); ledger.campaignStartUtc = TimestampUtc();
                ledger.campaignDeadlineQpc = AddDeadline(*ledger.campaignStartQpc, DeadlineTicks(ledger.manifest.campaignTimeoutMs, *ledger.qpcFrequency));
                ledger.state = "running"; Advance(ledger, writer);
            }
            if (!ledger.fatalReason) {
                const auto next = std::ranges::find_if(ledger.slots, [](const auto& s) { return !Resolved(s.disposition); });
                if (next == ledger.slots.end()) GlobalFailure(ledger, "internal_control_failure");
                else {
                    const auto sequence = static_cast<std::uint64_t>(next - ledger.slots.begin());
                    if (!next->enteredRevision) Enter(ledger, writer, sequence);
                    Fatal(ledger, writer, sequence, "internal_control_failure");
                }
            }
            try { TerminalIntegrity(ledger, paths, services); } catch (...) { GlobalFailure(ledger, "terminal_integrity_failure"); }
            ledger.state = "incomplete"; ledger.campaignEndUtc = TimestampUtc(); Advance(ledger, writer);
            writer.Finalize(ledger); return ExitCode::CampaignIncomplete;
        } catch (const LedgerPublicationError&) { return ExitCode::ControlPublicationFailure; }
        catch (...) { return ExitCode::InternalControlFailure; }
    }
}
ExitCode ExecuteManifest(const Manifest& m, const RuntimePaths& paths)
{
    RuntimeServices services; services.collectFacts = CollectPreflightFacts;
    services.runProcess = [](const Manifest& manifest, const RuntimePaths& p, std::uint64_t slot, std::int64_t frequency, std::int64_t deadline) {
        return RunSupervisedProcess(p.repositoryRoot / manifest.childExecutablePath, ChildArguments(manifest, slot), slot,
            manifest.operationTimeoutMs, manifest.childTimeoutMs, frequency, deadline);
    };
    services.inspectPackage = [](const Manifest& manifest, std::uint64_t slot, const std::filesystem::path& root) { return InspectPackage(manifest, slot, root); };
    services.publishAnalysis = PublishAnalysis; return ExecuteManifestWithServices(m, paths, services);
}
} // namespace computelab::ex2::stage6::control
