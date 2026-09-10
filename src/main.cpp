#ifdef _WIN32
#include <Windows.h>
#endif

#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"
#include "core/RoseCore.h"
#include "logging/Logger.h"
#include "model/LlamaCppModelProvider.h"
#include "model/LlamaLogBridge.h"
#include "platform/SdlRuntime.h"
#include "ui/ChatBridge.h"
#include "ui/SdlChatWindow.h"
#include "platform/TtfRuntime.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <optional>


int main()
{
#ifdef _WIN32
    // -------------------------------------------------------------------------
    // Windows console UTF-8
    // -------------------------------------------------------------------------
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
                .mode = rose::logging::LogMode::Normal
            }
        };


        // SDL itself owns the graphics/window subsystem lifetime.
        rose::platform::SdlRuntime sdlRuntime;


        // SDL_ttf is a separate library with its own initialization reference.
        //
        // Declaration order is intentional:
        //
        //     construct: SdlRuntime -> TtfRuntime
        //     destroy:   TtfRuntime -> SdlRuntime
        rose::platform::TtfRuntime ttfRuntime;


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

        // -------------------------------------------------------------------------
        // Graphical conversation bridge
        // -------------------------------------------------------------------------
        //
        // ChatBridge is the thread boundary:
        //
        //     SDL/main thread
        //         |
        //         | submitUserMessage()
        //         v
        //     ChatBridge
        //         |
        //         v
        //     Rose worker
        //
        // The bridge outlives the worker because it is owned here in main().
        rose::ui::ChatBridge chatBridge;


        rose::ui::SdlChatWindow chatWindow{
            sdlRuntime,
            ttfRuntime,
            chatBridge,
            "assets/fonts/RoseSans.ttf",
            720,
            520
        };

        // Published by the worker when its RoseCore/model lifetime has ended.
        std::atomic<bool> conversationFinished{
            false
        };


        // Worker exceptions cannot naturally cross std::thread boundaries.
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
        // RoseCore, Conversation, and llama.cpp stay entirely on this thread.
        //
        // The only cross-thread avatar operation is setState(), which is an
        // atomic publication inside SdlAvatar.
        std::thread conversationWorker{
            [&logger,
             &avatarController,
             &chatBridge,
             &conversationFinished,
             &workerException]()
            {
                try
                {
                    rose::model::LlamaLogBridge llamaLogBridge{
                        logger
                    };


                    rose::model::LlamaCppConfig modelConfig{
                        .modelPath =
                            "models/Qwen3-8B-Q4_K_M.gguf",

                        .contextSize = 4096,

                        // Request more GPU layers than this model can possibly contain.
                        //
                        // llama.cpp will therefore place all available model layers on the GPU.
                        // Using a deliberately large value avoids coupling Rose to the exact layer
                        // count of Qwen3-8B and also works when we swap models later.
                        .gpuLayers = 999
                    };


                    auto modelProvider =
                        std::make_unique<
                            rose::model::LlamaCppModelProvider>(
                                modelConfig,
                                logger);


                    rose::core::RoseCore roseCore{
                        std::move(modelProvider),
                        logger
                    };


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);


                    std::cout
                        << "Local model initialized.\n"
                        << "Input is now handled by Rose's SDL chat window.\n"
                        << "Commands: /clear, /log silent|normal|verbose, /quit\n\n";




                    while (true)
                    {
                        avatarController.handleActivity(
                            rose::core::RoseActivity::Listening);


                        // ---------------------------------------------------------------------
                        // Wait for graphical user input
                        // ---------------------------------------------------------------------
                        //
                        // The Rose worker sleeps here without consuming CPU until the SDL/UI
                        // thread submits a message.
                        //
                        // ChatBridge's condition_variable wakes us when either:
                        //
                        //     - a user message arrives
                        //     - shutdown is requested
                        //
                        // RoseCore itself remains entirely owned by this worker thread.
                        std::optional<std::string> pendingInput =
                            chatBridge.waitForUserMessage();


                        if (!pendingInput)
                        {
                            // nullopt means the UI requested shutdown.
                            break;
                        }


                        std::string input =
                            std::move(
                                *pendingInput);


                        // Keep this temporarily so we can verify exactly what the GUI delivered.
                        std::cout
                            << "You: "
                            << input
                            << '\n';

                        // -----------------------------------------------------
                        // Logging commands
                        // -----------------------------------------------------

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


                        // -----------------------------------------------------
                        // Application commands
                        // -----------------------------------------------------

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
                                        rose::ui::ChatEventType::ConversationCleared
                                });


                            continue;
                        }


                        if (input.empty())
                        {
                            continue;
                        }


                        // -----------------------------------------------------
                        // Rose activity -> avatar
                        // -----------------------------------------------------

                        const rose::core::RoseActivityCallback onActivity =
                            [&avatarController](
                                const rose::core::RoseActivity activity)
                            {
                                avatarController.handleActivity(
                                    activity);
                            };


                        // -----------------------------------------------------
                        // Streaming console output
                        // -----------------------------------------------------

                        // -------------------------------------------------------------------------
                        // Stream Rose -> graphical UI
                        // -------------------------------------------------------------------------
                        //
                        // The model worker never touches SDL.
                        //
                        // Instead, generated text crosses the thread boundary as owned ChatEvents:
                        //
                        //     model worker
                        //         |
                        //         | ChatEvent
                        //         v
                        //     ChatBridge
                        //         |
                        //         v
                        //     SDL/main thread
                        //
                        // SdlChatWindow::update() consumes those events and updates presentation
                        // state before render().

                        bool responseStarted{
                            false
                        };


                        const rose::model::ModelTextCallback onText =
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
                                                rose::ui::ChatEventType::AssistantStarted
                                        });


                                    responseStarted = true;
                                }


                                // ModelTextCallback only lends us the string_view for this invocation.
                                //
                                // ChatBridge must own the text after this callback returns, so create
                                // an owned std::string here.
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::AssistantText,

                                        .text =
                                            std::string{
                                                text
                                            }
                                    });
                            };


                        const rose::model::ModelResponse response =
                            roseCore.processMessage(
                                input,
                                onText,
                                onActivity);

                        // -------------------------------------------------------------------------
                        // Defensive non-streaming fallback
                        // -------------------------------------------------------------------------
                        //
                        // Normally our llama.cpp provider streams visible text incrementally.
                        //
                        // A future provider might return only the final ModelResponse. In that case,
                        // still make sure the graphical frontend receives the response.
                        if (!responseStarted)
                        {
                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::AssistantStarted
                                });


                            if (!response.text.empty())
                            {
                                chatBridge.postEvent(
                                    rose::ui::ChatEvent{
                                        .type =
                                            rose::ui::ChatEventType::AssistantText,

                                        .text =
                                            response.text
                                    });
                            }
                        }


                        // AssistantFinished tells the UI that streaming for THIS response is done.
                        //
                        // SdlChatWindow can then move its temporary streaming string into the
                        // permanent transcript.
                        chatBridge.postEvent(
                            rose::ui::ChatEvent{
                                .type =
                                    rose::ui::ChatEventType::AssistantFinished
                            });

                        std::cout
                            << "\n\n"
                            << "[Generated tokens: "
                            << response.generatedTokens
                            << "]\n\n";
                    }


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);
                }
                catch (...)
                {
                    workerException =
                        std::current_exception();


                    avatarController.handleActivity(
                        rose::core::RoseActivity::Confused);
                }

                chatBridge.postEvent(
                    rose::ui::ChatEvent{
                        .type =
                            rose::ui::ChatEventType::Error,

                        .text =
                            "Rose's conversation worker stopped unexpectedly."
                    });

                conversationFinished.store(
                    true,
                    std::memory_order_release);
            }
        };


        // =====================================================================
        // SDL main thread
        // =====================================================================

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
            // ---------------------------------------------------------------------
            // Central SDL event pump
            // ---------------------------------------------------------------------
            //
            // main() is the ONLY place that consumes SDL's process-wide event queue.
            SDL_Event event{};


            while (SDL_PollEvent(
                &event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    // Wake the Rose worker if it is currently waiting for input.
                    chatBridge.requestShutdown();

                    avatarWindowOpen = false;
                    chatWindowOpen = false;

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
                        chatWindowOpen = false;


                        // The chat window is Rose's primary application UI now.
                        //
                        // Closing it requests application shutdown. This immediately
                        // wakes the worker if it is waiting for another message.
                        chatBridge.requestShutdown();
                    }
                }
            }


            // ---------------------------------------------------------------------
            // UI state update
            // ---------------------------------------------------------------------
            //
            // There are no Rose -> GUI chat events yet, but calling update() now makes
            // the main loop structurally correct for Checkpoint 3.
            if (chatWindowOpen)
            {
                chatWindow.update();
            }


            // ---------------------------------------------------------------------
            // Rendering
            // ---------------------------------------------------------------------

            if (avatarWindowOpen)
            {
                avatar.render();
            }


            if (chatWindowOpen)
            {
                chatWindow.render();
            }


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