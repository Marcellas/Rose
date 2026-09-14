#include "ui/RichResponseRenderer.h"

#include "ui/MathRenderer.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rose::ui
{

    namespace
    {
        struct FontDeleter
        {
            void operator()(
                TTF_Font* font) const noexcept
            {
                if (font != nullptr)
                {
                    TTF_CloseFont(font);
                }
            }
        };


        struct TextDeleter
        {
            void operator()(
                TTF_Text* text) const noexcept
            {
                if (text != nullptr)
                {
                    TTF_DestroyText(text);
                }
            }
        };


        using FontPtr =
            std::unique_ptr<TTF_Font, FontDeleter>;

        using TextPtr =
            std::unique_ptr<TTF_Text, TextDeleter>;


        [[nodiscard]]
        bool sameColor(
            const SDL_Color& a,
            const SDL_Color& b) noexcept
        {
            return
                a.r == b.r
                && a.g == b.g
                && a.b == b.b
                && a.a == b.a;
        }


        [[nodiscard]]
        bool isWhitespaceOnly(
            const std::string_view text) noexcept
        {
            return std::all_of(
                text.begin(),
                text.end(),
                [](const char value)
                {
                    return std::isspace(
                        static_cast<unsigned char>(value)) != 0;
                });
        }


        [[nodiscard]]
        std::size_t nextUtf8CodePointLength(
            const std::string_view text) noexcept
        {
            if (text.empty())
            {
                return 0;
            }

            const unsigned char lead =
                static_cast<unsigned char>(text.front());

            if ((lead & 0x80u) == 0u)
            {
                return 1;
            }

            if ((lead & 0xE0u) == 0xC0u)
            {
                return std::min<std::size_t>(2, text.size());
            }

            if ((lead & 0xF0u) == 0xE0u)
            {
                return std::min<std::size_t>(3, text.size());
            }

            if ((lead & 0xF8u) == 0xF0u)
            {
                return std::min<std::size_t>(4, text.size());
            }

            return 1;
        }


        [[nodiscard]]
        std::vector<std::string_view> tokenizeForWrapping(
            const std::string_view text)
        {
            std::vector<std::string_view> tokens;

            std::size_t begin{ 0 };

            while (begin < text.size())
            {
                if (text[begin] == '\n')
                {
                    tokens.emplace_back(
                        text.data() + begin,
                        std::size_t{ 1 });

                    ++begin;
                    continue;
                }

                const bool whitespace =
                    text[begin] == ' '
                    || text[begin] == '\t';

                std::size_t end = begin + 1;

                while (
                    end < text.size()
                    && text[end] != '\n'
                    && (
                        (text[end] == ' ' || text[end] == '\t')
                        == whitespace))
                {
                    ++end;
                }

                tokens.push_back(
                    text.substr(
                        begin,
                        end - begin));

                begin = end;
            }

            return tokens;
        }


        [[nodiscard]]
        FontPtr openStyledFont(
            const std::filesystem::path& path,
            const float size,
            const TTF_FontStyleFlags style)
        {
            const std::string pathString =
                path.string();

            FontPtr font{
                TTF_OpenFont(
                    pathString.c_str(),
                    size)
            };

            if (!font)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not open Rose rich-text font '"
                    }
                    + pathString
                    + "': "
                    + SDL_GetError()
                };
            }

            TTF_SetFontStyle(
                font.get(),
                style);

            return font;
        }

    } // namespace


    // =========================================================================
    // RichResponseLayout
    // =========================================================================

    struct RichResponseLayout::Impl
    {
        enum class CommandKind
        {
            Rectangle,
            Text,
            Math
        };


        struct Command
        {
            CommandKind kind{ CommandKind::Text };

            SDL_FRect rect{};
            SDL_Color color{};

            TextPtr text;
            MathTexture math;
        };


        std::vector<Command> commands;

        int height{ 0 };
    };


    RichResponseLayout::RichResponseLayout() noexcept = default;

    RichResponseLayout::~RichResponseLayout() = default;

    RichResponseLayout::RichResponseLayout(
        RichResponseLayout&&) noexcept = default;

    RichResponseLayout& RichResponseLayout::operator=(
        RichResponseLayout&&) noexcept = default;


    RichResponseLayout::RichResponseLayout(
        std::unique_ptr<Impl> impl) noexcept
        : impl_{
            std::move(impl)
        }
    {
    }


    int RichResponseLayout::height() const noexcept
    {
        return impl_ ? impl_->height : 0;
    }


    RichResponseLayout::operator bool() const noexcept
    {
        return impl_ != nullptr;
    }


    // =========================================================================
    // RichResponseRenderer
    // =========================================================================

    struct RichResponseRenderer::Impl
    {
        SDL_Renderer& renderer_;
        TTF_TextEngine& textEngine_;

        FontPtr regularFont_;
        FontPtr boldFont_;
        FontPtr italicFont_;
        FontPtr boldItalicFont_;
        FontPtr codeFont_;

        FontPtr heading1Font_;
        FontPtr heading2Font_;
        FontPtr heading3Font_;

        MathRenderer mathRenderer_;


        static constexpr SDL_Color bodyColor{
            235,
            235,
            240,
            255
        };

        static constexpr SDL_Color codeColor{
            232,
            230,
            238,
            255
        };

        static constexpr SDL_Color mutedColor{
            165,
            160,
            178,
            255
        };

        static constexpr SDL_Color codeBackground{
            38,
            35,
            47,
            255
        };

        static constexpr SDL_Color inlineCodeColor{
            226,
            218,
            236,
            255
        };

        static constexpr SDL_Color separatorColor{
            75,
            71,
            88,
            255
        };


        explicit Impl(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            std::filesystem::path fontPath,
            std::filesystem::path microTexResourceRoot)
            : renderer_{ renderer }
            , textEngine_{ textEngine }
            , mathRenderer_{
                renderer,
                std::move(microTexResourceRoot)
            }
        {
            const std::filesystem::path absoluteFontPath =
                std::filesystem::absolute(
                    std::move(fontPath));

            if (!std::filesystem::exists(absoluteFontPath))
            {
                throw std::runtime_error{
                    std::string{
                        "Rose rich-text font does not exist: "
                    }
                    + absoluteFontPath.string()
                };
            }

            regularFont_ =
                openStyledFont(
                    absoluteFontPath,
                    18.0f,
                    TTF_STYLE_NORMAL);

            boldFont_ =
                openStyledFont(
                    absoluteFontPath,
                    18.0f,
                    TTF_STYLE_BOLD);

            italicFont_ =
                openStyledFont(
                    absoluteFontPath,
                    18.0f,
                    TTF_STYLE_ITALIC);

            boldItalicFont_ =
                openStyledFont(
                    absoluteFontPath,
                    18.0f,
                    static_cast<TTF_FontStyleFlags>(
                        TTF_STYLE_BOLD
                        | TTF_STYLE_ITALIC));

            // Rose currently ships one UI font. Code is still rendered through a
            // separate font object so a bundled monospace face can be substituted
            // later without changing document/layout ownership.
            codeFont_ =
                openStyledFont(
                    absoluteFontPath,
                    17.0f,
                    TTF_STYLE_NORMAL);

            heading1Font_ =
                openStyledFont(
                    absoluteFontPath,
                    28.0f,
                    TTF_STYLE_BOLD);

            heading2Font_ =
                openStyledFont(
                    absoluteFontPath,
                    24.0f,
                    TTF_STYLE_BOLD);

            heading3Font_ =
                openStyledFont(
                    absoluteFontPath,
                    21.0f,
                    TTF_STYLE_BOLD);
        }


        [[nodiscard]]
        TTF_Font* headingFont(
            const int level) const noexcept
        {
            if (level <= 1)
            {
                return heading1Font_.get();
            }

            if (level == 2)
            {
                return heading2Font_.get();
            }

            return heading3Font_.get();
        }


        [[nodiscard]]
        TTF_Font* fontForStyle(
            const InlineStyle style) const noexcept
        {
            switch (style)
            {
            case InlineStyle::Bold:
                return boldFont_.get();

            case InlineStyle::Italic:
                return italicFont_.get();

            case InlineStyle::BoldItalic:
                return boldItalicFont_.get();

            case InlineStyle::Code:
                return codeFont_.get();

            case InlineStyle::Normal:
            case InlineStyle::Math:
            default:
                return regularFont_.get();
            }
        }


        [[nodiscard]]
        SDL_Color colorForStyle(
            const InlineStyle style) const noexcept
        {
            return style == InlineStyle::Code
                ? inlineCodeColor
                : bodyColor;
        }


        [[nodiscard]]
        std::pair<int, int> measureText(
            TTF_Font* font,
            const std::string_view text) const
        {
            if (text.empty())
            {
                return {
                    0,
                    std::max(
                        1,
                        TTF_GetFontLineSkip(font))
                };
            }

            int width{ 0 };
            int height{ 0 };

            if (!TTF_GetStringSize(
                font,
                text.data(),
                text.size(),
                &width,
                &height))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not measure Rose rich text: "
                    }
                    + SDL_GetError()
                };
            }

            return {
                width,
                std::max(
                    height,
                    TTF_GetFontLineSkip(font))
            };
        }


        [[nodiscard]]
        TextPtr createText(
            TTF_Font* font,
            const std::string_view text,
            const SDL_Color color) const
        {
            TextPtr result{
                TTF_CreateText(
                    &textEngine_,
                    font,
                    text.data(),
                    text.size())
            };

            if (!result)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not create Rose rich text object: "
                    }
                    + SDL_GetError()
                };
            }

            if (!TTF_SetTextColor(
                result.get(),
                color.r,
                color.g,
                color.b,
                color.a))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not set Rose rich text color: "
                    }
                    + SDL_GetError()
                };
            }

            return result;
        }


        [[nodiscard]]
        TextPtr createWrappedText(
            TTF_Font* font,
            const std::string_view text,
            const int width,
            const SDL_Color color,
            int& measuredWidth,
            int& measuredHeight) const
        {
            TextPtr result =
                createText(
                    font,
                    text,
                    color);

            if (!TTF_SetTextWrapWidth(
                result.get(),
                width))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not set Rose rich text wrapping: "
                    }
                    + SDL_GetError()
                };
            }

            if (!TTF_GetTextSize(
                result.get(),
                &measuredWidth,
                &measuredHeight))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not measure wrapped Rose rich text: "
                    }
                    + SDL_GetError()
                };
            }

            return result;
        }


        struct PendingItem
        {
            enum class Kind
            {
                Text,
                Math
            };

            Kind kind{ Kind::Text };

            TTF_Font* font{ nullptr };
            SDL_Color color{};
            std::string text;

            MathTexture math;

            int width{ 0 };
            int height{ 0 };
        };


        void addTextPending(
            std::vector<PendingItem>& line,
            TTF_Font* font,
            const SDL_Color color,
            std::string text,
            const int width,
            const int height) const
        {
            if (text.empty())
            {
                return;
            }

            if (
                !line.empty()
                && line.back().kind == PendingItem::Kind::Text
                && line.back().font == font
                && sameColor(line.back().color, color))
            {
                line.back().text += text;
                line.back().width += width;
                line.back().height =
                    std::max(
                        line.back().height,
                        height);

                return;
            }

            PendingItem item;
            item.kind = PendingItem::Kind::Text;
            item.font = font;
            item.color = color;
            item.text = std::move(text);
            item.width = width;
            item.height = height;

            line.push_back(
                std::move(item));
        }


        void flushLine(
            RichResponseLayout::Impl& layout,
            std::vector<PendingItem>& line,
            const float x,
            float& y,
            const int baseLineHeight,
            const int lineGap) const
        {
            if (line.empty())
            {
                y += static_cast<float>(
                    baseLineHeight + lineGap);
                return;
            }

            int lineHeight = baseLineHeight;

            for (const PendingItem& item : line)
            {
                lineHeight =
                    std::max(
                        lineHeight,
                        item.height);
            }

            float cursorX = x;

            for (PendingItem& item : line)
            {
                RichResponseLayout::Impl::Command command;

                command.rect = SDL_FRect{
                    cursorX,
                    y + static_cast<float>(
                        lineHeight - item.height)
                        * 0.5f,
                    static_cast<float>(item.width),
                    static_cast<float>(item.height)
                };

                if (item.kind == PendingItem::Kind::Math)
                {
                    command.kind =
                        RichResponseLayout::Impl::CommandKind::Math;

                    command.math =
                        std::move(item.math);
                }
                else
                {
                    command.kind =
                        RichResponseLayout::Impl::CommandKind::Text;

                    command.color = item.color;

                    command.text =
                        createText(
                            item.font,
                            item.text,
                            item.color);
                }

                layout.commands.push_back(
                    std::move(command));

                cursorX += static_cast<float>(item.width);
            }

            y += static_cast<float>(
                lineHeight + lineGap);

            line.clear();
        }


        void layoutInlineSpans(
            RichResponseLayout::Impl& layout,
            const std::vector<InlineSpan>& spans,
            const float x,
            const int width,
            float& y,
            TTF_Font* forcedFont = nullptr,
            const int lineGap = 4) const
        {
            if (width <= 0)
            {
                return;
            }

            const int baseLineHeight =
                std::max(
                    1,
                    TTF_GetFontLineSkip(
                        forcedFont != nullptr
                            ? forcedFont
                            : regularFont_.get()));

            std::vector<PendingItem> line;
            int lineWidth{ 0 };


            const auto flush =
                [&]()
                {
                    flushLine(
                        layout,
                        line,
                        x,
                        y,
                        baseLineHeight,
                        lineGap);

                    lineWidth = 0;
                };


            const auto addTextPiece =
                [&](TTF_Font* font,
                    const SDL_Color color,
                    std::string_view piece)
                {
                    while (!piece.empty())
                    {
                        const auto [fullWidth, fullHeight] =
                            measureText(
                                font,
                                piece);

                        const int remainingWidth =
                            width - lineWidth;

                        if (
                            fullWidth <= remainingWidth
                            || line.empty())
                        {
                            if (
                                fullWidth <= width
                                || piece.size() <= 1)
                            {
                                addTextPending(
                                    line,
                                    font,
                                    color,
                                    std::string{ piece },
                                    fullWidth,
                                    fullHeight);

                                lineWidth += fullWidth;
                                return;
                            }
                        }


                        if (!line.empty())
                        {
                            flush();
                            continue;
                        }


                        int measuredWidth{ 0 };
                        std::size_t measuredLength{ 0 };

                        if (!TTF_MeasureString(
                            font,
                            piece.data(),
                            piece.size(),
                            width,
                            &measuredWidth,
                            &measuredLength))
                        {
                            throw std::runtime_error{
                                std::string{
                                    "Could not wrap Rose rich text token: "
                                }
                                + SDL_GetError()
                            };
                        }

                        if (measuredLength == 0)
                        {
                            measuredLength =
                                nextUtf8CodePointLength(piece);

                            const auto [forcedWidth, forcedHeight] =
                                measureText(
                                    font,
                                    piece.substr(0, measuredLength));

                            measuredWidth = forcedWidth;

                            addTextPending(
                                line,
                                font,
                                color,
                                std::string{
                                    piece.substr(0, measuredLength)
                                },
                                forcedWidth,
                                forcedHeight);
                        }
                        else
                        {
                            const std::string_view fitted =
                                piece.substr(
                                    0,
                                    measuredLength);

                            const auto [fittedWidth, fittedHeight] =
                                measureText(
                                    font,
                                    fitted);

                            addTextPending(
                                line,
                                font,
                                color,
                                std::string{ fitted },
                                fittedWidth,
                                fittedHeight);

                            measuredWidth = fittedWidth;
                        }

                        lineWidth = measuredWidth;
                        flush();

                        piece.remove_prefix(measuredLength);
                    }
                };


            for (const InlineSpan& span : spans)
            {
                if (span.style == InlineStyle::Math)
                {
                    if (span.text.empty())
                    {
                        continue;
                    }

                    try
                    {
                        MathTexture math =
                            mathRenderer_.renderInlineMath(
                                span.text,
                                width,
                                18.0f);

                        if (math)
                        {
                            if (
                                !line.empty()
                                && lineWidth + math.width() > width)
                            {
                                flush();
                            }

                            PendingItem item;
                            item.kind = PendingItem::Kind::Math;
                            item.width = math.width();
                            item.height = math.height();
                            item.math = std::move(math);

                            lineWidth += item.width;

                            line.push_back(
                                std::move(item));

                            continue;
                        }
                    }
                    catch (...)
                    {
                        // Model-authored math is untrusted presentation input. If a
                        // formula is malformed, preserve the source as visible text
                        // rather than failing the entire chat window.
                    }

                    const std::string fallback =
                        std::string{ "$" }
                        + span.text
                        + "$";

                    addTextPiece(
                        regularFont_.get(),
                        bodyColor,
                        fallback);

                    continue;
                }


                TTF_Font* font =
                    forcedFont != nullptr
                        ? forcedFont
                        : fontForStyle(span.style);

                const SDL_Color color =
                    forcedFont != nullptr
                        ? bodyColor
                        : colorForStyle(span.style);

                for (
                    const std::string_view token :
                    tokenizeForWrapping(span.text))
                {
                    if (token == "\n")
                    {
                        flush();
                        continue;
                    }

                    const bool whitespaceOnly =
                        isWhitespaceOnly(token);

                    if (line.empty() && whitespaceOnly)
                    {
                        continue;
                    }

                    const auto [tokenWidth, tokenHeight] =
                        measureText(
                            font,
                            token);

                    if (
                        !line.empty()
                        && lineWidth + tokenWidth > width)
                    {
                        flush();

                        if (whitespaceOnly)
                        {
                            continue;
                        }
                    }

                    addTextPiece(
                        font,
                        color,
                        token);
                }
            }


            if (!line.empty())
            {
                flush();
            }
        }


        void layoutParagraph(
            RichResponseLayout::Impl& layout,
            const ResponseBlock& block,
            const int width,
            float& y) const
        {
            layoutInlineSpans(
                layout,
                block.spans,
                0.0f,
                width,
                y);

            y += 8.0f;
        }


        void layoutHeading(
            RichResponseLayout::Impl& layout,
            const ResponseBlock& block,
            const int width,
            float& y) const
        {
            y += block.headingLevel <= 2
                ? 8.0f
                : 4.0f;

            layoutInlineSpans(
                layout,
                block.spans,
                0.0f,
                width,
                y,
                headingFont(block.headingLevel),
                3);

            y += 7.0f;
        }


        void layoutListItem(
            RichResponseLayout::Impl& layout,
            const ResponseBlock& block,
            const int width,
            float& y) const
        {
            constexpr int contentIndent{ 28 };

            std::string marker =
                block.orderedIndex > 0
                    ? std::to_string(block.orderedIndex) + "."
                    : "•";

            const auto [markerWidth, markerHeight] =
                measureText(
                    boldFont_.get(),
                    marker);

            RichResponseLayout::Impl::Command markerCommand;
            markerCommand.kind =
                RichResponseLayout::Impl::CommandKind::Text;

            markerCommand.rect = SDL_FRect{
                0.0f,
                y,
                static_cast<float>(markerWidth),
                static_cast<float>(markerHeight)
            };

            markerCommand.color = bodyColor;

            markerCommand.text =
                createText(
                    boldFont_.get(),
                    marker,
                    bodyColor);

            layout.commands.push_back(
                std::move(markerCommand));

            const float contentYBefore = y;

            layoutInlineSpans(
                layout,
                block.spans,
                static_cast<float>(contentIndent),
                std::max(
                    1,
                    width - contentIndent),
                y,
                nullptr,
                3);

            if (y <= contentYBefore)
            {
                y = contentYBefore
                    + static_cast<float>(
                        std::max(
                            markerHeight,
                            TTF_GetFontLineSkip(
                                regularFont_.get())));
            }

            y += 2.0f;
        }


        void layoutSeparator(
            RichResponseLayout::Impl& layout,
            const int width,
            float& y) const
        {
            y += 6.0f;

            RichResponseLayout::Impl::Command command;
            command.kind =
                RichResponseLayout::Impl::CommandKind::Rectangle;

            command.rect = SDL_FRect{
                0.0f,
                y,
                static_cast<float>(width),
                1.0f
            };

            command.color = separatorColor;

            layout.commands.push_back(
                std::move(command));

            y += 13.0f;
        }


        void layoutCodeBlock(
            RichResponseLayout::Impl& layout,
            const ResponseBlock& block,
            const int width,
            float& y) const
        {
            constexpr int horizontalPadding{ 14 };
            constexpr int verticalPadding{ 12 };
            constexpr int languageGap{ 7 };

            const int innerWidth =
                std::max(
                    1,
                    width - horizontalPadding * 2);

            const std::size_t backgroundIndex =
                layout.commands.size();

            RichResponseLayout::Impl::Command background;
            background.kind =
                RichResponseLayout::Impl::CommandKind::Rectangle;
            background.color = codeBackground;
            background.rect = SDL_FRect{
                0.0f,
                y,
                static_cast<float>(width),
                1.0f
            };

            layout.commands.push_back(
                std::move(background));

            const float blockTop = y;
            y += static_cast<float>(verticalPadding);


            if (!block.language.empty())
            {
                const auto [languageWidth, languageHeight] =
                    measureText(
                        codeFont_.get(),
                        block.language);

                RichResponseLayout::Impl::Command language;
                language.kind =
                    RichResponseLayout::Impl::CommandKind::Text;

                language.rect = SDL_FRect{
                    static_cast<float>(horizontalPadding),
                    y,
                    static_cast<float>(languageWidth),
                    static_cast<float>(languageHeight)
                };

                language.color = mutedColor;

                language.text =
                    createText(
                        codeFont_.get(),
                        block.language,
                        mutedColor);

                layout.commands.push_back(
                    std::move(language));

                y += static_cast<float>(
                    languageHeight + languageGap);
            }


            std::string codeText = block.text;

            // SDL_ttf supports tabs, but fixed expansion gives code blocks stable
            // indentation independent of platform/tab-stop policy.
            std::size_t tab = 0;

            while ((tab = codeText.find('\t', tab)) != std::string::npos)
            {
                codeText.replace(tab, 1, "    ");
                tab += 4;
            }

            if (codeText.empty())
            {
                codeText = " ";
            }

            int codeWidth{ 0 };
            int codeHeight{ 0 };

            TextPtr code =
                createWrappedText(
                    codeFont_.get(),
                    codeText,
                    innerWidth,
                    codeColor,
                    codeWidth,
                    codeHeight);

            RichResponseLayout::Impl::Command codeCommand;
            codeCommand.kind =
                RichResponseLayout::Impl::CommandKind::Text;

            codeCommand.rect = SDL_FRect{
                static_cast<float>(horizontalPadding),
                y,
                static_cast<float>(codeWidth),
                static_cast<float>(codeHeight)
            };

            codeCommand.color = codeColor;
            codeCommand.text = std::move(code);

            layout.commands.push_back(
                std::move(codeCommand));

            y += static_cast<float>(
                codeHeight + verticalPadding);

            layout.commands[backgroundIndex].rect.h =
                y - blockTop;

            y += 10.0f;
        }


        void layoutDisplayMath(
            RichResponseLayout::Impl& layout,
            const ResponseBlock& block,
            const int width,
            float& y) const
        {
            y += 4.0f;

            try
            {
                MathTexture math =
                    mathRenderer_.renderDisplayMath(
                        block.text,
                        width,
                        26.0f);

                if (math)
                {
                    RichResponseLayout::Impl::Command command;
                    command.kind =
                        RichResponseLayout::Impl::CommandKind::Math;

                    command.rect = SDL_FRect{
                        0.0f,
                        y,
                        static_cast<float>(math.width()),
                        static_cast<float>(math.height())
                    };

                    command.math =
                        std::move(math);

                    y += command.rect.h + 10.0f;

                    layout.commands.push_back(
                        std::move(command));

                    return;
                }
            }
            catch (...)
            {
                // Fall through to a literal source rendering below.
            }


            ResponseBlock fallback;
            fallback.kind = ResponseBlockKind::Paragraph;
            fallback.spans.push_back(
                InlineSpan{
                    .style = InlineStyle::Code,
                    .text = block.text
                });

            layoutParagraph(
                layout,
                fallback,
                width,
                y);
        }


        [[nodiscard]]
        RichResponseLayout layoutDocument(
            const ResponseDocument& document,
            const int width) const
        {
            if (width <= 0)
            {
                throw std::invalid_argument{
                    "RichResponseRenderer layout width must be greater than zero."
                };
            }

            auto result =
                std::make_unique<RichResponseLayout::Impl>();

            float y{ 0.0f };

            for (const ResponseBlock& block : document.blocks)
            {
                switch (block.kind)
                {
                case ResponseBlockKind::Paragraph:
                    layoutParagraph(
                        *result,
                        block,
                        width,
                        y);
                    break;

                case ResponseBlockKind::Heading:
                    layoutHeading(
                        *result,
                        block,
                        width,
                        y);
                    break;

                case ResponseBlockKind::CodeBlock:
                    layoutCodeBlock(
                        *result,
                        block,
                        width,
                        y);
                    break;

                case ResponseBlockKind::DisplayMath:
                    layoutDisplayMath(
                        *result,
                        block,
                        width,
                        y);
                    break;

                case ResponseBlockKind::ListItem:
                    layoutListItem(
                        *result,
                        block,
                        width,
                        y);
                    break;

                case ResponseBlockKind::Separator:
                    layoutSeparator(
                        *result,
                        width,
                        y);
                    break;
                }
            }

            result->height =
                std::max(
                    0,
                    static_cast<int>(y + 0.5f));

            return RichResponseLayout{
                std::move(result)
            };
        }


        void drawLayout(
            const RichResponseLayout& layout,
            const float x,
            const float y) const
        {
            if (!layout.impl_)
            {
                return;
            }

            for (
                const RichResponseLayout::Impl::Command& command :
                layout.impl_->commands)
            {
                const SDL_FRect destination{
                    x + command.rect.x,
                    y + command.rect.y,
                    command.rect.w,
                    command.rect.h
                };

                switch (command.kind)
                {
                case RichResponseLayout::Impl::CommandKind::Rectangle:
                    SDL_SetRenderDrawColor(
                        &renderer_,
                        command.color.r,
                        command.color.g,
                        command.color.b,
                        command.color.a);

                    SDL_RenderFillRect(
                        &renderer_,
                        &destination);
                    break;

                case RichResponseLayout::Impl::CommandKind::Text:
                    if (command.text)
                    {
                        if (!TTF_DrawRendererText(
                            command.text.get(),
                            destination.x,
                            destination.y))
                        {
                            throw std::runtime_error{
                                std::string{
                                    "Could not draw Rose rich text: "
                                }
                                + SDL_GetError()
                            };
                        }
                    }
                    break;

                case RichResponseLayout::Impl::CommandKind::Math:
                    if (command.math)
                    {
                        if (!SDL_RenderTexture(
                            &renderer_,
                            command.math.texture(),
                            nullptr,
                            &destination))
                        {
                            throw std::runtime_error{
                                std::string{
                                    "Could not draw Rose LaTeX texture: "
                                }
                                + SDL_GetError()
                            };
                        }
                    }
                    break;
                }
            }
        }
    };


    RichResponseRenderer::RichResponseRenderer(
        SDL_Renderer& renderer,
        TTF_TextEngine& textEngine,
        std::filesystem::path fontPath,
        std::filesystem::path microTexResourceRoot)
        : impl_{
            std::make_unique<Impl>(
                renderer,
                textEngine,
                std::move(fontPath),
                std::move(microTexResourceRoot))
        }
    {
    }


    RichResponseRenderer::~RichResponseRenderer() = default;


    RichResponseLayout RichResponseRenderer::layout(
        const ResponseDocument& document,
        const int width) const
    {
        return impl_->layoutDocument(
            document,
            width);
    }


    void RichResponseRenderer::draw(
        const RichResponseLayout& layout,
        const float x,
        const float y) const
    {
        impl_->drawLayout(
            layout,
            x,
            y);
    }

} // namespace rose::ui
