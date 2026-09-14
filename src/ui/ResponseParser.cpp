#include "ui/ResponseParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rose::ui
{

    namespace
    {
        [[nodiscard]]
        bool asciiSpace(
            const char value) noexcept
        {
            return std::isspace(
                static_cast<unsigned char>(value)) != 0;
        }


        [[nodiscard]]
        std::string_view trimView(
            std::string_view text) noexcept
        {
            while (!text.empty() && asciiSpace(text.front()))
            {
                text.remove_prefix(1);
            }

            while (!text.empty() && asciiSpace(text.back()))
            {
                text.remove_suffix(1);
            }

            return text;
        }


        void appendSpan(
            std::vector<InlineSpan>& spans,
            const InlineStyle style,
            std::string_view text)
        {
            if (text.empty())
            {
                return;
            }

            if (
                !spans.empty()
                && spans.back().style == style)
            {
                spans.back().text.append(text);
                return;
            }

            spans.push_back(
                InlineSpan{
                    .style = style,
                    .text = std::string{ text }
                });
        }


        [[nodiscard]]
        std::size_t findUnescaped(
            const std::string_view text,
            const std::string_view marker,
            const std::size_t begin)
        {
            std::size_t position = begin;

            while (position < text.size())
            {
                position = text.find(marker, position);

                if (position == std::string_view::npos)
                {
                    return position;
                }

                std::size_t backslashes{ 0 };

                for (
                    std::size_t cursor = position;
                    cursor > 0 && text[cursor - 1] == '\\';
                    --cursor)
                {
                    ++backslashes;
                }

                if ((backslashes % 2u) == 0u)
                {
                    return position;
                }

                position += marker.size();
            }

            return std::string_view::npos;
        }


        [[nodiscard]]
        std::vector<InlineSpan> parseInline(
            const std::string_view text)
        {
            std::vector<InlineSpan> spans;

            std::size_t index{ 0 };
            std::size_t plainBegin{ 0 };

            const auto flushPlain =
                [&](const std::size_t end)
                {
                    if (end > plainBegin)
                    {
                        appendSpan(
                            spans,
                            InlineStyle::Normal,
                            text.substr(
                                plainBegin,
                                end - plainBegin));
                    }
                };

            while (index < text.size())
            {
                // Inline code is parsed before emphasis or math so punctuation inside
                // code is left completely untouched.
                if (text[index] == '`')
                {
                    const std::size_t close =
                        text.find('`', index + 1);

                    if (close != std::string_view::npos)
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::Code,
                            text.substr(
                                index + 1,
                                close - index - 1));

                        index = close + 1;
                        plainBegin = index;
                        continue;
                    }
                }


                // \( ... \) inline math.
                if (
                    index + 1 < text.size()
                    && text[index] == '\\'
                    && text[index + 1] == '(')
                {
                    const std::size_t close =
                        findUnescaped(
                            text,
                            "\\)",
                            index + 2);

                    if (close != std::string_view::npos)
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::Math,
                            text.substr(
                                index + 2,
                                close - index - 2));

                        index = close + 2;
                        plainBegin = index;
                        continue;
                    }
                }


                // $ ... $ inline math. $$ is reserved for block math and is handled by
                // the block parser before we reach this function.
                if (
                    text[index] == '$'
                    && (index + 1 >= text.size() || text[index + 1] != '$'))
                {
                    const std::size_t close =
                        findUnescaped(
                            text,
                            "$",
                            index + 1);

                    if (
                        close != std::string_view::npos
                        && (close + 1 >= text.size() || text[close + 1] != '$'))
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::Math,
                            text.substr(
                                index + 1,
                                close - index - 1));

                        index = close + 1;
                        plainBegin = index;
                        continue;
                    }
                }


                // ***bold italic***
                if (
                    index + 2 < text.size()
                    && text.substr(index, 3) == "***")
                {
                    const std::size_t close =
                        text.find("***", index + 3);

                    if (close != std::string_view::npos)
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::BoldItalic,
                            text.substr(
                                index + 3,
                                close - index - 3));

                        index = close + 3;
                        plainBegin = index;
                        continue;
                    }
                }


                // **bold** and __bold__.
                if (
                    index + 1 < text.size()
                    && (
                        text.substr(index, 2) == "**"
                        || text.substr(index, 2) == "__"))
                {
                    const std::string_view marker =
                        text.substr(index, 2);

                    const std::size_t close =
                        text.find(marker, index + 2);

                    if (close != std::string_view::npos)
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::Bold,
                            text.substr(
                                index + 2,
                                close - index - 2));

                        index = close + 2;
                        plainBegin = index;
                        continue;
                    }
                }


                // *italic*. Underscore emphasis is intentionally not interpreted here;
                // underscores are common in identifiers and TeX subscripts.
                if (
                    text[index] == '*'
                    && (index + 1 >= text.size() || text[index + 1] != '*'))
                {
                    const std::size_t close =
                        text.find('*', index + 1);

                    if (
                        close != std::string_view::npos
                        && close > index + 1)
                    {
                        flushPlain(index);

                        appendSpan(
                            spans,
                            InlineStyle::Italic,
                            text.substr(
                                index + 1,
                                close - index - 1));

                        index = close + 1;
                        plainBegin = index;
                        continue;
                    }
                }


                ++index;
            }

            flushPlain(text.size());

            return spans;
        }


        [[nodiscard]]
        bool isFence(
            const std::string_view line,
            std::string_view& marker,
            std::string_view& info) noexcept
        {
            const std::string_view trimmed =
                trimView(line);

            if (trimmed.starts_with("```"))
            {
                marker = "```";
                info = trimView(trimmed.substr(3));
                return true;
            }

            if (trimmed.starts_with("~~~"))
            {
                marker = "~~~";
                info = trimView(trimmed.substr(3));
                return true;
            }

            return false;
        }


        [[nodiscard]]
        bool isHorizontalRule(
            const std::string_view line) noexcept
        {
            const std::string_view trimmed =
                trimView(line);

            if (trimmed.size() < 3)
            {
                return false;
            }

            const char marker = trimmed.front();

            if (
                marker != '-'
                && marker != '*'
                && marker != '_')
            {
                return false;
            }

            for (const char value : trimmed)
            {
                if (value != marker && value != ' ' && value != '\t')
                {
                    return false;
                }
            }

            return true;
        }


        struct ListPrefix
        {
            bool matched{ false };
            int orderedIndex{ 0 };
            std::size_t contentOffset{ 0 };
        };


        [[nodiscard]]
        ListPrefix parseListPrefix(
            const std::string_view line) noexcept
        {
            const std::string_view trimmed =
                trimView(line);

            if (
                trimmed.size() >= 2
                && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')
                && trimmed[1] == ' ')
            {
                return ListPrefix{
                    .matched = true,
                    .orderedIndex = 0,
                    .contentOffset = static_cast<std::size_t>(
                        trimmed.data() - line.data()) + 2
                };
            }

            std::size_t digits{ 0 };

            while (
                digits < trimmed.size()
                && std::isdigit(
                    static_cast<unsigned char>(trimmed[digits])) != 0)
            {
                ++digits;
            }

            if (
                digits > 0
                && digits + 1 < trimmed.size()
                && trimmed[digits] == '.'
                && trimmed[digits + 1] == ' ')
            {
                int value{ 0 };

                const char* begin = trimmed.data();
                const char* end = begin + digits;

                const auto [pointer, error] =
                    std::from_chars(begin, end, value);

                if (error == std::errc{} && pointer == end)
                {
                    return ListPrefix{
                        .matched = true,
                        .orderedIndex = value,
                        .contentOffset = static_cast<std::size_t>(
                            trimmed.data() - line.data()) + digits + 2
                    };
                }
            }

            return {};
        }


        [[nodiscard]]
        int headingLevel(
            const std::string_view line) noexcept
        {
            std::size_t count{ 0 };

            while (
                count < line.size()
                && count < 6
                && line[count] == '#')
            {
                ++count;
            }

            if (
                count == 0
                || count >= line.size()
                || line[count] != ' ')
            {
                return 0;
            }

            return static_cast<int>(count);
        }


        [[nodiscard]]
        bool startsDisplayMath(
            const std::string_view trimmed) noexcept
        {
            return trimmed.starts_with("$$") || trimmed.starts_with("\\[");
        }


        [[nodiscard]]
        bool looksLikeStandaloneLatex(
            const std::string_view trimmed) noexcept
        {
            if (
                trimmed.empty()
                || trimmed.size() > 1024
                || trimmed.ends_with('.')
                || trimmed.ends_with(':'))
            {
                return false;
            }

            // Qwen sometimes emits a display equation on its own line without
            // Markdown/TeX delimiters. Recognize only strong TeX-shaped signals so
            // ordinary prose containing a backslash is not reclassified as math.
            const bool hasStructuredTeX =
                trimmed.find("\\frac") != std::string_view::npos
                || trimmed.find("\\sqrt") != std::string_view::npos
                || trimmed.find("\\sum") != std::string_view::npos
                || trimmed.find("\\int") != std::string_view::npos
                || trimmed.find("_{") != std::string_view::npos
                || trimmed.find("^{") != std::string_view::npos;

            const bool hasTeXCommand =
                trimmed.find('\\') != std::string_view::npos;

            const bool hasEquationRelation =
                trimmed.find('=') != std::string_view::npos
                || trimmed.find("\\approx") != std::string_view::npos
                || trimmed.find("\\leq") != std::string_view::npos
                || trimmed.find("\\geq") != std::string_view::npos;

            return
                hasStructuredTeX
                && (hasEquationRelation || hasTeXCommand);
        }


        [[nodiscard]]
        bool endsDisplayMath(
            const std::string_view trimmed,
            const bool dollarDelimited) noexcept
        {
            return dollarDelimited
                ? trimmed.ends_with("$$")
                : trimmed.ends_with("\\]");
        }


        void appendParagraph(
            ResponseDocument& document,
            std::string& paragraph)
        {
            if (paragraph.empty())
            {
                return;
            }

            document.blocks.push_back(
                ResponseBlock{
                    .kind = ResponseBlockKind::Paragraph,
                    .spans = parseInline(paragraph)
                });

            paragraph.clear();
        }

    } // namespace


    ResponseDocument parseResponseDocument(
        const std::string_view source)
    {
        ResponseDocument document;

        std::string paragraph;

        bool inCodeFence{ false };
        std::string fenceMarker;
        std::string codeLanguage;
        std::string code;

        bool inDisplayMath{ false };
        bool displayMathDollarDelimited{ false };
        std::string displayMath;

        std::size_t lineBegin{ 0 };

        while (lineBegin <= source.size())
        {
            const std::size_t newline =
                source.find('\n', lineBegin);

            const bool hasNewline =
                newline != std::string_view::npos;

            const std::size_t lineEnd =
                hasNewline
                    ? newline
                    : source.size();

            std::string_view line =
                source.substr(
                    lineBegin,
                    lineEnd - lineBegin);

            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            const std::string_view trimmed =
                trimView(line);


            if (inCodeFence)
            {
                if (trimmed.starts_with(fenceMarker))
                {
                    document.blocks.push_back(
                        ResponseBlock{
                            .kind = ResponseBlockKind::CodeBlock,
                            .text = std::move(code),
                            .language = std::move(codeLanguage)
                        });

                    code.clear();
                    codeLanguage.clear();
                    fenceMarker.clear();
                    inCodeFence = false;
                }
                else
                {
                    code.append(line);

                    if (hasNewline)
                    {
                        code.push_back('\n');
                    }
                }

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            if (inDisplayMath)
            {
                if (endsDisplayMath(trimmed, displayMathDollarDelimited))
                {
                    std::string_view content = line;

                    if (displayMathDollarDelimited)
                    {
                        const std::size_t close = content.rfind("$$");
                        content = content.substr(0, close);
                    }
                    else
                    {
                        const std::size_t close = content.rfind("\\]");
                        content = content.substr(0, close);
                    }

                    if (!trimView(content).empty())
                    {
                        if (!displayMath.empty())
                        {
                            displayMath.push_back('\n');
                        }

                        displayMath.append(content);
                    }

                    document.blocks.push_back(
                        ResponseBlock{
                            .kind = ResponseBlockKind::DisplayMath,
                            .text = std::move(displayMath)
                        });

                    displayMath.clear();
                    inDisplayMath = false;
                }
                else
                {
                    if (!displayMath.empty())
                    {
                        displayMath.push_back('\n');
                    }

                    displayMath.append(line);
                }

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            std::string_view fence;
            std::string_view fenceInfo;

            if (isFence(line, fence, fenceInfo))
            {
                appendParagraph(document, paragraph);

                inCodeFence = true;
                fenceMarker = std::string{ fence };
                codeLanguage = std::string{ fenceInfo };

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            if (startsDisplayMath(trimmed))
            {
                appendParagraph(document, paragraph);

                displayMathDollarDelimited =
                    trimmed.starts_with("$$");

                const std::string_view opener =
                    displayMathDollarDelimited
                        ? std::string_view{ "$$" }
                        : std::string_view{ "\\[" };

                const std::string_view closer =
                    displayMathDollarDelimited
                        ? std::string_view{ "$$" }
                        : std::string_view{ "\\]" };

                std::string_view afterOpen =
                    trimmed.substr(opener.size());

                if (
                    afterOpen.size() >= closer.size()
                    && afterOpen.ends_with(closer))
                {
                    afterOpen.remove_suffix(closer.size());

                    document.blocks.push_back(
                        ResponseBlock{
                            .kind = ResponseBlockKind::DisplayMath,
                            .text = std::string{ trimView(afterOpen) }
                        });
                }
                else
                {
                    inDisplayMath = true;
                    displayMath = std::string{ afterOpen };
                }

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            if (trimmed.empty())
            {
                appendParagraph(document, paragraph);

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            if (looksLikeStandaloneLatex(trimmed))
            {
                appendParagraph(document, paragraph);

                document.blocks.push_back(
                    ResponseBlock{
                        .kind = ResponseBlockKind::DisplayMath,
                        .text = std::string{ trimmed }
                    });

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            if (isHorizontalRule(line))
            {
                appendParagraph(document, paragraph);

                document.blocks.push_back(
                    ResponseBlock{
                        .kind = ResponseBlockKind::Separator
                    });

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            const int level =
                headingLevel(trimmed);

            if (level > 0)
            {
                appendParagraph(document, paragraph);

                const std::string_view content =
                    trimView(
                        trimmed.substr(
                            static_cast<std::size_t>(level) + 1));

                document.blocks.push_back(
                    ResponseBlock{
                        .kind = ResponseBlockKind::Heading,
                        .spans = parseInline(content),
                        .headingLevel = level
                    });

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            const ListPrefix list =
                parseListPrefix(line);

            if (list.matched)
            {
                appendParagraph(document, paragraph);

                const std::string_view content =
                    trimView(
                        line.substr(list.contentOffset));

                document.blocks.push_back(
                    ResponseBlock{
                        .kind = ResponseBlockKind::ListItem,
                        .spans = parseInline(content),
                        .orderedIndex = list.orderedIndex
                    });

                if (!hasNewline)
                {
                    break;
                }

                lineBegin = newline + 1;
                continue;
            }


            // Blockquote presentation is currently indentation-free, but the marker
            // itself is not useful to the final reader.
            std::string_view normalLine = trimmed;

            if (normalLine.starts_with("> "))
            {
                normalLine.remove_prefix(2);
            }

            if (!paragraph.empty())
            {
                paragraph.push_back(' ');
            }

            paragraph.append(normalLine);


            if (!hasNewline)
            {
                break;
            }

            lineBegin = newline + 1;
        }


        appendParagraph(document, paragraph);


        // Preserve incomplete final fenced content rather than silently dropping it.
        if (inCodeFence)
        {
            document.blocks.push_back(
                ResponseBlock{
                    .kind = ResponseBlockKind::CodeBlock,
                    .text = std::move(code),
                    .language = std::move(codeLanguage)
                });
        }

        if (inDisplayMath)
        {
            document.blocks.push_back(
                ResponseBlock{
                    .kind = ResponseBlockKind::DisplayMath,
                    .text = std::move(displayMath)
                });
        }


        return document;
    }


    ResponseDocument makePlainResponseDocument(
        const std::string_view prefix,
        const std::string_view text)
    {
        ResponseDocument document;

        ResponseBlock block{
            .kind = ResponseBlockKind::Paragraph
        };

        appendSpan(
            block.spans,
            InlineStyle::Bold,
            prefix);

        appendSpan(
            block.spans,
            InlineStyle::Normal,
            text);

        document.blocks.push_back(
            std::move(block));

        return document;
    }


    void prependDocumentLabel(
        ResponseDocument& document,
        const std::string_view label)
    {
        if (label.empty())
        {
            return;
        }

        if (
            !document.blocks.empty()
            && document.blocks.front().kind == ResponseBlockKind::Paragraph)
        {
            document.blocks.front().spans.insert(
                document.blocks.front().spans.begin(),
                InlineSpan{
                    .style = InlineStyle::Bold,
                    .text = std::string{ label }
                });

            return;
        }

        ResponseBlock labelBlock{
            .kind = ResponseBlockKind::Paragraph
        };

        labelBlock.spans.push_back(
            InlineSpan{
                .style = InlineStyle::Bold,
                .text = std::string{ label }
            });

        document.blocks.insert(
            document.blocks.begin(),
            std::move(labelBlock));
    }

} // namespace rose::ui
