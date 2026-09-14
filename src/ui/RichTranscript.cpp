#include "ui/RichTranscript.h"

#include "ui/ResponseParser.h"
#include "ui/RichResponseRenderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rose::ui
{

    struct RichTranscript::Impl
    {
        struct Entry
        {
            ResponseDocument document;
            std::string copyText;
            RichResponseLayout layout;
            int layoutWidth{ 0 };
        };


        SDL_Renderer& renderer_;
        RichResponseRenderer responseRenderer_;

        std::vector<Entry> entries_;

        bool assistantStreaming_{ false };
        std::string streamingAssistantText_;
        RichResponseLayout streamingLayout_;
        int streamingLayoutWidth_{ 0 };
        bool streamingDirty_{ true };

        float scrollOffset_{ 0.0f };
        float maxScrollOffset_{ 0.0f };
        bool followLatest_{ true };


        explicit Impl(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            std::filesystem::path fontPath,
            std::filesystem::path microTexResourceRoot)
            : renderer_{ renderer }
            , responseRenderer_{
                renderer,
                textEngine,
                std::move(fontPath),
                std::move(microTexResourceRoot)
            }
        {
        }


        void appendDocument(
            ResponseDocument document,
            std::string copyText)
        {
            entries_.push_back(
                Entry{
                    .document = std::move(document),
                    .copyText = std::move(copyText),
                    .layout = {},
                    .layoutWidth = 0
                });
        }


        void appendUserMessage(
            const std::string_view text)
        {
            appendDocument(
                makePlainResponseDocument(
                    "You: ",
                    text),
                std::string{ "You: " }
                    + std::string{ text });

            // Submitting a new prompt means the user has intentionally returned to
            // the live edge of the conversation.
            followLatest_ = true;
        }


        void startAssistantResponse()
        {
            assistantStreaming_ = true;
            streamingAssistantText_.clear();
            streamingLayout_ = {};
            streamingLayoutWidth_ = 0;
            streamingDirty_ = true;
        }


        void appendAssistantText(
            const std::string_view text)
        {
            if (text.empty())
            {
                return;
            }

            if (!assistantStreaming_)
            {
                startAssistantResponse();
            }

            streamingAssistantText_.append(text);
            streamingDirty_ = true;
        }


        void finishAssistantResponse(
            const std::string_view authoritativeText)
        {
            std::string finalText;

            if (!authoritativeText.empty())
            {
                finalText.assign(authoritativeText);
            }
            else
            {
                finalText = streamingAssistantText_;
            }

            if (!finalText.empty() || assistantStreaming_)
            {
                ResponseDocument document =
                    parseResponseDocument(finalText);

                prependDocumentLabel(
                    document,
                    "Rose: ");

                appendDocument(
                    std::move(document),
                    std::string{ "Rose: " }
                        + finalText);
            }

            assistantStreaming_ = false;
            streamingAssistantText_.clear();
            streamingLayout_ = {};
            streamingLayoutWidth_ = 0;
            streamingDirty_ = true;
        }


        void appendError(
            const std::string_view text)
        {
            appendDocument(
                makePlainResponseDocument(
                    "Error: ",
                    text),
                std::string{ "Error: " }
                    + std::string{ text });

            assistantStreaming_ = false;
            streamingAssistantText_.clear();
            streamingLayout_ = {};
            streamingLayoutWidth_ = 0;
            streamingDirty_ = true;

            followLatest_ = true;
        }


        void clear()
        {
            entries_.clear();

            assistantStreaming_ = false;
            streamingAssistantText_.clear();
            streamingLayout_ = {};
            streamingLayoutWidth_ = 0;
            streamingDirty_ = true;

            scrollOffset_ = 0.0f;
            maxScrollOffset_ = 0.0f;
            followLatest_ = true;
        }


        [[nodiscard]]
        std::string copyableText() const
        {
            std::string result;

            bool haveContent{ false };

            for (const Entry& entry : entries_)
            {
                if (haveContent)
                {
                    result += "\n\n";
                }

                result += entry.copyText;
                haveContent = true;
            }

            if (assistantStreaming_)
            {
                if (haveContent)
                {
                    result += "\n\n";
                }

                result += "Rose: ";
                result += streamingAssistantText_;
            }

            return result;
        }


        void scrollBy(
            const float deltaPixels)
        {
            scrollOffset_ =
                std::clamp(
                    scrollOffset_ + deltaPixels,
                    0.0f,
                    maxScrollOffset_);

            constexpr float bottomTolerance{ 1.0f };

            followLatest_ =
                scrollOffset_
                >= maxScrollOffset_
                - bottomTolerance;
        }


        void ensureLayouts(
            const int width)
        {
            for (Entry& entry : entries_)
            {
                if (
                    !entry.layout
                    || entry.layoutWidth != width)
                {
                    entry.layout =
                        responseRenderer_.layout(
                            entry.document,
                            width);

                    entry.layoutWidth = width;
                }
            }


            if (assistantStreaming_)
            {
                if (
                    streamingDirty_
                    || !streamingLayout_
                    || streamingLayoutWidth_ != width)
                {
                    const ResponseDocument streamingDocument =
                        makePlainResponseDocument(
                            "Rose: ",
                            streamingAssistantText_);

                    streamingLayout_ =
                        responseRenderer_.layout(
                            streamingDocument,
                            width);

                    streamingLayoutWidth_ = width;
                    streamingDirty_ = false;
                }
            }
            else
            {
                streamingLayout_ = {};
                streamingLayoutWidth_ = 0;
                streamingDirty_ = false;
            }
        }


        [[nodiscard]]
        int contentHeight() const noexcept
        {
            constexpr int turnGap{ 12 };

            int height{ 0 };
            bool haveContent{ false };

            for (const Entry& entry : entries_)
            {
                if (haveContent)
                {
                    height += turnGap;
                }

                height += entry.layout.height();
                haveContent = true;
            }

            if (assistantStreaming_ && streamingLayout_)
            {
                if (haveContent)
                {
                    height += turnGap;
                }

                height += streamingLayout_.height();
            }

            return height;
        }


        void render(
            const int x,
            const int y,
            const int width,
            const int height)
        {
            if (width <= 0 || height <= 0)
            {
                return;
            }

            constexpr int scrollbarWidth{ 8 };
            constexpr int scrollbarGap{ 10 };
            constexpr int turnGap{ 12 };

            const int contentWidth =
                std::max(
                    1,
                    width
                    - scrollbarGap
                    - scrollbarWidth);

            ensureLayouts(contentWidth);

            const int totalHeight =
                contentHeight();

            maxScrollOffset_ =
                std::max(
                    0.0f,
                    static_cast<float>(
                        totalHeight - height));

            if (followLatest_)
            {
                scrollOffset_ =
                    maxScrollOffset_;
            }

            scrollOffset_ =
                std::clamp(
                    scrollOffset_,
                    0.0f,
                    maxScrollOffset_);


            const SDL_Rect clip{
                x,
                y,
                contentWidth,
                height
            };

            if (!SDL_SetRenderClipRect(
                &renderer_,
                &clip))
            {
                return;
            }


            float cursorY =
                static_cast<float>(y)
                - scrollOffset_;

            bool haveContent{ false };

            for (const Entry& entry : entries_)
            {
                if (haveContent)
                {
                    cursorY += static_cast<float>(turnGap);
                }

                const float entryBottom =
                    cursorY
                    + static_cast<float>(
                        entry.layout.height());

                if (
                    entryBottom >= static_cast<float>(y)
                    && cursorY <= static_cast<float>(y + height))
                {
                    responseRenderer_.draw(
                        entry.layout,
                        static_cast<float>(x),
                        cursorY);
                }

                cursorY = entryBottom;
                haveContent = true;
            }


            if (assistantStreaming_ && streamingLayout_)
            {
                if (haveContent)
                {
                    cursorY += static_cast<float>(turnGap);
                }

                const float streamBottom =
                    cursorY
                    + static_cast<float>(
                        streamingLayout_.height());

                if (
                    streamBottom >= static_cast<float>(y)
                    && cursorY <= static_cast<float>(y + height))
                {
                    responseRenderer_.draw(
                        streamingLayout_,
                        static_cast<float>(x),
                        cursorY);
                }
            }


            SDL_SetRenderClipRect(
                &renderer_,
                nullptr);


            if (maxScrollOffset_ <= 0.0f)
            {
                return;
            }


            const SDL_FRect scrollbarTrack{
                static_cast<float>(
                    x + contentWidth + scrollbarGap),
                static_cast<float>(y),
                static_cast<float>(scrollbarWidth),
                static_cast<float>(height)
            };

            SDL_SetRenderDrawColor(
                &renderer_,
                50,
                48,
                58,
                255);

            SDL_RenderFillRect(
                &renderer_,
                &scrollbarTrack);


            const float visibleFraction =
                std::clamp(
                    static_cast<float>(height)
                    / static_cast<float>(
                        std::max(totalHeight, 1)),
                    0.0f,
                    1.0f);

            constexpr float minimumThumbHeight{ 28.0f };

            const float thumbHeight =
                std::max(
                    minimumThumbHeight,
                    scrollbarTrack.h
                    * visibleFraction);

            const float thumbTravel =
                std::max(
                    0.0f,
                    scrollbarTrack.h
                    - thumbHeight);

            const float scrollFraction =
                maxScrollOffset_ > 0.0f
                    ? scrollOffset_ / maxScrollOffset_
                    : 0.0f;

            const SDL_FRect scrollbarThumb{
                scrollbarTrack.x,
                scrollbarTrack.y
                    + thumbTravel
                    * scrollFraction,
                scrollbarTrack.w,
                thumbHeight
            };

            SDL_SetRenderDrawColor(
                &renderer_,
                135,
                130,
                150,
                255);

            SDL_RenderFillRect(
                &renderer_,
                &scrollbarThumb);
        }
    };


    RichTranscript::RichTranscript(
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


    RichTranscript::~RichTranscript() = default;


    void RichTranscript::appendUserMessage(
        const std::string_view text)
    {
        impl_->appendUserMessage(text);
    }


    void RichTranscript::startAssistantResponse()
    {
        impl_->startAssistantResponse();
    }


    void RichTranscript::appendAssistantText(
        const std::string_view text)
    {
        impl_->appendAssistantText(text);
    }


    void RichTranscript::finishAssistantResponse(
        const std::string_view authoritativeText)
    {
        impl_->finishAssistantResponse(
            authoritativeText);
    }


    void RichTranscript::appendError(
        const std::string_view text)
    {
        impl_->appendError(text);
    }


    void RichTranscript::clear()
    {
        impl_->clear();
    }


    std::string RichTranscript::copyableText() const
    {
        return impl_->copyableText();
    }


    void RichTranscript::scrollBy(
        const float deltaPixels)
    {
        impl_->scrollBy(deltaPixels);
    }


    void RichTranscript::render(
        const int x,
        const int y,
        const int width,
        const int height)
    {
        impl_->render(
            x,
            y,
            width,
            height);
    }

} // namespace rose::ui
