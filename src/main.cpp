#ifdef _WIN32
#include <Windows.h>
#endif

#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"
#include "core/RoseCore.h"
#include "logging/Logger.h"
#include "model/LlamaCppModelProvider.h"
#include "model/LlamaLogBridge.h"
#include "persistence/FileConversationStore.h"
#include "platform/SdlRuntime.h"
#include "platform/TtfRuntime.h"
#include "ui/ChatBridge.h"
#include "ui/SdlChatWindow.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>


int main()
{
#ifdef _WIN32
    // -------------------------------------------------------------------------
    // Windows console UTF-8
    // -------------------------------------------------------------------------
    //
    // Rose uses UTF-8 internally. Keep the development console in UTF-8 too so
    // diagnostic output can display Unicode text correctly.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif


    try
    {
        // =====================================================================
        // Application-lifetime infrastructure
        // =====================================================================

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode =
                    rose::logging::LogMode::Normal
            }
        };


        // SDL owns process-level graphics/window initialization.
        //
        // This object must outlive every SDL-backed Rose object.
        rose::platform::SdlRuntime sdlRuntime;


        // SDL_ttf has its own initialization lifetime.
        //
        // Declaration order is intentional:
        //
        //     construct:
        //         SdlRuntime
        //         TtfRuntime
        //
        //     destroy:
        //         TtfRuntime
        //         SdlRuntime
        rose::platform::TtfRuntime ttfRuntime;


        // =====================================================================
        // Avatar
        // =====================================================================

        rose::avatar::SdlAvatar avatar{
            sdlRuntime,
            320,
            300
        };


        avatar.loadSprite(
            "assets/avatar/RoseIdle.png");


        rose::avatar::AvatarController avatarController{
            avatar
        };


        // =====================================================================
        // Graphical conversation UI
        // =====================================================================
        //
        // ChatBridge is the only communication boundary between:
        //
        //     SDL/main thread
        //
        // and
        //
        //     Rose/model worker thread
        //
        // The worker never directly touches SDL objects.

        rose::ui::ChatBridge chatBridge;


        rose::ui::SdlChatWindow chatWindow{
            sdlRuntime,
            ttfRuntime,
            chatBridge,
            "assets/fonts/RoseSans.ttf",
            720,
            520
        };


        // =====================================================================
        // Worker synchronization
        // =====================================================================

        // Published by the Rose worker after all of its worker-owned objects
        // have been destroyed.
        std::atomic<bool> conversationFinished{
            false
        };


        // Exceptions cannot cross std::thread automatically.
        //
        // The worker stores fatal exceptions here and main rethrows them after
        // joining the thread.
        std::exception_ptr workerException;


        avatarController.handleActivity(
            rose::core::RoseActivity::Working);


        std::cout
            << "Rose v0.1\n"
            << "Loading local model...\n";


        // =====================================================================
        // Rose worker
        // =====================================================================
        //
        // OWNERSHIP
        // ---------
        //
        // These objects live entirely on this worker:
        //
        //     LlamaLogBridge
        //     LlamaCppModelProvider
        //     FileConversationStore
        //     RoseCore
        //     Conversation
        //
        // Model inference, conversation mutation, and persistence therefore
        // remain single-thread-owned.
        //
        // The only cross-thread paths are:
        //
        //     ChatBridge
        //     SdlAvatar::setState() through AvatarController
        //
        // SdlAvatar publishes its state atomically and performs actual SDL work
        // only on the main thread.

        std::thread conversationWorker{
            [&logger,
             &avatarController,
             &chatBridge,
             &conversationFinished,
             &workerException]()
            {
                try
                {
                    // ---------------------------------------------------------
                    // llama.cpp diagnostic bridge
                    // ---------------------------------------------------------

                    rose::model::LlamaLogBridge llamaLogBridge{
                        logger
                    };


                    // ---------------------------------------------------------
                    // Model configuration
                    // ---------------------------------------------------------

                    rose::model::LlamaCppConfig modelConfig{
                        .modelPath =
                            "models/Qwen3-8B-Q4_K_M.gguf",

                        .contextSize =
                            8192,

                        // Rose's development machine currently has enough VRAM
                        // for full Qwen3-8B Q4 GPU offload.
                        //
                        // Asking for more layers than the model contains lets
                        // llama.cpp place every available layer on the GPU
                        // without coupling this configuration to Qwen's exact
                        // layer count.
                        .gpuLayers =
                            999
                    };


                    auto modelProvider =
                        std::make_unique<
                            rose::model::LlamaCppModelProvider>(
                                modelConfig,
                                logger);


                    // ---------------------------------------------------------
                    // Persistent conversation journal
                    // ---------------------------------------------------------
                    //
                    // This object remains separate from RoseCore so persistence
                    // can later move to SQLite or another backend without
                    // changing the core conversation architecture.

                    rose::persistence::FileConversationStore
                        conversationStore{
                            "data/conversations/current.rosechat"
                        };


                    rose::core::RoseCore roseCore{
                        std::move(
                            modelProvider),

                        logger,

                        conversationStore
                    };


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);


                    std::cout
                        << "Local model initialized.\n"
                        << "Input is now handled by Rose's SDL chat window.\n"
                        << "Commands: "
                        << "/clear, "
                        << "/log silent|normal|verbose, "
                        << "/quit\n\n";


                    // =========================================================
                    // Conversation loop
                    // =========================================================

                    while (true)
                    {
                        avatarController.handleActivity(
                            rose::core::RoseActivity::Listening);


                        // -----------------------------------------------------
                        // Wait for graphical user input
                        // -----------------------------------------------------
                        //
                        // The condition variable inside ChatBridge sleeps the
                        // worker here without consuming CPU.
                        //
                        // nullopt means shutdown has been requested.

                        std::optional<std::string> pendingInput =
                            chatBridge.waitForUserMessage();


                        if (!pendingInput)
                        {
                            break;
                        }


                        std::string input =
                            std::move(
                                *pendingInput);


                        // Temporary development diagnostic.
                        std::cout
                            << "You: "
                            << input
                            << '\n';


                        // =====================================================
                        // Development logging commands
                        // =====================================================

                        if (input == "/log silent")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Silent);


                            std::cout
                                << "Logging mode: Silent.\n\n";


                            continue;
                        }


                        if (input == "/log normal")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Normal);


                            std::cout
                                << "Logging mode: Normal.\n\n";


                            continue;
                        }


                        if (input == "/log verbose")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Verbose);


                            std::cout
                                << "Logging mode: Verbose.\n\n";


                            continue;
                        }


                        // =====================================================
                        // Application commands
                        // =====================================================

                        if (input == "/quit")
                        {
                            chatBridge.requestShutdown();

                            break;
                        }


                        if (input == "/clear")
                        {
                            roseCore.clearConversation();


                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::
                                            ConversationCleared
                                });


                            continue;
                        }


                        if (input.empty())
                        {
                            continue;
                        }


                        // =====================================================
                        // One user/model request
                        // =====================================================
                        //
                        // This try/catch is deliberately INSIDE the conversation
                        // loop.
                        //
                        // A failure processing one message must not terminate
                        // Rose.
                        //
                        // Examples of recoverable request failures:
                        //
                        //     context overflow
                        //     oversized user input
                        //     inference request failure
                        //     persistence write failure
                        //
                        // Fatal worker initialization failures are still caught
                        // by the outer worker try/catch.

                        try
                        {
                            bool responseStarted{
                                false
                            };


                            // -------------------------------------------------
                            // Rose activity -> avatar
                            // -------------------------------------------------

                            const rose::core::RoseActivityCallback
                                onActivity =
                                [&avatarController](
                                    const rose::core::RoseActivity
                                        activity)
                                {
                                    avatarController.handleActivity(
                                        activity);
                                };


                            // -------------------------------------------------
                            // Model streaming -> graphical chat
                            // -------------------------------------------------
                            //
                            // ModelTextCallback only lends us its string_view
                            // during this callback.
                            //
                            // ChatBridge owns events after this function
                            // returns, so streamed text is copied into an owned
                            // std::string before crossing the thread boundary.

                            const rose::model::ModelTextCallback
                                onText =
                                [&chatBridge,
                                 &responseStarted](
                                    const std::string_view text)
                                {
                                    if (text.empty())
                                    {
                                        return;
                                    }


                                    if (!responseStarted)
                                    {
                                        chatBridge.postEvent(
                                            rose::ui::ChatEvent{
                                                .type =
                                                    rose::ui::
                                                    ChatEventType::
                                                    AssistantStarted
                                            });


                                        responseStarted =
                                            true;
                                    }


                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::
                                                ChatEventType::
                                                AssistantText,

                                            .text =
                                                std::string{
                                                    text
                                                }
                                        });
                                };


                            // -------------------------------------------------
                            // Generate exactly ONE response for this input
                            // -------------------------------------------------

                            const rose::model::ModelResponse response =
                                roseCore.processMessage(
                                    input,
                                    onText,
                                    onActivity);


                            // -------------------------------------------------
                            // Defensive non-streaming fallback
                            // -------------------------------------------------
                            //
                            // LlamaCppModelProvider currently streams.
                            //
                            // A future provider may implement generate() only
                            // and therefore return the whole response at once.
                            //
                            // The frontend should work in either case.

                            if (!responseStarted)
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::
                                            ChatEventType::
                                            AssistantStarted
                                    });


                                if (!response.text.empty())
                                {
                                    chatBridge.postEvent(
                                        rose::ui::ChatEvent{
                                            .type =
                                                rose::ui::
                                                ChatEventType::
                                                AssistantText,

                                            .text =
                                                response.text
                                        });
                                }
                            }


                            // Tell the frontend this response has completely
                            // finished streaming.
                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::
                                        ChatEventType::
                                        AssistantFinished
                                });


                            std::cout
                                << "\n\n"
                                << "[Generated tokens: "
                                << response.generatedTokens
                                << "]\n"
                                << "[Finish reason: "
                                << (
                                    response.finishReason
                                    == rose::model::ModelFinishReason::TokenLimit
                                    ? "Token limit"
                                    : "End of generation")
                                << "]\n\n";
                        }
                        catch (const std::exception& exception)
                        {
                            // ---------------------------------------------
                            // Recoverable request failure
                            // ---------------------------------------------
                            //
                            // RoseCore may already have published Confused,
                            // but explicitly setting it here also covers
                            // failures originating outside RoseCore.

                            avatarController.handleActivity(
                                rose::core::RoseActivity::Confused);


                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::Error,

                                    .text =
                                        std::string{
                                            "Rose could not complete "
                                            "that request: "
                                        }
                                        + exception.what()
                                });


                            // Do NOT break.
                            //
                            // Rose returns to the top of the loop and accepts
                            // another user message.
                        }
                    }


                    // Normal shutdown leaves Rose in a neutral state while the
                    // worker-owned objects unwind.
                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);
                }
                catch (...)
                {
                    // =========================================================
                    // Fatal worker failure
                    // =========================================================
                    //
                    // This path is reserved for failures outside an individual
                    // user request, such as model initialization or persistent
                    // conversation restoration.

                    workerException =
                        std::current_exception();


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Confused);


                    chatBridge.postEvent(
                        rose::ui::ChatEvent{
                            .type =
                                rose::ui::ChatEventType::Error,

                            .text =
                                "Rose's conversation worker "
                                "stopped unexpectedly."
                        });
                }


                // Always publish worker completion, whether it exited normally
                // or because of a fatal exception.
                conversationFinished.store(
                    true,
                    std::memory_order_release);
            }
        };


        // =====================================================================
        // SDL main thread
        // =====================================================================
        //
        // This is the ONLY thread that consumes SDL's event queue or renders
        // SDL windows.

        bool avatarWindowOpen{
            true
        };


        bool chatWindowOpen{
            true
        };


        while (
            !conversationFinished.load(
                std::memory_order_acquire))
        {
            // -----------------------------------------------------------------
            // Central SDL event pump
            // -----------------------------------------------------------------

            SDL_Event event{};


            while (SDL_PollEvent(
                &event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    // Wake the worker if it is blocked waiting for input.
                    chatBridge.requestShutdown();


                    avatarWindowOpen =
                        false;

                    chatWindowOpen =
                        false;


                    continue;
                }


                if (avatarWindowOpen)
                {
                    avatarWindowOpen =
                        avatar.handleEvent(
                            event);
                }


                if (chatWindowOpen)
                {
                    const bool stillOpen =
                        chatWindow.handleEvent(
                            event);


                    if (!stillOpen)
                    {
                        chatWindowOpen =
                            false;


                        // The graphical chat is still our primary development
                        // UI. Closing it currently means shutting Rose down.
                        //
                        // Later Rose herself becomes the primary interface and
                        // closing a chat surface will no longer necessarily
                        // terminate the application.
                        chatBridge.requestShutdown();
                    }
                }
            }


            // -----------------------------------------------------------------
            // UI state updates
            // -----------------------------------------------------------------

            if (chatWindowOpen)
            {
                chatWindow.update();
            }


            // -----------------------------------------------------------------
            // Rendering
            // -----------------------------------------------------------------

            if (avatarWindowOpen)
            {
                avatar.render();
            }


            if (chatWindowOpen)
            {
                chatWindow.render();
            }


            // Approximately 60 Hz presentation loop.
            //
            // Model inference is on the worker, so sleeping here does not slow
            // generation.
            std::this_thread::sleep_for(
                std::chrono::milliseconds{
                    16
                });
        }


        // =====================================================================
        // Deterministic shutdown
        // =====================================================================

        if (conversationWorker.joinable())
        {
            conversationWorker.join();
        }


        // Only actual worker-level failures reach the application's fatal
        // exception handler.
        if (workerException)
        {
            std::rethrow_exception(
                workerException);
        }


        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Fatal Rose error: "
            << exception.what()
            << '\n';


        return 1;
    }
}