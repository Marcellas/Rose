#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rose::integrations::json
{
    class Value
    {
    public:
        using Object = std::map<std::string, Value, std::less<>>;
        using Array = std::vector<Value>;
        using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

        Value();
        explicit Value(Storage storage);

        [[nodiscard]] const Object* object() const noexcept;
        [[nodiscard]] const Array* array() const noexcept;
        [[nodiscard]] const std::string* string() const noexcept;
        [[nodiscard]] std::optional<double> number() const noexcept;
        [[nodiscard]] std::optional<bool> boolean() const noexcept;
        [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    private:
        Storage storage_;
    };

    [[nodiscard]] Value parse(std::string_view text);
    [[nodiscard]] std::string stringOr(const Value& object, std::string_view key, std::string fallback = {});
    [[nodiscard]] std::int64_t integerOr(const Value& object, std::string_view key, std::int64_t fallback);
    [[nodiscard]] bool boolOr(const Value& object, std::string_view key, bool fallback);
}
