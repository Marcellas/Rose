#include "documents/ContextSafeDocumentSynthesizer.h"

#include "model/IModelProvider.h"
#include "model/ModelTypes.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "ContextSafeDocumentSynthesizer test failed: "
                << message
                << '\n';
            std::exit(1);
        }
    }


    class RecordingModelProvider final
        : public rose::model::IModelProvider
    {
    public:
        int capacity{ 8192 };
        [[nodiscard]]
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest& request) override
        {
            requests.push_back(request);

            const std::string& user =
                request.messages.back().content;

            const bool reduction =
                user.find("chunk_notes_1_begin") != std::string::npos;

            return rose::model::ModelResponse{
                .text = reduction
                    ? "Merged document facts: Alpha, Beta, 123."
                    : "Chunk facts: Alpha, Beta, 123.",
                .reasoning = {},
                .generatedTokens = 12,
                .finishReason = rose::model::ModelFinishReason::EndOfGeneration
            };
        }


        [[nodiscard]]
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        {
            std::size_t bytes{ 0 };
            for (const auto& message : request.messages)
            {
                bytes += message.content.size();
            }

            return rose::model::ModelContextUsage{
                .promptTokens = static_cast<std::int32_t>(bytes / 4u),
                .contextCapacity = capacity,
                .requestedGenerationTokens = request.maxGeneratedTokens
            };
        }


        std::vector<rose::model::ModelRequest> requests;
    };
}


int main()
{
    RecordingModelProvider provider;

    rose::documents::ContextSafeDocumentSynthesizer synthesizer{
        provider,
        rose::documents::ContextSafeDocumentSynthesisConfig{
            .maximumRawObservationBytes = 64,
            .chunkBytes = 80,
            .maximumSourceBytes = 1024,
            .reductionGroupSize = 3,
            .maximumFinalBytes = 512,
            .chunkSummaryTokens = 64,
            .reductionTokens = 96
        }
    };

    {
        const auto result = synthesizer.synthesize(
            "small document",
            "office/word",
            "summarize it");

        require(!result.synthesized,
                "small documents should remain transparent raw observations");
        require(result.text == "small document",
                "small document text should be preserved exactly");
        require(provider.requests.empty(),
                "small documents should not spend model inference");
    }

    {
        std::string large;
        for (int i = 0; i < 40; ++i)
        {
            large += "Alpha section with important number 123 and Beta relationship.\n";
        }

        const auto result = synthesizer.synthesize(
            large,
            "office/word",
            "analyze the design and provide a synopsis");

        require(result.synthesized,
                "large documents should use hierarchical synthesis");
        require(result.chunkCount > 1,
                "large documents should be split into multiple bounded chunks");
        require(provider.requests.size() > result.chunkCount,
                "large documents should include at least one reduction pass");
        require(result.text.find("Merged document facts") != std::string::npos,
                "final output should come from the reduction pass");
        require(result.text.size() <= 512,
                "final observation should respect the configured byte ceiling");

        bool instructionObserved{ false };
        for (const auto& request : provider.requests)
        {
            if (request.messages.back().content.find(
                    "analyze the design and provide a synopsis")
                != std::string::npos)
            {
                instructionObserved = true;
                break;
            }
        }

        require(instructionObserved,
                "user analysis intent should be preserved through chunk synthesis");
    }

    {
        RecordingModelProvider narrow;
        narrow.capacity = 1000;
        rose::documents::ContextSafeDocumentSynthesizer adaptive{
            narrow,
            rose::documents::ContextSafeDocumentSynthesisConfig{
                .maximumRawObservationBytes = 64,
                .chunkBytes = 4096,
                .maximumSourceBytes = 5000,
                .reductionGroupSize = 3,
                .maximumFinalBytes = 512,
                .chunkSummaryTokens = 64,
                .reductionTokens = 96
            } };
        const auto result = adaptive.synthesize(std::string(3900, 'A'),
            "pdf", "read all pages");
        require(result.chunkCount > 1 && !narrow.requests.empty(),
            "oversized model chunks should split and retry without dropping text");
    }

    std::cout
        << "Rose ContextSafeDocumentSynthesizer tests: PASS\n";

    return 0;
}
