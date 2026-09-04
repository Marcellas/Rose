#ifdef _WIN32
#include <Windows.h>
#endif

#include "core/RoseCore.h"
#include "model/LlamaCppModelProvider.h"
#include "logging/Logger.h"
#include "model/LlamaLogBridge.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <string_view>


int main()
{
#ifdef _WIN32
    // -------------------------------------------------------------------------
    // Windows console UTF-8
    // -------------------------------------------------------------------------
    //
    // Language models emit UTF-8 text. Windows consoles may otherwise interpret
    // those bytes using a legacy code page, producing output such as:
    //
    //     ?Çö
    //
    // instead of:
    //
    //     —
    //
    // This is presentation-layer configuration only. It does not affect model
    // inference or Rose's internal string representation.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try
    {
        // =====================================================================
        // Composition root
        // =====================================================================
        //
        // main() decides WHICH implementation Rose receives.
        //
        // RoseCore itself still knows nothing about llama.cpp.
        //
        // Later this configuration will come from Rose's configuration system
        // instead of being hard-coded here.

        rose::logging::Logger logger{
    rose::logging::LoggerConfig{
        .mode = rose::logging::LogMode::Normal
    }
        };


        // Install llama.cpp logging redirection BEFORE constructing the provider.
        //
        // Object declaration order is intentional:
        //
        //     logger
        //     llamaLogBridge
        //     model provider / RoseCore
        //
        // C++ destroys local objects in reverse construction order, so the model and
        // llama backend disappear before the bridge, and the bridge disappears before
        // Logger.
        rose::model::LlamaLogBridge llamaLogBridge{
            logger
        };

        rose::model::LlamaCppConfig modelConfig{
            .modelPath = "models/Qwen3-8B-Q4_K_M.gguf",
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


        std::cout << "Rose v0.1\n";
        std::cout << "Local model initialized.\n";
        std::cout << "Type /clear to clear the conversation.\n";
        std::cout << "Type /log silent|normal|verbose to change logging.\n";
        std::cout << "Type /quit to exit.\n\n";


        std::string input;

        while (true)
        {
            std::cout << "You: ";

            if (!std::getline(std::cin, input))
            {
                break;
            }

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

            // -------------------------------------------------------------------------
// Stream Rose's visible response as it is generated.
// -------------------------------------------------------------------------
//
// The callback borrows each text chunk only during this invocation.
//
// We immediately write the bytes to stdout and retain nothing here. The model
// provider separately constructs the complete ModelResponse that RoseCore will
// commit to Conversation after successful generation.
            bool responseStarted{ false };


            const rose::model::ModelTextCallback onText =
                [&responseStarted](
                    const std::string_view text)
                {
                    if (!responseStarted)
                    {
                        std::cout << "Rose: ";

                        responseStarted = true;
                    }


                    // write() is intentional here because the callback gives us an exact
                    // string_view rather than requiring a null-terminated string.
                    std::cout.write(
                        text.data(),
                        static_cast<std::streamsize>(
                            text.size()));


                    // Normally stdout may buffer text. Flushing here makes each generated
                    // chunk visible immediately.
                    std::cout.flush();
                };


            const rose::model::ModelResponse response =
                roseCore.processMessage(
                    input,
                    onText);


            // Defensive fallback.
            //
            // A provider might return a complete response without invoking the callback.
            // The default IModelProvider implementation should already invoke it, but this
            // keeps the console frontend usable even if a future provider misbehaves.
            if (!responseStarted)
            {
                std::cout
                    << "Rose: "
                    << response.text;
            }


            std::cout << "\n\n";

            std::cout
                << "[Generated tokens: "
                << response.generatedTokens
                << "]\n\n";
        }

        return 0;
    }
    catch (const std::exception& exception)
    {
        // At this stage stderr is enough.
        //
        // Soon this will also be captured by Rose's bounded diagnostic logger.
        std::cerr
            << "Fatal Rose error: "
            << exception.what()
            << '\n';

        return 1;
    }
}
