#include "integrations/OutlookCommand.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace rose::integrations
{
    namespace
    {
        constexpr std::string_view usage{
            "Usage: /outlook status | configure <Azure-client-id> <tenant> | connect | disconnect | inbox [count] | search <query>"
        };

        std::size_t parseCount(std::string_view value)
        {
            if (value.empty()) return 10;
            std::size_t count = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
            if (error != std::errc{} || end != value.data() + value.size() || count == 0)
                throw std::invalid_argument{ std::string{ usage } };
            return std::clamp<std::size_t>(count, 1, 50);
        }
    }

    std::optional<OutlookCommand> parseOutlookCommand(const std::string_view text)
    {
        if (text == "/outlook") throw std::invalid_argument{ std::string{ usage } };
        constexpr std::string_view prefix{ "/outlook " };
        if (!text.starts_with(prefix)) return std::nullopt;
        const std::string_view rest = text.substr(prefix.size());
        if (rest == "status") return OutlookCommand{ .kind = OutlookCommandKind::Status, .text = {}, .tenant = {}, .count = 10 };
        if (rest == "connect") return OutlookCommand{ .kind = OutlookCommandKind::Connect, .text = {}, .tenant = {}, .count = 10 };
        if (rest == "disconnect") return OutlookCommand{ .kind = OutlookCommandKind::Disconnect, .text = {}, .tenant = {}, .count = 10 };
        if (rest == "inbox") return OutlookCommand{ .kind = OutlookCommandKind::Inbox, .text = {}, .tenant = {}, .count = 10 };
        if (rest.starts_with("inbox "))
            return OutlookCommand{ .kind = OutlookCommandKind::Inbox, .text = {}, .tenant = {}, .count = parseCount(rest.substr(6)) };
        if (rest.starts_with("search "))
        {
            const std::string_view query = rest.substr(7);
            if (query.empty()) throw std::invalid_argument{ std::string{ usage } };
            return OutlookCommand{ .kind = OutlookCommandKind::Search, .text = std::string{ query }, .tenant = {}, .count = 10 };
        }
        if (rest.starts_with("configure "))
        {
            const std::string_view arguments = rest.substr(10);
            const std::size_t separator = arguments.find(' ');
            if (separator == std::string_view::npos)
                throw std::invalid_argument{ std::string{ usage } };
            if (separator == 0 || separator + 1 >= arguments.size())
                throw std::invalid_argument{ std::string{ usage } };
            return OutlookCommand{
                .kind = OutlookCommandKind::Configure,
                .text = std::string{ arguments.substr(0, separator) },
                .tenant = std::string{ arguments.substr(separator + 1) }
            };
        }
        throw std::invalid_argument{ std::string{ usage } };
    }
}
