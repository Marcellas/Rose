#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rose::knowledge
{
    // A logical text-bearing portion of a source document.  The locator is
    // intentionally format-neutral: future readers can report values such as
    // "page=4", "slide=7", "sheet=Budget!A1:F20", or "frame=120" while
    // plain-text readers simply report "document".
    struct ProjectContentSegment
    {
        std::string locator;
        std::string text;
    };

    struct ExtractedProjectContent
    {
        // Stable descriptive values persisted with the knowledge index. They are
        // provenance, not dispatch keys, so changing a future reader does not make
        // old indexes impossible to load.
        std::string contentKind;
        std::string readerId;
        std::vector<ProjectContentSegment> segments;
    };

    // Format adapter used by ProjectKnowledgeIndexer. Implementations are
    // read-only and must not mutate the source document.
    class IProjectContentReader
    {
    public:
        virtual ~IProjectContentReader() = default;

        [[nodiscard]]
        virtual std::string_view id() const noexcept = 0;

        // Per-format hard bound. The indexer also has a global bound; the smaller
        // value wins. This keeps a giant source file from inheriting a PDF/image
        // budget merely because all formats share one indexing pipeline.
        [[nodiscard]]
        virtual std::uintmax_t maximumSourceBytes() const noexcept
        {
            return (std::numeric_limits<std::uintmax_t>::max)();
        }

        // True for readers that actually load/decode the source bytes through the
        // indexer's normal bounded-document path. Metadata-only/streaming readers
        // can return false so a large ZIP/video is still discoverable without
        // inheriting the 64 MiB text/document in-memory budget.
        [[nodiscard]]
        virtual bool usesWholeFileByteBudget() const noexcept
        {
            return true;
        }

        [[nodiscard]]
        virtual bool supports(
            const std::filesystem::path& path) const noexcept = 0;

        [[nodiscard]]
        virtual ExtractedProjectContent read(
            const std::filesystem::path& path,
            std::uintmax_t sourceBytes) const = 0;
    };


    // Owns the ordered set of content readers used by the project indexer.
    // The first reader that claims a path owns extraction for that path.
    //
    // Extension point:
    //   PDF, Office/OpenXML, archive, image/vision, media, database/schema, and
    //   other readers can be added here without changing retrieval/storage.
    class ProjectContentReaderRegistry final
    {
    public:
        ProjectContentReaderRegistry() = default;

        ProjectContentReaderRegistry(ProjectContentReaderRegistry&&) noexcept = default;
        ProjectContentReaderRegistry& operator=(ProjectContentReaderRegistry&&) noexcept = default;

        ProjectContentReaderRegistry(const ProjectContentReaderRegistry&) = delete;
        ProjectContentReaderRegistry& operator=(const ProjectContentReaderRegistry&) = delete;

        void add(
            std::unique_ptr<IProjectContentReader> reader);

        // Symmetric inverse of add(). Ownership is returned to the caller so a
        // plugin can unregister a reader without hidden destruction.
        [[nodiscard]]
        std::unique_ptr<IProjectContentReader> remove(
            std::string_view readerId);

        [[nodiscard]]
        const IProjectContentReader* findReader(
            const std::filesystem::path& path) const noexcept;

    private:
        std::vector<std::unique_ptr<IProjectContentReader>> readers_;
    };


    [[nodiscard]]
    ProjectContentReaderRegistry makeDefaultProjectContentReaderRegistry();
}
