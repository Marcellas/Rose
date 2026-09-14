#pragma once

#include <string>
#include <vector>

namespace rose::ui
{

    // -----------------------------------------------------------------------------
    // InlineStyle
    // -----------------------------------------------------------------------------
    //
    // Semantic styling produced by ResponseParser. The parser describes meaning;
    // RichResponseRenderer decides how that meaning looks on screen.
    enum class InlineStyle
    {
        Normal,
        Bold,
        Italic,
        BoldItalic,
        Code,
        Math
    };


    struct InlineSpan
    {
        InlineStyle style{ InlineStyle::Normal };
        std::string text;
    };


    enum class ResponseBlockKind
    {
        Paragraph,
        Heading,
        CodeBlock,
        DisplayMath,
        ListItem,
        Separator
    };


    // -----------------------------------------------------------------------------
    // ResponseBlock
    // -----------------------------------------------------------------------------
    //
    // A deliberately small rich-document vocabulary for Rose v0.1.
    //
    // Paragraph / Heading / ListItem use spans.
    // CodeBlock / DisplayMath use text.
    // CodeBlock may also carry a language label.
    // ListItem uses orderedIndex == 0 for bullets, otherwise the displayed number.
    struct ResponseBlock
    {
        ResponseBlockKind kind{ ResponseBlockKind::Paragraph };

        std::vector<InlineSpan> spans;

        std::string text;
        std::string language;

        int headingLevel{ 0 };
        int orderedIndex{ 0 };
    };


    struct ResponseDocument
    {
        std::vector<ResponseBlock> blocks;
    };

} // namespace rose::ui
