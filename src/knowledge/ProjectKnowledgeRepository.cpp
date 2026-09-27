#include "knowledge/ProjectKnowledgeRepository.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace rose::knowledge
{
    namespace
    {
        [[nodiscard]]
        std::string lowerAscii(
            std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (const unsigned char value : text)
            {
                result.push_back(
                    static_cast<char>(std::tolower(value)));
            }
            return result;
        }

        [[nodiscard]]
        std::vector<std::string> queryTokens(
            const std::string_view query)
        {
            std::vector<std::string> tokens;
            std::unordered_set<std::string> seen;
            std::string current;

            auto flush = [&]()
            {
                if (current.size() >= 2 && seen.insert(current).second)
                {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
                else
                {
                    current.clear();
                }
            };

            for (const unsigned char value : query)
            {
                if (std::isalnum(value) != 0 || value == '_' || value == '-')
                {
                    current.push_back(
                        static_cast<char>(std::tolower(value)));
                }
                else
                {
                    flush();
                }
            }
            flush();
            return tokens;
        }

        [[nodiscard]]
        std::size_t countOccurrences(
            const std::string_view text,
            const std::string_view needle)
        {
            if (needle.empty())
            {
                return 0;
            }

            std::size_t count{ 0 };
            std::size_t position{ 0 };
            while ((position = text.find(needle, position)) != std::string_view::npos)
            {
                ++count;
                position += needle.size();
            }
            return count;
        }

        [[nodiscard]]
        double scoreChunk(
            const std::string& lowerPath,
            const std::string& lowerText,
            const std::string& lowerQuery,
            const std::vector<std::string>& tokens)
        {
            if (tokens.empty())
            {
                return 0.0;
            }

            double score{ 0.0 };
            std::size_t matchedTokens{ 0 };

            if (!lowerQuery.empty() && lowerText.find(lowerQuery) != std::string::npos)
            {
                score += 18.0;
            }

            for (const std::string& token : tokens)
            {
                const std::size_t occurrences =
                    countOccurrences(lowerText, token);
                if (occurrences > 0)
                {
                    ++matchedTokens;
                    score += 2.0 * static_cast<double>((std::min)(occurrences, std::size_t{ 4 }));
                }

                if (lowerPath.find(token) != std::string::npos)
                {
                    score += 4.0;
                }
            }

            const double coverage =
                static_cast<double>(matchedTokens)
                / static_cast<double>(tokens.size());
            score += coverage * 8.0;

            return score;
        }
    }

    ProjectKnowledgeRepository::ProjectKnowledgeRepository(
        IProjectKnowledgeStore& store)
        : store_{ store }
        , snapshot_{ store_.load() }
    {
        validateLoadedSnapshot();
    }

    void ProjectKnowledgeRepository::replaceProject(
        ProjectKnowledgeRecord project)
    {
        if (project.projectId.empty())
        {
            throw std::invalid_argument{
                "Project knowledge requires a project id."
            };
        }

        std::unique_lock lock{ mutex_ };
        auto found = std::find_if(
            snapshot_.projects.begin(),
            snapshot_.projects.end(),
            [&](const ProjectKnowledgeRecord& existing)
            {
                return existing.projectId == project.projectId;
            });

        if (found == snapshot_.projects.end())
        {
            snapshot_.projects.push_back(std::move(project));
        }
        else
        {
            *found = std::move(project);
        }
        persistLocked();
    }

    void ProjectKnowledgeRepository::clearProject(
        const std::string_view projectId)
    {
        std::unique_lock lock{ mutex_ };
        const auto oldSize = snapshot_.projects.size();
        std::erase_if(
            snapshot_.projects,
            [&](const ProjectKnowledgeRecord& project)
            {
                return project.projectId == projectId;
            });

        if (snapshot_.projects.size() != oldSize)
        {
            persistLocked();
        }
    }

    ProjectKnowledgeStats ProjectKnowledgeRepository::stats(
        const std::string_view projectId) const
    {
        std::shared_lock lock{ mutex_ };
        const auto found = std::find_if(
            snapshot_.projects.begin(),
            snapshot_.projects.end(),
            [&](const ProjectKnowledgeRecord& project)
            {
                return project.projectId == projectId;
            });

        if (found == snapshot_.projects.end())
        {
            return {};
        }

        ProjectKnowledgeStats result;
        result.documentCount = found->documents.size();
        result.indexedUnixMilliseconds = found->indexedUnixMilliseconds;

        for (const KnowledgeDocumentRecord& document : found->documents)
        {
            result.sourceBytes += document.sourceBytes;
            result.chunkCount += document.chunks.size();
        }
        return result;
    }

    std::vector<KnowledgeSearchHit> ProjectKnowledgeRepository::search(
        const std::string_view projectId,
        const std::string_view query,
        const std::size_t maximumResults) const
    {
        if (maximumResults == 0 || query.empty())
        {
            return {};
        }

        const std::string lowerQuery = lowerAscii(query);
        const std::vector<std::string> tokens = queryTokens(query);
        if (tokens.empty())
        {
            return {};
        }

        std::vector<KnowledgeSearchHit> hits;
        std::shared_lock lock{ mutex_ };

        const auto project = std::find_if(
            snapshot_.projects.begin(),
            snapshot_.projects.end(),
            [&](const ProjectKnowledgeRecord& record)
            {
                return record.projectId == projectId;
            });
        if (project == snapshot_.projects.end())
        {
            return {};
        }

        for (const KnowledgeDocumentRecord& document : project->documents)
        {
            const std::string lowerPath = lowerAscii(document.sourcePath);
            for (const KnowledgeChunkRecord& chunk : document.chunks)
            {
                const std::string lowerText = lowerAscii(chunk.text);
                const double score = scoreChunk(
                    lowerPath,
                    lowerText,
                    lowerQuery,
                    tokens);

                if (score <= 0.0)
                {
                    continue;
                }

                hits.push_back(
                    KnowledgeSearchHit{
                        .sourcePath = document.sourcePath,
                        .sourceLocator = chunk.sourceLocator,
                        .contentKind = document.contentKind,
                        .readerId = document.readerId,
                        .chunkOrdinal = chunk.ordinal,
                        .score = score,
                        .excerpt = chunk.text
                    });
            }
        }

        std::sort(
            hits.begin(),
            hits.end(),
            [](const KnowledgeSearchHit& left,
               const KnowledgeSearchHit& right)
            {
                if (std::abs(left.score - right.score) > 0.0001)
                {
                    return left.score > right.score;
                }
                if (left.sourcePath != right.sourcePath)
                {
                    return left.sourcePath < right.sourcePath;
                }
                return left.chunkOrdinal < right.chunkOrdinal;
            });

        if (hits.size() > maximumResults)
        {
            hits.resize(maximumResults);
        }
        return hits;
    }

    void ProjectKnowledgeRepository::validateLoadedSnapshot() const
    {
        std::unordered_set<std::string> projectIds;
        for (const ProjectKnowledgeRecord& project : snapshot_.projects)
        {
            if (project.projectId.empty())
            {
                throw std::runtime_error{
                    "Project knowledge index contains an empty project id."
                };
            }
            if (!projectIds.insert(project.projectId).second)
            {
                throw std::runtime_error{
                    "Project knowledge index contains duplicate project ids."
                };
            }

            std::unordered_set<std::string> sourcePaths;
            for (const KnowledgeDocumentRecord& document : project.documents)
            {
                if (document.sourcePath.empty())
                {
                    throw std::runtime_error{
                        "Project knowledge index contains an empty source path."
                    };
                }
                if (!sourcePaths.insert(document.sourcePath).second)
                {
                    throw std::runtime_error{
                        "Project knowledge index contains duplicate source paths."
                    };
                }
                if (document.contentKind.empty() || document.readerId.empty())
                {
                    throw std::runtime_error{
                        "Project knowledge index contains missing reader provenance."
                    };
                }

                for (const KnowledgeChunkRecord& chunk : document.chunks)
                {
                    if (chunk.text.empty() || chunk.sourceLocator.empty())
                    {
                        throw std::runtime_error{
                            "Project knowledge index contains incomplete chunk provenance."
                        };
                    }
                }
            }
        }
    }

    void ProjectKnowledgeRepository::persistLocked()
    {
        store_.save(snapshot_);
    }
}
