#include "knowledge/ProjectKnowledgeIndexer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace rose::knowledge
{
    namespace
    {
        [[nodiscard]]
        std::int64_t currentUnixMilliseconds()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        [[nodiscard]]
        std::int64_t fileTimeToUnixMilliseconds(
            const std::filesystem::file_time_type value)
        {
            const auto systemTime =
                std::chrono::time_point_cast<std::chrono::milliseconds>(
                    value
                    - std::filesystem::file_time_type::clock::now()
                    + std::chrono::system_clock::now());
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                systemTime.time_since_epoch()).count();
        }

        [[nodiscard]]
        std::string lowerAscii(
            std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        [[nodiscard]]
        bool ignoredDirectoryName(
            const std::string_view name)
        {
            static constexpr std::string_view ignored[]{
                ".git", ".svn", ".hg", ".vs", ".idea", ".cache",
                "build", "out", "dist", "node_modules", "external",
                "third_party", "vendor", ".venv", "venv", "__pycache__"
            };
            return std::find(std::begin(ignored), std::end(ignored), name)
                != std::end(ignored);
        }

        [[nodiscard]]
        std::string trimAsciiWhitespace(
            std::string value)
        {
            const auto nonWhitespace = [](const unsigned char character)
            {
                return std::isspace(character) == 0;
            };

            const auto first = std::find_if(value.begin(), value.end(), nonWhitespace);
            if (first == value.end()) return {};
            const auto last = std::find_if(value.rbegin(), value.rend(), nonWhitespace).base();
            return std::string{ first, last };
        }

        [[nodiscard]]
        std::size_t safeUtf8End(
            const std::string_view text,
            std::size_t end)
        {
            end = (std::min)(end, text.size());
            while (end > 0 && end < text.size()
                   && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
            {
                --end;
            }
            return end;
        }

        [[nodiscard]]
        std::size_t safeUtf8Start(
            const std::string_view text,
            std::size_t start)
        {
            start = (std::min)(start, text.size());
            while (start < text.size()
                   && (static_cast<unsigned char>(text[start]) & 0xC0u) == 0x80u)
            {
                ++start;
            }
            return start;
        }

        [[nodiscard]]
        std::vector<KnowledgeChunkRecord> chunkText(
            const std::string& text,
            const std::string_view baseLocator,
            const std::size_t maximumChunkBytes,
            const std::size_t overlapBytes)
        {
            std::vector<KnowledgeChunkRecord> chunks;
            if (text.empty()) return chunks;

            std::size_t start{ 0 };
            std::size_t ordinal{ 0 };

            while (start < text.size())
            {
                std::size_t end = safeUtf8End(
                    text,
                    (std::min)(text.size(), start + maximumChunkBytes));
                if (end <= start)
                {
                    break;
                }

                if (end < text.size())
                {
                    const std::size_t searchFloor =
                        start + maximumChunkBytes / 2;
                    for (std::size_t cursor = end;
                         cursor > searchFloor;
                         --cursor)
                    {
                        const char character = text[cursor - 1];
                        if (character == '\n' || character == ' ' || character == '\t')
                        {
                            end = cursor;
                            break;
                        }
                    }
                }

                std::string chunk = trimAsciiWhitespace(
                    text.substr(start, end - start));
                if (!chunk.empty())
                {
                    const std::size_t chunkOrdinal = ordinal++;
                    chunks.push_back(
                        KnowledgeChunkRecord{
                            .ordinal = chunkOrdinal,
                            .sourceLocator =
                                std::string{ baseLocator }
                                + "#chunk="
                                + std::to_string(chunkOrdinal),
                            .text = std::move(chunk)
                        });
                }

                if (end >= text.size()) break;

                const std::size_t overlap = (std::min)(overlapBytes, end - start - 1);
                std::size_t next = safeUtf8Start(text, end - overlap);
                if (next <= start) next = end;
                start = next;
            }
            return chunks;
        }

        void appendWarning(
            ProjectKnowledgeIndexReport& report,
            const std::size_t maximumWarnings,
            std::string warning)
        {
            if (report.warnings.size() < maximumWarnings)
            {
                report.warnings.push_back(std::move(warning));
            }
        }

    }

    ProjectKnowledgeIndexer::ProjectKnowledgeIndexer(
        const ProjectKnowledgeIndexerConfig config,
        ProjectContentReaderRegistry contentReaders)
        : config_{ config }
        , contentReaders_{ std::move(contentReaders) }
    {
        if (config_.maximumFiles == 0
            || config_.maximumFileBytes == 0
            || config_.maximumChunkBytes < 128
            || config_.chunkOverlapBytes >= config_.maximumChunkBytes)
        {
            throw std::invalid_argument{
                "ProjectKnowledgeIndexer received an invalid bound."
            };
        }
    }

    ProjectKnowledgeIndexResult ProjectKnowledgeIndexer::index(
        std::string projectId,
        const std::vector<std::string>& approvedRoots,
        const std::stop_token stopToken) const
    {
        if (projectId.empty())
        {
            throw std::invalid_argument{
                "Project knowledge indexing requires a project id."
            };
        }

        ProjectKnowledgeIndexResult result;
        result.project.projectId = projectId;
        result.project.indexedUnixMilliseconds = currentUnixMilliseconds();
        result.report.projectId = std::move(projectId);

        std::unordered_set<std::string> seenPaths;

        for (const std::string& configuredRoot : approvedRoots)
        {
            if (stopToken.stop_requested())
            {
                result.report.cancelled = true;
                break;
            }

            std::error_code error;
            const std::filesystem::path root =
                std::filesystem::weakly_canonical(
                    std::filesystem::path{ configuredRoot },
                    error);
            if (error || root.empty())
            {
                appendWarning(
                    result.report,
                    config_.maximumWarnings,
                    "Could not resolve approved root: " + configuredRoot);
                continue;
            }
            if (!std::filesystem::is_directory(root, error) || error)
            {
                appendWarning(
                    result.report,
                    config_.maximumWarnings,
                    "Approved root is not a readable directory: " + root.string());
                continue;
            }

            ++result.report.rootsScanned;
            std::filesystem::recursive_directory_iterator iterator{
                root,
                std::filesystem::directory_options::skip_permission_denied,
                error
            };
            const std::filesystem::recursive_directory_iterator end;
            if (error)
            {
                appendWarning(
                    result.report,
                    config_.maximumWarnings,
                    "Could not enumerate approved root: " + root.string());
                continue;
            }

            for (; iterator != end; iterator.increment(error))
            {
                if (stopToken.stop_requested())
                {
                    result.report.cancelled = true;
                    break;
                }

                if (error)
                {
                    appendWarning(
                        result.report,
                        config_.maximumWarnings,
                        "Directory enumeration skipped an unreadable entry under: " + root.string());
                    error.clear();
                    continue;
                }

                const auto& entry = *iterator;
                if (entry.is_directory(error))
                {
                    if (error)
                    {
                        error.clear();
                        continue;
                    }

                    const std::string name = lowerAscii(entry.path().filename().string());
                    if (ignoredDirectoryName(name)
                        || static_cast<std::size_t>(iterator.depth()) >= config_.maximumDepth)
                    {
                        iterator.disable_recursion_pending();
                    }
                    continue;
                }

                if (entry.is_symlink(error))
                {
                    error.clear();
                    ++result.report.filesSkipped;
                    continue;
                }

                if (!entry.is_regular_file(error) || error)
                {
                    error.clear();
                    continue;
                }

                const IProjectContentReader* contentReader =
                    contentReaders_.findReader(entry.path());
                if (contentReader == nullptr)
                {
                    ++result.report.filesSkipped;
                    continue;
                }

                if (result.report.documentsIndexed >= config_.maximumFiles)
                {
                    appendWarning(
                        result.report,
                        config_.maximumWarnings,
                        "Project index reached the configured file limit; remaining files were skipped.");
                    result.report.filesSkipped++;
                    result.report.cancelled = true;
                    break;
                }

                const std::uintmax_t fileSize = entry.file_size(error);
                if (error)
                {
                    error.clear();
                    ++result.report.filesSkipped;
                    continue;
                }
                const std::uintmax_t readerLimit =
                    contentReader->maximumSourceBytes();
                const std::uintmax_t effectiveLimit =
                    contentReader->usesWholeFileByteBudget()
                        ? (std::min)(
                            static_cast<std::uintmax_t>(config_.maximumFileBytes),
                            readerLimit)
                        : readerLimit;
                if (fileSize > effectiveLimit)
                {
                    ++result.report.filesSkipped;
                    appendWarning(
                        result.report,
                        config_.maximumWarnings,
                        "Skipped oversized "
                        + std::string{ contentReader->id() }
                        + " source file: "
                        + entry.path().string());
                    continue;
                }

                const std::filesystem::path absolutePath =
                    std::filesystem::weakly_canonical(entry.path(), error);
                if (error)
                {
                    error.clear();
                    ++result.report.filesSkipped;
                    continue;
                }

                const std::string pathKey = absolutePath.generic_string();
                if (!seenPaths.insert(pathKey).second)
                {
                    continue;
                }

                try
                {
                    ExtractedProjectContent extracted =
                        contentReader->read(absolutePath, fileSize);

                    std::vector<KnowledgeChunkRecord> chunks;
                    for (const ProjectContentSegment& segment : extracted.segments)
                    {
                        std::vector<KnowledgeChunkRecord> segmentChunks =
                            chunkText(
                                segment.text,
                                segment.locator.empty()
                                    ? std::string_view{ "document" }
                                    : std::string_view{ segment.locator },
                                config_.maximumChunkBytes,
                                config_.chunkOverlapBytes);

                        for (KnowledgeChunkRecord& chunk : segmentChunks)
                        {
                            chunk.ordinal = chunks.size();
                            chunks.push_back(std::move(chunk));
                        }
                    }
                    if (chunks.empty())
                    {
                        ++result.report.filesSkipped;
                        continue;
                    }

                    const auto modified = entry.last_write_time(error);
                    const std::int64_t modifiedMilliseconds = error
                        ? 0
                        : fileTimeToUnixMilliseconds(modified);
                    error.clear();

                    result.report.sourceBytes += fileSize;
                    result.report.chunksCreated += chunks.size();
                    ++result.report.documentsIndexed;

                    result.project.documents.push_back(
                        KnowledgeDocumentRecord{
                            .sourcePath = pathKey,
                            .contentKind = std::move(extracted.contentKind),
                            .readerId = std::move(extracted.readerId),
                            .sourceBytes = fileSize,
                            .modifiedUnixMilliseconds = modifiedMilliseconds,
                            .chunks = std::move(chunks)
                        });
                }
                catch (const std::exception& exception)
                {
                    ++result.report.filesSkipped;
                    appendWarning(
                        result.report,
                        config_.maximumWarnings,
                        "Skipped " + absolutePath.string() + ": " + exception.what());
                }
            }

            if (result.report.cancelled)
            {
                break;
            }
        }

        std::sort(
            result.project.documents.begin(),
            result.project.documents.end(),
            [](const KnowledgeDocumentRecord& left,
               const KnowledgeDocumentRecord& right)
            {
                return left.sourcePath < right.sourcePath;
            });

        return result;
    }
}
