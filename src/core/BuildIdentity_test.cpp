#include "core/BuildIdentity.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void require(
        const bool condition,
        const std::string_view message)
    {
        if (!condition)
        {
            std::cerr
                << "BuildIdentity test failed: "
                << message
                << '\n';

            std::exit(1);
        }
    }
}


int main()
{
    const rose::core::BuildIdentity identity =
        rose::core::currentBuildIdentity();

    require(
        !identity.version.empty(),
        "project version must not be empty");

    require(
        !identity.revision.empty(),
        "source revision/snapshot identity must not be empty");

    require(
        !identity.configuration.empty(),
        "build configuration must not be empty");

    require(
        !identity.compilerId.empty(),
        "compiler ID must not be empty");

    const std::string formatted =
        rose::core::formatBuildIdentity(
            identity);

    require(
        formatted.find("Rose v") != std::string::npos,
        "formatted identity should contain Rose version");

    require(
        formatted.find("rev ") != std::string::npos,
        "formatted identity should contain source revision");

    std::cout
        << "Rose BuildIdentity test: PASS\n"
        << formatted
        << '\n';

    return 0;
}
