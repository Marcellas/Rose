#include "knowledge/ShortcutProjectContentReader.h"

#include "files/FileFormatCatalog.h"
#include "shortcuts/ShortcutService.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace rose::knowledge
{
    ShortcutProjectContentReader::ShortcutProjectContentReader(
        std::unique_ptr<shortcuts::IShortcutService> shortcutService)
        : shortcutService_{ std::move(shortcutService) }
    {
        if (!shortcutService_) throw std::invalid_argument{ "ShortcutProjectContentReader requires a shortcut service." };
    }

    ShortcutProjectContentReader::~ShortcutProjectContentReader() = default;

    bool ShortcutProjectContentReader::supports(const std::filesystem::path& path) const noexcept
    {
        return files::classifyFileFormat(path).kind == files::FileFormatKind::Shortcut;
    }

    ExtractedProjectContent ShortcutProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        std::ostringstream text;
        text << "Shortcut asset: " << path.filename().string() << ".\nSource bytes: " << sourceBytes << ".\n";
        try
        {
            const shortcuts::ShortcutInspection inspection = shortcutService_->inspect(path);
            text << "Kind: " << inspection.kind << ".\n";
            if (!inspection.target.empty()) text << "Target: " << inspection.target << ".\n";
            if (!inspection.url.empty()) text << "URL: " << inspection.url << ".\n";
            if (!inspection.arguments.empty()) text << "Arguments: " << inspection.arguments << ".\n";
            if (!inspection.workingDirectory.empty()) text << "Working directory: " << inspection.workingDirectory << ".\n";
            if (!inspection.description.empty()) text << "Description: " << inspection.description << ".\n";
            if (!inspection.iconLocation.empty()) text << "Icon: " << inspection.iconLocation << ".\n";
        }
        catch (const std::exception& exception)
        {
            text << "Shortcut metadata probe failed: " << exception.what() << "\n";
        }
        text << "The shortcut target is indexed as metadata only and is never launched during Project Knowledge indexing.";
        return ExtractedProjectContent{
            .contentKind = "shortcut",
            .readerId = std::string{ id() },
            .segments = { ProjectContentSegment{ .locator = "shortcut", .text = text.str() } }
        };
    }
}
