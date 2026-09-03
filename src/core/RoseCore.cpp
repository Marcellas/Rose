#include "core/RoseCore.h"

#include "model/IModelProvider.h"

#include <memory>
#include <stdexcept>
#include <utility>

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

    std::string RoseCore::processMessage(const std::string_view message)
    {
        // RoseCore currently delegates directly to the model.
        //
        // This line is intentionally simple. The Agent layer will eventually sit
        // between RoseCore and the provider and become responsible for reasoning,
        // retrieval, tool use, and prompt construction.
        return modelProvider_->generate(message);
    }

} // namespace rose::core
