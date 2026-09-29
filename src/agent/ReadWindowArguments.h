#pragma once

#include "tools/ToolTypes.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::agent
{
    // Qwen occasionally emits ARG start_line=820,line_count=1000 on one line.
    // Unpack only this unambiguous numeric shorthand before displaying a
    // confirmation. Invalid values fail closed instead of wasting a /confirm.
    [[nodiscard]] inline bool normalizeReadWindowArguments(tools::ToolRequest& request)
    {
        if (request.toolId != "read_text_file") return true;

        const auto numeric = [](const std::string_view text,
                                std::size_t& value) -> bool
        {
            if (text.empty()) return false;
            const auto [end, error] = std::from_chars(
                text.data(), text.data() + text.size(), value);
            return error == std::errc{} && end == text.data() + text.size()
                && value > 0;
        };

        auto start = request.arguments.find("start_line");
        auto count = request.arguments.find("line_count");
        if (start != request.arguments.end())
        {
            const auto comma = start->second.find(",line_count=");
            if (comma != std::string::npos)
            {
                if (count != request.arguments.end()) return false;
                const std::string first = start->second.substr(0, comma);
                const std::string second =
                    start->second.substr(comma + sizeof(",line_count=") - 1);
                std::size_t firstValue{ 0 }, secondValue{ 0 };
                if (!numeric(first, firstValue) || !numeric(second, secondValue))
                    return false;
                start->second = first;
                request.arguments.emplace("line_count", second);
                start = request.arguments.find("start_line");
                count = request.arguments.find("line_count");
            }

            std::size_t firstValue{ 0 };
            if (!numeric(start->second, firstValue)) return false;
        }
        if (count != request.arguments.end())
        {
            if (start == request.arguments.end()) return false;
            std::size_t countValue{ 0 };
            if (!numeric(count->second, countValue)) return false;
            constexpr std::size_t maximumLineCount{ 200 };
            count->second = std::to_string(std::min(countValue, maximumLineCount));
        }
        return true;
    }
}
