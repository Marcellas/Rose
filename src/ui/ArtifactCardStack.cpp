#include "ui/ArtifactCardStack.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Shellapi.h>
#endif

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
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


    struct ArtifactCardStack::Impl
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


        using TexturePtr =
            std::unique_ptr<SDL_Texture, TextureDeleter>;

        using TextPtr =
            std::unique_ptr<TTF_Text, TextDeleter>;


        struct Card
        {
            artifacts::Artifact artifact;
            TextPtr label;
            TexturePtr texture;

            float sourceWidth{ 0.0f };
            float sourceHeight{ 0.0f };

            int layoutWidth{ 0 };
            int labelHeight{ 0 };
            int previewHeight{ 0 };
            int renderHeight{ 0 };

            SDL_FRect lastClickRect{};
            bool hasClickableRegion{ false };
        };


        SDL_Renderer& renderer_;
        TTF_TextEngine& textEngine_;
        TTF_Font& font_;

        std::vector<Card> cards_;

        float scrollOffset_{ 0.0f };
        float maximumScrollOffset_{ 0.0f };
        bool followLatest_{ true };

        SDL_FRect lastViewport_{};


        Impl(
            SDL_Renderer& renderer,
            TTF_TextEngine& textEngine,
            TTF_Font& font)
            : renderer_{ renderer }
            , textEngine_{ textEngine }
            , font_{ font }
        {
        }


        [[nodiscard]]
        static std::string cardLabel(
            const artifacts::Artifact& artifact)
        {
            const bool image =
                artifact.kind
                == artifacts::ArtifactKind::Image;

            return
                std::string{
                    image
                        ? "Generated image: "
                        : "Created file: "
                }
                + artifact.displayName
                + "\n"
                + artifact.path.string()
                + "\nLeft-click to open; right-click to reveal in Explorer.";
        }


        void appendArtifact(
            artifacts::Artifact artifact)
        {
            Card card;
            card.artifact =
                std::move(artifact);

            const std::string labelText =
                cardLabel(
                    card.artifact);

            card.label.reset(
                TTF_CreateText(
                    &textEngine_,
                    &font_,
                    labelText.c_str(),
                    labelText.size()));

            if (!card.label)
            {
                throw std::runtime_error{
                    std::string{
                        "Could not create Rose artifact-card text: "
                    }
                    + SDL_GetError()
                };
            }

            if (!TTF_SetTextColor(
                card.label.get(),
                236,
                233,
                242,
                255))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not set Rose artifact-card text color: "
                    }
                    + SDL_GetError()
                };
            }

            if (
                card.artifact.kind
                == artifacts::ArtifactKind::Image)
            {
                const std::string pathText =
                    card.artifact.path.string();

                SDL_Texture* rawTexture =
                    IMG_LoadTexture(
                        &renderer_,
                        pathText.c_str());

                if (rawTexture != nullptr)
                {
                    card.texture.reset(
                        rawTexture);

                    SDL_GetTextureSize(
                        rawTexture,
                        &card.sourceWidth,
                        &card.sourceHeight);
                }
            }

            cards_.push_back(
                std::move(card));

            followLatest_ =
                true;
        }


        void clear() noexcept
        {
            cards_.clear();
            scrollOffset_ = 0.0f;
            maximumScrollOffset_ = 0.0f;
            followLatest_ = true;
            lastViewport_ = {};
        }


        [[nodiscard]]
        int previewHeight(
            const Card& card,
            const int availableWidth) const noexcept
        {
            if (
                !card.texture
                || card.sourceWidth <= 0.0f
                || card.sourceHeight <= 0.0f
                || availableWidth <= 0)
            {
                return 0;
            }

            constexpr float maximumPreviewHeight{
                180.0f
            };

            const float widthScale =
                static_cast<float>(availableWidth)
                / card.sourceWidth;

            const float heightScale =
                maximumPreviewHeight
                / card.sourceHeight;

            const float scale =
                std::min({
                    1.0f,
                    widthScale,
                    heightScale
                });

            return
                std::max(
                    1,
                    static_cast<int>(
                        card.sourceHeight
                        * scale));
        }


        void ensureLayout(
            Card& card,
            const int width)
        {
            constexpr int cardPadding{ 10 };
            constexpr int previewGap{ 10 };

            const int textWidth =
                std::max(
                    1,
                    width
                    - cardPadding * 2);

            if (card.layoutWidth == textWidth)
            {
                return;
            }

            if (!TTF_SetTextWrapWidth(
                card.label.get(),
                textWidth))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not wrap Rose artifact-card text: "
                    }
                    + SDL_GetError()
                };
            }

            int labelWidth{ 0 };
            int labelHeight{ 0 };

            if (!TTF_GetTextSize(
                card.label.get(),
                &labelWidth,
                &labelHeight))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not measure Rose artifact-card text: "
                    }
                    + SDL_GetError()
                };
            }

            card.layoutWidth =
                textWidth;

            card.labelHeight =
                std::max(
                    1,
                    labelHeight);

            card.previewHeight =
                previewHeight(
                    card,
                    textWidth);

            card.renderHeight =
                cardPadding
                + card.labelHeight
                + (
                    card.previewHeight > 0
                        ? previewGap
                            + card.previewHeight
                        : 0)
                + cardPadding;
        }


        void ensureLayouts(
            const int width)
        {
            for (Card& card : cards_)
            {
                ensureLayout(
                    card,
                    width);
            }
        }


        [[nodiscard]]
        int contentHeight(
            const int width)
        {
            constexpr int cardGap{ 10 };

            ensureLayouts(
                width);

            int result{ 0 };

            for (std::size_t index = 0;
                index < cards_.size();
                ++index)
            {
                if (index > 0)
                {
                    result +=
                        cardGap;
                }

                result +=
                    cards_[index].renderHeight;
            }

            return result;
        }


        [[nodiscard]]
        int preferredHeight(
            const int width,
            const int maxHeight)
        {
            if (
                cards_.empty()
                || width <= 0
                || maxHeight <= 0)
            {
                return 0;
            }

            constexpr int scrollbarReservation{
                16
            };

            const int contentWidth =
                std::max(
                    1,
                    width
                    - scrollbarReservation);

            return
                std::min(
                    contentHeight(contentWidth),
                    maxHeight);
        }


        void scrollBy(
            const float deltaPixels) noexcept
        {
            scrollOffset_ =
                std::clamp(
                    scrollOffset_
                    + deltaPixels,
                    0.0f,
                    maximumScrollOffset_);

            constexpr float bottomTolerance{
                1.0f
            };

            followLatest_ =
                scrollOffset_
                >= maximumScrollOffset_
                - bottomTolerance;
        }


        [[nodiscard]]
        bool containsPoint(
            const float x,
            const float y) const noexcept
        {
            return
                lastViewport_.w > 0.0f
                && lastViewport_.h > 0.0f
                && pointInside(
                    lastViewport_,
                    x,
                    y);
        }


        [[nodiscard]]
        bool handlePointerDown(
            const float x,
            const float y,
            const bool revealFolder)
        {
            if (!containsPoint(x, y))
            {
                return false;
            }

            for (Card& card : cards_)
            {
                if (
                    card.hasClickableRegion
                    && pointInside(
                        card.lastClickRect,
                        x,
                        y))
                {
                    if (revealFolder)
                    {
                        revealArtifact(
                            card.artifact.path);
                    }
                    else
                    {
                        openArtifact(
                            card.artifact.path);
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
            if (
                cards_.empty()
                || width <= 0
                || height <= 0)
            {
                lastViewport_ = {};
                return;
            }

            constexpr int scrollbarWidth{ 8 };
            constexpr int scrollbarGap{ 8 };
            constexpr int cardGap{ 10 };
            constexpr int cardPadding{ 10 };
            constexpr int previewGap{ 10 };

            const int contentWidth =
                std::max(
                    1,
                    width
                    - scrollbarGap
                    - scrollbarWidth);

            const int totalHeight =
                contentHeight(
                    contentWidth);

            maximumScrollOffset_ =
                std::max(
                    0.0f,
                    static_cast<float>(
                        totalHeight - height));

            if (followLatest_)
            {
                scrollOffset_ =
                    maximumScrollOffset_;
            }

            scrollOffset_ =
                std::clamp(
                    scrollOffset_,
                    0.0f,
                    maximumScrollOffset_);

            lastViewport_ =
                SDL_FRect{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(width),
                    static_cast<float>(height)
                };

            for (Card& card : cards_)
            {
                card.hasClickableRegion =
                    false;
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

            for (Card& card : cards_)
            {
                const float cardBottom =
                    cursorY
                    + static_cast<float>(
                        card.renderHeight);

                if (
                    cardBottom >= static_cast<float>(y)
                    && cursorY <= static_cast<float>(y + height))
                {
                    const SDL_FRect cardRect{
                        static_cast<float>(x),
                        cursorY,
                        static_cast<float>(contentWidth),
                        static_cast<float>(card.renderHeight)
                    };

                    SDL_SetRenderDrawColor(
                        &renderer_,
                        42,
                        40,
                        49,
                        255);

                    SDL_RenderFillRect(
                        &renderer_,
                        &cardRect);

                    SDL_SetRenderDrawColor(
                        &renderer_,
                        88,
                        82,
                        102,
                        255);

                    SDL_RenderRect(
                        &renderer_,
                        &cardRect);

                    card.lastClickRect =
                        cardRect;

                    card.hasClickableRegion =
                        true;

                    TTF_DrawRendererText(
                        card.label.get(),
                        cardRect.x
                            + static_cast<float>(cardPadding),
                        cardRect.y
                            + static_cast<float>(cardPadding));

                    if (
                        card.texture
                        && card.previewHeight > 0)
                    {
                        const float scale =
                            static_cast<float>(card.previewHeight)
                            / card.sourceHeight;

                        const float previewWidth =
                            std::min(
                                static_cast<float>(
                                    contentWidth
                                    - cardPadding * 2),
                                card.sourceWidth
                                * scale);

                        const SDL_FRect destination{
                            cardRect.x
                                + static_cast<float>(cardPadding),
                            cardRect.y
                                + static_cast<float>(cardPadding)
                                + static_cast<float>(card.labelHeight)
                                + static_cast<float>(previewGap),
                            previewWidth,
                            static_cast<float>(card.previewHeight)
                        };

                        SDL_RenderTexture(
                            &renderer_,
                            card.texture.get(),
                            nullptr,
                            &destination);
                    }
                }

                cursorY =
                    cardBottom
                    + static_cast<float>(cardGap);
            }

            SDL_SetRenderClipRect(
                &renderer_,
                nullptr);

            if (maximumScrollOffset_ <= 0.0f)
            {
                return;
            }

            const SDL_FRect track{
                static_cast<float>(
                    x
                    + contentWidth
                    + scrollbarGap),
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
                &track);

            const float visibleFraction =
                std::clamp(
                    static_cast<float>(height)
                    / static_cast<float>(
                        std::max(totalHeight, 1)),
                    0.0f,
                    1.0f);

            const float thumbHeight =
                std::max(
                    28.0f,
                    track.h
                    * visibleFraction);

            const float thumbTravel =
                std::max(
                    0.0f,
                    track.h
                    - thumbHeight);

            const float scrollFraction =
                maximumScrollOffset_ > 0.0f
                    ? scrollOffset_
                        / maximumScrollOffset_
                    : 0.0f;

            const SDL_FRect thumb{
                track.x,
                track.y
                    + thumbTravel
                    * scrollFraction,
                track.w,
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
                &thumb);
        }
    };


    ArtifactCardStack::ArtifactCardStack(
        SDL_Renderer& renderer,
        TTF_TextEngine& textEngine,
        TTF_Font& font)
        : impl_{
            std::make_unique<Impl>(
                renderer,
                textEngine,
                font)
        }
    {
    }


    ArtifactCardStack::~ArtifactCardStack() = default;


    void ArtifactCardStack::appendArtifact(
        artifacts::Artifact artifact)
    {
        impl_->appendArtifact(
            std::move(artifact));
    }


    void ArtifactCardStack::clear()
    {
        impl_->clear();
    }


    bool ArtifactCardStack::empty() const noexcept
    {
        return impl_->cards_.empty();
    }


    int ArtifactCardStack::preferredHeight(
        const int width,
        const int maxHeight)
    {
        return impl_->preferredHeight(
            width,
            maxHeight);
    }


    void ArtifactCardStack::scrollBy(
        const float deltaPixels)
    {
        impl_->scrollBy(
            deltaPixels);
    }


    bool ArtifactCardStack::containsPoint(
        const float x,
        const float y) const noexcept
    {
        return impl_->containsPoint(
            x,
            y);
    }


    bool ArtifactCardStack::handlePointerDown(
        const float x,
        const float y,
        const bool revealFolder)
    {
        return impl_->handlePointerDown(
            x,
            y,
            revealFolder);
    }


    void ArtifactCardStack::render(
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
