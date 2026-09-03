#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace rose::model
{
    class IModelProvider;
}

namespace rose::core
{

    // RoseCore
    // -----------------------------------------------------------------------------
    // Coordinates Rose's high-level application behavior.
    //
    // IMPORTANT:
    // RoseCore is NOT intended to eventually contain every Rose subsystem.
    //
    // It will coordinate things such as:
    //
    //     Agent
    //     Memory
    //     Tools
    //     Permissions
    //     ModelProvider
    //
    // but those systems should remain independent modules.
    //
    // At this stage RoseCore only coordinates a model provider because that is all
    // our first vertical slice requires.
    class RoseCore
    {
    public:
        // RoseCore exclusively owns its currently selected model provider.
        //
        // unique_ptr communicates that ownership directly:
        //
        //     RoseCore creates/receives provider ownership
        //             ↓
        //     RoseCore remains responsible for provider lifetime
        //             ↓
        //     destroying RoseCore destroys the provider
        //
        // If provider lifetime requirements change later, we can revisit this,
        // but shared_ptr would currently introduce unnecessary shared ownership.
        explicit RoseCore(std::unique_ptr<model::IModelProvider> modelProvider);

        // Convert a user message into Rose's response.
        //
        // Today:
        //
        //     User message
        //         ↓
        //     ModelProvider
        //         ↓
        //     Response
        //
        // Eventually:
        //
        //     User message
        //         ↓
        //     Agent
        //         ↓
        //     Memory retrieval / planning / tools
        //         ↓
        //     Prompt construction
        //         ↓
        //     ModelProvider
        //         ↓
        //     Rose response
        [[nodiscard]]
        std::string processMessage(std::string_view message);

    private:
        std::unique_ptr<model::IModelProvider> modelProvider_;
    };

} // namespace rose::core
