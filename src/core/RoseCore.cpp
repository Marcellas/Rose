#include "core/RoseCore.h"

#include "model/IModelProvider.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <string>

namespace rose::core
{

    RoseCore::RoseCore(std::unique_ptr<model::IModelProvider> modelProvider)
        : modelProvider_{ std::move(modelProvider) }
    {
        // A RoseCore without a model provider would currently be unusable.
        //
        // Fail immediately instead of allowing a null provider to create a much
        // harder-to-diagnose crash later inside processMessage().
        if (!modelProvider_)
        {
            throw std::invalid_argument{
                "RoseCore requires a valid ModelProvider."
            };
        }
    }

    model::ModelResponse RoseCore::processMessage(
        const std::string_view message)
    {
        // For now RoseCore converts one user input into one ModelRequest.
        //
        // This looks slightly more elaborate than our previous direct call, but it
        // establishes the boundary we'll need for conversation history.
        //
        // TODAY:
        //
        //     one User message
        //
        // SOON:
        //
        //     System identity
        //     previous User message
        //     previous Assistant response
        //     current User message
        //     retrieved memory
        //     tool results
        model::ModelRequest request;

        request.messages.push_back(
            model::ModelMessage{
                .role = model::ModelRole::User,
                .content = std::string{message}
            });

        return modelProvider_->generate(request);
    }

} // namespace rose::core
