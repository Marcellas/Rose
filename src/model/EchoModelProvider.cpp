#include "model/EchoModelProvider.h"

#include <string>

namespace rose::model
{

    std::string EchoModelProvider::generate(const std::string_view input)
    {
        // For now the "AI" simply confirms what it received.
        //
        // Later, replacing EchoModelProvider with a local model implementation
        // should require very little or no modification to RoseCore.
        std::string response{ "I heard you say: " };
        response.append(input);

        return response;
    }

} // namespace rose::model
