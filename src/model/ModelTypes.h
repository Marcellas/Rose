#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rose::model
{

    // ModelRole
    // -------------------------------------------------------------------------
    // Identifies the role a message plays in a model conversation.
    //
    // IMPORTANT:
    // These are Rose-level concepts, not llama.cpp concepts.
    //
    // Each ModelProvider is responsible for translating these roles into the
    // representation expected by its particular model/API.
    enum class ModelRole
    {
        System,
        User,
        Assistant,
        Tool
    };


    // ModelMessage
    // -------------------------------------------------------------------------
    // Represents one logical message supplied to a language model.
    //
    // Examples:
    //
    //     System:
    //         "You are Rose..."
    //
    //     User:
    //         "What files did I work on yesterday?"
    //
    //     Assistant:
    //         Previous Rose response
    //
    //     Tool:
    //         Result returned from a tool call
    //
    // OWNERSHIP:
    //
    // ModelMessage owns its text.
    //
    // This is intentional. Messages may eventually outlive the UI input buffer
    // that originally created them.
    struct ModelMessage
    {
        ModelRole role{ ModelRole::User };

        std::string content;
    };


    // SamplingConfig
    // -------------------------------------------------------------------------
    // Controls how the provider selects generated tokens.
    //
    // These are request-level settings rather than properties of the loaded
    // model itself.
    //
    // That distinction matters because Rose may want:
    //
    //     deterministic generation for one task
    //     more creative generation for another
    //
    // while using the exact same loaded model.
    struct SamplingConfig
    {
        std::int32_t topK{ 20 };

        float topP{ 0.95f };

        float temperature{ 0.6f };

        // nullopt means:
        //
        //     "Use the provider/runtime's normal default seed behavior."
        //
        // A concrete seed will later allow reproducible generations when
        // debugging agent decisions.
        std::optional<std::uint32_t> seed{};
    };


    // ModelRequest
    // -------------------------------------------------------------------------
    // Complete model-independent description of one inference request.
    //
    // Rose creates this.
    //
    // ModelProvider consumes it.
    //
    // No llama.cpp types, OpenAI types, JSON types, or other provider-specific
    // implementation details should appear here.
    struct ModelRequest
    {
        std::vector<ModelMessage> messages;

        SamplingConfig sampling{};

        std::int32_t maxGeneratedTokens{ 256 };
    };


    // ModelFinishReason
    // -------------------------------------------------------------------------
    // Describes why generation stopped.
    //
    // Strong typing is preferable to a bool like "hitLimit" because additional
    // reasons can be added later:
    //
    //     ToolCall
    //     Cancelled
    //     SafetyStop
    //     Error
    //
    // without changing the meaning of existing values.
    enum class ModelFinishReason
    {
        EndOfGeneration,
        TokenLimit
    };


    // ModelResponse
    // -------------------------------------------------------------------------
    // Result returned by any model provider.
    //
    // generatedTokens is useful for:
    //
    //     diagnostics
    //     performance measurements
    //     context budgeting
    //     future usage statistics
    struct ModelResponse
    {
        std::string text;

        std::int32_t generatedTokens{ 0 };

        ModelFinishReason finishReason{
            ModelFinishReason::EndOfGeneration
        };
    };

} // namespace rose::model
