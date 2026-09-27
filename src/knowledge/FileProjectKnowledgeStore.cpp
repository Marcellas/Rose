#include "knowledge/FileProjectKnowledgeStore.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace rose::knowledge
{
    namespace
    {
        constexpr std::string_view header{
            "ROSE_PROJECT_KNOWLEDGE_V1"
        };

        constexpr std::size_t maximumProjects{ 10000 };
        constexpr std::size_t maximumDocumentsPerProject{ 100000 };
        constexpr std::size_t maximumChunksPerDocument{ 100000 };
        constexpr std::size_t maximumStringBytes{ 8u * 1024u * 1024u };

        [[nodiscard]]
        std::string readLine(
            std::istream& input,
            const std::string_view what)
        {
            std::string line;
            if (!std::getline(input, line))
            {
                throw std::runtime_error{
                    "Project knowledge index ended while reading "
                    + std::string{ what }
                    + "."
                };
            }
            return line;
        }

        template <typename Integer>
        [[nodiscard]]
        Integer parseInteger(
            const std::string_view text,
            const std::string_view what)
        {
            Integer value{};
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            const auto result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                throw std::runtime_error{
                    "Project knowledge index contains an invalid "
                    + std::string{ what }
                    + "."
                };
            }
            return value;
        }

        [[nodiscard]]
        std::size_t readCount(
            std::istream& input,
            const std::size_t maximum,
            const std::string_view what)
        {
            const auto value = parseInteger<unsigned long long>(
                readLine(input, what),
                what);
            if (value > maximum)
            {
                throw std::runtime_error{
                    "Project knowledge index "
                    + std::string{ what }
                    + " exceeds Rose's safety limit."
                };
            }
            return static_cast<std::size_t>(value);
        }

        [[nodiscard]]
        std::string readString(
            std::istream& input,
            const std::string_view what)
        {
            const std::size_t size = readCount(
                input,
                maximumStringBytes,
                what);

            std::string value(size, '\0');
            if (size > 0)
            {
                input.read(
                    value.data(),
                    static_cast<std::streamsize>(size));
                if (input.gcount() != static_cast<std::streamsize>(size))
                {
                    throw std::runtime_error{
                        "Project knowledge index ended inside "
                        + std::string{ what }
                        + "."
                    };
                }
            }

            const int delimiter = input.get();
            if (delimiter != '\n')
            {
                throw std::runtime_error{
                    "Project knowledge index has an invalid string delimiter."
                };
            }
            return value;
        }

        void writeString(
            std::ostream& output,
            const std::string_view value)
        {
            output << value.size() << '\n';
            output.write(
                value.data(),
                static_cast<std::streamsize>(value.size()));
            output << '\n';
        }

        [[nodiscard]]
        ProjectKnowledgeSnapshot readSnapshot(
            std::istream& input)
        {
            if (readLine(input, "header") != header)
            {
                throw std::runtime_error{
                    "Project knowledge index has an unsupported format."
                };
            }

            ProjectKnowledgeSnapshot snapshot;
            const std::size_t projectCount = readCount(
                input,
                maximumProjects,
                "project count");
            snapshot.projects.reserve(projectCount);

            for (std::size_t projectIndex = 0;
                 projectIndex < projectCount;
                 ++projectIndex)
            {
                ProjectKnowledgeRecord project;
                project.projectId = readString(input, "project id");
                project.indexedUnixMilliseconds =
                    parseInteger<std::int64_t>(
                        readLine(input, "project timestamp"),
                        "project timestamp");

                const std::size_t documentCount = readCount(
                    input,
                    maximumDocumentsPerProject,
                    "document count");
                project.documents.reserve(documentCount);

                for (std::size_t documentIndex = 0;
                     documentIndex < documentCount;
                     ++documentIndex)
                {
                    KnowledgeDocumentRecord document;
                    document.sourcePath = readString(input, "source path");
                    document.contentKind = readString(input, "content kind");
                    document.readerId = readString(input, "reader id");
                    document.sourceBytes =
                        parseInteger<std::uintmax_t>(
                            readLine(input, "source byte count"),
                            "source byte count");
                    document.modifiedUnixMilliseconds =
                        parseInteger<std::int64_t>(
                            readLine(input, "source timestamp"),
                            "source timestamp");

                    const std::size_t chunkCount = readCount(
                        input,
                        maximumChunksPerDocument,
                        "chunk count");
                    document.chunks.reserve(chunkCount);

                    for (std::size_t chunkIndex = 0;
                         chunkIndex < chunkCount;
                         ++chunkIndex)
                    {
                        KnowledgeChunkRecord chunk;
                        chunk.ordinal = readCount(
                            input,
                            maximumChunksPerDocument,
                            "chunk ordinal");
                        chunk.sourceLocator = readString(input, "chunk source locator");
                        chunk.text = readString(input, "chunk text");
                        document.chunks.push_back(std::move(chunk));
                    }

                    project.documents.push_back(std::move(document));
                }

                snapshot.projects.push_back(std::move(project));
            }

            return snapshot;
        }
    }

    FileProjectKnowledgeStore::FileProjectKnowledgeStore(
        std::filesystem::path path)
        : path_{ std::move(path) }
    {
        if (path_.empty())
        {
            throw std::invalid_argument{
                "Project knowledge store path cannot be empty."
            };
        }
    }

    ProjectKnowledgeSnapshot FileProjectKnowledgeStore::load()
    {
        std::error_code error;
        if (!std::filesystem::exists(path_, error))
        {
            if (error)
            {
                throw std::runtime_error{
                    "Could not inspect project knowledge index: "
                    + error.message()
                };
            }
            return {};
        }

        std::ifstream input{ path_, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{
                "Could not open project knowledge index: "
                + path_.string()
            };
        }
        return readSnapshot(input);
    }

    void FileProjectKnowledgeStore::save(
        const ProjectKnowledgeSnapshot& snapshot)
    {
        std::error_code error;
        if (path_.has_parent_path())
        {
            std::filesystem::create_directories(
                path_.parent_path(),
                error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not create project knowledge directory: "
                    + error.message()
                };
            }
        }

        std::filesystem::path temporary = path_;
        temporary += ".tmp";

        {
            std::ofstream output{
                temporary,
                std::ios::binary | std::ios::trunc
            };
            if (!output)
            {
                throw std::runtime_error{
                    "Could not create temporary project knowledge index: "
                    + temporary.string()
                };
            }

            output << header << '\n';
            output << snapshot.projects.size() << '\n';

            for (const ProjectKnowledgeRecord& project : snapshot.projects)
            {
                writeString(output, project.projectId);
                output << project.indexedUnixMilliseconds << '\n';
                output << project.documents.size() << '\n';

                for (const KnowledgeDocumentRecord& document : project.documents)
                {
                    writeString(output, document.sourcePath);
                    writeString(output, document.contentKind);
                    writeString(output, document.readerId);
                    output << document.sourceBytes << '\n';
                    output << document.modifiedUnixMilliseconds << '\n';
                    output << document.chunks.size() << '\n';

                    for (const KnowledgeChunkRecord& chunk : document.chunks)
                    {
                        output << chunk.ordinal << '\n';
                        writeString(output, chunk.sourceLocator);
                        writeString(output, chunk.text);
                    }
                }
            }

            output.flush();
            if (!output)
            {
                throw std::runtime_error{
                    "Could not finish writing project knowledge index."
                };
            }
        }

        // Windows rename does not replace an existing destination. Remove the old
        // cache only after the new complete sibling has been closed successfully.
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary, path_, error);
        if (error)
        {
            std::filesystem::remove(temporary);
            throw std::runtime_error{
                "Could not publish project knowledge index: "
                + error.message()
            };
        }
    }

    const std::filesystem::path&
        FileProjectKnowledgeStore::path() const noexcept
    {
        return path_;
    }
}
