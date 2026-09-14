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
#include "permissions/PermissionSystem.h"
#include "tools/ReadFileTool.h"
#include "tools/AttachmentIngestion.h"
#include "ocr/TesseractCliOcrEngine.h"
#include "vision/LlamaMtmdVisionProvider.h"

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

                    rose::permissions::PermissionSystem permissionSystem;

                    rose::tools::ReadFileTool readFileTool{
                        permissionSystem
                    };


                    auto ocrEngine =
                        std::make_unique<
                        rose::ocr::TesseractCliOcrEngine>();


                    auto visionProvider =
                        std::make_unique<
                        rose::vision::LlamaMtmdVisionProvider>(
                            rose::vision::LlamaMtmdVisionConfig{
                                .modelPath =
                                    "models/vision/Qwen3VL-8B-Instruct-Q4_K_M.gguf",

                                .mmprojPath =
                                    "models/vision/mmproj-Qwen3VL-8B-Instruct-Q8_0.gguf",

                                .contextSize =
                                    4096,

                                .maxGeneratedTokens =
                                    512,

                                .gpuLayers =
                                    999,

                                .mmprojUseGpu =
                                    true,

                                .imageMaxTokens =
                                    1536
                            });


                    rose::tools::AttachmentIngestion attachmentIngestion{
                        permissionSystem,
                        readFileTool,
                        std::move(ocrEngine),
                        std::move(visionProvider)
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

                        std::optional<rose::input::UserSubmission> pendingSubmission =
                            chatBridge.waitForUserSubmission();

                        if (!pendingSubmission)
                        {
                            break;
                        }

                        rose::input::UserSubmission submission =
                            std::move(*pendingSubmission);

                        // -----------------------------------------------------
                        // Submission metadata
                        // -----------------------------------------------------
                        //
                        // At this point we have not ingested the files yet.
                        //
                        // submission.text is still the user's canonical text.
                        // submission.attachments contains only paths selected/dropped
                        // for this one request.

                        const bool commandEligible =
                            submission.attachments.empty();

                        const std::string& submittedText =
                            submission.text;


                        // Temporary development diagnostic.
                        //
                        // Do not use `input` here because the model-ready input does
                        // not exist until after AttachmentIngestion below.
                        std::cout
                            << "You: "
                            << submittedText;

                        if (!submission.attachments.empty())
                        {
                            std::cout
                                << " ["
                                << submission.attachments.size()
                                << " attachment";

                            if (submission.attachments.size() != 1)
                            {
                                std::cout << 's';
                            }

                            std::cout << ']';
                        }

                        std::cout << '\n';


                        // =====================================================
                        // Development logging commands
                        // =====================================================

                        if (
                            commandEligible
                            && submittedText == "/log silent")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Silent);

                            std::cout
                                << "Logging mode: Silent.\n\n";

                            continue;
                        }


                        if (
                            commandEligible
                            && submittedText == "/log normal")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Normal);

                            std::cout
                                << "Logging mode: Normal.\n\n";

                            continue;
                        }


                        if (
                            commandEligible
                            && submittedText == "/log verbose")
                        {
                            logger.setMode(
                                rose::logging::LogMode::Verbose);

                            std::cout
                                << "Logging mode: Verbose.\n\n";

                            continue;
                        }


                        if (
                            commandEligible
                            && submittedText == "/quit")
                        {
                            chatBridge.requestShutdown();
                            break;
                        }


                        if (
                            commandEligible
                            && submittedText == "/clear")
                        {
                            roseCore.clearConversation();

                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::ChatEventType::ConversationCleared
                                });

                            continue;
                        }


                        // Nothing typed and nothing attached means there is no request.
                        //
                        // Keep this check BEFORE ingestion because an attachment-only
                        // submission is valid and AttachmentIngestion will supply the
                        // default "Please analyze the attached file(s)." user message.
                        if (
                            submittedText.empty()
                            && submission.attachments.empty())
                        {
                            continue;
                        }


                        // =====================================================
                        // One complete user request
                        // =====================================================
                        //
                        // EVERYTHING that can fail because of this particular
                        // submission belongs inside this try:
                        //
                        //     file permission
                        //     file opening
                        //     attachment parsing
                        //     context construction
                        //     model inference
                        //     persistence
                        //
                        // A bad PDF must not kill Rose's worker.

                        try
                        {
                            // -------------------------------------------------
                            // Attachment ingestion
                            // -------------------------------------------------

                            rose::tools::IngestedUserSubmission ingested =
                                attachmentIngestion.ingest(
                                    std::move(submission));


                            std::string input =
                                std::move(
                                    ingested.userText);


                            std::string transientContext =
                                std::move(
                                    ingested.transientContext);


                            bool responseStarted{
                                false
                            };


                            // -------------------------------------------------
                            // Rose activity -> avatar
                            // -------------------------------------------------

                            const rose::core::RoseActivityCallback
                                onActivity =
                                [&avatarController](
                                    const rose::core::RoseActivity activity)
                                {
                                    avatarController.handleActivity(
                                        activity);
                                };


                            // -------------------------------------------------
                            // Streaming response -> UI
                            // -------------------------------------------------

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


                                        responseStarted = true;
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
                            // Generate response
                            // -------------------------------------------------

                            const rose::model::ModelResponse response =
                                roseCore.processMessage(
                                    input,
                                    transientContext,
                                    onText,
                                    onActivity);


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


                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::
                                        ChatEventType::
                                        AssistantFinished,

                                    .text =
                                        response.text
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
                            avatarController.handleActivity(
                                rose::core::RoseActivity::Confused);


                            chatBridge.postEvent(
                                rose::ui::ChatEvent{
                                    .type =
                                        rose::ui::
                                        ChatEventType::
                                        Error,

                                    .text =
                                        std::string{
                                            "Rose could not complete "
                                            "that request: "
                                        }
                                        + exception.what()
                                });


                            // Important:
                            // no break; Rose remains alive and accepts the
                            // next submission.
                        }
                    }


                    // Normal worker-loop shutdown leaves Rose neutral while
                    // worker-owned objects unwind.
                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);
                }
                catch (...)
                {
                    // Fatal worker failures are initialization/lifetime failures,
                    // not errors caused by one user submission.
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