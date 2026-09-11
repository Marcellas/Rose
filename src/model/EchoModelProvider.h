#pragma once

#include "model/IModelProvider.h"

namespace rose::model
{

    class EchoModelProvider final
        : public IModelProvider
    {
    public:
        [[nodiscard]]
        ModelContextUsage inspectContext(
            const ModelRequest& request) const override;


        [[nodiscard]]
        ModelResponse generate(
            const ModelRequest& request) override;
    };

} // namespace rose::model