#ifdef _WIN32
#include <Windows.h>
#endif

#include "avatar/AvatarController.h"
#include "avatar/SdlAvatar.h"
#include "core/RoseCore.h"
#include "logging/Logger.h"
#include "model/LlamaCppModelProvider.h"
#include "model/LlamaLogBridge.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
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
    // Rose and local language models use UTF-8 internally.
    //
    // Configure the Windows console so streamed UTF-8 output is interpreted
    // correctly rather than through a legacy Windows code page.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try
    {
        // =====================================================================
        // Composition root
        // =====================================================================
        //
        // main() owns application-lifetime infrastructure.
        //
        // OWNERSHIP / LIFETIME:
        //
        //     Logger
        //
        //     SdlAvatar
        //         ^
        //         |
        //     AvatarController
        //
        //     Conversation worker
        //         |
        //         +-- LlamaLogBridge
        //         +-- LlamaCppModelProvider
        //         +-- RoseCore
        //
        // The worker is always joined before these application-level objects
        // are destroyed.

        rose::logging::Logger logger{
            rose::logging::LoggerConfig{
                .mode = rose::logging::LogMode::Normal
            }
        };


        // SDL window creation and rendering remain on the main thread.
        rose::avatar::SdlAvatar avatar{
            320,
            300
        };


        // AvatarController is a translation layer between RoseActivity and
        // presentation-specific AvatarState.
        //
        // It borrows avatar, so avatar must outlive it.
        rose::avatar::AvatarController avatarController{
            avatar
        };


        // ---------------------------------------------------------------------
        // Worker completion state
        // ---------------------------------------------------------------------
        //
        // The worker publishes completion through one atomic flag.
        //
        // RoseCore itself is not shared across threads and therefore does not
        // require locking.
        std::atomic<bool> conversationFinished{
            false
        };


        // Exceptions cannot propagate directly across std::thread boundaries.
        //
        // The worker captures any failure here. main() rethrows it after joining
        // the worker so our existing fatal-error handling remains useful.
        std::exception_ptr workerException;


        // Model loading can take noticeable time, so show the Working state
        // while the worker initializes llama.cpp.
        avatarController.handleActivity(
            rose::core::RoseActivity::Working);


        std::cout
            << "Rose v0.1\n"
            << "Loading local model...\n";


        // =====================================================================
        // Conversation / inference worker
        // =====================================================================
        //
        // Everything that operates on RoseCore remains on this one thread:
        //
        //     console input
        //          |
        //          v
        //     RoseCore
        //          |
        //          v
        //     llama.cpp inference
        //
        // This preserves deterministic ownership and avoids adding mutexes
        // throughout Rose's core architecture.
        std::thread conversationWorker{
            [&logger,
             &avatarController,
             &conversationFinished,
             &workerException]()
            {
                try
                {
                    // ---------------------------------------------------------
                    // llama.cpp logging bridge
                    // ---------------------------------------------------------
                    //
                    // The bridge is constructed before the provider so llama
                    // diagnostics are routed into Rose's Logger during model
                    // loading as well as inference.
                    //
                    // Destruction occurs in reverse order:
                    //
                    //     RoseCore/provider/backend
                    //     LlamaLogBridge
                    //
                    // so the callback remains valid for llama's entire lifetime.
                    rose::model::LlamaLogBridge llamaLogBridge{
                        logger
                    };


                    rose::model::LlamaCppConfig modelConfig{
                        .modelPath =
                            "models/Qwen3-8B-Q4_K_M.gguf",

                        .contextSize = 4096,

                        .gpuLayers = 0
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


                    // Model construction succeeded.
                    avatarController.handleActivity(
                        rose::core::RoseActivity::Idle);


                    std::cout
                        << "Local model initialized.\n"
                        << "Type /clear to clear the conversation.\n"
                        << "Type /log silent|normal|verbose to change logging.\n"
                        << "Type /quit to exit.\n\n";


                    std::string input;


                    while (true)
                    {
                        // Waiting for user input is conceptually Rose listening.
                        avatarController.handleActivity(
                            rose::core::RoseActivity::Listening);


                        std::cout << "You: ";
                        std::cout.flush();


                        // std::getline() is intentionally confined to this worker.
                        //
                        // It is blocking, but it cannot block the SDL event/render
                        // loop because that loop runs independently on main.
                        if (!std::getline(
                                std::cin,
                                input))
                        {
                            break;
                        }


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
                            break;
                        }


                        if (input == "/clear")
                        {
                            roseCore.clearConversation();

                            std::cout
                                << "Conversation cleared.\n\n";

                            continue;
                        }


                        if (input.empty())
                        {
                            continue;
                        }


                        // -----------------------------------------------------
                        // Rose activity forwarding
                        // -----------------------------------------------------
                        //
                        // processMessage() reports:
                        //
                        //     Thinking
                        //         |
                        //         v
                        //     Speaking
                        //         |
                        //         v
                        //       Idle
                        //
                        // AvatarController translates those into AvatarState.
                        //
                        // SdlAvatar::setState() performs only an atomic state
                        // publication, so this callback never performs SDL work
                        // from the worker thread.
                        const rose::core::RoseActivityCallback
                            onActivity =
                                [&avatarController](
                                    const rose::core::RoseActivity activity)
                                {
                                    avatarController.handleActivity(
                                        activity);
                                };


                        // -----------------------------------------------------
                        // Streaming response
                        // -----------------------------------------------------
                        //
                        // The model invokes this callback incrementally as
                        // user-visible text becomes available.
                        //
                        // Console output happens on this worker. The SDL main
                        // thread never touches stdout during normal operation.
                        bool responseStarted{
                            false
                        };


                        const rose::model::ModelTextCallback onText =
                            [&responseStarted](
                                const std::string_view text)
                            {
                                if (!responseStarted)
                                {
                                    std::cout << "Rose: ";

                                    responseStarted = true;
                                }


                                // The supplied string_view is borrowed only for
                                // this callback invocation, so consume it now.
                                std::cout.write(
                                    text.data(),
                                    static_cast<std::streamsize>(
                                        text.size()));


                                // Make streamed chunks visible immediately.
                                std::cout.flush();
                            };


                        const rose::model::ModelResponse response =
                            roseCore.processMessage(
                                input,
                                onText,
                                onActivity);


                        // -----------------------------------------------------
                        // Defensive non-streaming fallback
                        // -----------------------------------------------------
                        //
                        // A conforming provider should invoke onText when using
                        // generateStreaming(), but retain this fallback so the
                        // console remains usable with future/simple providers.
                        if (!responseStarted)
                        {
                            std::cout
                                << "Rose: "
                                << response.text;
                        }


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
                    // Store the original exception for main() to handle after
                    // the worker has terminated.
                    workerException =
                        std::current_exception();


                    // Make failure visible through the avatar immediately.
                    avatarController.handleActivity(
                        rose::core::RoseActivity::Confused);
                }


                // release pairs with main's acquire load.
                //
                // Everything performed by this worker before this store becomes
                // visible before main observes completion.
                conversationFinished.store(
                    true,
                    std::memory_order_release);
            }
        };


        // =====================================================================
        // Main SDL loop
        // =====================================================================
        //
        // SDL stays here.
        //
        // The loop remains responsive while:
        //
        //     - std::getline() waits
        //     - the model loads
        //     - inference runs
        //     - responses stream
        //
        // This is the fundamental threading architecture needed by the eventual
        // desktop version of Rose.

        bool avatarWindowOpen{
            true
        };


        while (
            !conversationFinished.load(
                std::memory_order_acquire))
        {
            if (avatarWindowOpen)
            {
                avatarWindowOpen =
                    avatar.processEvents();


                if (avatarWindowOpen)
                {
                    avatar.render();
                }
            }


            // ~60 Hz maximum update cadence.
            //
            // Once the graphical animation system exists, frame pacing can move
            // into the renderer/platform layer.
            std::this_thread::sleep_for(
                std::chrono::milliseconds{
                    16
                });
        }


        // ---------------------------------------------------------------------
        // Deterministic shutdown
        // ---------------------------------------------------------------------
        //
        // Do not destroy the avatar, controller, or logger while the worker
        // could still reference them.
        if (conversationWorker.joinable())
        {
            conversationWorker.join();
        }


        // Propagate worker failures back through main's existing error path.
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