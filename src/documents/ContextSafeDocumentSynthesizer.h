#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rose::model
{
    class IModelProvider;
}

namespace rose::documents
{
    struct ContextSafeDocumentSynthesisConfig
    {
        // Small reads remain transparent: Rose gets the original extracted text.
        std::size_t maximumRawObservationBytes{ 16u * 1024u };

        // Large sources are split into model-sized pieces and hierarchically
        // reduced. This keeps arbitrary document size out of the Agent context.
        std::size_t chunkBytes{ 12u * 1024u };
        std::size_t maximumSourceBytes{ 512u * 1024u };
        std::size_t reductionGroupSize{ 6u };
        std::size_t maximumFinalBytes{ 6u * 1024u };

        int chunkSummaryTokens{ 320 };
        int reductionTokens{ 512 };
    };

    struct ContextSafeDocumentSynthesisResult
    {
        std::string text;
        bool synthesized{ false };
        bool sourceTruncated{ false };
        std::size_t sourceBytes{ 0 };
        std::size_t processedBytes{ 0 };
        std::size_t chunkCount{ 0 };
    };

    // Converts potentially large extracted document text into an observation that
    // can safely pass through Rose's bounded Agent/final-response context.
    //
    // Small documents are returned unchanged. Large documents are processed by
    // the currently selected replaceable model provider in a hierarchical map /
    // reduce pass. Source text is explicitly treated as untrusted data during all
    // synthesis calls so instructions embedded in a document cannot become Agent
    // instructions merely because Rose opened the file.
    class ContextSafeDocumentSynthesizer final
    {
    public:
        explicit ContextSafeDocumentSynthesizer(
            model::IModelProvider& modelProvider,
            ContextSafeDocumentSynthesisConfig config = {});

        [[nodiscard]]
        ContextSafeDocumentSynthesisResult synthesize(
            std::string_view extractedText,
            std::string_view sourceKind,
            std::string_view userInstruction) const;

    private:
        model::IModelProvider& modelProvider_;
        ContextSafeDocumentSynthesisConfig config_;
    };
}
