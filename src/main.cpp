#include "core/RoseCore.h"
#include "model/LlamaCppModelProvider.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

int main()
{
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

        rose::model::LlamaCppConfig modelConfig{
            .modelPath = "models/Qwen3-8B-Q4_K_M.gguf",
            .contextSize = 4096,
            .gpuLayers = 0
        };

        auto modelProvider =
            std::make_unique<
            rose::model::LlamaCppModelProvider>(
                std::move(modelConfig));

        rose::core::RoseCore roseCore{
            std::move(modelProvider)
        };


        std::cout << "Rose v0.1\n";
        std::cout << "Local model initialized.\n";
        std::cout << "Type /clear to clear the conversation.\n";
        std::cout << "Type /quit to exit.\n\n";


        std::string input;

        while (true)
        {
            std::cout << "You: ";

            if (!std::getline(std::cin, input))
            {
                break;
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

            const rose::model::ModelResponse response =
                roseCore.processMessage(input);

            std::cout
                << "Rose: "
                << response.text
                << "\n\n";

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
