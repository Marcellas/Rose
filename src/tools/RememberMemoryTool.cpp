#include "tools/RememberMemoryTool.h"
#include "persistence/IConversationStore.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        const std::string& requiredArgument(
            const ToolRequest& request,
            const std::string_view name)
        {
            const auto found =
                request.arguments.find(
                    std::string{ name });

            if (
                found == request.arguments.end()
                || found->second.empty())
            {
                throw std::invalid_argument{
                    "Tool '"
                    + request.toolId
                    + "' requires argument '"
                    + std::string{ name }
                    + "'."
                };
            }

            return found->second;
        }


        [[nodiscard]]
        std::string_view trimAsciiWhitespace(
            const std::string_view text) noexcept
        {
            constexpr std::string_view whitespace{
                " \t\r\n"
            };

            const std::size_t first =
                text.find_first_not_of(whitespace);

            if (first == std::string_view::npos)
            {
                return {};
            }

            const std::size_t last =
                text.find_last_not_of(whitespace);

            return text.substr(
                first,
                last - first + 1);
        }


        [[nodiscard]]
        std::string cleanedMemoryContent(
            const std::string_view raw)
        {
            std::string_view text =
                trimAsciiWhitespace(raw);

            // Deterministic recovery may pass the user's whole sentence rather
            // than a model-extracted content argument. Strip only very obvious
            // request wrappers; never rewrite the actual fact/preference.
            static constexpr std::string_view prefixes[]{
                "please remember that ",
                "remember that ",
                "please remember ",
                "remember ",
                "save this memory: ",
                "store this memory: "
            };

            for (const std::string_view prefix : prefixes)
            {
                if (
                    text.size() >= prefix.size())
                {
                    bool matches{ true };

                    for (std::size_t index = 0;
                         index < prefix.size();
                         ++index)
                    {
                        char left = text[index];
                        char right = prefix[index];

                        if (left >= 'A' && left <= 'Z')
                        {
                            left = static_cast<char>(left - 'A' + 'a');
                        }

                        if (left != right)
                        {
                            matches = false;
                            break;
                        }
                    }

                    if (matches)
                    {
                        text =
                            trimAsciiWhitespace(
                                text.substr(prefix.size()));
                        break;
                    }
                }
            }

            return std::string{ text };
        }


        [[nodiscard]] bool referencesPriorDiscussion(std::string_view text)
        {
            std::string lower{ text };
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.find("entire history") != std::string::npos
                || lower.find("entire timeline") != std::string::npos
                || lower.find("timeline that was provided") != std::string::npos
                || lower.find("this discussion") != std::string::npos
                || lower.find("what we discussed") != std::string::npos
                || lower.find("summary about the six files") != std::string::npos;
        }


        [[nodiscard]] bool hasYear(const std::string_view line) noexcept
        {
            for (std::size_t i = 0; i + 3 < line.size(); ++i)
            {
                if ((line.substr(i, 2) == "19" || line.substr(i, 2) == "20")
                    && std::isdigit(static_cast<unsigned char>(line[i + 2]))
                    && std::isdigit(static_cast<unsigned char>(line[i + 3])))
                {
                    return true;
                }
            }
            return false;
        }


        [[nodiscard]] std::string userTimeline(
            persistence::IConversationStore& store,
            std::size_t& savedEntries)
        {
            constexpr std::size_t maximumMemoryBytes{ 48u * 1024u };
            std::string timeline =
                "User-provided timeline from this discussion (verbatim entries; "
                "not independently verified):\n";
            std::unordered_set<std::string> seen;
            const auto turns = store.loadTurns();
            for (const auto& turn : turns)
            {
                std::istringstream lines{ turn.userText };
                std::string line;
                while (std::getline(lines, line))
                {
                    const std::string_view trimmed = trimAsciiWhitespace(line);
                    if (!hasYear(trimmed) || trimmed.size() > 2048)
                        continue;
                    std::string entry{ trimmed };
                    if (!seen.insert(entry).second) continue;
                    if (timeline.size() + entry.size() + 1 > maximumMemoryBytes)
                    {
                        throw std::runtime_error{
                            "The dated discussion entries exceed one memory record. "
                            "Please select a narrower timeline range. Nothing was saved."
                        };
                    }
                    timeline += entry + "\n";
                    ++savedEntries;
                }
            }
            return timeline;
        }
    }


    RememberMemoryTool::RememberMemoryTool(
        memory::MemoryRepository& repository,
        persistence::IConversationStore* conversationStore)
        : repository_{ repository }
        , conversationStore_{ conversationStore }
        , descriptor_{
            .id = "remember_memory",
            .displayName = "Remember Memory",
            .description =
                "Store one durable local memory only when the user explicitly asks "
                "Rose to remember, save, or retain a fact, preference, or project "
                "note for future conversations. Do not use for ordinary statements.",
            .risk = ToolRisk::LocalWrite,
            .consent = ToolConsent::AutoAllowed,
            .parameters = {
                ToolParameterDescriptor{
                    .name = "content",
                    .description =
                        "The fact, preference, or project note to remember. Omit the "
                        "request wrapper such as 'remember that'.",
                    .type = ToolValueType::String,
                    .required = true
                }
            }
        }
    {
    }


    const ToolDescriptor&
        RememberMemoryTool::descriptor() const noexcept
    {
        return descriptor_;
    }


    ToolResult RememberMemoryTool::execute(
        const ToolRequest& request)
    {
        std::string content =
            cleanedMemoryContent(
                requiredArgument(
                    request,
                    "content"));

        if (content.empty())
        {
            throw std::invalid_argument{
                "The memory content is empty after removing the remember-request wrapper."
            };
        }

        // When routing supplies a compound read-and-remember request verbatim,
        // save only the user-authored dated timeline. Never store the PDF
        // instructions or pretend document assertions were verified.
        std::string lowerContent = content;
        std::transform(lowerContent.begin(), lowerContent.end(),
            lowerContent.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c)); });
        if (lowerContent.find("remember timeline:") != std::string::npos
            || lowerContent.find("submit facts to memory") != std::string::npos)
        {
            std::istringstream lines{ content };
            std::string line;
            std::string timeline =
                "User-provided timeline (not independently verified):\n";
            std::size_t entries{ 0 };
            while (std::getline(lines, line))
            {
                const std::string_view trimmed = trimAsciiWhitespace(line);
                if (!trimmed.starts_with("-") || !hasYear(trimmed)) continue;
                timeline.append(trimmed);
                timeline += '\n';
                ++entries;
            }
            if (entries == 0u)
                throw std::invalid_argument{
                    "No dated user timeline entries were found to remember. "
                    "The compound read instruction was not saved as a memory." };
            content = std::move(timeline);
        }

        if (referencesPriorDiscussion(content))
        {
            if (conversationStore_ == nullptr)
            {
                throw std::invalid_argument{
                    "Rose needs the active discussion to resolve that memory request. "
                    "No instruction text was saved."
                };
            }

            const std::string lower = [&]() {
                std::string value = content;
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            }();
            if (lower.find("timeline") == std::string::npos
                && lower.find("history") == std::string::npos)
            {
                throw std::invalid_argument{
                    "Please name the facts or timeline to remember. "
                    "Rose did not save the memory instruction itself."
                };
            }

            std::size_t savedEntries{ 0 };
            const std::string timeline = userTimeline(
                *conversationStore_, savedEntries);
            if (savedEntries < 3)
            {
                throw std::invalid_argument{
                    "Rose could not find a dated user-provided timeline in this "
                    "discussion. No memory instruction was saved."
                };
            }

            // Retrieval currently has a 4 KiB combined budget. Keep each
            // record below that threshold so a remembered long timeline can
            // actually be found in later questions.
            constexpr std::size_t maximumChunkBytes{ 3000 };
            const std::string prefix =
                "User-provided discussion timeline (not independently verified):\n";
            std::vector<std::string> chunks;
            std::string chunk = prefix;
            std::istringstream lines{ timeline };
            std::string line;
            std::getline(lines, line); // provenance header
            while (std::getline(lines, line))
            {
                if (chunk.size() + line.size() + 1 > maximumChunkBytes)
                {
                    if (chunk == prefix)
                        throw std::runtime_error{ "One timeline entry exceeds the memory index window." };
                    chunks.push_back(std::move(chunk));
                    chunk = prefix;
                }
                chunk += line + '\n';
            }
            if (chunk != prefix) chunks.push_back(std::move(chunk));
            std::size_t created{ 0 };
            for (std::size_t index = 0; index < chunks.size(); ++index)
            {
                chunks[index].insert(0, "Part " + std::to_string(index + 1)
                    + " of " + std::to_string(chunks.size()) + ".\n");
                const auto result = repository_.remember(
                    chunks[index], memory::MemoryKind::ExplicitUser,
                    "discussion-user-timeline");
                if (result.created) ++created;
            }
            return ToolResult{
                .success = true,
                .message = (created ? "Saved " : "Already saved ")
                    + std::to_string(savedEntries)
                    + " distinct user-authored dated entries across "
                    + std::to_string(chunks.size()) + " searchable memory part(s). "
                      "They are recorded as user statements, not verified findings. "
                      "No six-file summary was saved; the requested source files "
                      "must be read and checked before one can be catalogued.",
                .responseMode = ToolResponseMode::AuthoritativeCompletion,
                .artifacts = {}
            };
        }

        // Long user-supplied timelines must remain retrievable under the 4 KiB
        // memory search budget. Split only at line boundaries, in order.
        if (content.size() > 3000u && content.find('\n') != std::string::npos)
        {
            std::vector<std::string> chunks;
            std::istringstream lines{ content };
            std::string line;
            std::string chunk;
            while (std::getline(lines, line))
            {
                if (line.size() + 1u > 2800u)
                    throw std::runtime_error{
                        "One memory line exceeds the retrievable memory window." };
                if (chunk.size() + line.size() + 1u > 2800u)
                {
                    chunks.push_back(std::move(chunk));
                    chunk.clear();
                }
                chunk += line + '\n';
            }
            if (!chunk.empty()) chunks.push_back(std::move(chunk));
            std::size_t created{ 0 };
            for (std::size_t i = 0; i < chunks.size(); ++i)
            {
                const auto saved = repository_.remember(
                    "Part " + std::to_string(i + 1u) + " of "
                    + std::to_string(chunks.size()) + ".\n" + chunks[i],
                    memory::MemoryKind::ExplicitUser,
                    "explicit-user-request");
                if (saved.created) ++created;
            }
            return ToolResult{
                .success = true,
                .message = "Saved " + std::to_string(chunks.size())
                    + " ordered memory part(s) from the user's supplied text ("
                    + std::to_string(created) + " new).",
                .responseMode = ToolResponseMode::AuthoritativeCompletion,
                .artifacts = {}
            };
        }

        const memory::RememberMemoryResult result = repository_.remember(
            content, memory::MemoryKind::ExplicitUser, "explicit-user-request");

        return ToolResult{
            .success = true,
            .message =
                result.created
                    ? "Saved that to Rose's local long-term memory."
                    : "That is already in Rose's local long-term memory.",
            .responseMode =
                ToolResponseMode::AuthoritativeCompletion,
            .artifacts = {}
        };
    }

} // namespace rose::tools
