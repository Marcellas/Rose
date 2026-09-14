#pragma once

#include "ui/ResponseDocument.h"

#include <string_view>

namespace rose::ui
{

    // Parse Rose's model-authored Markdown / LaTeX into semantic presentation
    // blocks. The original model response is never modified by this operation.
    [[nodiscard]]
    ResponseDocument parseResponseDocument(
        std::string_view source);


    // Build a document from literal user/error text without interpreting Markdown.
    // prefix is emitted as a bold span, for example "You: " or "Error: ".
    [[nodiscard]]
    ResponseDocument makePlainResponseDocument(
        std::string_view prefix,
        std::string_view text);


    // Add a small bold label before an already-parsed document. If the document
    // begins with a paragraph, the label is inserted into that paragraph; otherwise
    // a short label paragraph is inserted before the first block.
    void prependDocumentLabel(
        ResponseDocument& document,
        std::string_view label);

} // namespace rose::ui
