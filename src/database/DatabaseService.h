#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rose::database
{
    struct DatabaseColumnSample
    {
        std::string name;
        std::string value;
    };

    struct DatabaseRowSample
    {
        std::vector<DatabaseColumnSample> columns;
    };

    struct DatabaseObject
    {
        std::string type;
        std::string name;
        std::string definition;
        std::vector<DatabaseRowSample> sampleRows;
    };

    struct DatabaseInspection
    {
        std::string family;
        std::string backend;
        std::vector<DatabaseObject> objects;
        bool truncated{ false };
    };

    class IDatabaseService
    {
    public:
        virtual ~IDatabaseService() = default;

        [[nodiscard]] virtual bool availableFor(const std::filesystem::path& path) const noexcept = 0;
        [[nodiscard]] virtual std::string availabilityMessage(const std::filesystem::path& path) const = 0;
        [[nodiscard]] virtual DatabaseInspection inspect(
            const std::filesystem::path& path,
            std::size_t maximumObjects = 24,
            std::size_t sampleRowsPerObject = 4) const = 0;
    };

    // Local, read-only database facade.
    //
    // SQLite is inspected through an optional sqlite3 CLI discovered at runtime.
    // Access .mdb/.accdb files use Windows' installed ACE/Jet OLE DB provider.
    // Rose never creates a database connection in write mode in this service.
    class LocalDatabaseService final : public IDatabaseService
    {
    public:
        LocalDatabaseService();
        ~LocalDatabaseService() override;

        LocalDatabaseService(const LocalDatabaseService&) = delete;
        LocalDatabaseService& operator=(const LocalDatabaseService&) = delete;

        [[nodiscard]] bool availableFor(const std::filesystem::path& path) const noexcept override;
        [[nodiscard]] std::string availabilityMessage(const std::filesystem::path& path) const override;
        [[nodiscard]] DatabaseInspection inspect(
            const std::filesystem::path& path,
            std::size_t maximumObjects = 24,
            std::size_t sampleRowsPerObject = 4) const override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
