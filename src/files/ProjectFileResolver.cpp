#include "files/ProjectFileResolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

namespace rose::files
{
    namespace
    {
        [[nodiscard]]
        std::string lowerCopy(
            const std::string_view text)
        {
            std::string result{ text };
            std::transform(
                result.begin(),
                result.end(),
                result.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
            return result;
        }


        [[nodiscard]]
        bool looksLikeAbsoluteWindowsPath(
            const std::string_view text) noexcept
        {
            if (text.size() >= 3)
            {
                const unsigned char drive =
                    static_cast<unsigned char>(text[0]);

                const bool asciiLetter =
                    (drive >= static_cast<unsigned char>('A')
                     && drive <= static_cast<unsigned char>('Z'))
                    || (drive >= static_cast<unsigned char>('a')
                        && drive <= static_cast<unsigned char>('z'));

                if (asciiLetter
                    && text[1] == ':'
                    && (text[2] == '\\' || text[2] == '/'))
                {
                    return true;
                }
            }

            return text.starts_with("\\\\");
        }


        [[nodiscard]]
        bool plausibleRelativeFileReference(
            const std::string_view text)
        {
            if (text.empty()
                || looksLikeAbsoluteWindowsPath(text)
                || text.find("://") != std::string_view::npos
                || text.find('@') != std::string_view::npos)
            {
                return false;
            }

            const std::filesystem::path path{ std::string{ text } };
            const std::string extension = path.extension().string();

            if (extension.size() < 2 || extension.size() > 12)
            {
                return false;
            }

            bool hasExtensionLetter{ false };
            for (std::size_t i = 1; i < extension.size(); ++i)
            {
                const unsigned char value =
                    static_cast<unsigned char>(extension[i]);

                if (std::isalpha(value) != 0)
                {
                    hasExtensionLetter = true;
                    break;
                }
            }

            return hasExtensionLetter;
        }


        [[nodiscard]]
        std::string trimReferencePunctuation(
            std::string token)
        {
            while (!token.empty()
                   && (token.front() == '('
                       || token.front() == '['
                       || token.front() == '{'
                       || token.front() == '<'
                       || token.front() == '"'
                       || token.front() == '\''))
            {
                token.erase(token.begin());
            }

            while (!token.empty()
                   && (token.back() == ')'
                       || token.back() == ']'
                       || token.back() == '}'
                       || token.back() == '>'
                       || token.back() == ','
                       || token.back() == ';'
                       || token.back() == ':'
                       || token.back() == '!'
                       || token.back() == '?'
                       || token.back() == '.'
                       || token.back() == '"'
                       || token.back() == '\''))
            {
                token.pop_back();
            }

            return token;
        }


        [[nodiscard]]
        std::vector<std::string> extractRelativeReferences(
            const std::string_view userText)
        {
            std::vector<std::string> references;
            std::unordered_set<std::string> seen;

            const auto addReference =
                [&](std::string candidate)
                {
                    candidate = trimReferencePunctuation(std::move(candidate));
                    if (!plausibleRelativeFileReference(candidate))
                    {
                        return;
                    }

                    const std::string key = lowerCopy(candidate);
                    if (seen.insert(key).second)
                    {
                        references.push_back(std::move(candidate));
                    }
                };

            // Quoted relative paths may contain spaces.
            for (const char quote : { '\'', '"' })
            {
                std::size_t begin{ 0 };
                while ((begin = userText.find(quote, begin)) != std::string_view::npos)
                {
                    const std::size_t end = userText.find(quote, begin + 1);
                    if (end == std::string_view::npos)
                    {
                        break;
                    }

                    addReference(
                        std::string{
                            userText.substr(begin + 1, end - begin - 1)
                        });
                    begin = end + 1;
                }
            }

            // Common unquoted form: "Read Dunamis.docx and summarize it." This
            // intentionally does not try to infer unquoted filenames containing
            // spaces; users can quote those names and Rose will resolve them.
            std::size_t position{ 0 };
            while (position < userText.size())
            {
                while (position < userText.size()
                       && std::isspace(
                              static_cast<unsigned char>(userText[position])) != 0)
                {
                    ++position;
                }

                const std::size_t begin = position;
                while (position < userText.size()
                       && std::isspace(
                              static_cast<unsigned char>(userText[position])) == 0)
                {
                    ++position;
                }

                if (position > begin)
                {
                    const std::string token{
                        userText.substr(begin, position - begin)
                    };

                    // Quoted references were already captured as one logical
                    // filename above. Do not re-add the individual whitespace
                    // tokens from inside that quoted span.
                    if (token.find('\"') == std::string::npos
                        && token.find('\'') == std::string::npos)
                    {
                        addReference(token);
                    }
                }
            }

            return references;
        }


        [[nodiscard]]
        std::string normalizedAbsoluteString(
            const std::filesystem::path& path)
        {
            std::error_code error;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(path, error);

            if (!error)
            {
                return canonical.string();
            }

            return std::filesystem::absolute(path, error).string();
        }


        void addMatch(
            std::vector<std::string>& matches,
            const std::filesystem::path& path,
            const std::size_t maximumMatches)
        {
            if (matches.size() >= maximumMatches)
            {
                return;
            }

            const std::string normalized = normalizedAbsoluteString(path);
            const std::string key = lowerCopy(normalized);

            const bool duplicate =
                std::any_of(
                    matches.begin(),
                    matches.end(),
                    [&](const std::string& existing)
                    {
                        return lowerCopy(existing) == key;
                    });

            if (!duplicate)
            {
                matches.push_back(normalized);
            }
        }


        [[nodiscard]]
        ProjectFileResolution makeResolution(
            std::string requested,
            const std::vector<std::string>& matches,
            const std::size_t scannedEntries,
            const bool searchLimitReached)
        {
            ProjectFileResolution resolution;
            resolution.requestedReference = std::move(requested);
            resolution.matchCount = matches.size();
            resolution.scannedEntries = scannedEntries;

            if (matches.size() == 1)
            {
                resolution.status = ProjectFileResolutionStatus::Unique;
                resolution.absolutePath = matches.front();
                return resolution;
            }

            if (matches.size() > 1)
            {
                resolution.status = ProjectFileResolutionStatus::Ambiguous;
                return resolution;
            }

            resolution.status =
                searchLimitReached
                    ? ProjectFileResolutionStatus::SearchLimitReached
                    : ProjectFileResolutionStatus::NotFound;
            return resolution;
        }


        [[nodiscard]]
        std::string_view statusText(
            const ProjectFileResolutionStatus status) noexcept
        {
            switch (status)
            {
            case ProjectFileResolutionStatus::Unique:
                return "unique";
            case ProjectFileResolutionStatus::Ambiguous:
                return "ambiguous";
            case ProjectFileResolutionStatus::NotFound:
                return "not_found";
            case ProjectFileResolutionStatus::SearchLimitReached:
                return "search_limit_reached";
            }

            return "not_found";
        }
    }


    ProjectFileResolver::ProjectFileResolver(
        const ProjectFileResolverConfig config)
        : config_{ config }
    {
        if (config_.maximumMatchesPerReference == 0)
        {
            throw std::invalid_argument{
                "ProjectFileResolver maximumMatchesPerReference must be greater than zero."
            };
        }
    }


    std::vector<ProjectFileResolution> ProjectFileResolver::resolve(
        const std::string_view userText,
        const std::vector<std::string>& approvedRoots) const
    {
        const std::vector<std::string> references =
            extractRelativeReferences(userText);

        std::vector<ProjectFileResolution> resolutions;
        resolutions.reserve(references.size());

        for (const std::string& reference : references)
        {
            std::vector<std::string> matches;

            // Prefer a direct path beneath an approved root. This keeps the common
            // "Desktop/Dunamis.docx" case effectively constant-time even when the
            // approved root also contains large source/build trees.
            for (const std::string& rootText : approvedRoots)
            {
                const std::filesystem::path root{ rootText };
                std::error_code error;

                if (!std::filesystem::is_directory(root, error) || error)
                {
                    continue;
                }

                const std::filesystem::path direct =
                    root / std::filesystem::path{ reference };

                error.clear();
                if (std::filesystem::is_regular_file(direct, error) && !error)
                {
                    addMatch(
                        matches,
                        direct,
                        config_.maximumMatchesPerReference);
                }
            }

            if (!matches.empty())
            {
                resolutions.push_back(
                    makeResolution(reference, matches, 0, false));
                continue;
            }

            // A relative path with directories is intentionally not recursively
            // reinterpreted as a basename. Either root/reference exists or Rose
            // reports it unresolved.
            const std::filesystem::path relativePath{ reference };
            if (relativePath.has_parent_path())
            {
                resolutions.push_back(
                    makeResolution(reference, matches, 0, false));
                continue;
            }

            std::size_t scannedEntries{ 0 };
            bool searchLimitReached{ false };
            const std::string requestedLower = lowerCopy(reference);

            for (const std::string& rootText : approvedRoots)
            {
                if (matches.size() >= config_.maximumMatchesPerReference)
                {
                    break;
                }

                const std::filesystem::path root{ rootText };
                std::error_code error;

                if (!std::filesystem::is_directory(root, error) || error)
                {
                    continue;
                }

                std::filesystem::recursive_directory_iterator iterator{
                    root,
                    std::filesystem::directory_options::skip_permission_denied,
                    error
                };
                const std::filesystem::recursive_directory_iterator end;

                if (error)
                {
                    continue;
                }

                while (iterator != end)
                {
                    if (scannedEntries >= config_.maximumRecursiveEntries)
                    {
                        searchLimitReached = true;
                        break;
                    }

                    ++scannedEntries;

                    const std::filesystem::directory_entry entry = *iterator;
                    error.clear();

                    if (entry.is_regular_file(error)
                        && !error
                        && lowerCopy(entry.path().filename().string())
                            == requestedLower)
                    {
                        addMatch(
                            matches,
                            entry.path(),
                            config_.maximumMatchesPerReference);
                    }

                    iterator.increment(error);
                    if (error)
                    {
                        error.clear();
                    }
                }

                if (searchLimitReached)
                {
                    break;
                }
            }

            resolutions.push_back(
                makeResolution(
                    reference,
                    matches,
                    scannedEntries,
                    searchLimitReached));
        }

        return resolutions;
    }


    std::string ProjectFileResolver::buildTransientContext(
        const std::vector<ProjectFileResolution>& resolutions)
    {
        if (resolutions.empty())
        {
            return {};
        }

        std::ostringstream text;
        text
            << "<rose_project_file_resolutions>\n"
            << "source=active_project_approved_roots\n"
            << "Paths below are Rose-resolved evidence, not permission grants.\n"
            << "Use a unique absolute_path for an exact-file reader when it matches "
               "the requested reference.\n"
            << "For ambiguous, not_found, or search_limit_reached results, do not "
               "guess a path; ask the user for a more specific path.\n";

        for (const ProjectFileResolution& resolution : resolutions)
        {
            text
                << "<rose_project_file_resolution>\n"
                << "requested=" << resolution.requestedReference << '\n'
                << "status=" << statusText(resolution.status) << '\n'
                << "matches=" << resolution.matchCount << '\n'
                << "scanned_entries=" << resolution.scannedEntries << '\n';

            if (resolution.status == ProjectFileResolutionStatus::Unique)
            {
                text
                    << "absolute_path=" << resolution.absolutePath << '\n';
            }

            text << "</rose_project_file_resolution>\n";
        }

        text << "</rose_project_file_resolutions>";
        return text.str();
    }

} // namespace rose::files
