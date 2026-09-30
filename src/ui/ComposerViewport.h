#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace rose::ui
{
    struct ComposerViewport
    {
        std::size_t start{ 0 };
        std::size_t end{ 0 };
    };

    // Keep the complete draft in the editor while SDL_ttf lays out a short
    // UTF-8 slice around the caret. Both offsets are canonical byte offsets.
    [[nodiscard]] inline ComposerViewport composerViewport(
        const std::string_view draft,
        const std::size_t caret,
        const std::size_t maximumVisibleBytes = 4096)
    {
        if (draft.size() <= maximumVisibleBytes)
            return { 0, draft.size() };

        const std::size_t boundedCaret = std::min(caret, draft.size());
        std::size_t start = boundedCaret > maximumVisibleBytes / 2
            ? boundedCaret - maximumVisibleBytes / 2 : 0;
        start = std::min(start, draft.size() - maximumVisibleBytes);
        while (start < draft.size()
            && (static_cast<unsigned char>(draft[start]) & 0xC0u) == 0x80u)
            ++start;

        std::size_t end = std::min(draft.size(), start + maximumVisibleBytes);
        while (end > start && end < draft.size()
            && (static_cast<unsigned char>(draft[end]) & 0xC0u) == 0x80u)
            --end;
        return { start, end };
    }
}
