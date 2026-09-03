#pragma once

#include "model/ModelTypes.h"

namespace rose::model
{

    // IModelProvider
    // -------------------------------------------------------------------------
    // Common interface implemented by every language-model backend Rose can use.
    //
    // Rose should depend on THIS interface rather than depending directly on:
    //
    //     llama.cpp
    //     OpenAI
    //     Ollama
    //     another future provider
    //
    // This keeps the language model replaceable without changing Rose's memory,
    // personality, tools, permissions, avatar, or agent architecture.
    class IModelProvider
    {
    public:
        virtual ~IModelProvider() = default;

        // Execute one structured inference request.
        //
        // OWNERSHIP:
        //
        // request:
        //     Borrowed for the duration of this call.
        //
        // returned ModelResponse:
        //     Owned by the caller.
        [[nodiscard]]
        virtual ModelResponse generate(
            const ModelRequest& request) = 0;
    };

} // namespace rose::model
