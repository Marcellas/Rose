#include "knowledge/DatabaseProjectContentReader.h"

#include "database/DatabaseService.h"
#include "files/FileFormatCatalog.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace rose::knowledge
{
    DatabaseProjectContentReader::DatabaseProjectContentReader(
        std::unique_ptr<database::IDatabaseService> databaseService)
        : databaseService_{ std::move(databaseService) }
    {
        if (!databaseService_) throw std::invalid_argument{ "DatabaseProjectContentReader requires a database service." };
    }

    DatabaseProjectContentReader::~DatabaseProjectContentReader() = default;

    bool DatabaseProjectContentReader::supports(const std::filesystem::path& path) const noexcept
    {
        return files::classifyFileFormat(path).kind == files::FileFormatKind::Database;
    }

    ExtractedProjectContent DatabaseProjectContentReader::read(
        const std::filesystem::path& path,
        const std::uintmax_t sourceBytes) const
    {
        std::ostringstream text;
        text << "Database asset: " << path.filename().string() << ".\n";
        text << "Source bytes: " << sourceBytes << ".\n";
        if (!databaseService_->availableFor(path))
        {
            text << "Detailed database schema unavailable: " << databaseService_->availabilityMessage(path) << "\n";
        }
        else
        {
            try
            {
                const database::DatabaseInspection inspection = databaseService_->inspect(path, 32, 0);
                text << "Family: " << inspection.family << ".\n";
                text << "Backend: " << inspection.backend << ".\n";
                text << "Objects indexed: " << inspection.objects.size() << ".\n";
                for (const auto& object : inspection.objects)
                {
                    text << object.type << ": " << object.name;
                    if (!object.definition.empty()) text << " | " << object.definition;
                    text << "\n";
                }
                if (inspection.truncated) text << "NOTICE: Database object list was bounded during indexing.\n";
            }
            catch (const std::exception& exception)
            {
                text << "Database schema probe failed: " << exception.what() << "\n";
            }
        }
        text << "Project indexing records schema/object metadata only; row contents are sampled only on explicit inspection.";
        return ExtractedProjectContent{
            .contentKind = "database",
            .readerId = std::string{ id() },
            .segments = { ProjectContentSegment{ .locator = "schema", .text = text.str() } }
        };
    }
}
