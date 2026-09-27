#include "agent/ToolCompletionAccumulator.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    void require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "ToolCompletionAccumulator test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }
}

int main()
{
    using namespace rose;

    {
        agent::ToolCompletionAccumulator accumulator;

        tools::ToolResult image;
        image.message = "Generated the image successfully.";
        image.responseMode =
            tools::ToolResponseMode::AuthoritativeCompletion;

        accumulator.observe(image);

        const auto response =
            accumulator.authoritativeResponseIfComplete(1);

        require(response.has_value(),
                "authoritative-only run should bypass model synthesis");

        require(*response == "Generated the image successfully.",
                "authoritative text should be preserved");
    }

    {
        agent::ToolCompletionAccumulator accumulator;

        tools::ToolResult image;
        image.message = "Generated image.";
        image.responseMode =
            tools::ToolResponseMode::AuthoritativeCompletion;
        accumulator.observe(image);

        tools::ToolResult read;
        read.message = "Read text file.";
        accumulator.observe(read);

        require(!accumulator.authoritativeResponseIfComplete(2).has_value(),
                "mixed synthesis run must still use the model");
    }

    {
        agent::ToolCompletionAccumulator accumulator;
        require(!accumulator.authoritativeResponseIfComplete(0).has_value(),
                "zero-tool runs must use normal model response");
    }

    std::cout
        << "Rose ToolCompletionAccumulator tests: PASS\n";

    return 0;
}
