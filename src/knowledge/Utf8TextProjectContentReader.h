#pragma once

#include "knowledge/ProjectContentReader.h"

namespace rose::knowledge
{
    // Reads source/configuration/project files that are valid UTF-8 text.
    // The supported set intentionally includes Visual Studio/CMake project files
    // and a broad range of programming-language extensions, so Rose can index a
    // mixed source tree without needing language-specific parsers first.
    class Utf8TextProjectContentReader final : public IProjectContentReader
    {
    public:
        [[nodiscard]]
        std::string_view id() const noexcept override
        {
            return "utf8-text-v1";
        }

        [[nodiscard]]
        std::uintmax_t maximumSourceBytes() const noexcept override
        {
            return 2u * 1024u * 1024u;
        }

        [[nodiscard]]
        bool supports(
            const std::filesystem::path& path) const noexcept override;

        [[nodiscard]]
        ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const override;
    };
}
