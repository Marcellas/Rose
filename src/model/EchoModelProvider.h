#pragma once

#include "model/IModelProvider.h"

namespace rose::model
{

    // Deterministic provider useful for testing Rose without loading an actual
    // language model.
    class EchoModelProvider final : public IModelProvider
    {
    public:
        [[nodiscard]]
        ModelResponse generate(
            const ModelRequest& request) override;
    };

} // namespace rose::model
