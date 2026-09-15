#include "ui/SdlChatWindow.h"
#include "ui/TextPresentation.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <iostream>
#include <limits>


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
        // Context-menu labels
        // -------------------------------------------------------------------------
        //
        // Create these once. Opening the context menu should not allocate a fresh
        // set of SDL_ttf text objects every time the user right-clicks.
        const auto createContextMenuLabel =
            [&](const char* label) -> TextPtr
            {
                TextPtr text{
                    TTF_CreateText(
                        textEngine_.get(),
                        font_.get(),
                        label,
                        0)
                };

                if (!text)
                {
                    throw std::runtime_error{
                        std::string{
                            "Could not create Rose context-menu text: "
                        }
                        + SDL_GetError()
                    };
                }

                return text;
            };


        contextMenuUndoText_ =
            createContextMenuLabel(
                "Undo");

        contextMenuRedoText_ =
            createContextMenuLabel(
                "Redo");

        contextMenuCutText_ =
            createContextMenuLabel(
                "Cut");

        contextMenuCopyText_ =
            createContextMenuLabel(
                "Copy");

        contextMenuPasteText_ =
            createContextMenuLabel(
                "Paste");

        contextMenuSelectAllText_ =
            createContextMenuLabel(
                "Select All");

        contextMenuCopyMessageText_ =
            createContextMenuLabel(
                "Copy Message");

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


            closeContextMenu();


            if (event.text.text != nullptr)
            {
                insertInputText(
                    event.text.text);
            }

            break;


        case SDL_EVENT_KEY_DOWN:
            if (event.key.windowID != windowId)
            {
                break;
            }


            {
                // Escape dismisses the transient editing menu before it is allowed
                // to close Rose's chat window.
                if (
                    contextMenuOpen_
                    && event.key.key == SDLK_ESCAPE)
                {
                    closeContextMenu();
                    break;
                }


                // Any other keyboard action returns focus to normal editing while
                // still allowing the key itself to be processed below.
                closeContextMenu();


                // Rose targets Windows first, so Ctrl owns the standard editing
                // shortcuts. Clipboard access stays entirely on the SDL/UI thread.
                const bool primaryShortcut =
                    (event.key.mod & SDL_KMOD_CTRL) != 0;

                const bool extendSelection =
                    (event.key.mod & SDL_KMOD_SHIFT) != 0;


                if (primaryShortcut)
                {
                    switch (event.key.key)
                    {
                    case SDLK_Z:
                        if (!event.key.repeat)
                        {
                            if (extendSelection)
                            {
                                redoInputEdit();
                            }
                            else
                            {
                                undoInputEdit();
                            }
                        }
                        break;

                    case SDLK_Y:
                        if (!event.key.repeat)
                        {
                            redoInputEdit();
                        }
                        break;

                    case SDLK_LEFT:
                        // Word navigation deliberately supports OS key repeat.
                        // Ctrl+Shift+Left extends the existing selection.
                        moveInputCursorWordLeft(
                            extendSelection);
                        break;

                    case SDLK_RIGHT:
                        moveInputCursorWordRight(
                            extendSelection);
                        break;

                    case SDLK_UP:
                        if (!event.key.repeat)
                        {
                            recallPreviousInput();
                        }
                        break;

                    case SDLK_DOWN:
                        if (!event.key.repeat)
                        {
                            recallNextInput();
                        }
                        break;

                    case SDLK_A:
                        if (!event.key.repeat)
                        {
                            selectAllFocusedText();
                        }
                        break;

                    case SDLK_C:
                        if (!event.key.repeat)
                        {
                            copyFocusedSelectionToClipboard();
                        }
                        break;

                    case SDLK_X:
                        if (
                            !event.key.repeat
                            && textFocus_ == TextFocus::Composer)
                        {
                            cutInputSelectionToClipboard();
                        }
                        break;

                    case SDLK_V:
                        if (!event.key.repeat)
                        {
                            pasteClipboardText();
                        }
                        break;

                    default:
                        break;
                    }

                    break;
                }


                switch (event.key.key)
                {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    // Enter sends. Shift+Enter inserts a real newline into the
                    // canonical input buffer. Insertion replaces any selection.
                    if (extendSelection)
                    {
                        insertInputText(
                            "\n");
                    }
                    else
                    {
                        submitInput();
                    }

                    break;


                case SDLK_BACKSPACE:
                    erasePreviousUtf8CodePoint();
                    break;


                case SDLK_DELETE:
                    eraseNextUtf8CodePoint();
                    break;


                case SDLK_LEFT:
                    moveInputCursorLeft(
                        extendSelection);
                    break;


                case SDLK_RIGHT:
                    moveInputCursorRight(
                        extendSelection);
                    break;


                case SDLK_UP:
                    moveInputCursorVertical(
                        -1,
                        extendSelection);
                    break;


                case SDLK_DOWN:
                    moveInputCursorVertical(
                        1,
                        extendSelection);
                    break;


                case SDLK_HOME:
                    moveInputCursorTo(
                        0,
                        extendSelection);
                    break;


                case SDLK_END:
                    moveInputCursorTo(
                        inputText_.size(),
                        extendSelection);
                    break;


                case SDLK_ESCAPE:
                    return false;


                default:
                    break;
                }
            }

            break;


        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.windowID != windowId)
            {
                break;
            }


            if (event.button.button == SDL_BUTTON_RIGHT)
            {
                endMouseSelection();

                openContextMenu(
                    event.button.x,
                    event.button.y);

                break;
            }


            if (event.button.button == SDL_BUTTON_LEFT)
            {
                if (contextMenuOpen_)
                {
                    const int itemIndex =
                        contextMenuItemIndexForPoint(
                            event.button.x,
                            event.button.y);

                    if (itemIndex >= 0)
                    {
                        activateContextMenuItem(
                            itemIndex);

                        break;
                    }


                    closeContextMenu();
                }


                if (event.button.clicks == 2)
                {
                    selectWordAtPoint(
                        event.button.x,
                        event.button.y);

                    break;
                }


                beginMouseSelection(
                    event.button.x,
                    event.button.y);
            }

            break;


        case SDL_EVENT_MOUSE_MOTION:
            if (event.motion.windowID != windowId)
            {
                break;
            }


            if (contextMenuOpen_)
            {
                updateContextMenuHover(
                    event.motion.x,
                    event.motion.y);

                break;
            }


            if (selectionDrag_ != SelectionDrag::None)
            {
                updateMouseSelection(
                    event.motion.x,
                    event.motion.y);
            }

            break;


        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (
                event.button.windowID == windowId
                && event.button.button == SDL_BUTTON_LEFT)
            {
                endMouseSelection();
            }

            break;


        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.windowID != windowId)
            {
                break;
            }


            closeContextMenu();


            {
                float wheelY =
                    event.wheel.y;


                // SDL reports natural/trackpad scrolling through the FLIPPED flag.
                // Normalize it so the rest of Rose's transcript scrolling has one meaning.
                if (
                    event.wheel.direction
                    == SDL_MOUSEWHEEL_FLIPPED)
                {
                    wheelY =
                        -wheelY;
                }


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


        // =====================================================================
        // Composer geometry and layout
        // =====================================================================
        //
        // Calculate the composer first because its height is dynamic. The
        // transcript then consumes whatever vertical space remains above it.
        constexpr float windowSideMargin{
            10.0f
        };

        constexpr float windowBottomMargin{
            10.0f
        };

        constexpr float inputTextLeftPadding{
            10.0f
        };

        constexpr float inputTextRightPadding{
            10.0f
        };

        constexpr float inputTextTopPadding{
            7.0f
        };

        constexpr float inputTextBottomPadding{
            7.0f
        };

        constexpr int maximumVisibleInputLines{
            7
        };


        const int lineHeight =
            std::max(
                1,
                TTF_GetFontLineSkip(
                    font_.get()));


        const float inputAreaWidth =
            std::max(
                1.0f,
                static_cast<float>(width)
                - windowSideMargin
                * 2.0f);


        const int inputWrapWidth =
            std::max(
                1,
                static_cast<int>(
                    inputAreaWidth
                    - inputTextLeftPadding
                    - inputTextRightPadding));


        if (inputWrapWidth != inputWrapWidth_)
        {
            if (!TTF_SetTextWrapWidth(
                inputTextObject_.get(),
                inputWrapWidth))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not update Rose input wrapping: "
                    }
                    + SDL_GetError()
                };
            }


            inputWrapWidth_ =
                inputWrapWidth;
        }


        int inputTextWidth{ 0 };
        int inputTextHeight{ 0 };


        if (!TTF_GetTextSize(
            inputTextObject_.get(),
            &inputTextWidth,
            &inputTextHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not measure Rose input text: "
                }
                + SDL_GetError()
            };
        }


        // Empty text may report zero height. Keep one visible editing line.
        const int laidOutInputHeight =
            std::max(
                lineHeight,
                inputTextHeight);


        const int maximumVisibleInputTextHeight =
            lineHeight
            * maximumVisibleInputLines;


        const int visibleInputTextHeight =
            std::min(
                laidOutInputHeight,
                maximumVisibleInputTextHeight);


        const float inputAreaHeight =
            inputTextTopPadding
            + static_cast<float>(
                visibleInputTextHeight)
            + inputTextBottomPadding;


        const SDL_FRect inputArea{
            windowSideMargin,
            static_cast<float>(height)
                - windowBottomMargin
                - inputAreaHeight,
            inputAreaWidth,
            inputAreaHeight
        };


        const float inputTextX =
            inputArea.x
            + inputTextLeftPadding;


        const float inputViewportTop =
            inputArea.y
            + inputTextTopPadding;


        const float inputViewportHeight =
            static_cast<float>(
                visibleInputTextHeight);


        // Locate the caret from SDL_ttf's real wrapped layout. TTF substring
        // offsets are UTF-8 byte offsets, matching inputCursorByteOffset_.
        TTF_SubString caretSubstring{};


        if (!TTF_GetTextSubString(
            inputTextObject_.get(),
            static_cast<int>(
                inputCursorByteOffset_),
            &caretSubstring))
        {
            throw std::runtime_error{
                std::string{
                    "Could not locate Rose input caret: "
                }
                + SDL_GetError()
            };
        }


        const float caretContentTop =
            static_cast<float>(
                caretSubstring.rect.y);


        const float caretContentBottom =
            caretContentTop
            + static_cast<float>(
                lineHeight);


        // Once the composer reaches seven lines, scroll only its contents and
        // keep the caret visible.
        if (caretContentTop < inputScrollOffsetY_)
        {
            inputScrollOffsetY_ =
                caretContentTop;
        }
        else if (
            caretContentBottom
            > inputScrollOffsetY_
            + inputViewportHeight)
        {
            inputScrollOffsetY_ =
                caretContentBottom
                - inputViewportHeight;
        }


        const float maximumInputScroll =
            std::max(
                0.0f,
                static_cast<float>(
                    laidOutInputHeight
                    - visibleInputTextHeight));


        inputScrollOffsetY_ =
            std::clamp(
                inputScrollOffsetY_,
                0.0f,
                maximumInputScroll);


        // =====================================================================
        // Transcript layout
        // =====================================================================
        constexpr int transcriptLeft{
            20
        };

        constexpr int transcriptTop{
            20
        };

        constexpr int transcriptRightPadding{
            12
        };

        constexpr int transcriptComposerGap{
            10
        };

        constexpr int scrollbarWidth{
            8
        };

        constexpr int scrollbarGap{
            10
        };


        const int transcriptWrapWidth =
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
                static_cast<int>(
                    inputArea.y)
                - transcriptComposerGap
                - transcriptTop);


        if (transcriptWrapWidth != transcriptWrapWidth_)
        {
            if (!TTF_SetTextWrapWidth(
                transcriptText_.get(),
                transcriptWrapWidth))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not update Rose transcript wrapping: "
                    }
                    + SDL_GetError()
                };
            }


            transcriptWrapWidth_ =
                transcriptWrapWidth;
        }


        int transcriptTextWidth{ 0 };
        int transcriptTextHeight{ 0 };


        if (!TTF_GetTextSize(
            transcriptText_.get(),
            &transcriptTextWidth,
            &transcriptTextHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not measure Rose transcript: "
                }
                + SDL_GetError()
            };
        }


        const SDL_Rect transcriptClip{
            transcriptLeft,
            transcriptTop,
            transcriptWrapWidth,
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


        transcriptMaxScrollOffset_ =
            std::max(
                0.0f,
                static_cast<float>(
                    transcriptTextHeight
                    - transcriptHeight));


        if (followLatest_)
        {
            transcriptScrollOffset_ =
                transcriptMaxScrollOffset_;
        }


        transcriptScrollOffset_ =
            std::clamp(
                transcriptScrollOffset_,
                0.0f,
                transcriptMaxScrollOffset_);


        const float transcriptY =
            static_cast<float>(
                transcriptTop)
            - transcriptScrollOffset_;


        // Remember the exact geometry used this frame. Mouse hit-testing consumes
        // this snapshot on the same SDL thread, so selection never needs to reach
        // into RoseCore or any worker-owned state.
        textLayout_.transcriptAreaX =
            static_cast<float>(transcriptClip.x);
        textLayout_.transcriptAreaY =
            static_cast<float>(transcriptClip.y);
        textLayout_.transcriptAreaWidth =
            static_cast<float>(transcriptClip.w);
        textLayout_.transcriptAreaHeight =
            static_cast<float>(transcriptClip.h);
        textLayout_.transcriptTextX =
            static_cast<float>(transcriptLeft);
        textLayout_.transcriptTextY =
            transcriptY;


        if (hasTranscriptSelection())
        {
            drawSelectionRange(
                transcriptText_.get(),
                transcriptSelectionStart(),
                transcriptSelectionEnd(),
                textLayout_.transcriptTextX,
                textLayout_.transcriptTextY);
        }


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


        // ---------------------------------------------------------------------
        // Transcript scrollbar
        // ---------------------------------------------------------------------
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


            SDL_SetRenderDrawColor(
                renderer_.get(),
                50,
                48,
                58,
                255);


            SDL_RenderFillRect(
                renderer_.get(),
                &scrollbarTrack);


            const float visibleFraction =
                std::clamp(
                    static_cast<float>(
                        transcriptHeight)
                    / static_cast<float>(
                        std::max(
                            transcriptTextHeight,
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


        // =====================================================================
        // Composer rendering
        // =====================================================================
        SDL_SetRenderDrawColor(
            renderer_.get(),
            55,
            52,
            65,
            255);


        SDL_RenderFillRect(
            renderer_.get(),
            &inputArea);


        const SDL_Rect inputClip{
            static_cast<int>(
                inputArea.x
                + inputTextLeftPadding),
            static_cast<int>(
                inputViewportTop),
            inputWrapWidth,
            visibleInputTextHeight
        };


        if (!SDL_SetRenderClipRect(
            renderer_.get(),
            &inputClip))
        {
            throw std::runtime_error{
                std::string{
                    "Could not set Rose input clip rectangle: "
                }
                + SDL_GetError()
            };
        }


        const float inputTextY =
            inputViewportTop
            - inputScrollOffsetY_;


        textLayout_.inputAreaX =
            inputArea.x;
        textLayout_.inputAreaY =
            inputArea.y;
        textLayout_.inputAreaWidth =
            inputArea.w;
        textLayout_.inputAreaHeight =
            inputArea.h;
        textLayout_.inputTextX =
            inputTextX;
        textLayout_.inputTextY =
            inputTextY;
        textLayout_.valid =
            true;


        if (hasInputSelection())
        {
            drawSelectionRange(
                inputTextObject_.get(),
                inputSelectionStart(),
                inputSelectionEnd(),
                inputTextX,
                inputTextY);
        }


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


        const float caretX =
            inputTextX
            + static_cast<float>(
                caretSubstring.rect.x);


        const float caretY =
            inputTextY
            + static_cast<float>(
                caretSubstring.rect.y);


        const SDL_FRect caret{
            caretX,
            caretY,
            2.0f,
            static_cast<float>(
                lineHeight)
        };


        // The composer keeps its cursor state even while the transcript owns
        // selection focus, but only render the caret while the composer is active.
        if (textFocus_ == TextFocus::Composer)
        {
            SDL_SetRenderDrawColor(
                renderer_.get(),
                245,
                245,
                248,
                255);


            SDL_RenderFillRect(
                renderer_.get(),
                &caret);
        }


        if (!SDL_SetRenderClipRect(
            renderer_.get(),
            nullptr))
        {
            throw std::runtime_error{
                std::string{
                    "Could not clear Rose input clipping: "
                }
                + SDL_GetError()
            };
        }


        renderContextMenu(
            width,
            height);


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


        // Prompt recall stores a bounded session-local copy before inputText_ is
        // moved into the worker submission.
        inputRecallHistory_.recordSubmitted(
            inputText_);


        std::string message =
            std::move(
                inputText_);


        inputText_.clear();

        // A submitted turn has crossed the UI -> worker boundary. Undo is an
        // editor operation, not an "unsend" feature, so a new turn starts with a
        // clean composer history.
        inputEditHistory_.clear();

        inputCursorByteOffset_ = 0;
        inputScrollOffsetY_ = 0.0f;
        clearInputSelection();
        clearTranscriptSelection();
        textFocus_ = TextFocus::Composer;
        resetPreferredCaretX();

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
        {
            // AssistantText events are deliberately shown raw while generation is
            // still streaming. AssistantFinished carries RoseCore's authoritative
            // final assistant text, so this is the safe point to apply a whole-
            // response presentation transform.
            //
            // The canonical response remains unchanged in Conversation/Persistence;
            // only this UI-owned transcript copy is reformatted.
            std::string finalAssistantText =
                std::move(
                    event.text);


            // Defensive fallback for a future provider/worker that finishes without
            // attaching the canonical final text to AssistantFinished. The current
            // streaming buffer includes the visible "Rose: " prefix, so remove it
            // before formatting.
            if (finalAssistantText.empty())
            {
                constexpr std::string_view rosePrefix{
                    "Rose: "
                };

                if (
                    streamingAssistantText_.starts_with(
                        rosePrefix))
                {
                    finalAssistantText =
                        streamingAssistantText_.substr(
                            rosePrefix.size());
                }
                else
                {
                    finalAssistantText =
                        streamingAssistantText_;
                }
            }


            if (
                !finalAssistantText.empty()
                || !streamingAssistantText_.empty())
            {
                transcript_.push_back(
                    std::string{
                        "Rose: "
                    }
                    + makeReadableChatText(
                        finalAssistantText));
            }


            streamingAssistantText_.clear();

            break;
        }


        case ChatEventType::ConversationCleared:
            transcript_.clear();

            streamingAssistantText_.clear();

            // /clear should not leave an invisible RAM-only stack of old prompts
            // behind after the visible conversation has been cleared.
            inputRecallHistory_.clear();
            displayedTranscriptText_.clear();
            clearTranscriptSelection();

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


    void SdlChatWindow::insertInputText(
        const std::string_view text,
        const TextEditHistory::Kind editKind)
    {
        if (text.empty())
        {
            return;
        }


        inputRecallHistory_.cancelBrowsing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();


        const TextEditHistory::SelectionState before =
            currentInputSelectionState();


        std::size_t editStart =
            inputCursorByteOffset_;

        std::string removedText;


        if (hasInputSelection())
        {
            editStart =
                inputSelectionStart();

            const std::size_t editEnd =
                inputSelectionEnd();

            removedText =
                inputText_.substr(
                    editStart,
                    editEnd - editStart);

            deleteInputSelection();
        }


        inputText_.insert(
            inputCursorByteOffset_,
            text.data(),
            text.size());


        inputCursorByteOffset_ +=
            text.size();


        resetPreferredCaretX();

        inputTextDirty_ = true;


        inputEditHistory_.record(
            editKind,
            editStart,
            std::move(
                removedText),
            std::string{
                text
            },
            before,
            currentInputSelectionState());
    }


    void SdlChatWindow::erasePreviousUtf8CodePoint()
    {
        inputRecallHistory_.cancelBrowsing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();


        const TextEditHistory::SelectionState before =
            currentInputSelectionState();


        if (hasInputSelection())
        {
            const std::size_t start =
                inputSelectionStart();

            const std::size_t end =
                inputSelectionEnd();

            std::string removed =
                inputText_.substr(
                    start,
                    end - start);


            deleteInputSelection();


            inputEditHistory_.record(
                TextEditHistory::Kind::Backspace,
                start,
                std::move(
                    removed),
                {},
                before,
                currentInputSelectionState());

            return;
        }


        if (inputCursorByteOffset_ == 0)
        {
            return;
        }


        const std::size_t previous =
            previousUtf8Boundary(
                inputCursorByteOffset_);

        std::string removed =
            inputText_.substr(
                previous,
                inputCursorByteOffset_
                    - previous);


        inputText_.erase(
            previous,
            inputCursorByteOffset_
                - previous);


        inputCursorByteOffset_ =
            previous;


        resetPreferredCaretX();

        inputTextDirty_ = true;


        inputEditHistory_.record(
            TextEditHistory::Kind::Backspace,
            previous,
            std::move(
                removed),
            {},
            before,
            currentInputSelectionState());
    }


    void SdlChatWindow::eraseNextUtf8CodePoint()
    {
        inputRecallHistory_.cancelBrowsing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();


        const TextEditHistory::SelectionState before =
            currentInputSelectionState();


        if (hasInputSelection())
        {
            const std::size_t start =
                inputSelectionStart();

            const std::size_t end =
                inputSelectionEnd();

            std::string removed =
                inputText_.substr(
                    start,
                    end - start);


            deleteInputSelection();


            inputEditHistory_.record(
                TextEditHistory::Kind::DeleteForward,
                start,
                std::move(
                    removed),
                {},
                before,
                currentInputSelectionState());

            return;
        }


        if (
            inputCursorByteOffset_
            >= inputText_.size())
        {
            return;
        }


        const std::size_t next =
            nextUtf8Boundary(
                inputCursorByteOffset_);

        std::string removed =
            inputText_.substr(
                inputCursorByteOffset_,
                next
                    - inputCursorByteOffset_);


        const std::size_t editStart =
            inputCursorByteOffset_;


        inputText_.erase(
            inputCursorByteOffset_,
            next
                - inputCursorByteOffset_);


        resetPreferredCaretX();

        inputTextDirty_ = true;


        inputEditHistory_.record(
            TextEditHistory::Kind::DeleteForward,
            editStart,
            std::move(
                removed),
            {},
            before,
            currentInputSelectionState());
    }


    void SdlChatWindow::moveInputCursorLeft(
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();

        if (
            !extendSelection
            && hasInputSelection())
        {
            const std::size_t start =
                inputSelectionStart();

            clearInputSelection();

            inputCursorByteOffset_ =
                start;

            resetPreferredCaretX();
            return;
        }


        moveInputCursorTo(
            previousUtf8Boundary(
                inputCursorByteOffset_),
            extendSelection);
    }


    void SdlChatWindow::moveInputCursorRight(
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();

        if (
            !extendSelection
            && hasInputSelection())
        {
            const std::size_t end =
                inputSelectionEnd();

            clearInputSelection();

            inputCursorByteOffset_ =
                end;

            resetPreferredCaretX();
            return;
        }


        moveInputCursorTo(
            nextUtf8Boundary(
                inputCursorByteOffset_),
            extendSelection);
    }


    void SdlChatWindow::moveInputCursorVertical(
        const int direction,
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        if (direction == 0)
        {
            return;
        }


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();

        if (extendSelection)
        {
            if (!inputSelectionAnchorByteOffset_.has_value())
            {
                inputSelectionAnchorByteOffset_ =
                    inputCursorByteOffset_;
            }
        }
        else
        {
            clearInputSelection();
        }


        // Up/Down depends on SDL_ttf's current wrapped layout. If text changed
        // earlier in this event cycle, synchronize the text object before asking
        // it for substring geometry.
        if (inputTextDirty_)
        {
            refreshInputText();
        }


        TTF_SubString current{};


        if (!TTF_GetTextSubString(
            inputTextObject_.get(),
            static_cast<int>(
                inputCursorByteOffset_),
            &current))
        {
            return;
        }


        if (preferredCaretX_ < 0.0f)
        {
            preferredCaretX_ =
                static_cast<float>(
                    current.rect.x);
        }


        const int lineHeight =
            std::max(
                1,
                TTF_GetFontLineSkip(
                    font_.get()));


        const int targetY =
            current.rect.y
            + direction
            * lineHeight
            + lineHeight / 2;


        TTF_SubString target{};


        if (!TTF_GetTextSubStringForPoint(
            inputTextObject_.get(),
            static_cast<int>(
                preferredCaretX_),
            targetY,
            &target))
        {
            return;
        }


        inputCursorByteOffset_ =
            std::clamp(
                static_cast<std::size_t>(
                    std::max(
                        target.offset,
                        0)),
                std::size_t{ 0 },
                inputText_.size());
    }


    void SdlChatWindow::moveInputCursorTo(
        const std::size_t byteOffset,
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();

        if (extendSelection)
        {
            if (!inputSelectionAnchorByteOffset_.has_value())
            {
                inputSelectionAnchorByteOffset_ =
                    inputCursorByteOffset_;
            }
        }
        else
        {
            clearInputSelection();
        }


        inputCursorByteOffset_ =
            std::min(
                byteOffset,
                inputText_.size());


        resetPreferredCaretX();
    }



    TextEditHistory::SelectionState
    SdlChatWindow::currentInputSelectionState() const noexcept
    {
        return TextEditHistory::SelectionState{
            .cursorByteOffset =
                inputCursorByteOffset_,
            .anchorByteOffset =
                inputSelectionAnchorByteOffset_
        };
    }


    void SdlChatWindow::restoreInputSelectionState(
        const TextEditHistory::SelectionState& state) noexcept
    {
        inputCursorByteOffset_ =
            (std::min)(
                state.cursorByteOffset,
                inputText_.size());


        if (
            state.anchorByteOffset.has_value()
            && *state.anchorByteOffset
                <= inputText_.size())
        {
            inputSelectionAnchorByteOffset_ =
                state.anchorByteOffset;
        }
        else
        {
            inputSelectionAnchorByteOffset_.reset();
        }


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();
        resetPreferredCaretX();

        inputTextDirty_ =
            true;
    }


    void SdlChatWindow::undoInputEdit()
    {
        inputRecallHistory_.cancelBrowsing();


        std::optional<TextEditHistory::SelectionState> state =
            inputEditHistory_.undo(
                inputText_);


        if (!state.has_value())
        {
            return;
        }


        closeContextMenu();

        restoreInputSelectionState(
            *state);
    }


    void SdlChatWindow::redoInputEdit()
    {
        inputRecallHistory_.cancelBrowsing();


        std::optional<TextEditHistory::SelectionState> state =
            inputEditHistory_.redo(
                inputText_);


        if (!state.has_value())
        {
            return;
        }


        closeContextMenu();

        restoreInputSelectionState(
            *state);
    }



    void SdlChatWindow::recallPreviousInput()
    {
        const std::optional<std::string_view> recalled =
            inputRecallHistory_.older(
                inputText_);


        if (!recalled.has_value())
        {
            return;
        }


        applyRecalledInput(
            *recalled);
    }


    void SdlChatWindow::recallNextInput()
    {
        const std::optional<std::string_view> recalled =
            inputRecallHistory_.newer();


        if (!recalled.has_value())
        {
            return;
        }


        applyRecalledInput(
            *recalled);
    }


    void SdlChatWindow::applyRecalledInput(
        const std::string_view text)
    {
        inputText_.assign(
            text);


        // Recall is navigation across submitted prompts, not an ordinary text edit.
        // Starting from a recalled prompt therefore begins a fresh Undo/Redo branch.
        inputEditHistory_.clear();


        inputCursorByteOffset_ =
            inputText_.size();

        inputScrollOffsetY_ =
            0.0f;

        clearInputSelection();
        clearTranscriptSelection();

        textFocus_ =
            TextFocus::Composer;

        resetPreferredCaretX();

        inputTextDirty_ =
            true;
    }


    bool SdlChatWindow::hasInputSelection() const noexcept
    {
        return
            inputSelectionAnchorByteOffset_.has_value()
            && *inputSelectionAnchorByteOffset_
                != inputCursorByteOffset_;
    }


    std::size_t SdlChatWindow::inputSelectionStart() const noexcept
    {
        if (!inputSelectionAnchorByteOffset_.has_value())
        {
            return inputCursorByteOffset_;
        }


        return (std::min)(
            *inputSelectionAnchorByteOffset_,
            inputCursorByteOffset_);
    }


    std::size_t SdlChatWindow::inputSelectionEnd() const noexcept
    {
        if (!inputSelectionAnchorByteOffset_.has_value())
        {
            return inputCursorByteOffset_;
        }


        return (std::max)(
            *inputSelectionAnchorByteOffset_,
            inputCursorByteOffset_);
    }


    void SdlChatWindow::clearInputSelection() noexcept
    {
        inputSelectionAnchorByteOffset_.reset();
    }


    bool SdlChatWindow::deleteInputSelection()
    {
        if (!hasInputSelection())
        {
            clearInputSelection();
            return false;
        }


        const std::size_t start =
            inputSelectionStart();

        const std::size_t end =
            inputSelectionEnd();


        inputText_.erase(
            start,
            end - start);

        inputCursorByteOffset_ =
            start;

        clearInputSelection();
        resetPreferredCaretX();

        inputTextDirty_ = true;
        return true;
    }


    bool SdlChatWindow::hasTranscriptSelection() const noexcept
    {
        return
            transcriptSelectionAnchorByteOffset_.has_value()
            && *transcriptSelectionAnchorByteOffset_
                != transcriptSelectionCaretByteOffset_;
    }


    std::size_t SdlChatWindow::transcriptSelectionStart() const noexcept
    {
        if (!transcriptSelectionAnchorByteOffset_.has_value())
        {
            return transcriptSelectionCaretByteOffset_;
        }


        return (std::min)(
            *transcriptSelectionAnchorByteOffset_,
            transcriptSelectionCaretByteOffset_);
    }


    std::size_t SdlChatWindow::transcriptSelectionEnd() const noexcept
    {
        if (!transcriptSelectionAnchorByteOffset_.has_value())
        {
            return transcriptSelectionCaretByteOffset_;
        }


        return (std::max)(
            *transcriptSelectionAnchorByteOffset_,
            transcriptSelectionCaretByteOffset_);
    }


    void SdlChatWindow::clearTranscriptSelection() noexcept
    {
        transcriptSelectionAnchorByteOffset_.reset();
        transcriptSelectionCaretByteOffset_ = 0;
    }


    void SdlChatWindow::selectAllFocusedText()
    {
        inputEditHistory_.breakCoalescing();


        if (textFocus_ == TextFocus::Transcript)
        {
            clearInputSelection();

            if (displayedTranscriptText_.empty())
            {
                clearTranscriptSelection();
                return;
            }


            transcriptSelectionAnchorByteOffset_ = 0;
            transcriptSelectionCaretByteOffset_ =
                displayedTranscriptText_.size();

            return;
        }


        clearTranscriptSelection();

        if (inputText_.empty())
        {
            clearInputSelection();
            return;
        }


        inputSelectionAnchorByteOffset_ = 0;
        inputCursorByteOffset_ =
            inputText_.size();

        resetPreferredCaretX();
    }


    void SdlChatWindow::copyFocusedSelectionToClipboard()
    {
        std::string selectedText;


        if (
            textFocus_ == TextFocus::Transcript
            && hasTranscriptSelection())
        {
            const std::size_t start =
                transcriptSelectionStart();

            const std::size_t end =
                transcriptSelectionEnd();


            selectedText =
                displayedTranscriptText_.substr(
                    start,
                    end - start);
        }
        else if (
            textFocus_ == TextFocus::Composer
            && hasInputSelection())
        {
            const std::size_t start =
                inputSelectionStart();

            const std::size_t end =
                inputSelectionEnd();


            selectedText =
                inputText_.substr(
                    start,
                    end - start);
        }


        if (selectedText.empty())
        {
            return;
        }


        if (!SDL_SetClipboardText(
            selectedText.c_str()))
        {
            // Clipboard failure is a UI inconvenience, not a reason to terminate
            // Rose's conversation worker or the desktop process.
            std::cerr
                << "Rose clipboard copy failed: "
                << SDL_GetError()
                << '\n';
        }
    }


    void SdlChatWindow::cutInputSelectionToClipboard()
    {
        inputRecallHistory_.cancelBrowsing();


        if (!hasInputSelection())
        {
            return;
        }


        textFocus_ =
            TextFocus::Composer;


        const TextEditHistory::SelectionState before =
            currentInputSelectionState();

        const std::size_t start =
            inputSelectionStart();

        const std::size_t end =
            inputSelectionEnd();

        std::string removed =
            inputText_.substr(
                start,
                end - start);


        copyFocusedSelectionToClipboard();
        deleteInputSelection();


        inputEditHistory_.record(
            TextEditHistory::Kind::Cut,
            start,
            std::move(
                removed),
            {},
            before,
            currentInputSelectionState());
    }


    void SdlChatWindow::pasteClipboardText()
    {
        char* clipboardText =
            SDL_GetClipboardText();


        if (clipboardText == nullptr)
        {
            std::cerr
                << "Rose clipboard paste failed: "
                << SDL_GetError()
                << '\n';

            return;
        }


        std::string text{
            clipboardText
        };

        SDL_free(
            clipboardText);


        // A clipboard can contain arbitrarily large text. Bound one paste so a
        // mistaken multi-megabyte copy cannot freeze the composer or explode the
        // model prompt. One MiB is still far above normal chat/log usage.
        constexpr std::size_t maximumPasteBytes{
            1024u * 1024u
        };


        if (text.size() > maximumPasteBytes)
        {
            std::size_t safeEnd =
                maximumPasteBytes;

            while (
                safeEnd > 0
                && safeEnd < text.size()
                && (
                    static_cast<unsigned char>(
                        text[safeEnd])
                    & 0xC0u)
                == 0x80u)
            {
                --safeEnd;
            }


            text.resize(
                safeEnd);

            std::cerr
                << "Rose clipboard paste was truncated to "
                << safeEnd
                << " UTF-8 bytes.\n";
        }


        if (text.empty())
        {
            return;
        }


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();

        insertInputText(
            text,
            TextEditHistory::Kind::Paste);
    }



    void SdlChatWindow::openContextMenu(
        const float x,
        const float y)
    {
        if (!textLayout_.valid)
        {
            closeContextMenu();
            return;
        }


        if (pointInside(
            x,
            y,
            textLayout_.inputAreaX,
            textLayout_.inputAreaY,
            textLayout_.inputAreaWidth,
            textLayout_.inputAreaHeight))
        {
            textFocus_ =
                TextFocus::Composer;

            clearTranscriptSelection();

            const std::size_t clickedOffset =
                inputOffsetForPoint(
                    x,
                    y);


            // Right-clicking inside an existing selection should preserve it so
            // Copy/Cut operate on exactly what the user selected. Right-clicking
            // elsewhere moves the caret and collapses the old selection.
            if (
                !hasInputSelection()
                || clickedOffset < inputSelectionStart()
                || clickedOffset > inputSelectionEnd())
            {
                moveInputCursorTo(
                    clickedOffset,
                    false);
            }
        }
        else if (pointInside(
            x,
            y,
            textLayout_.transcriptAreaX,
            textLayout_.transcriptAreaY,
            textLayout_.transcriptAreaWidth,
            textLayout_.transcriptAreaHeight))
        {
            textFocus_ =
                TextFocus::Transcript;

            clearInputSelection();

            const std::size_t clickedOffset =
                transcriptOffsetForPoint(
                    x,
                    y);


            contextMenuMessageRange_ =
                transcriptMessageRangeAt(
                    clickedOffset);


            if (
                !hasTranscriptSelection()
                || clickedOffset < transcriptSelectionStart()
                || clickedOffset > transcriptSelectionEnd())
            {
                transcriptSelectionAnchorByteOffset_.reset();
                transcriptSelectionCaretByteOffset_ =
                    clickedOffset;
            }
        }
        else
        {
            closeContextMenu();
            return;
        }


        if (textFocus_ == TextFocus::Composer)
        {
            contextMenuMessageRange_.reset();
        }


        contextMenuOpen_ =
            true;

        contextMenuRequestedX_ =
            x;

        contextMenuRequestedY_ =
            y;

        contextMenuHoveredItem_ =
            -1;

        contextMenuLayout_.valid =
            false;
    }


    void SdlChatWindow::closeContextMenu() noexcept
    {
        contextMenuOpen_ =
            false;

        contextMenuHoveredItem_ =
            -1;

        contextMenuLayout_.valid =
            false;

        contextMenuMessageRange_.reset();
    }


    void SdlChatWindow::updateContextMenuHover(
        const float x,
        const float y) noexcept
    {
        contextMenuHoveredItem_ =
            contextMenuItemIndexForPoint(
                x,
                y);
    }


    int SdlChatWindow::contextMenuItemIndexForPoint(
        const float x,
        const float y) const noexcept
    {
        if (
            !contextMenuOpen_
            || !contextMenuLayout_.valid
            || !pointInside(
                x,
                y,
                contextMenuLayout_.x,
                contextMenuLayout_.y,
                contextMenuLayout_.width,
                contextMenuLayout_.itemHeight * 7.0f))
        {
            return -1;
        }


        const float localY =
            y
            - contextMenuLayout_.y;

        const int index =
            static_cast<int>(
                localY
                / contextMenuLayout_.itemHeight);


        if (index < 0 || index >= 7)
        {
            return -1;
        }


        return index;
    }


    bool SdlChatWindow::contextMenuUndoEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
            && inputEditHistory_.canUndo();
    }


    bool SdlChatWindow::contextMenuRedoEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
            && inputEditHistory_.canRedo();
    }


    bool SdlChatWindow::contextMenuCopyEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
                ? hasInputSelection()
                : hasTranscriptSelection();
    }


    bool SdlChatWindow::contextMenuCutEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
            && hasInputSelection();
    }


    bool SdlChatWindow::contextMenuPasteEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
            && SDL_HasClipboardText();
    }


    bool SdlChatWindow::contextMenuSelectAllEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Composer
                ? !inputText_.empty()
                : !displayedTranscriptText_.empty();
    }


    bool SdlChatWindow::contextMenuCopyMessageEnabled() const noexcept
    {
        return
            textFocus_ == TextFocus::Transcript
            && contextMenuMessageRange_.has_value()
            && !contextMenuMessageRange_->empty();
    }


    std::optional<SdlChatWindow::ByteRange>
    SdlChatWindow::transcriptMessageRangeAt(
        std::size_t byteOffset) const noexcept
    {
        if (displayedTranscriptText_.empty())
        {
            return std::nullopt;
        }


        byteOffset =
            (std::min)(
                byteOffset,
                displayedTranscriptText_.size());


        std::size_t cursor{
            0
        };


        for (const std::string& message : transcript_)
        {
            const std::size_t start =
                cursor;

            const std::size_t end =
                start
                + message.size();

            const std::size_t separatorEnd =
                (std::min)(
                    end + std::size_t{ 2 },
                    displayedTranscriptText_.size());


            if (
                byteOffset >= start
                && byteOffset <= separatorEnd)
            {
                return ByteRange{
                    .start = start,
                    .end = end
                };
            }


            cursor =
                separatorEnd;
        }


        if (!streamingAssistantText_.empty())
        {
            const std::size_t end =
                cursor
                + streamingAssistantText_.size();


            if (
                byteOffset >= cursor
                && byteOffset <= end)
            {
                return ByteRange{
                    .start = cursor,
                    .end = end
                };
            }
        }


        // A click just beyond the final glyph should still refer to the final
        // completed message instead of producing a dead context-menu item.
        if (!transcript_.empty())
        {
            const std::string& last =
                transcript_.back();

            const std::size_t end =
                displayedTranscriptText_.size();

            const std::size_t trailingSeparator =
                displayedTranscriptText_.ends_with("\n\n")
                    ? std::size_t{ 2 }
                    : std::size_t{ 0 };

            const std::size_t messageEnd =
                end >= trailingSeparator
                    ? end - trailingSeparator
                    : end;

            const std::size_t messageStart =
                messageEnd >= last.size()
                    ? messageEnd - last.size()
                    : std::size_t{ 0 };

            return ByteRange{
                .start = messageStart,
                .end = messageEnd
            };
        }


        return std::nullopt;
    }


    void SdlChatWindow::copyContextMessageToClipboard()
    {
        if (!contextMenuCopyMessageEnabled())
        {
            return;
        }


        const ByteRange range =
            *contextMenuMessageRange_;


        if (
            range.end > displayedTranscriptText_.size()
            || range.start >= range.end)
        {
            return;
        }


        const std::string text =
            displayedTranscriptText_.substr(
                range.start,
                range.end - range.start);


        if (!text.empty())
        {
            SDL_SetClipboardText(
                text.c_str());
        }
    }


    void SdlChatWindow::activateContextMenuItem(
        const int itemIndex)
    {
        switch (itemIndex)
        {
        case 0:
            if (contextMenuUndoEnabled())
            {
                undoInputEdit();
            }

            break;

        case 1:
            if (contextMenuRedoEnabled())
            {
                redoInputEdit();
            }

            break;

        case 2:
            if (contextMenuCutEnabled())
            {
                cutInputSelectionToClipboard();
            }

            break;

        case 3:
            if (contextMenuCopyEnabled())
            {
                copyFocusedSelectionToClipboard();
            }

            break;

        case 4:
            if (contextMenuPasteEnabled())
            {
                pasteClipboardText();
            }

            break;

        case 5:
            if (contextMenuSelectAllEnabled())
            {
                selectAllFocusedText();
            }

            break;

        case 6:
            if (contextMenuCopyMessageEnabled())
            {
                copyContextMessageToClipboard();
            }

            break;

        default:
            break;
        }


        closeContextMenu();
    }


    void SdlChatWindow::renderContextMenu(
        const int windowWidth,
        const int windowHeight)
    {
        if (!contextMenuOpen_)
        {
            contextMenuLayout_.valid =
                false;

            return;
        }


        constexpr float menuWidth{
            156.0f
        };

        constexpr float itemHeight{
            30.0f
        };

        constexpr float menuHeight{
            itemHeight * 7.0f
        };

        constexpr float textLeftPadding{
            12.0f
        };

        constexpr float textTopPadding{
            4.0f
        };


        const float maximumX =
            (std::max)(
                0.0f,
                static_cast<float>(windowWidth)
                - menuWidth);

        const float maximumY =
            (std::max)(
                0.0f,
                static_cast<float>(windowHeight)
                - menuHeight);


        const float menuX =
            std::clamp(
                contextMenuRequestedX_,
                0.0f,
                maximumX);

        const float menuY =
            std::clamp(
                contextMenuRequestedY_,
                0.0f,
                maximumY);


        contextMenuLayout_ =
            ContextMenuLayout{
                .valid = true,
                .x = menuX,
                .y = menuY,
                .width = menuWidth,
                .itemHeight = itemHeight
            };


        const SDL_FRect menuRect{
            menuX,
            menuY,
            menuWidth,
            menuHeight
        };


        // Menu shadow.
        const SDL_FRect shadowRect{
            menuX + 3.0f,
            menuY + 3.0f,
            menuWidth,
            menuHeight
        };


        SDL_SetRenderDrawColor(
            renderer_.get(),
            12,
            11,
            15,
            180);

        SDL_RenderFillRect(
            renderer_.get(),
            &shadowRect);


        // Menu body.
        SDL_SetRenderDrawColor(
            renderer_.get(),
            43,
            40,
            51,
            255);

        SDL_RenderFillRect(
            renderer_.get(),
            &menuRect);


        TTF_Text* itemTexts[7]{
            contextMenuUndoText_.get(),
            contextMenuRedoText_.get(),
            contextMenuCutText_.get(),
            contextMenuCopyText_.get(),
            contextMenuPasteText_.get(),
            contextMenuSelectAllText_.get(),
            contextMenuCopyMessageText_.get()
        };

        const bool enabled[7]{
            contextMenuUndoEnabled(),
            contextMenuRedoEnabled(),
            contextMenuCutEnabled(),
            contextMenuCopyEnabled(),
            contextMenuPasteEnabled(),
            contextMenuSelectAllEnabled(),
            contextMenuCopyMessageEnabled()
        };


        for (int index = 0; index < 7; ++index)
        {
            const float itemY =
                menuY
                + itemHeight
                * static_cast<float>(index);


            if (
                index == contextMenuHoveredItem_
                && enabled[index])
            {
                const SDL_FRect highlight{
                    menuX + 2.0f,
                    itemY + 2.0f,
                    menuWidth - 4.0f,
                    itemHeight - 4.0f
                };


                SDL_SetRenderDrawColor(
                    renderer_.get(),
                    76,
                    68,
                    99,
                    255);

                SDL_RenderFillRect(
                    renderer_.get(),
                    &highlight);
            }


            const unsigned char channel =
                enabled[index]
                    ? static_cast<unsigned char>(242)
                    : static_cast<unsigned char>(132);


            if (!TTF_SetTextColor(
                itemTexts[index],
                channel,
                channel,
                static_cast<unsigned char>(
                    enabled[index]
                        ? 247
                        : 138),
                255))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not set Rose context-menu text color: "
                    }
                    + SDL_GetError()
                };
            }


            if (!TTF_DrawRendererText(
                itemTexts[index],
                menuX + textLeftPadding,
                itemY + textTopPadding))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not draw Rose context-menu text: "
                    }
                    + SDL_GetError()
                };
            }


            if (index < 6)
            {
                const bool sectionBreak =
                    index == 1;

                SDL_SetRenderDrawColor(
                    renderer_.get(),
                    sectionBreak ? 86 : 60,
                    sectionBreak ? 80 : 57,
                    sectionBreak ? 99 : 69,
                    255);

                SDL_RenderLine(
                    renderer_.get(),
                    menuX + 6.0f,
                    itemY + itemHeight,
                    menuX + menuWidth - 6.0f,
                    itemY + itemHeight);
            }
        }


        SDL_SetRenderDrawColor(
            renderer_.get(),
            104,
            98,
            120,
            255);

        SDL_RenderRect(
            renderer_.get(),
            &menuRect);
    }


    bool SdlChatWindow::pointInside(
        const float x,
        const float y,
        const float left,
        const float top,
        const float width,
        const float height) const noexcept
    {
        return
            x >= left
            && y >= top
            && x < left + width
            && y < top + height;
    }


    std::size_t SdlChatWindow::inputOffsetForPoint(
        const float x,
        const float y)
    {
        if (inputTextDirty_)
        {
            refreshInputText();
        }


        TTF_SubString target{};

        const int localX =
            static_cast<int>(
                x - textLayout_.inputTextX);

        const int localY =
            static_cast<int>(
                y - textLayout_.inputTextY);


        if (!TTF_GetTextSubStringForPoint(
            inputTextObject_.get(),
            localX,
            localY,
            &target))
        {
            return inputCursorByteOffset_;
        }


        std::size_t offset =
            static_cast<std::size_t>(
                (std::max)(
                    target.offset,
                    0));


        // SDL_ttf returns the cluster nearest the point. Choose the leading or
        // trailing edge according to which half of that cluster was clicked.
        if (
            target.length > 0
            && localX
                > target.rect.x
                + target.rect.w / 2)
        {
            offset +=
                static_cast<std::size_t>(
                    target.length);
        }


        return (std::min)(
            offset,
            inputText_.size());
    }


    std::size_t SdlChatWindow::transcriptOffsetForPoint(
        const float x,
        const float y)
    {
        if (transcriptDirty_)
        {
            refreshTranscriptText();
        }


        TTF_SubString target{};

        const int localX =
            static_cast<int>(
                x - textLayout_.transcriptTextX);

        const int localY =
            static_cast<int>(
                y - textLayout_.transcriptTextY);


        if (!TTF_GetTextSubStringForPoint(
            transcriptText_.get(),
            localX,
            localY,
            &target))
        {
            return transcriptSelectionCaretByteOffset_;
        }


        std::size_t offset =
            static_cast<std::size_t>(
                (std::max)(
                    target.offset,
                    0));


        if (
            target.length > 0
            && localX
                > target.rect.x
                + target.rect.w / 2)
        {
            offset +=
                static_cast<std::size_t>(
                    target.length);
        }


        return (std::min)(
            offset,
            displayedTranscriptText_.size());
    }


    void SdlChatWindow::beginMouseSelection(
        const float x,
        const float y)
    {
        inputEditHistory_.breakCoalescing();


        if (!textLayout_.valid)
        {
            return;
        }


        if (pointInside(
            x,
            y,
            textLayout_.inputAreaX,
            textLayout_.inputAreaY,
            textLayout_.inputAreaWidth,
            textLayout_.inputAreaHeight))
        {
            textFocus_ =
                TextFocus::Composer;

            clearTranscriptSelection();

            inputCursorByteOffset_ =
                inputOffsetForPoint(
                    x,
                    y);

            inputSelectionAnchorByteOffset_ =
                inputCursorByteOffset_;

            selectionDrag_ =
                SelectionDrag::Composer;

            resetPreferredCaretX();
            return;
        }


        if (pointInside(
            x,
            y,
            textLayout_.transcriptAreaX,
            textLayout_.transcriptAreaY,
            textLayout_.transcriptAreaWidth,
            textLayout_.transcriptAreaHeight))
        {
            textFocus_ =
                TextFocus::Transcript;

            clearInputSelection();

            transcriptSelectionCaretByteOffset_ =
                transcriptOffsetForPoint(
                    x,
                    y);

            transcriptSelectionAnchorByteOffset_ =
                transcriptSelectionCaretByteOffset_;

            selectionDrag_ =
                SelectionDrag::Transcript;

            return;
        }


        clearInputSelection();
        clearTranscriptSelection();
        selectionDrag_ =
            SelectionDrag::None;
    }



    void SdlChatWindow::selectWordAtPoint(
        const float x,
        const float y)
    {
        inputEditHistory_.breakCoalescing();


        if (!textLayout_.valid)
        {
            return;
        }


        closeContextMenu();
        endMouseSelection();


        if (pointInside(
            x,
            y,
            textLayout_.inputAreaX,
            textLayout_.inputAreaY,
            textLayout_.inputAreaWidth,
            textLayout_.inputAreaHeight))
        {
            textFocus_ =
                TextFocus::Composer;

            clearTranscriptSelection();


            const std::size_t offset =
                inputOffsetForPoint(
                    x,
                    y);

            const ByteRange range =
                wordRangeAt(
                    inputText_,
                    offset);


            if (range.empty())
            {
                clearInputSelection();

                inputCursorByteOffset_ =
                    offset;
            }
            else
            {
                inputSelectionAnchorByteOffset_ =
                    range.start;

                inputCursorByteOffset_ =
                    range.end;
            }


            resetPreferredCaretX();
            return;
        }


        if (pointInside(
            x,
            y,
            textLayout_.transcriptAreaX,
            textLayout_.transcriptAreaY,
            textLayout_.transcriptAreaWidth,
            textLayout_.transcriptAreaHeight))
        {
            textFocus_ =
                TextFocus::Transcript;

            clearInputSelection();


            const std::size_t offset =
                transcriptOffsetForPoint(
                    x,
                    y);

            const ByteRange range =
                wordRangeAt(
                    displayedTranscriptText_,
                    offset);


            if (range.empty())
            {
                clearTranscriptSelection();
                transcriptSelectionCaretByteOffset_ =
                    offset;
            }
            else
            {
                transcriptSelectionAnchorByteOffset_ =
                    range.start;

                transcriptSelectionCaretByteOffset_ =
                    range.end;
            }

            return;
        }


        clearInputSelection();
        clearTranscriptSelection();
    }


    void SdlChatWindow::updateMouseSelection(
        const float x,
        const float y)
    {
        switch (selectionDrag_)
        {
        case SelectionDrag::Composer:
            inputCursorByteOffset_ =
                inputOffsetForPoint(
                    x,
                    y);

            resetPreferredCaretX();
            break;

        case SelectionDrag::Transcript:
            transcriptSelectionCaretByteOffset_ =
                transcriptOffsetForPoint(
                    x,
                    y);
            break;

        case SelectionDrag::None:
            break;
        }
    }


    void SdlChatWindow::endMouseSelection() noexcept
    {
        if (
            selectionDrag_ == SelectionDrag::Composer
            && !hasInputSelection())
        {
            clearInputSelection();
        }
        else if (
            selectionDrag_ == SelectionDrag::Transcript
            && !hasTranscriptSelection())
        {
            clearTranscriptSelection();
        }


        selectionDrag_ =
            SelectionDrag::None;
    }


    void SdlChatWindow::drawSelectionRange(
        TTF_Text* text,
        const std::size_t start,
        const std::size_t end,
        const float originX,
        const float originY)
    {
        if (
            text == nullptr
            || end <= start
            || start
                > static_cast<std::size_t>(
                    (std::numeric_limits<int>::max)())
            || end - start
                > static_cast<std::size_t>(
                    (std::numeric_limits<int>::max)()))
        {
            return;
        }


        int count{ 0 };

        TTF_SubString** ranges =
            TTF_GetTextSubStringsForRange(
                text,
                static_cast<int>(start),
                static_cast<int>(end - start),
                &count);


        if (ranges == nullptr)
        {
            return;
        }


        SDL_SetRenderDrawColor(
            renderer_.get(),
            82,
            74,
            112,
            255);


        for (int index = 0; index < count; ++index)
        {
            const TTF_SubString* range =
                ranges[index];

            if (
                range == nullptr
                || range->rect.w <= 0
                || range->rect.h <= 0)
            {
                continue;
            }


            const SDL_FRect highlight{
                originX
                    + static_cast<float>(
                        range->rect.x),
                originY
                    + static_cast<float>(
                        range->rect.y),
                static_cast<float>(
                    range->rect.w),
                static_cast<float>(
                    range->rect.h)
            };


            SDL_RenderFillRect(
                renderer_.get(),
                &highlight);
        }


        SDL_free(
            ranges);
    }


    std::size_t SdlChatWindow::previousUtf8Boundary(
        const std::size_t offset) const noexcept
    {
        return previousUtf8BoundaryIn(
            inputText_,
            offset);
    }


    std::size_t SdlChatWindow::nextUtf8Boundary(
        const std::size_t offset) const noexcept
    {
        return nextUtf8BoundaryIn(
            inputText_,
            offset);
    }



    std::size_t SdlChatWindow::previousUtf8BoundaryIn(
        const std::string_view text,
        const std::size_t offset) noexcept
    {
        const std::size_t boundedOffset =
            (std::min)(
                offset,
                text.size());


        if (boundedOffset == 0)
        {
            return 0;
        }


        std::size_t position =
            boundedOffset - 1;


        while (
            position > 0
            && (
                static_cast<unsigned char>(
                    text[position])
                & 0xC0u)
            == 0x80u)
        {
            --position;
        }


        return position;
    }


    std::size_t SdlChatWindow::nextUtf8BoundaryIn(
        const std::string_view text,
        const std::size_t offset) noexcept
    {
        const std::size_t boundedOffset =
            (std::min)(
                offset,
                text.size());


        if (boundedOffset >= text.size())
        {
            return text.size();
        }


        std::size_t position =
            boundedOffset + 1;


        while (
            position < text.size()
            && (
                static_cast<unsigned char>(
                    text[position])
                & 0xC0u)
            == 0x80u)
        {
            ++position;
        }


        return position;
    }


    SdlChatWindow::TextRunClass SdlChatWindow::classifyTextRun(
        const std::string_view text,
        const std::size_t offset) noexcept
    {
        if (offset >= text.size())
        {
            return TextRunClass::Punctuation;
        }


        const unsigned char byte =
            static_cast<unsigned char>(
                text[offset]);


        // Non-ASCII UTF-8 code points are treated as word content. We do not need
        // a Unicode database merely to give sensible editor navigation; importantly,
        // their multi-byte sequences are never split because every caller moves only
        // on UTF-8 code-point boundaries.
        if (byte >= 0x80u)
        {
            return TextRunClass::Word;
        }


        if (std::isspace(byte) != 0)
        {
            return TextRunClass::Whitespace;
        }


        if (
            std::isalnum(byte) != 0
            || byte == static_cast<unsigned char>('_'))
        {
            return TextRunClass::Word;
        }


        return TextRunClass::Punctuation;
    }


    SdlChatWindow::ByteRange SdlChatWindow::wordRangeAt(
        const std::string_view text,
        std::size_t offset) noexcept
    {
        if (text.empty())
        {
            return {};
        }


        offset =
            (std::min)(
                offset,
                text.size());


        if (offset == text.size())
        {
            offset =
                previousUtf8BoundaryIn(
                    text,
                    offset);
        }


        const TextRunClass runClass =
            classifyTextRun(
                text,
                offset);


        std::size_t start =
            offset;

        while (start > 0)
        {
            const std::size_t previous =
                previousUtf8BoundaryIn(
                    text,
                    start);

            if (
                classifyTextRun(
                    text,
                    previous)
                != runClass)
            {
                break;
            }

            start =
                previous;
        }


        std::size_t end =
            nextUtf8BoundaryIn(
                text,
                offset);

        while (end < text.size())
        {
            if (
                classifyTextRun(
                    text,
                    end)
                != runClass)
            {
                break;
            }

            end =
                nextUtf8BoundaryIn(
                    text,
                    end);
        }


        return ByteRange{
            .start = start,
            .end = end
        };
    }


    std::size_t SdlChatWindow::previousWordBoundary(
        const std::string_view text,
        std::size_t offset) noexcept
    {
        offset =
            (std::min)(
                offset,
                text.size());


        if (offset == 0)
        {
            return 0;
        }


        // First skip whitespace immediately to the left of the caret.
        while (offset > 0)
        {
            const std::size_t previous =
                previousUtf8BoundaryIn(
                    text,
                    offset);

            if (
                classifyTextRun(
                    text,
                    previous)
                != TextRunClass::Whitespace)
            {
                break;
            }

            offset =
                previous;
        }


        if (offset == 0)
        {
            return 0;
        }


        std::size_t position =
            previousUtf8BoundaryIn(
                text,
                offset);

        const TextRunClass runClass =
            classifyTextRun(
                text,
                position);


        while (position > 0)
        {
            const std::size_t previous =
                previousUtf8BoundaryIn(
                    text,
                    position);

            if (
                classifyTextRun(
                    text,
                    previous)
                != runClass)
            {
                break;
            }

            position =
                previous;
        }


        return position;
    }


    std::size_t SdlChatWindow::nextWordBoundary(
        const std::string_view text,
        std::size_t offset) noexcept
    {
        offset =
            (std::min)(
                offset,
                text.size());


        if (offset >= text.size())
        {
            return text.size();
        }


        const TextRunClass currentClass =
            classifyTextRun(
                text,
                offset);


        // Leave the run containing the caret.
        while (
            offset < text.size()
            && classifyTextRun(
                text,
                offset)
                == currentClass)
        {
            offset =
                nextUtf8BoundaryIn(
                    text,
                    offset);
        }


        // Standard editor behavior is more useful when Ctrl+Right lands on the
        // next visible token rather than the whitespace immediately before it.
        while (
            offset < text.size()
            && classifyTextRun(
                text,
                offset)
                == TextRunClass::Whitespace)
        {
            offset =
                nextUtf8BoundaryIn(
                    text,
                    offset);
        }


        return offset;
    }


    void SdlChatWindow::moveInputCursorWordLeft(
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();


        if (
            !extendSelection
            && hasInputSelection())
        {
            const std::size_t start =
                inputSelectionStart();

            clearInputSelection();

            inputCursorByteOffset_ =
                start;

            resetPreferredCaretX();
            return;
        }


        moveInputCursorTo(
            previousWordBoundary(
                inputText_,
                inputCursorByteOffset_),
            extendSelection);
    }


    void SdlChatWindow::moveInputCursorWordRight(
        const bool extendSelection)
    {
        inputEditHistory_.breakCoalescing();


        textFocus_ =
            TextFocus::Composer;

        clearTranscriptSelection();


        if (
            !extendSelection
            && hasInputSelection())
        {
            const std::size_t end =
                inputSelectionEnd();

            clearInputSelection();

            inputCursorByteOffset_ =
                end;

            resetPreferredCaretX();
            return;
        }


        moveInputCursorTo(
            nextWordBoundary(
                inputText_,
                inputCursorByteOffset_),
            extendSelection);
    }


    void SdlChatWindow::resetPreferredCaretX() noexcept
    {
        preferredCaretX_ =
            -1.0f;
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
        displayedTranscriptText_ =
            buildTranscriptText();


        if (!TTF_SetTextString(
            transcriptText_.get(),
            displayedTranscriptText_.c_str(),
            displayedTranscriptText_.size()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not update Rose transcript text: "
                }
                + SDL_GetError()
            };
        }


        if (transcriptSelectionAnchorByteOffset_.has_value())
        {
            *transcriptSelectionAnchorByteOffset_ =
                (std::min)(
                    *transcriptSelectionAnchorByteOffset_,
                    displayedTranscriptText_.size());
        }

        transcriptSelectionCaretByteOffset_ =
            (std::min)(
                transcriptSelectionCaretByteOffset_,
                displayedTranscriptText_.size());

        if (!hasTranscriptSelection())
        {
            clearTranscriptSelection();
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