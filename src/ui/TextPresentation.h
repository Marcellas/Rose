#pragma once

#include <string>
#include <string_view>


namespace rose::ui
{

    // -----------------------------------------------------------------------------
    // makeReadableChatText()
    // -----------------------------------------------------------------------------
    //
    // Converts model-authored Markdown / lightweight LaTeX into a form that is more
    // pleasant to read in Rose's current plain SDL_ttf transcript.
    //
    // IMPORTANT:
    // This is a PRESENTATION transform only.
    //
    // The caller should keep the original model response unchanged for persistence,
    // memory, retrieval, export, debugging, and future rich-document rendering.
    //
    // This function intentionally implements only conservative formatting that can
    // be represented safely in a plain text surface. It is not a Markdown parser or
    // a TeX engine.
    [[nodiscard]]
    std::string makeReadableChatText(
        std::string_view text);

} // namespace rose::ui
