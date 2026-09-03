#pragma once

#include <string>
#include <string_view>

namespace rose::model
{

    // IModelProvider
    // -----------------------------------------------------------------------------
    // Represents something capable of turning model input into model output.
    //
    // This interface intentionally knows nothing about:
    //
    //   - Rose's personality
    //   - long-term memory
    //   - tools
    //   - permissions
    //   - the avatar
    //   - application UI
    //
    // Those capabilities belong to Rose.
    //
    // A ModelProvider is only responsible for communicating with a language model.
    // Later implementations might include:
    //
    //   LocalModelProvider
    //   OpenAIModelProvider
    //   OllamaModelProvider
    //   TestModelProvider
    //
    // Keeping this boundary small allows Rose to change models without replacing
    // the rest of her architecture.
    class IModelProvider
    {
    public:
        virtual ~IModelProvider() = default;

        // Submit text to the underlying model and return its generated response.
        //
        // std::string_view:
        //     The provider only borrows the input for the duration of this call.
        //
        // std::string:
        //     The provider returns ownership of the generated response to the
        //     caller.
        [[nodiscard]]
        virtual std::string generate(std::string_view input) = 0;
    };

} // namespace rose::model