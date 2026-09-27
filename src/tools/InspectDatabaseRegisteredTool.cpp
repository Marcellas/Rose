#include "tools/InspectDatabaseRegisteredTool.h"

#include "database/DatabaseService.h"
#include "files/FileFormatCatalog.h"
#include "permissions/PermissionSystem.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace rose::tools
{
    InspectDatabaseRegisteredTool::InspectDatabaseRegisteredTool(
        permissions::PermissionSystem& permissions,
        database::IDatabaseService& databaseService)
        : permissions_{ permissions }
        , databaseService_{ databaseService }
        , descriptor_{
            .id = "inspect_database",
            .displayName = "Inspect Database",
            .description = "Read the schema and a small bounded sample of rows from one exact local database file. SQLite uses Windows WinSQLite/native sqlite3 in read-only mode; Access uses an installed Windows ACE/Jet provider. Never modifies the database.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                ToolParameterDescriptor{ .name = "path", .description = "Absolute path of the exact database file to inspect.", .type = ToolValueType::String, .required = true }
            }
        }
    {
    }

    const ToolDescriptor& InspectDatabaseRegisteredTool::descriptor() const noexcept { return descriptor_; }

    ToolResult InspectDatabaseRegisteredTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "InspectDatabaseRegisteredTool received a different tool id." };
        for (const auto& [name, value] : request.arguments)
        {
            (void)value;
            if (name != "path") throw std::invalid_argument{ "inspect_database does not accept argument '" + name + "'." };
        }
        const auto found = request.arguments.find("path");
        if (found == request.arguments.end() || found->second.empty())
            throw std::invalid_argument{ "inspect_database requires argument 'path'." };
        const std::filesystem::path path{ found->second };
        if (!path.is_absolute()) throw std::invalid_argument{ "inspect_database requires an absolute file path." };
        if (files::classifyFileFormat(path).kind != files::FileFormatKind::Database)
            throw std::invalid_argument{ "inspect_database requires a recognized database extension." };

        permissions_.grantReadOnce(path);
        if (!permissions_.consumeReadOnce(path)) throw std::runtime_error{ "Database read permission could not be established." };
        if (!databaseService_.availableFor(path)) throw std::runtime_error{ databaseService_.availabilityMessage(path) };

        const database::DatabaseInspection inspection = databaseService_.inspect(path, 24, 4);
        std::ostringstream message;
        message << "Inspected database: " << path.string()
                << "\nfamily=" << inspection.family
                << "\nbackend=" << inspection.backend
                << "\nobjects=" << inspection.objects.size()
                << "\ncontent_truncated=" << (inspection.truncated ? "true" : "false")
                << "\n<rose_untrusted_database_content>";
        for (const auto& object : inspection.objects)
        {
            message << "\n--- " << object.type << " " << object.name << " ---";
            if (!object.definition.empty()) message << "\nSchema: " << object.definition;
            if (!object.sampleRows.empty()) message << "\nSample rows:";
            for (const auto& row : object.sampleRows)
            {
                message << "\n  ";
                bool first{ true };
                for (const auto& cell : row.columns)
                {
                    if (!first) message << " | ";
                    first = false;
                    message << cell.name << '=' << cell.value;
                }
            }
        }
        message << "\n</rose_untrusted_database_content>"
                << "\nNOTICE: Row sampling is intentionally bounded and does not imply the entire database was read.";
        return ToolResult{ .success = true, .message = message.str(), .artifacts = {} };
    }
}
