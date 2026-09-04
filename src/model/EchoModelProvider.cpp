#include "model/EchoModelProvider.h"

#include <string>
#include <utility>

namespace rose::model
{

    ModelResponse EchoModelProvider::generate(
        const ModelRequest& request)
    {
        // Search backward because the most recent user message is normally the
        // one Rose is currently responding to.
        //
        // Reverse iteration also means this still behaves sensibly after
        // conversation history is added.
        for (
            auto iterator = request.messages.rbegin();
            iterator != request.messages.rend();
            ++iterator)
        {
            if (iterator->role != ModelRole::User)
            {
                continue;
            }

            std::string response{
                "I heard you say: "
            };

            response.append(iterator->content);

            return ModelResponse{
            .text = std::move(response),
            .reasoning = {},
            .generatedTokens = 0,
            .finishReason =
            ModelFinishReason::EndOfGeneration
            };
        }

        // A request containing no user message is valid at the type level, but
        // EchoModelProvider has nothing meaningful to echo.
        return ModelResponse{
            .text = {},
            .generatedTokens = 0,
            .finishReason =
                ModelFinishReason::EndOfGeneration
        };
    }

} // namespace rose::model
