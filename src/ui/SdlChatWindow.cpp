#include "ui/SdlChatWindow.h"
#include "ui/RichTranscript.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
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


        attachmentTextObject_.reset(
            TTF_CreateText(
                textEngine_.get(),
                font_.get(),
                "",
                0));

        if (!attachmentTextObject_)
        {
            throw std::runtime_error{
                std::string{
                    "Could not create Rose attachment text: "
                }
                + SDL_GetError()
            };
        }

        if (!TTF_SetTextColor(
            attachmentTextObject_.get(),
            194,
            190,
            210,
            255))
        {
            throw std::runtime_error{
                std::string{
                    "Could not set Rose attachment text color: "
                }
                + SDL_GetError()
            };
        }

        // -------------------------------------------------------------------------
        // Rich transcript renderer
        // -------------------------------------------------------------------------
        //
        // Completed responses are parsed and laid out only for presentation. The
        // model/conversation/persistence layers continue to own the canonical text.
        transcript_ =
            std::make_unique<RichTranscript>(
                *renderer_,
                *textEngine_,
                absoluteFontPath,
                std::filesystem::path{
                    "external/MicroTex/res"
                });

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
                const bool controlDown =
                    (event.key.mod & SDL_KMOD_CTRL) != 0;

                const bool shiftDown =
                    (event.key.mod & SDL_KMOD_SHIFT) != 0;

                if (controlDown)
                {
                    if (event.key.key == SDLK_V)
                    {
                        pasteClipboardIntoComposer();
                        break;
                    }

                    if (event.key.key == SDLK_C)
                    {
                        if (shiftDown)
                        {
                            copyTranscriptToClipboard();
                        }
                        else
                        {
                            copyComposerToClipboard();
                        }

                        break;
                    }

                    if (event.key.key == SDLK_X)
                    {
                        cutComposerToClipboard();
                        break;
                    }

                    if (event.key.key == SDLK_BACKSPACE)
                    {
                        removeLastPendingAttachment();
                        break;
                    }
                }
            }


            switch (event.key.key)
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                // Enter sends. Shift+Enter inserts a real newline into the canonical
                // input buffer. Visual word wrapping itself never modifies inputText_.
                if ((event.key.mod & SDL_KMOD_SHIFT) != 0)
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
                moveInputCursorLeft();
                break;


            case SDLK_RIGHT:
                moveInputCursorRight();
                break;


            case SDLK_UP:
                moveInputCursorVertical(-1);
                break;


            case SDLK_DOWN:
                moveInputCursorVertical(1);
                break;


            case SDLK_HOME:
                inputCursorByteOffset_ = 0;
                resetPreferredCaretX();
                break;


            case SDLK_END:
                inputCursorByteOffset_ =
                    inputText_.size();

                resetPreferredCaretX();
                break;


            case SDLK_ESCAPE:
                return false;


            default:
                break;
            }

            break;


        case SDL_EVENT_DROP_FILE:
            // The central SDL pump sends every event to SdlChatWindow. Accept file
            // drops from either Rose window so the user can drop a file on the chat
            // surface OR directly on Rose's avatar.
            if (event.drop.data != nullptr)
            {
                // SDL owns event.drop.data. Copy it immediately into Rose-owned
                // storage before this event leaves the central pump.
                addDroppedFile(
                    event.drop.data);
            }

            break;


        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.windowID != windowId)
            {
                break;
            }

            if (
                event.button.button == SDL_BUTTON_LEFT
                || event.button.button == SDL_BUTTON_RIGHT)
            {
                const bool revealFolder =
                    event.button.button == SDL_BUTTON_RIGHT;

                (void) transcript_->handlePointerDown(
                    event.button.x,
                    event.button.y,
                    revealFolder);
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


                transcript_->scrollBy(
                    -wheelY
                    * scrollPixelsPerUnit);
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


        if (inputTextDirty_)
        {
            refreshInputText();
        }

        if (attachmentTextDirty_)
        {
            refreshAttachmentText();
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


        if (inputWrapWidth != attachmentWrapWidth_)
        {
            if (!TTF_SetTextWrapWidth(
                attachmentTextObject_.get(),
                inputWrapWidth))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not update Rose attachment wrapping: "
                    }
                    + SDL_GetError()
                };
            }

            attachmentWrapWidth_ =
                inputWrapWidth;
        }


        int attachmentTextWidth{ 0 };
        int attachmentTextHeight{ 0 };

        if (!TTF_GetTextSize(
            attachmentTextObject_.get(),
            &attachmentTextWidth,
            &attachmentTextHeight))
        {
            throw std::runtime_error{
                std::string{
                    "Could not measure Rose attachment text: "
                }
                + SDL_GetError()
            };
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


        constexpr int maximumVisibleAttachmentLines{ 2 };
        constexpr float attachmentGap{ 6.0f };

        const int visibleAttachmentTextHeight =
            pendingAttachments_.empty()
                ? 0
                : std::min(
                    std::max(
                        lineHeight,
                        attachmentTextHeight),
                    lineHeight
                        * maximumVisibleAttachmentLines);

        const float attachmentReservedHeight =
            pendingAttachments_.empty()
                ? 0.0f
                : static_cast<float>(
                    visibleAttachmentTextHeight)
                    + attachmentGap;

        const float inputAreaHeight =
            inputTextTopPadding
            + attachmentReservedHeight
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


        const float attachmentTextY =
            inputArea.y
            + inputTextTopPadding;

        const float inputViewportTop =
            inputArea.y
            + inputTextTopPadding
            + attachmentReservedHeight;


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
        // Rich transcript layout + rendering
        // =====================================================================
        //
        // RichTranscript owns width-dependent response layouts and scrolling.
        // Composer geometry remains independent and continues to determine the
        // vertical viewport available above it.
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


        const int transcriptWidth =
            std::max(
                1,
                width
                - transcriptLeft
                - transcriptRightPadding);


        const int transcriptHeight =
            std::max(
                1,
                static_cast<int>(
                    inputArea.y)
                - transcriptComposerGap
                - transcriptTop);


        transcript_->render(
            transcriptLeft,
            transcriptTop,
            transcriptWidth,
            transcriptHeight);


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


        if (!pendingAttachments_.empty())
        {
            const SDL_Rect attachmentClip{
                static_cast<int>(
                    inputArea.x
                    + inputTextLeftPadding),
                static_cast<int>(attachmentTextY),
                inputWrapWidth,
                visibleAttachmentTextHeight
            };

            if (!SDL_SetRenderClipRect(
                renderer_.get(),
                &attachmentClip))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not set Rose attachment clip rectangle: "
                    }
                    + SDL_GetError()
                };
            }

            if (!TTF_DrawRendererText(
                attachmentTextObject_.get(),
                inputArea.x
                    + inputTextLeftPadding,
                attachmentTextY))
            {
                throw std::runtime_error{
                    std::string{
                        "Could not draw Rose attachment text: "
                    }
                    + SDL_GetError()
                };
            }
        }


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


        SDL_SetRenderDrawColor(
            renderer_.get(),
            245,
            245,
            248,
            255);


        SDL_RenderFillRect(
            renderer_.get(),
            &caret);


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
        if (
            inputText_.empty()
            && pendingAttachments_.empty())
        {
            return;
        }

        input::UserSubmission submission{
            .text = std::move(inputText_),
            .attachments = std::move(pendingAttachments_)
        };

        inputText_.clear();
        pendingAttachments_.clear();

        inputCursorByteOffset_ = 0;
        inputScrollOffsetY_ = 0.0f;
        resetPreferredCaretX();

        inputTextDirty_ = true;
        attachmentTextDirty_ = true;

        transcript_->appendUserMessage(
            userTranscriptText(submission));

        chatBridge_.submitUserSubmission(
            std::move(submission));
    }


    void SdlChatWindow::handleChatEvent(
        ChatEvent event)
    {
        switch (event.type)
        {
        case ChatEventType::AssistantStarted:
            transcript_->startAssistantResponse();
            break;


        case ChatEventType::AssistantText:
            transcript_->appendAssistantText(
                event.text);
            break;


        case ChatEventType::AssistantFinished:
            // AssistantFinished carries RoseCore's authoritative final visible text.
            // RichTranscript parses only this completed response; streaming chunks
            // remain literal so partial Markdown/LaTeX cannot corrupt presentation.
            transcript_->finishAssistantResponse(
                event.text);
            break;


        case ChatEventType::ArtifactReady:
            if (event.artifact.has_value())
            {
                transcript_->appendArtifact(
                    std::move(*event.artifact));
            }
            break;


        case ChatEventType::ConversationCleared:
            transcript_->clear();
            break;


        case ChatEventType::Error:
            transcript_->appendError(
                event.text);
            break;
        }
    }


    void SdlChatWindow::pasteClipboardIntoComposer()
    {
        char* clipboardText =
            SDL_GetClipboardText();

        if (clipboardText == nullptr)
        {
            return;
        }

        if (*clipboardText != '\0')
        {
            insertInputText(
                clipboardText);
        }

        SDL_free(
            clipboardText);
    }


    void SdlChatWindow::copyComposerToClipboard() const
    {
        if (inputText_.empty())
        {
            // With no draft text, Ctrl+C behaves as a convenient whole-chat copy.
            copyTranscriptToClipboard();
            return;
        }

        SDL_SetClipboardText(
            inputText_.c_str());
    }


    void SdlChatWindow::cutComposerToClipboard()
    {
        if (inputText_.empty())
        {
            return;
        }

        if (!SDL_SetClipboardText(
            inputText_.c_str()))
        {
            return;
        }

        inputText_.clear();
        inputCursorByteOffset_ = 0;
        inputScrollOffsetY_ = 0.0f;
        resetPreferredCaretX();
        inputTextDirty_ = true;
    }


    void SdlChatWindow::copyTranscriptToClipboard() const
    {
        const std::string transcriptText =
            transcript_->copyableText();

        if (transcriptText.empty())
        {
            return;
        }

        SDL_SetClipboardText(
            transcriptText.c_str());
    }


    void SdlChatWindow::addDroppedFile(
        const std::string_view pathText)
    {
        if (pathText.empty())
        {
            return;
        }

        constexpr std::size_t maximumPendingAttachments{ 16 };

        if (pendingAttachments_.size() >= maximumPendingAttachments)
        {
            transcript_->appendError(
                "Rose currently accepts at most 16 files in one submission.");
            return;
        }

        const std::filesystem::path path{
            std::string{ pathText }
        };

        const std::filesystem::path normalized =
            path.lexically_normal();

        const auto duplicate =
            std::find_if(
                pendingAttachments_.begin(),
                pendingAttachments_.end(),
                [&normalized](const input::FileAttachment& existing)
                {
                    return existing.path.lexically_normal()
                        == normalized;
                });

        if (duplicate != pendingAttachments_.end())
        {
            return;
        }

        std::string displayName =
            path.filename().string();

        if (displayName.empty())
        {
            displayName = path.string();
        }

        pendingAttachments_.push_back(
            input::FileAttachment{
                .path = path,
                .displayName = std::move(displayName)
            });

        attachmentTextDirty_ = true;
    }


    void SdlChatWindow::removeLastPendingAttachment()
    {
        if (pendingAttachments_.empty())
        {
            return;
        }

        pendingAttachments_.pop_back();
        attachmentTextDirty_ = true;
    }


    std::string SdlChatWindow::attachmentSummaryText() const
    {
        if (pendingAttachments_.empty())
        {
            return {};
        }

        std::string result{
            "Attachments: "
        };

        for (std::size_t index = 0;
             index < pendingAttachments_.size();
             ++index)
        {
            if (index != 0)
            {
                result += "  |  ";
            }

            result += pendingAttachments_[index].displayName;
        }

        return result;
    }


    std::string SdlChatWindow::userTranscriptText(
        const input::UserSubmission& submission) const
    {
        std::string result =
            submission.text.empty()
                ? std::string{ "[Attached files]" }
                : submission.text;

        if (!submission.attachments.empty())
        {
            result += "\nAttachments: ";

            for (std::size_t index = 0;
                 index < submission.attachments.size();
                 ++index)
            {
                if (index != 0)
                {
                    result += ", ";
                }

                result += submission.attachments[index].displayName;
            }
        }

        return result;
    }


    void SdlChatWindow::insertInputText(
        const std::string_view text)
    {
        if (text.empty())
        {
            return;
        }


        inputText_.insert(
            inputCursorByteOffset_,
            text.data(),
            text.size());


        inputCursorByteOffset_ +=
            text.size();


        resetPreferredCaretX();

        inputTextDirty_ = true;
    }


    void SdlChatWindow::erasePreviousUtf8CodePoint()
    {
        if (inputCursorByteOffset_ == 0)
        {
            return;
        }


        const std::size_t previous =
            previousUtf8Boundary(
                inputCursorByteOffset_);


        inputText_.erase(
            previous,
            inputCursorByteOffset_
                - previous);


        inputCursorByteOffset_ =
            previous;


        resetPreferredCaretX();

        inputTextDirty_ = true;
    }


    void SdlChatWindow::eraseNextUtf8CodePoint()
    {
        if (
            inputCursorByteOffset_
            >= inputText_.size())
        {
            return;
        }


        const std::size_t next =
            nextUtf8Boundary(
                inputCursorByteOffset_);


        inputText_.erase(
            inputCursorByteOffset_,
            next
                - inputCursorByteOffset_);


        resetPreferredCaretX();

        inputTextDirty_ = true;
    }


    void SdlChatWindow::moveInputCursorLeft()
    {
        inputCursorByteOffset_ =
            previousUtf8Boundary(
                inputCursorByteOffset_);


        resetPreferredCaretX();
    }


    void SdlChatWindow::moveInputCursorRight()
    {
        inputCursorByteOffset_ =
            nextUtf8Boundary(
                inputCursorByteOffset_);


        resetPreferredCaretX();
    }


    void SdlChatWindow::moveInputCursorVertical(
        const int direction)
    {
        if (direction == 0)
        {
            return;
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


    std::size_t SdlChatWindow::previousUtf8Boundary(
        const std::size_t offset) const noexcept
    {
        const std::size_t boundedOffset =
            std::min(
                offset,
                inputText_.size());


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
                    inputText_[position])
                & 0xC0u)
            == 0x80u)
        {
            --position;
        }


        return position;
    }


    std::size_t SdlChatWindow::nextUtf8Boundary(
        const std::size_t offset) const noexcept
    {
        const std::size_t boundedOffset =
            std::min(
                offset,
                inputText_.size());


        if (boundedOffset >= inputText_.size())
        {
            return inputText_.size();
        }


        std::size_t position =
            boundedOffset + 1;


        while (
            position < inputText_.size()
            && (
                static_cast<unsigned char>(
                    inputText_[position])
                & 0xC0u)
            == 0x80u)
        {
            ++position;
        }


        return position;
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


    void SdlChatWindow::refreshAttachmentText()
    {
        const std::string summary =
            attachmentSummaryText();

        if (!TTF_SetTextString(
            attachmentTextObject_.get(),
            summary.c_str(),
            summary.size()))
        {
            throw std::runtime_error{
                std::string{
                    "Could not update Rose attachment text: "
                }
                + SDL_GetError()
            };
        }

        attachmentTextDirty_ = false;
    }

} // namespace rose::ui