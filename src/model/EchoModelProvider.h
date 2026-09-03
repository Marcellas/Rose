#pragma once

#include "model/IModelProvider.h"

namespace rose::model
{

    // Temporary model implementation used while constructing Rose's architecture.
    //
    // Having a deterministic provider is useful even after real AI models are
    // integrated because it gives us something extremely simple for testing the
    // rest of the application.
    class EchoModelProvider final : public IModelProvider
    {
    public:
        [[nodiscard]]
        std::string generate(std::string_view input) override;
    };

} // namespace rose::model