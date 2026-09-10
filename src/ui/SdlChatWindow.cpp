#include "ui/SdlChatWindow.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <iostream>


namespace rose::ui
{

    SdlChatWindow::SdlChatWindow(
        platform::SdlRuntime& runtime,
        platform::TtfRuntime& ttfRuntime,
        ChatBridge& chatBridge,
        std::filesystem::path fontPath,
        const int width,
        const int height)
        : runtime_{ runtime }
        , ttfRuntime_{ ttfRuntime }
        , chatBridge_{ chatBridge }
    {
        if (width <= 0 || height <= 0)
        {
            throw std::invalid_argument{
                "SdlChatWindow dimensions must be greater than zero."
            };
        }


        SDL_Window* rawWindow{
            nullptr
        };

        SDL_Renderer* rawRenderer{
            nullptr
        };


        // -------------------------------------------------------------------------
        // Create SDL window + renderer
        // -------------------------------------------------------------------------

        if (!SDL_CreateWindowAndRenderer(
            "Rose",
            width,
            height,
            SDL_WINDOW_RESIZABLE,
            &rawWindow,
            &rawRenderer))
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose chat window: "
                }
                + SDL_GetError()
            };
        }


        // IMPORTANT:
        //
        // Acquire ownership immediately.
        //
        // Everything below this point that needs an SDL renderer must use
        // renderer_.get(), so renderer_ must already contain rawRenderer.
        window_.reset(
            rawWindow);

        renderer_.reset(
            rawRenderer);

        if (!renderer_)
        {
            throw std::runtime_error{
                "Rose chat renderer ownership transfer failed."
            };
        }

        std::cout
            << "Chat renderer: "
            << renderer_.get()
            << '\n';


        // -------------------------------------------------------------------------
        // Font
        // -------------------------------------------------------------------------

        const std::filesystem::path absoluteFontPath =
            std::filesystem::absolute(
                fontPath);


        if (!std::filesystem::exists(
            absoluteFontPath))
        {
            throw std::runtime_error{
                std::string{
                    "Rose UI font does not exist: "
                }
                + absoluteFontPath.string()
            };
        }


        const std::string fontPathString =
            absoluteFontPath.string();


        font_.reset(
            TTF_OpenFont(
                fontPathString.c_str(),
                18.0f));


        if (!font_)
        {
            throw std::runtime_error{
                std::string{
                    "Could not open Rose UI font '"
                }
                + fontPathString
                + "': "
                + SDL_GetError()
            };
        }


        // -------------------------------------------------------------------------
        // Renderer-backed SDL_ttf engine
        // -------------------------------------------------------------------------
        //
        // At this point renderer_.get() MUST be non-null because ownership of
        // rawRenderer was transferred above.
        textEngine_.reset(
            TTF_CreateRendererTextEngine(
                renderer_.get()));


        if (!textEngine_)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose text engine: "
                }
                + SDL_GetError()
            };
        }


        // -------------------------------------------------------------------------
        // Transcript text
        // -------------------------------------------------------------------------

        transcriptText_.reset(
            TTF_CreateText(
                textEngine_.get(),
                font_.get(),
                "",
                0));


        if (!transcriptText_)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose transcript text: "
                }
                + SDL_GetError()
            };
        }


        if (!TTF_SetTextColor(
            transcriptText_.get(),
            235,
            235,
            240,
            255))
        {
            throw std::runtime_error{
                std::string{
                    "Could not set Rose transcript color: "
                }
                + SDL_GetError()
            };
        }

        inputTextObject_.reset(
            TTF_CreateText(
                textEngine_.get(),
                font_.get(),
                "",
                0));


        if (!inputTextObject_)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose input text: "
                }
                + SDL_GetError()
            };
        }


        if (!TTF_SetTextColor(
            inputTextObject_.get(),
            245,
            245,
            248,
            255))
        {
            throw std::runtime_error{
                std::string{
                    "Could not set Rose input text color: "
                }
                + SDL_GetError()
            };
        }

        // -------------------------------------------------------------------------
        // Text input
        // -------------------------------------------------------------------------

        if (!SDL_StartTextInput(
            window_.get()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not start SDL text input: "
                }
                + SDL_GetError()
            };
        }
    }


    SdlChatWindow::~SdlChatWindow()
    {
        if (window_)
        {
            SDL_StopTextInput(
                window_.get());
        }
    }


    bool SdlChatWindow::handleEvent(
        const SDL_Event& event)
    {
        const SDL_WindowID windowId =
            SDL_GetWindowID(
                window_.get());


        switch (event.type)
        {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == windowId)
            {
                return false;
            }

            break;


        case SDL_EVENT_TEXT_INPUT:
            if (event.text.windowID != windowId)
            {
                break;
            }


            if (event.text.text != nullptr)
            {
                inputText_ +=
                    event.text.text;
            }

            inputTextDirty_ = true;

            break;


        case SDL_EVENT_KEY_DOWN:
            if (event.key.windowID != windowId)
            {
                break;
            }


            switch (event.key.key)
            {
            case SDLK_RETURN:
                submitInput();
                break;


            case SDLK_BACKSPACE:
                eraseLastUtf8CodePoint();

                inputTextDirty_ = true;

                break;


            case SDLK_ESCAPE:
                return false;


            default:
                break;
            }

            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.windowID != windowId)
            {
                break;
            }


            {
                float wheelY =
                    event.wheel.y;


                // SDL reports natural/trackpad scrolling through the FLIPPED flag.
                //
                // Normalize it so the rest of Rose's scrolling code has one meaning.
                if (
                    event.wheel.direction
                    == SDL_MOUSEWHEEL_FLIPPED)
                {
                    wheelY =
                        -wheelY;
                }


                // This is deliberately a UI constant rather than tying scrolling
                // directly to font line height. It gives reasonably smooth scrolling
                // with both mouse wheels and trackpads.
                constexpr float scrollPixelsPerUnit{
                    48.0f
                };


                transcriptScrollOffset_ =
                    std::clamp(
                        transcriptScrollOffset_
                        - wheelY
                        * scrollPixelsPerUnit,

                        0.0f,
                        transcriptMaxScrollOffset_);


                // If the user reaches the bottom again, resume automatic following.
                constexpr float bottomTolerance{
                    1.0f
                };


                followLatest_ =
                    transcriptScrollOffset_
                    >= transcriptMaxScrollOffset_
                    - bottomTolerance;
            }

            break;


        default:
            break;
        }


        return true;
    }


    void SdlChatWindow::update()
    {
        while (true)
        {
            std::optional<ChatEvent> event =
                chatBridge_.tryPopEvent();


            if (!event)
            {
                break;
            }


            handleChatEvent(
                std::move(*event));
        }


        // Perform potentially non-trivial text layout once per UI frame rather
        // than once for every streamed model chunk.
        if (transcriptDirty_)
        {
            refreshTranscriptText();
        }

        if (inputTextDirty_)
        {
            refreshInputText();
        }
    }


    void SdlChatWindow::render()
    {
        SDL_SetRenderDrawColor(
            renderer_.get(),
            25,
            23,
            31,
            255);


        SDL_RenderClear(
            renderer_.get());


        // -------------------------------------------------------------------------
        // Transcript
        // -------------------------------------------------------------------------
        //
        // SDL_RenderDebugText is TEMPORARY scaffolding.
        //
        // SDL documents it as debug-only: fixed tiny bitmap font, ASCII rendering
        // only, and no automatic wrapping.
        //
        // The actual chat renderer will use SDL_ttf.
        
        // -------------------------------------------------------------------------
        // Real SDL_ttf transcript
        // -------------------------------------------------------------------------

        int width{ 0 };
        int height{ 0 };


        if (!SDL_GetWindowSize(
            window_.get(),
            &width,
            &height))
        {
            throw std::runtime_error{
                std::string{
                    "Could not query Rose chat window size: "
                }
                + SDL_GetError()
            };
        }


        // Leave space for the input box at the bottom.
        constexpr int transcriptLeft{
    20
        };

        constexpr int transcriptTop{
            20
        };

        constexpr int transcriptRightPadding{
            12
        };

        constexpr int inputReservedHeight{
            66
        };


        constexpr int scrollbarWidth{
            8
        };

        constexpr int scrollbarGap{
            10
        };


        const int wrapWidth =
            std::max(
                1,
                width
                - transcriptLeft
                - transcriptRightPadding
                - scrollbarGap
                - scrollbarWidth);


        const int transcriptHeight =
            std::max(
                1,
                height
                - transcriptTop
                - inputReservedHeight);


        // Wrapping only needs to be recalculated when the usable width changes.
        if (wrapWidth != transcriptWrapWidth_)
        {
            if (!TTF_SetTextWrapWidth(
                transcriptText_.get(),
                wrapWidth))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not update Rose transcript wrapping: "
                    }
                    + SDL_GetError()
                };
            }


            transcriptWrapWidth_ =
                wrapWidth;
        }


        // Find the fully laid-out height after wrapping.
        int textWidth{ 0 };
        int textHeight{ 0 };


        if (!TTF_GetTextSize(
            transcriptText_.get(),
            &textWidth,
            &textHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not measure Rose transcript: "
                }
                + SDL_GetError()
            };
        }


        // Prevent old conversation content from drawing through the input field or
        // outside the transcript viewport.
        const SDL_Rect transcriptClip{
            transcriptLeft,
            transcriptTop,
            wrapWidth,
            transcriptHeight
        };


        if (!SDL_SetRenderClipRect(
            renderer_.get(),
            &transcriptClip))
        {
            throw std::runtime_error{
                std::string{
                    "Could not set Rose transcript clip rectangle: "
                }
                + SDL_GetError()
            };
        }


        // Short conversations begin at the top.
        //
        // Once the conversation becomes taller than the available viewport, shift it
        // upward so the newest content remains visible at the bottom.
        // -------------------------------------------------------------------------
// Calculate scroll range
// -------------------------------------------------------------------------

        transcriptMaxScrollOffset_ =
            std::max(
                0.0f,

                static_cast<float>(
                    textHeight
                    - transcriptHeight));


        // Normal chat behavior: while we're following the latest message, remain
        // pinned to the bottom as streamed content increases the text height.
        if (followLatest_)
        {
            transcriptScrollOffset_ =
                transcriptMaxScrollOffset_;
        }


        // Window resizing or rewrapping can make the transcript shorter.
        //
        // Always guarantee that our existing offset remains valid.
        transcriptScrollOffset_ =
            std::clamp(
                transcriptScrollOffset_,
                0.0f,
                transcriptMaxScrollOffset_);


        // TTF_DrawRendererText() draws the complete laid-out text object.
        //
        // Moving its origin upward gives us a scrolling viewport; the SDL clip rect
        // prevents content outside the viewport from being visible.
        const float transcriptY =
            static_cast<float>(
                transcriptTop)
            - transcriptScrollOffset_;


        if (!TTF_DrawRendererText(
            transcriptText_.get(),
            static_cast<float>(
                transcriptLeft),
            transcriptY))
        {
            throw std::runtime_error{
                std::string{
                    "Could not draw Rose transcript: "
                }
                + SDL_GetError()
            };
        }


        // Do not let the transcript clipping affect the input box that we draw next.
        if (!SDL_SetRenderClipRect(
            renderer_.get(),
            nullptr))
        {
            throw std::runtime_error{
                std::string{
                    "Could not clear Rose transcript clipping: "
                }
                + SDL_GetError()
            };
        }

        // -------------------------------------------------------------------------
        // Transcript scrollbar
        // -------------------------------------------------------------------------
        //
        // Don't show a scrollbar when all content already fits in the viewport.
        if (transcriptMaxScrollOffset_ > 0.0f)
        {
            const float trackX =
                static_cast<float>(
                    width
                    - transcriptRightPadding
                    - scrollbarWidth);


            const SDL_FRect scrollbarTrack{
                trackX,

                static_cast<float>(
                    transcriptTop),

                static_cast<float>(
                    scrollbarWidth),

                static_cast<float>(
                    transcriptHeight)
            };


            // Subtle background track.
            SDL_SetRenderDrawColor(
                renderer_.get(),
                50,
                48,
                58,
                255);


            SDL_RenderFillRect(
                renderer_.get(),
                &scrollbarTrack);


            // ---------------------------------------------------------------------
            // Thumb size
            // ---------------------------------------------------------------------
            //
            // The visible fraction of the document determines how large the thumb is.
            //
            // Example:
            //
            //     viewport = 400px
            //     content  = 800px
            //
            //     visible fraction = 0.5
            //
            // so the thumb occupies half the scrollbar track.
            const float visibleFraction =
                std::clamp(
                    static_cast<float>(
                        transcriptHeight)
                    / static_cast<float>(
                        std::max(
                            textHeight,
                            1)),

                    0.0f,
                    1.0f);


            constexpr float minimumThumbHeight{
                28.0f
            };


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
                transcriptMaxScrollOffset_ > 0.0f
                ? transcriptScrollOffset_
                / transcriptMaxScrollOffset_
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
                renderer_.get(),
                135,
                130,
                150,
                255);


            SDL_RenderFillRect(
                renderer_.get(),
                &scrollbarThumb);
        }

        SDL_GetWindowSize(
            window_.get(),
            &width,
            &height);


        const SDL_FRect inputArea{
            10.0f,
            static_cast<float>(height - 46),
            static_cast<float>(width - 20),
            36.0f
        };


        SDL_SetRenderDrawColor(
            renderer_.get(),
            55,
            52,
            65,
            255);


        SDL_RenderFillRect(
            renderer_.get(),
            &inputArea);


        SDL_SetRenderDrawColor(
            renderer_.get(),
            245,
            245,
            248,
            255);

        // -------------------------------------------------------------------------
// Real SDL_ttf input text
// -------------------------------------------------------------------------

        constexpr float inputTextLeftPadding{
            10.0f
        };


        constexpr float inputTextTopPadding{
            7.0f
        };


        const float inputTextX =
            inputArea.x
            + inputTextLeftPadding;


        const float inputTextY =
            inputArea.y
            + inputTextTopPadding;


        if (!TTF_DrawRendererText(
            inputTextObject_.get(),
            inputTextX,
            inputTextY))
        {
            throw std::runtime_error{
                std::string{
                    "Could not draw Rose input text: "
                }
                + SDL_GetError()
            };
        }

        int inputWidth{ 0 };
        int inputHeight{ 0 };


        if (!TTF_GetTextSize(
            inputTextObject_.get(),
            &inputWidth,
            &inputHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not measure Rose input text: "
                }
                + SDL_GetError()
            };
        }


        const float caretX =
            inputTextX
            + static_cast<float>(
                inputWidth)
            + 2.0f;


        const SDL_FRect caret{
            caretX,
            inputTextY,
            2.0f,
            static_cast<float>(
                std::max(
                    inputHeight,
                    18))
        };


        SDL_SetRenderDrawColor(
            renderer_.get(),
            245,
            245,
            248,
            255);


        SDL_RenderFillRect(
            renderer_.get(),
            &caret);

        SDL_RenderPresent(
            renderer_.get());
    }


    void SdlChatWindow::FontDeleter::operator()(
        TTF_Font* font) const noexcept
    {
        if (font != nullptr)
        {
            TTF_CloseFont(
                font);
        }
    }


    void SdlChatWindow::TextEngineDeleter::operator()(
        TTF_TextEngine* engine) const noexcept
    {
        if (engine != nullptr)
        {
            TTF_DestroyRendererTextEngine(
                engine);
        }
    }


    void SdlChatWindow::TextDeleter::operator()(
        TTF_Text* text) const noexcept
    {
        if (text != nullptr)
        {
            TTF_DestroyText(
                text);
        }
    }

    void SdlChatWindow::submitInput()
    {
        followLatest_ = true;

        transcriptDirty_ = true;

        if (inputText_.empty())
        {
            return;
        }


        std::string message =
            std::move(
                inputText_);


        inputText_.clear();

        inputTextDirty_ = true;

        transcript_.push_back(
            std::string{
                "You: "
            }
        + message);


        chatBridge_.submitUserMessage(
            std::move(message));

        transcriptDirty_ = true;
    }


    void SdlChatWindow::handleChatEvent(
        ChatEvent event)
    {
        switch (event.type)
        {
        case ChatEventType::AssistantStarted:
            streamingAssistantText_ =
                "Rose: ";

            break;


        case ChatEventType::AssistantText:
            streamingAssistantText_ +=
                event.text;

            break;


        case ChatEventType::AssistantFinished:
            if (!streamingAssistantText_.empty())
            {
                transcript_.push_back(
                    std::move(
                        streamingAssistantText_));

                streamingAssistantText_.clear();
            }

            break;


        case ChatEventType::ConversationCleared:
            transcript_.clear();

            streamingAssistantText_.clear();

            transcriptScrollOffset_ =
                0.0f;

            transcriptMaxScrollOffset_ =
                0.0f;

            followLatest_ =
                true;

            break;


        case ChatEventType::Error:
            transcript_.push_back(
                std::string{
                    "Error: "
                }
            + event.text);

            streamingAssistantText_.clear();

            break;

        }

        transcriptDirty_ = true;
    }


    void SdlChatWindow::eraseLastUtf8CodePoint()
    {
        if (inputText_.empty())
        {
            return;
        }


        std::size_t position =
            inputText_.size() - 1;


        // UTF-8 continuation bytes have the binary form:
        //
        //     10xxxxxx
        //
        // Walk backward until the leading byte for the final code point.
        while (
            position > 0
            && (
                static_cast<unsigned char>(
                    inputText_[position])
                & 0xC0u)
            == 0x80u)
        {
            --position;
        }


        inputText_.erase(
            position);
    }


    void SdlChatWindow::WindowDeleter::operator()(
        SDL_Window* window) const noexcept
    {
        if (window != nullptr)
        {
            SDL_DestroyWindow(
                window);
        }
    }


    void SdlChatWindow::RendererDeleter::operator()(
        SDL_Renderer* renderer) const noexcept
    {
        if (renderer != nullptr)
        {
            SDL_DestroyRenderer(
                renderer);
        }
    }

    std::string SdlChatWindow::buildTranscriptText() const
    {
        std::size_t requiredSize =
            streamingAssistantText_.size();


        for (const std::string& line : transcript_)
        {
            requiredSize +=
                line.size() + 2;
        }


        std::string text;

        text.reserve(
            requiredSize);


        for (const std::string& line : transcript_)
        {
            text += line;

            // Give each conversational turn some breathing room.
            text += "\n\n";
        }


        if (!streamingAssistantText_.empty())
        {
            text +=
                streamingAssistantText_;
        }


        return text;
    }


    void SdlChatWindow::refreshTranscriptText()
    {
        const std::string text =
            buildTranscriptText();


        if (!TTF_SetTextString(
            transcriptText_.get(),
            text.c_str(),
            text.size()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not update Rose transcript text: "
                }
                + SDL_GetError()
            };
        }


        transcriptDirty_ = false;
    }

    void SdlChatWindow::refreshInputText()
    {
        if (!TTF_SetTextString(
            inputTextObject_.get(),
            inputText_.c_str(),
            inputText_.size()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not update Rose input text: "
                }
                + SDL_GetError()
            };
        }


        inputTextDirty_ = false;
    }

} // namespace rose::ui