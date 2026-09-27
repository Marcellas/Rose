#include "knowledge/ProjectContentReader.h"

#include "knowledge/DatabaseProjectContentReader.h"
#include "knowledge/ImageProjectContentReader.h"
#include "knowledge/MediaProjectContentReader.h"
#include "knowledge/OpenXmlProjectContentReader.h"
#include "knowledge/PdfProjectContentReader.h"
#include "knowledge/Utf8TextProjectContentReader.h"
#include "knowledge/ZipArchiveProjectContentReader.h"
#include "knowledge/ShortcutProjectContentReader.h"

#include "ocr/TesseractCliOcrEngine.h"
#include "media/MediaService.h"
#include "database/DatabaseService.h"
#include "shortcuts/ShortcutService.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace rose::knowledge
{
    void ProjectContentReaderRegistry::add(
        std::unique_ptr<IProjectContentReader> reader)
    {
        if (!reader)
        {
            throw std::invalid_argument{
                "Project content reader cannot be null."
            };
        }
        const std::string_view readerId = reader->id();
        if (readerId.empty())
        {
            throw std::invalid_argument{
                "Project content reader id cannot be empty."
            };
        }
        for (const auto& existing : readers_)
        {
            if (existing->id() == readerId)
            {
                throw std::invalid_argument{
                    "Project content reader id is already registered: "
                    + std::string{ readerId }
                };
            }
        }
        readers_.push_back(std::move(reader));
    }


    std::unique_ptr<IProjectContentReader> ProjectContentReaderRegistry::remove(
        const std::string_view readerId)
    {
        const auto found = std::find_if(
            readers_.begin(), readers_.end(),
            [&](const auto& reader)
            {
                return reader->id() == readerId;
            });
        if (found == readers_.end())
        {
            return nullptr;
        }

        std::unique_ptr<IProjectContentReader> removed = std::move(*found);
        readers_.erase(found);
        return removed;
    }


    const IProjectContentReader* ProjectContentReaderRegistry::findReader(
        const std::filesystem::path& path) const noexcept
    {
        for (const auto& reader : readers_)
        {
            if (reader->supports(path))
            {
                return reader.get();
            }
        }
        return nullptr;
    }


    ProjectContentReaderRegistry makeDefaultProjectContentReaderRegistry()
    {
        ProjectContentReaderRegistry registry;
        registry.add(
            std::make_unique<PdfProjectContentReader>(
                std::make_unique<ocr::TesseractCliOcrEngine>()));
        registry.add(std::make_unique<OpenXmlProjectContentReader>());
        registry.add(
            std::make_unique<ImageProjectContentReader>(
                std::make_unique<ocr::TesseractCliOcrEngine>()));
        registry.add(
            std::make_unique<MediaProjectContentReader>(
                std::make_unique<media::LocalMediaService>()));
        registry.add(std::make_unique<ZipArchiveProjectContentReader>());
        registry.add(std::make_unique<DatabaseProjectContentReader>(
            std::make_unique<database::LocalDatabaseService>()));
        registry.add(std::make_unique<ShortcutProjectContentReader>(
            std::make_unique<shortcuts::LocalShortcutService>()));
        registry.add(std::make_unique<Utf8TextProjectContentReader>());
        return registry;
    }
}
