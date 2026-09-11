#pragma once

#include "model/ModelTypes.h"

#include <functional>
#include <string_view>


namespace rose::model
{
    // -----------------------------------------------------------------------------
    // IModelProvider
    // -----------------------------------------------------------------------------
    //
    // Rose talks to models through this interface rather than depending directly on
    // llama.cpp or any future cloud/local implementation.
    class IModelProvider
    {
    public:
        virtual ~IModelProvider() = default;


        // Traditional complete-response generation.
        [[nodiscard]]
        virtual ModelResponse generate(
            const ModelRequest& request) = 0;

        [[nodiscard]]
        virtual ModelContextUsage inspectContext(
            const ModelRequest& request) const = 0;

        // Streaming generation.
        //
        // The default implementation preserves compatibility with providers that
        // do not support true token streaming yet:
        //
        //     generate complete response
        //              |
        //              v
        //     invoke callback once
        //
        // LlamaCppModelProvider will override this later and invoke the callback
        // incrementally as visible text becomes available.
        //
        // Keeping this fallback here means EchoModelProvider and future simple
        // providers do not need duplicate implementations merely to satisfy the
        // interface.

        [[nodiscard]]
        virtual ModelResponse generateStreaming(
            const ModelRequest& request,
            const ModelTextCallback& onText)
        {
            ModelResponse response =
                generate(request);

            if (
                onText
                && !response.text.empty())
            {
                onText(response.text);
            }

            return response;
        }
    };

} // namespace rose::model