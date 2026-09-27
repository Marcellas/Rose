#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rose::integrations
{
    enum class OutlookCommandKind { Status, Configure, Connect, Disconnect, Inbox, Search };

    struct OutlookCommand
    {
        OutlookCommandKind kind{OutlookCommandKind::Status};
        std::string text;
        std::string tenant;
        std::size_t count{10};
    };

    // nullopt means ordinary chat text; malformed /outlook commands throw with
    // user-facing usage text so they never fall through to the language model.
    [[nodiscard]] std::optional<OutlookCommand> parseOutlookCommand(std::string_view text);
}
