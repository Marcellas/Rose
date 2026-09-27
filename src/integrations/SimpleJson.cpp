#include "integrations/SimpleJson.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace rose::integrations::json
{
    Value::Value() : storage_{ nullptr } {}
    Value::Value(Storage storage) : storage_{ std::move(storage) } {}
    const Value::Object* Value::object() const noexcept { return std::get_if<Object>(&storage_); }
    const Value::Array* Value::array() const noexcept { return std::get_if<Array>(&storage_); }
    const std::string* Value::string() const noexcept { return std::get_if<std::string>(&storage_); }
    std::optional<double> Value::number() const noexcept
    {
        if (const auto* value = std::get_if<double>(&storage_)) return *value;
        return std::nullopt;
    }
    std::optional<bool> Value::boolean() const noexcept
    {
        if (const auto* value = std::get_if<bool>(&storage_)) return *value;
        return std::nullopt;
    }
    const Value* Value::find(const std::string_view key) const noexcept
    {
        const Object* values = object();
        if (!values) return nullptr;
        const auto it = values->find(key);
        return it == values->end() ? nullptr : &it->second;
    }

    namespace
    {
        class Parser
        {
        public:
            explicit Parser(std::string_view text) : text_{ text } {}

            Value parseDocument()
            {
                skipSpace();
                Value value = parseValue();
                skipSpace();
                if (position_ != text_.size()) error("trailing characters");
                return value;
            }

        private:
            [[noreturn]] void error(const char* message) const
            {
                throw std::runtime_error{
                    "Invalid JSON near byte " + std::to_string(position_) + ": " + message + "."
                };
            }

            void skipSpace()
            {
                while (position_ < text_.size())
                {
                    const char c = text_[position_];
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
                    ++position_;
                }
            }

            bool consume(const char c)
            {
                skipSpace();
                if (position_ >= text_.size() || text_[position_] != c) return false;
                ++position_;
                return true;
            }

            Value parseValue()
            {
                skipSpace();
                if (position_ >= text_.size()) error("unexpected end");
                switch (text_[position_])
                {
                case '{': return parseObject();
                case '[': return parseArray();
                case '"': return Value{ parseString() };
                case 't': return parseLiteral("true", Value::Storage{ true });
                case 'f': return parseLiteral("false", Value::Storage{ false });
                case 'n': return parseLiteral("null", Value::Storage{ nullptr });
                default:
                    if (text_[position_] == '-' || (text_[position_] >= '0' && text_[position_] <= '9'))
                        return Value{ parseNumber() };
                    error("unexpected token");
                }
            }

            Value parseLiteral(const std::string_view literal, Value::Storage storage)
            {
                if (text_.substr(position_, literal.size()) != literal) error("invalid literal");
                position_ += literal.size();
                return Value{ std::move(storage) };
            }

            static void appendUtf8(std::string& output, const unsigned codePoint)
            {
                if (codePoint <= 0x7f) output.push_back(static_cast<char>(codePoint));
                else if (codePoint <= 0x7ff)
                {
                    output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
                    output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
                }
                else
                {
                    output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
                    output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                    output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
                }
            }

            std::string parseString()
            {
                if (!consume('"')) error("expected string");
                std::string result;
                while (position_ < text_.size())
                {
                    const char c = text_[position_++];
                    if (c == '"') return result;
                    if (static_cast<unsigned char>(c) < 0x20) error("control character in string");
                    if (c != '\\')
                    {
                        result.push_back(c);
                        continue;
                    }
                    if (position_ >= text_.size()) error("unterminated escape");
                    const char escape = text_[position_++];
                    switch (escape)
                    {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u':
                    {
                        if (position_ + 4 > text_.size()) error("short unicode escape");
                        unsigned codePoint = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = text_[position_++];
                            codePoint <<= 4;
                            if (h >= '0' && h <= '9') codePoint += static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') codePoint += static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') codePoint += static_cast<unsigned>(h - 'A' + 10);
                            else error("invalid unicode escape");
                        }
                        appendUtf8(result, codePoint);
                        break;
                    }
                    default: error("invalid escape");
                    }
                }
                error("unterminated string");
            }

            double parseNumber()
            {
                const std::size_t start = position_;
                while (position_ < text_.size())
                {
                    const char c = text_[position_];
                    const bool numeric =
                        (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                        c == '.' || c == 'e' || c == 'E';
                    if (!numeric) break;
                    ++position_;
                }
                const std::string token{ text_.substr(start, position_ - start) };
                char* end = nullptr;
                const double value = std::strtod(token.c_str(), &end);
                if (!end || *end != '\0' || !std::isfinite(value)) error("invalid number");
                return value;
            }

            Value parseObject()
            {
                if (!consume('{')) error("expected object");
                Value::Object result;
                skipSpace();
                if (consume('}')) return Value{ std::move(result) };
                for (;;)
                {
                    skipSpace();
                    if (position_ >= text_.size() || text_[position_] != '"') error("expected object key");
                    std::string key = parseString();
                    if (!consume(':')) error("expected colon");
                    result.insert_or_assign(std::move(key), parseValue());
                    if (consume('}')) return Value{ std::move(result) };
                    if (!consume(',')) error("expected comma");
                }
            }

            Value parseArray()
            {
                if (!consume('[')) error("expected array");
                Value::Array result;
                skipSpace();
                if (consume(']')) return Value{ std::move(result) };
                for (;;)
                {
                    result.push_back(parseValue());
                    if (consume(']')) return Value{ std::move(result) };
                    if (!consume(',')) error("expected comma");
                }
            }

            std::string_view text_;
            std::size_t position_{0};
        };
    }

    Value parse(const std::string_view text) { return Parser{ text }.parseDocument(); }

    std::string stringOr(const Value& object, const std::string_view key, std::string fallback)
    {
        const Value* value = object.find(key);
        if (!value) return fallback;
        if (const auto* text = value->string()) return *text;
        return fallback;
    }

    std::int64_t integerOr(const Value& object, const std::string_view key, const std::int64_t fallback)
    {
        const Value* value = object.find(key);
        if (!value) return fallback;
        const auto number = value->number();
        if (!number) return fallback;
        if (*number < static_cast<double>(std::numeric_limits<std::int64_t>::min())
            || *number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) return fallback;
        return static_cast<std::int64_t>(*number);
    }

    bool boolOr(const Value& object, const std::string_view key, const bool fallback)
    {
        const Value* value = object.find(key);
        if (!value) return fallback;
        const auto boolean = value->boolean();
        return boolean.value_or(fallback);
    }
}
