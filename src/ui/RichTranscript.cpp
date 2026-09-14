#include "ui/RichTranscript.h"

#include "ui/ResponseParser.h"
#include "ui/RichResponseRenderer.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Shellapi.h>
#endif

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rose::ui
{

    namespace
    {
        [[nodiscard]]
        bool pointInside(
            const SDL_FRect& rect,
            const float x,
            const float y) noexcept
        {
            return
                x >= rect.x
                && y >= rect.y
                && x <= rect.x + rect.w
                && y <= rect.y + rect.h;
        }


#ifdef _WIN32
        void openArtifact(
            const std::filesystem::path& path)
        {
            ShellExecuteW(
                nullptr,
                L"open",
                path.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
        }


        void revealArtifact(
            const std::filesystem::path& path)
        {
            std::wstring arguments{
                L"/select,\""
            };

            arguments += path.wstring();
            arguments += L"\"";

            ShellExecuteW(
                nullptr,
                L"open",
                L"explorer.exe",
                arguments.c_str(),
                nullptr,
                SW_SHOWNORMAL);
        }
#else
        void openArtifact(
            const std::filesystem::path&)
        {
        }

        void revealArtifact(
            const std::filesystem::path&)
        {
        }
#endif
    }


    struct RichTranscript::Impl
    {
        struct TextureDeleter
        {
            void operator()(
                SDL_Texture* texture) const noexcept
            {
                if (texture != nullptr)
                {
                    SDL_DestroyTexture(texture);
                }
            }
        };

        using TexturePtr =
            std::unique_ptr<SDL_Texture, TextureDeleter>;


        struct ArtifactVisual
        {
            artifacts::Artifact artifact;
            TexturePtr texture;
            float sourceWidth{ 0.0f };
            float sourceHeight{ 0.0f };
            SDL_FRect lastPreviewRect{};
            bool hasClickablePreview{ false };
        };


        struct Entry
        {
            ResponseDocument document;
            std::string copyText;
            RichResponseLayout layout;
            int layoutWidth{ 0 };
            int renderHeight{ 0 };
            std::optional<ArtifactVisual> artifact;
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

        SDL_FRect lastViewport_{};


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
                    .layoutWidth = 0,
                    .renderHeight = 0,
                    .artifact = std::nullopt
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


        void appendArtifact(
            artifacts::Artifact artifact)
        {
            std::string description =
                "Generated image: "
                + artifact.displayName
                + "\n"
                + artifact.path.string()
                + "\nLeft-click the preview to open it; right-click to reveal it in Explorer.";

            Entry entry;
            entry.document =
                makePlainResponseDocument(
                    "Rose: ",
                    description);
            entry.copyText =
                std::string{ "Rose artifact: " }
                + artifact.path.string();

            ArtifactVisual visual;
            visual.artifact =
                std::move(artifact);

            if (
                visual.artifact.kind
                == artifacts::ArtifactKind::Image)
            {
                const std::string pathText =
                    visual.artifact.path.string();

                SDL_Texture* rawTexture =
                    IMG_LoadTexture(
                        &renderer_,
                        pathText.c_str());

                if (rawTexture != nullptr)
                {
                    visual.texture.reset(rawTexture);

                    SDL_GetTextureSize(
                        rawTexture,
                        &visual.sourceWidth,
                        &visual.sourceHeight);
                }
            }

            entry.artifact =
                std::move(visual);

            entries_.push_back(
                std::move(entry));

            followLatest_ = true;
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
            lastViewport_ = {};
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


        [[nodiscard]]
        int artifactPreviewHeight(
            const ArtifactVisual& artifact,
            const int width) const noexcept
        {
            if (
                !artifact.texture
                || artifact.sourceWidth <= 0.0f
                || artifact.sourceHeight <= 0.0f)
            {
                return 0;
            }

            const float targetWidth =
                std::min(
                    static_cast<float>(width),
                    artifact.sourceWidth);

            const float scale =
                targetWidth
                / artifact.sourceWidth;

            constexpr float maximumPreviewHeight{ 360.0f };

            return static_cast<int>(
                std::min(
                    maximumPreviewHeight,
                    artifact.sourceHeight * scale));
        }


        void ensureLayouts(
            const int width)
        {
            constexpr int artifactGap{ 10 };
            constexpr int cardPadding{ 10 };

            for (Entry& entry : entries_)
            {
                if (
                    !entry.layout
                    || entry.layoutWidth != width)
                {
                    const int documentWidth =
                        entry.artifact.has_value()
                            ? std::max(
                                1,
                                width - cardPadding * 2)
                            : width;

                    entry.layout =
                        responseRenderer_.layout(
                            entry.document,
                            documentWidth);

                    entry.layoutWidth = width;

                    if (entry.artifact.has_value())
                    {
                        const int previewHeight =
                            artifactPreviewHeight(
                                *entry.artifact,
                                documentWidth);

                        entry.renderHeight =
                            cardPadding
                            + entry.layout.height()
                            + (
                                previewHeight > 0
                                    ? artifactGap
                                        + previewHeight
                                    : 0)
                            + cardPadding;
                    }
                    else
                    {
                        entry.renderHeight =
                            entry.layout.height();
                    }
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

                height += entry.renderHeight;
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


        [[nodiscard]]
        bool handlePointerDown(
            const float x,
            const float y,
            const bool revealFolder)
        {
            if (!pointInside(lastViewport_, x, y))
            {
                return false;
            }

            for (Entry& entry : entries_)
            {
                if (
                    !entry.artifact.has_value()
                    || !entry.artifact->hasClickablePreview)
                {
                    continue;
                }

                if (
                    pointInside(
                        entry.artifact->lastPreviewRect,
                        x,
                        y))
                {
                    if (revealFolder)
                    {
                        revealArtifact(
                            entry.artifact->artifact.path);
                    }
                    else
                    {
                        openArtifact(
                            entry.artifact->artifact.path);
                    }

                    return true;
                }
            }

            return false;
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
            constexpr int cardPadding{ 10 };
            constexpr int artifactGap{ 10 };

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

            lastViewport_ =
                SDL_FRect{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(contentWidth),
                    static_cast<float>(height)
                };

            for (Entry& entry : entries_)
            {
                if (entry.artifact.has_value())
                {
                    entry.artifact->hasClickablePreview = false;
                }
            }

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

            for (Entry& entry : entries_)
            {
                if (haveContent)
                {
                    cursorY +=
                        static_cast<float>(turnGap);
                }

                const float entryBottom =
                    cursorY
                    + static_cast<float>(
                        entry.renderHeight);

                if (
                    entryBottom >= static_cast<float>(y)
                    && cursorY <= static_cast<float>(y + height))
                {
                    if (!entry.artifact.has_value())
                    {
                        responseRenderer_.draw(
                            entry.layout,
                            static_cast<float>(x),
                            cursorY);
                    }
                    else
                    {
                        const float cardX =
                            static_cast<float>(x);

                        const float cardWidth =
                            static_cast<float>(contentWidth);

                        const SDL_FRect card{
                            cardX,
                            cursorY,
                            cardWidth,
                            static_cast<float>(
                                entry.renderHeight)
                        };

                        SDL_SetRenderDrawColor(
                            &renderer_,
                            42,
                            40,
                            49,
                            255);

                        SDL_RenderFillRect(
                            &renderer_,
                            &card);

                        responseRenderer_.draw(
                            entry.layout,
                            card.x
                                + static_cast<float>(cardPadding),
                            card.y
                                + static_cast<float>(cardPadding));

                        ArtifactVisual& visual =
                            *entry.artifact;

                        const int previewHeight =
                            artifactPreviewHeight(
                                visual,
                                contentWidth
                                    - cardPadding * 2);

                        if (
                            visual.texture
                            && previewHeight > 0)
                        {
                            const float availableWidth =
                                static_cast<float>(
                                    contentWidth
                                    - cardPadding * 2);

                            float previewWidth =
                                std::min(
                                    availableWidth,
                                    visual.sourceWidth);

                            const float heightScale =
                                static_cast<float>(previewHeight)
                                / visual.sourceHeight;

                            previewWidth =
                                std::min(
                                    previewWidth,
                                    visual.sourceWidth
                                    * heightScale);

                            const SDL_FRect destination{
                                card.x
                                    + static_cast<float>(cardPadding),
                                card.y
                                    + static_cast<float>(cardPadding)
                                    + static_cast<float>(entry.layout.height())
                                    + static_cast<float>(artifactGap),
                                previewWidth,
                                static_cast<float>(previewHeight)
                            };

                            SDL_RenderTexture(
                                &renderer_,
                                visual.texture.get(),
                                nullptr,
                                &destination);

                            visual.lastPreviewRect =
                                destination;

                            visual.hasClickablePreview =
                                destination.y
                                    + destination.h
                                    >= static_cast<float>(y)
                                && destination.y
                                    <= static_cast<float>(y + height);
                        }
                    }
                }

                cursorY = entryBottom;
                haveContent = true;
            }


            if (assistantStreaming_ && streamingLayout_)
            {
                if (haveContent)
                {
                    cursorY +=
                        static_cast<float>(turnGap);
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
                    ? scrollOffset_
                        / maxScrollOffset_
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


    void RichTranscript::appendArtifact(
        artifacts::Artifact artifact)
    {
        impl_->appendArtifact(
            std::move(artifact));
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


    bool RichTranscript::handlePointerDown(
        const float x,
        const float y,
        const bool revealFolder)
    {
        return impl_->handlePointerDown(
            x,
            y,
            revealFolder);
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
