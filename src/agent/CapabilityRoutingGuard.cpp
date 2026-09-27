#include "agent/CapabilityRoutingGuard.h"

#include "files/FileFormatCatalog.h"

#include "tools/ToolRegistry.h"
#include "tools/ToolTypes.h"

#include <cctype>
#include <initializer_list>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>


namespace rose::agent
{
    namespace
    {
        [[nodiscard]]
        std::string asciiLower(
            const std::string_view text)
        {
            std::string lowered;
            lowered.reserve(
                text.size());

            for (const unsigned char character : text)
            {
                if (
                    character >= static_cast<unsigned char>('A')
                    && character <= static_cast<unsigned char>('Z'))
                {
                    lowered.push_back(
                        static_cast<char>(
                            character
                            - static_cast<unsigned char>('A')
                            + static_cast<unsigned char>('a')));
                }
                else
                {
                    lowered.push_back(
                        static_cast<char>(
                            character));
                }
            }

            return lowered;
        }


        [[nodiscard]]
        bool isAsciiWordCharacter(
            const unsigned char character) noexcept
        {
            return
                std::isalnum(character) != 0
                || character == static_cast<unsigned char>('_');
        }


        [[nodiscard]]
        bool containsAsciiWord(
            const std::string_view lowerText,
            const std::string_view lowerWord) noexcept
        {
            if (
                lowerText.empty()
                || lowerWord.empty()
                || lowerWord.size() > lowerText.size())
            {
                return false;
            }


            std::size_t position =
                lowerText.find(
                    lowerWord);


            while (position != std::string_view::npos)
            {
                const bool leftBoundary =
                    position == 0
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(
                            lowerText[position - 1]));


                const std::size_t end =
                    position
                    + lowerWord.size();


                const bool rightBoundary =
                    end >= lowerText.size()
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(
                            lowerText[end]));


                if (leftBoundary && rightBoundary)
                {
                    return true;
                }


                position =
                    lowerText.find(
                        lowerWord,
                        position + 1);
            }


            return false;
        }


        [[nodiscard]]
        bool containsAnyAsciiWord(
            const std::string_view lowerText,
            const std::initializer_list<std::string_view> words) noexcept
        {
            for (const std::string_view word : words)
            {
                if (containsAsciiWord(
                    lowerText,
                    word))
                {
                    return true;
                }
            }


            return false;
        }


        [[nodiscard]]
        bool completedTool(
            const std::span<const std::string_view> completedToolIds,
            const std::string_view toolId) noexcept
        {
            for (const std::string_view completed : completedToolIds)
            {
                if (completed == toolId)
                {
                    return true;
                }
            }


            return false;
        }


        [[nodiscard]]
        bool toolRegistered(
            const tools::ToolRegistry& toolRegistry,
            const std::string_view toolId)
        {
            return
                toolRegistry.find(
                    toolId)
                != nullptr;
        }


        [[nodiscard]]
        std::string decodeCommonPathEscapes(
            const std::string_view text)
        {
            std::string decoded;
            decoded.reserve(text.size());

            for (std::size_t index{ 0 }; index < text.size(); ++index)
            {
                if (
                    index + 2 < text.size()
                    && text[index] == '%'
                    && text[index + 1] == '2'
                    && text[index + 2] == '0')
                {
                    decoded.push_back(' ');
                    index += 2;
                    continue;
                }

                decoded.push_back(text[index]);
            }

            return decoded;
        }


        [[nodiscard]]
        std::optional<std::string> longestExistingWindowsPathPrefix(
            const std::string_view text,
            const std::size_t start)
        {
#ifdef _WIN32
            std::string remainder =
                decodeCommonPathEscapes(
                    text.substr(start));

            // Stop at a hard line boundary first. Natural-language instructions
            // may continue on the same line, so then search backwards for the
            // longest prefix that actually exists on disk. This lets unquoted
            // Windows paths contain spaces without forcing shell-style quoting.
            const std::size_t lineEnd =
                remainder.find_first_of("\r\n");

            if (lineEnd != std::string::npos)
            {
                remainder.resize(lineEnd);
            }

            while (!remainder.empty())
            {
                while (
                    !remainder.empty()
                    && (
                        std::isspace(
                            static_cast<unsigned char>(
                                remainder.back())) != 0
                        || remainder.back() == '.'
                        || remainder.back() == ','
                        || remainder.back() == ';'
                        || remainder.back() == ':'
                        || remainder.back() == '!'
                        || remainder.back() == '?'
                        || remainder.back() == ')'
                        || remainder.back() == ']'))
                {
                    remainder.pop_back();
                }

                if (remainder.empty())
                {
                    break;
                }

                std::error_code error;
                const std::filesystem::path candidate{ remainder };

                if (
                    std::filesystem::exists(candidate, error)
                    && !error)
                {
                    return candidate.lexically_normal().string();
                }

                const std::size_t previousSpace =
                    remainder.find_last_of(" \t");

                if (previousSpace == std::string::npos)
                {
                    break;
                }

                remainder.resize(previousSpace);
            }
#else
            (void)text;
            (void)start;
#endif

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> extractAbsoluteWindowsPath(
            const std::string_view text)
        {
            // Prefer a quoted path because Windows paths may contain spaces.
            for (const char quote : { '"', '\'' })
            {
                std::size_t quoteStart{
                    0
                };


                while (quoteStart < text.size())
                {
                    quoteStart =
                        text.find(
                            quote,
                            quoteStart);


                    if (quoteStart == std::string_view::npos)
                    {
                        break;
                    }


                    const std::size_t quoteEnd =
                        text.find(
                            quote,
                            quoteStart + 1);


                    if (quoteEnd == std::string_view::npos)
                    {
                        break;
                    }


                    const std::string_view candidate =
                        text.substr(
                            quoteStart + 1,
                            quoteEnd - quoteStart - 1);


                    if (
                        (
                            candidate.size() >= 3
                            && (
                                (
                                    candidate[0] >= 'A'
                                    && candidate[0] <= 'Z')
                                || (
                                    candidate[0] >= 'a'
                                    && candidate[0] <= 'z'))
                            && candidate[1] == ':'
                            && (
                                candidate[2] == '\\'
                                || candidate[2] == '/'))
                        || candidate.starts_with("\\\\"))
                    {
                        return decodeCommonPathEscapes(
                            candidate);
                    }


                    quoteStart =
                        quoteEnd + 1;
                }
            }


            // Unquoted fallback. This deliberately stops at whitespace; users can
            // quote paths containing spaces. Trim sentence punctuation from the end.
            std::size_t start =
                std::string_view::npos;


            for (
                std::size_t index{ 0 };
                index + 2 < text.size();
                ++index)
            {
                const unsigned char drive =
                    static_cast<unsigned char>(
                        text[index]);


                const bool asciiLetter =
                    (
                        drive >= static_cast<unsigned char>('A')
                        && drive <= static_cast<unsigned char>('Z'))
                    || (
                        drive >= static_cast<unsigned char>('a')
                        && drive <= static_cast<unsigned char>('z'));


                if (
                    asciiLetter
                    && text[index + 1] == ':'
                    && (
                        text[index + 2] == '\\'
                        || text[index + 2] == '/'))
                {
                    start =
                        index;

                    break;
                }
            }


            if (start == std::string_view::npos)
            {
                start =
                    text.find("\\\\");
            }


            if (start == std::string_view::npos)
            {
                return std::nullopt;
            }


            if (const std::optional<std::string> existing =
                    longestExistingWindowsPathPrefix(
                        text,
                        start);
                existing.has_value())
            {
                return existing;
            }


            std::size_t end =
                start;


            while (
                end < text.size()
                && std::isspace(
                    static_cast<unsigned char>(
                        text[end]))
                    == 0)
            {
                ++end;
            }


            while (
                end > start
                && (
                    text[end - 1] == '.'
                    || text[end - 1] == ','
                    || text[end - 1] == ';'
                    || text[end - 1] == ':'
                    || text[end - 1] == '!'
                    || text[end - 1] == '?'
                    || text[end - 1] == ')'
                    || text[end - 1] == ']'))
            {
                --end;
            }


            if (end <= start)
            {
                return std::nullopt;
            }


            return decodeCommonPathEscapes(
                text.substr(
                    start,
                    end - start));
        }


        [[nodiscard]]
        bool looksLikeAbsoluteWindowsPath(
            const std::string_view text) noexcept
        {
            // Drive-qualified path, for example:
            //
            //     C:\Users\chris\file.txt
            //     D:/Rose/data
            //
            for (
                std::size_t index{ 0 };
                index + 2 < text.size();
                ++index)
            {
                const unsigned char drive =
                    static_cast<unsigned char>(
                        text[index]);


                const bool asciiLetter =
                    (
                        drive >= static_cast<unsigned char>('A')
                        && drive <= static_cast<unsigned char>('Z'))
                    || (
                        drive >= static_cast<unsigned char>('a')
                        && drive <= static_cast<unsigned char>('z'));


                if (
                    asciiLetter
                    && text[index + 1] == ':'
                    && (
                        text[index + 2] == '\\'
                        || text[index + 2] == '/'))
                {
                    return true;
                }
            }


            // UNC path.
            return
                text.find("\\\\")
                != std::string_view::npos;
        }


        [[nodiscard]]
        std::optional<std::string> singleResolvedProjectFilePath(
            const std::string_view agentContext)
        {
            static constexpr std::string_view beginTag{
                "<rose_project_file_resolution>"
            };
            static constexpr std::string_view endTag{
                "</rose_project_file_resolution>"
            };

            std::optional<std::string> uniquePath;
            std::size_t position{ 0 };

            while ((position = agentContext.find(beginTag, position))
                   != std::string_view::npos)
            {
                const std::size_t blockEnd =
                    agentContext.find(endTag, position + beginTag.size());
                if (blockEnd == std::string_view::npos)
                {
                    break;
                }

                const std::string_view block =
                    agentContext.substr(
                        position,
                        blockEnd + endTag.size() - position);

                const bool isUnique =
                    block.find("\nstatus=unique\n") != std::string_view::npos;

                if (isUnique)
                {
                    const std::string_view key{ "\nabsolute_path=" };
                    const std::size_t pathStart = block.find(key);
                    if (pathStart != std::string_view::npos)
                    {
                        const std::size_t valueBegin = pathStart + key.size();
                        const std::size_t valueEnd = block.find('\n', valueBegin);
                        const std::string path{
                            block.substr(
                                valueBegin,
                                valueEnd == std::string_view::npos
                                    ? std::string_view::npos
                                    : valueEnd - valueBegin)
                        };

                        if (!path.empty())
                        {
                            if (uniquePath.has_value())
                            {
                                // More than one bare file reference was resolved.
                                // Deterministic recovery cannot safely decide which
                                // one is the primary target, so let the model/user
                                // disambiguate instead of choosing the first.
                                return std::nullopt;
                            }
                            uniquePath = path;
                        }
                    }
                }

                position = blockEnd + endTag.size();
            }

            return uniquePath;
        }


        [[nodiscard]]
        std::string capabilitySearchText(
            const tools::ToolDescriptor& descriptor)
        {
            std::string text;
            text.reserve(
                descriptor.id.size()
                + descriptor.displayName.size()
                + descriptor.description.size()
                + 3);

            text +=
                descriptor.id;

            text.push_back(' ');

            text +=
                descriptor.displayName;

            text.push_back(' ');

            text +=
                descriptor.description;


            for (char& character : text)
            {
                if (character == '_')
                {
                    character = ' ';
                }
            }


            return asciiLower(
                text);
        }

    } // namespace


    std::string CapabilityRoutingGuard::buildCapabilityContract(
        const tools::ToolRegistry& toolRegistry)
    {
        std::ostringstream text;

        text
            << "<rose_capability_contract>\n"
            << "source=registered_tool_registry\n"
            << "authoritative=true\n"
            << "Registered tools below are capabilities Rose has RIGHT NOW.\n"
            << "Do not claim Rose cannot perform a capability that is represented "
               "by a registered tool.\n"
            << "If the user's requested action is covered by a registered tool, "
               "the control step should invoke that tool when required arguments "
               "are available.\n"
            << "If required information is missing, ask for that information "
               "instead of claiming the capability does not exist.\n"
            << "A tool requiring confirmation should still be proposed; "
               "ToolExecutionPolicy owns the confirmation decision.\n"
            << "Registration does NOT mean an action has already executed. Never "
               "claim success unless an actual tool observation says it completed.\n"
            << "Only report a capability failure after a real policy/tool/runtime "
               "failure provides evidence for that failure.\n"
            << "registered_tools:\n";


        for (const tools::ToolDescriptor& descriptor :
             toolRegistry.descriptors())
        {
            text
                << "- "
                << descriptor.id
                << ": "
                << descriptor.description
                << '\n';
        }


        text
            << "</rose_capability_contract>";

        return text.str();
    }


    bool CapabilityRoutingGuard::likelyToolBackedRequest(
        const std::string_view userText,
        const tools::ToolRegistry& toolRegistry)
    {
        const std::string lowerUser =
            asciiLower(
                userText);


        // This is deliberately a cheap guard, not a second semantic router.
        // Its job is only to identify strong evidence that a "RESPOND" result
        // deserves one more look before Rose tells the user she cannot act.
        const bool actionLanguage =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "analyze",
                    "check",
                    "describe",
                    "append",
                    "annotate",
                    "clear",
                    "combine",
                    "close",
                    "compress",
                    "create",
                    "draw",
                    "extract",
                    "generate",
                    "edit",
                    "inspect",
                    "launch",
                    "list",
                    "look",
                    "make",
                    "merge",
                    "modify",
                    "open",
                    "pack",
                    "read",
                    "remove",
                    "remember",
                    "replace",
                    "render",
                    "retain",
                    "review",
                    "rotate",
                    "run",
                    "save",
                    "start",
                    "set",
                    "show",
                    "split",
                    "summarize",
                    "tell",
                    "store",
                    "unpack",
                    "unzip",
                    "update",
                    "write"
                });


        if (!actionLanguage)
        {
            return false;
        }


        // A concrete absolute filesystem path plus action language is strong
        // evidence for one of Rose's filesystem tools even when the user never
        // literally says "file" or "directory".
        if (looksLikeAbsoluteWindowsPath(userText))
        {
            for (const tools::ToolDescriptor& descriptor :
                 toolRegistry.descriptors())
            {
                const std::string capability =
                    capabilitySearchText(
                        descriptor);

                if (
                    containsAnyAsciiWord(
                        capability,
                        {
                            "directory",
                            "file",
                            "path"
                        }))
                {
                    return true;
                }
            }
        }


        // Add a few human-language aliases to the user's intent vocabulary.
        // These are capability nouns rather than tool IDs, keeping the guard
        // decoupled from particular registered implementations.
        std::string expandedUser =
            lowerUser;


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "artwork",
                "drawing",
                "illustration",
                "photo",
                "picture"
            }))
        {
            expandedUser +=
                " image";
        }


        if (containsAsciiWord(
            lowerUser,
            "folder"))
        {
            expandedUser +=
                " directory";
        }


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "code",
                "document",
                "note",
                "source"
            }))
        {
            expandedUser +=
                " file";
        }


        if (
            containsAnyAsciiWord(lowerUser, { "video", "clip", "animation", "animated" })
            || lowerUser.find(".webm") != std::string::npos
            || lowerUser.find(".mp4") != std::string::npos
            || lowerUser.find(".mkv") != std::string::npos
            || lowerUser.find(".mov") != std::string::npos
            || lowerUser.find(".gif") != std::string::npos)
        {
            expandedUser += " media video";
        }


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "memorize",
                "remember",
                "retain"
            }))
        {
            expandedUser +=
                " memory";
        }


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "app",
                "application",
                "process",
                "program"
            }))
        {
            expandedUser += " process program";
        }


        if (containsAsciiWord(lowerUser, "pdf") || lowerUser.find(".pdf") != std::string::npos)
        {
            expandedUser += " pdf file";
        }


        // Match meaningful capability nouns from each registered descriptor.
        // We intentionally ignore tiny/common words because this is a guard
        // against false negatives, not a full natural-language classifier.
        static constexpr std::string_view capabilityNouns[]{
            "archive",
            "directory",
            "file",
            "image",
            "media",
            "video",
            "memory",
            "path",
            "pdf",
            "process",
            "program",
            "text",
            "zip"
        };


        for (const tools::ToolDescriptor& descriptor :
             toolRegistry.descriptors())
        {
            const std::string capability =
                capabilitySearchText(
                    descriptor);


            for (const std::string_view noun : capabilityNouns)
            {
                if (
                    containsAsciiWord(
                        capability,
                        noun)
                    && containsAsciiWord(
                        expandedUser,
                        noun))
                {
                    return true;
                }
            }
        }


        return false;
    }


    std::string CapabilityRoutingGuard::buildRecheckGuard(
        const std::size_t completedToolCount)
    {
        std::ostringstream text;

        text
            << "<rose_agent_guard>\n"
            << "reason=registered_capability_recheck\n"
            << "completed_tool_count="
            << completedToolCount
            << "\n"
            << "The previous control decision selected RESPOND, but the original "
               "request strongly resembles an action covered by Rose's registered "
               "tools.\n"
            << "Re-evaluate the ORIGINAL user request against the authoritative "
               "registered capability contract and completed tool observations.\n"
            << "Completed observations satisfy only the actions they actually "
               "performed; they do not automatically satisfy other requested "
               "actions.\n"
            << "If another tool action is still required and its required "
               "arguments are available, invoke it now.\n"
            << "If a required argument is genuinely missing, choose normal "
               "response so Rose can ask for that missing information.\n"
            << "Do not claim Rose lacks a registered capability.\n"
            << "</rose_agent_guard>";

        return text.str();
    }


    std::optional<tools::ToolRequest>
    CapabilityRoutingGuard::recoverDirectToolRequest(
        const std::string_view userText,
        const tools::ToolRegistry& toolRegistry,
        const std::span<const std::string_view> completedToolIds,
        const std::string_view agentContext)
    {
        const std::string lowerUser =
            asciiLower(
                userText);


        // ---------------------------------------------------------------------
        // Controlled local process actions
        // ---------------------------------------------------------------------
        const bool launchIntent = containsAnyAsciiWord(
            lowerUser, { "launch", "run", "start", "open" });

        if (launchIntent
            && !completedTool(completedToolIds, "launch_program")
            && toolRegistered(toolRegistry, "launch_program"))
        {
            std::optional<std::string> path = extractAbsoluteWindowsPath(userText);
            if (!path.has_value()) path = singleResolvedProjectFilePath(agentContext);
            if (path.has_value())
            {
                std::string extension = std::filesystem::path{ *path }.extension().string();
                for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension == ".exe" || extension == ".lnk")
                {
                    return tools::ToolRequest{
                        .toolId = "launch_program",
                        .arguments = { { "path", *path } }
                    };
                }
            }
        }

        const bool listProcessesIntent =
            (containsAsciiWord(lowerUser, "process") || containsAsciiWord(lowerUser, "processes")
             || containsAsciiWord(lowerUser, "program") || containsAsciiWord(lowerUser, "programs")
             || containsAsciiWord(lowerUser, "apps") || containsAsciiWord(lowerUser, "applications"))
            && (containsAsciiWord(lowerUser, "list") || containsAsciiWord(lowerUser, "running")
                || lowerUser.find("what is running") != std::string::npos
                || lowerUser.find("what's running") != std::string::npos);

        if (listProcessesIntent
            && !completedTool(completedToolIds, "list_processes")
            && toolRegistered(toolRegistry, "list_processes"))
        {
            return tools::ToolRequest{ .toolId = "list_processes", .arguments = {} };
        }


        // ---------------------------------------------------------------------
        // Whole-directory content-based rename planning
        // ---------------------------------------------------------------------
        //
        // Large rename jobs are intentionally planned inside a dedicated tool.
        // The tool performs one-document-at-a-time inference and persists exact
        // operations in Rose-owned storage, preventing a 100+ document corpus from
        // overflowing the Agent's small control context. Once that planner has
        // completed, do NOT fall through to read_text_file(directory). The next
        // action is either apply_rename_plan (selected from the compact plan
        // observation) or a normal response if nothing was safely plannable.
        const bool directoryRenameIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "rename",
                    "filename",
                    "filenames"
                });

        const bool directoryContentIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "analyze",
                    "classify",
                    "contents",
                    "filing",
                    "read",
                    "review",
                    "type"
                });

        const bool directoryScopeIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "all",
                    "batch",
                    "directory",
                    "each",
                    "every",
                    "folder"
                });

        if (
            directoryRenameIntent
            && directoryContentIntent
            && directoryScopeIntent
            && toolRegistered(
                toolRegistry,
                "plan_directory_document_renames"))
        {
            if (completedTool(
                    completedToolIds,
                    "plan_directory_document_renames"))
            {
                // Critical fail-closed behavior: never reinterpret the original
                // directory request as an exact-file read after planning finished.
                return std::nullopt;
            }

            const std::optional<std::string> path =
                extractAbsoluteWindowsPath(userText);

            if (path.has_value())
            {
                return tools::ToolRequest{
                    .toolId = "plan_directory_document_renames",
                    .arguments = {
                        { "path", *path },
                        { "instruction", std::string{ userText } }
                    }
                };
            }
        }


        // ---------------------------------------------------------------------
        // Whole-directory document analysis
        // ---------------------------------------------------------------------
        //
        // Prefer the batch document reader before exact-file recovery. A request
        // such as "read each PDF under C:\Case Filings then rename them" must
        // never be converted into read_text_file(path=<directory>).
        // Directory scope must be explicit. Generic quantifiers such as
        // "every" are common inside exact-file analysis requests (for example,
        // "identify every magic-system decision in C:\\Docs\\design.docx").
        // Treating those words alone as directory intent caused a successful
        // read_office_document observation to be followed by the directory
        // analyzer against the .docx path.
        const bool directoryBatchIntent =
            lowerUser.find("all files") != std::string::npos
            || lowerUser.find("each file") != std::string::npos
            || lowerUser.find("every file") != std::string::npos
            || lowerUser.find("entire directory") != std::string::npos
            || lowerUser.find("whole directory") != std::string::npos
            || containsAsciiWord(lowerUser, "directory")
            || containsAsciiWord(lowerUser, "folder")
            || containsAsciiWord(lowerUser, "batch")
            || lowerUser.find("under ") != std::string::npos;


        const bool directoryReadIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "analyze",
                    "classify",
                    "inspect",
                    "read",
                    "review",
                    "summarize"
                });


        if (
            directoryBatchIntent
            && directoryReadIntent
            && !completedTool(
                completedToolIds,
                "analyze_directory_documents")
            && toolRegistered(
                toolRegistry,
                "analyze_directory_documents"))
        {
            const std::optional<std::string> path =
                extractAbsoluteWindowsPath(
                    userText);

            if (path.has_value())
            {
                return tools::ToolRequest{
                    .toolId =
                        "analyze_directory_documents",
                    .arguments = {
                        {
                            "path",
                            *path
                        }
                    }
                };
            }
        }


        // ---------------------------------------------------------------------
        // Exact-path text read
        // ---------------------------------------------------------------------
        //
        // This is checked before image recovery because a multi-action request
        // such as:
        //
        //     read C:\...\scene.txt, then generate an image from it
        //
        // must complete the source read before image generation.
        const bool readIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "analyze",
                    "check",
                    "describe",
                    "inspect",
                    "list",
                    "open",
                    "read",
                    "review",
                    "show",
                    "summarize",
                    "tell",
                    "watch",
                    "where",
                    "target",
                    "table",
                    "tables",
                    "schema",
                    "row",
                    "rows",
                    "record",
                    "records"
                });


        const bool anyFileReaderRegistered =
            toolRegistered(toolRegistry, "read_text_file")
            || toolRegistered(toolRegistry, "read_pdf")
            || toolRegistered(toolRegistry, "read_office_document")
            || toolRegistered(toolRegistry, "inspect_image")
            || toolRegistered(toolRegistry, "inspect_media")
            || toolRegistered(toolRegistry, "list_zip_archive")
            || toolRegistered(toolRegistry, "inspect_database")
            || toolRegistered(toolRegistry, "inspect_shortcut");

        if (readIntent && anyFileReaderRegistered)
        {
            std::optional<std::string> path =
                extractAbsoluteWindowsPath(userText);

            if (!path.has_value())
            {
                path = singleResolvedProjectFilePath(agentContext);
            }

            if (path.has_value())
            {
                const files::FileFormatInfo format =
                    files::classifyFileFormat(std::filesystem::path{ *path });

                // A format-aware reader owns a recognized binary/document
                // family exclusively. Once that reader has completed, the
                // original read request is satisfied; never fall through and
                // reinterpret the same path as UTF-8 text. This is especially
                // important during deterministic recovery after a successful
                // read_pdf/read_office_document/inspect_image observation.
                if (format.kind == files::FileFormatKind::Pdf)
                {
                    if (completedTool(completedToolIds, "read_pdf"))
                    {
                        return std::nullopt;
                    }

                    if (toolRegistered(toolRegistry, "read_pdf"))
                    {
                        return tools::ToolRequest{
                            .toolId = "read_pdf",
                            .arguments = {
                                { "path", *path },
                                { "instruction", std::string{ userText } }
                            }
                        };
                    }

                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::OfficeWordOpenXml
                    || format.kind == files::FileFormatKind::OfficeSpreadsheetOpenXml
                    || format.kind == files::FileFormatKind::OfficePresentationOpenXml)
                {
                    if (completedTool(completedToolIds, "read_office_document"))
                    {
                        return std::nullopt;
                    }

                    if (toolRegistered(toolRegistry, "read_office_document"))
                    {
                        return tools::ToolRequest{
                            .toolId = "read_office_document",
                            .arguments = {
                                { "path", *path },
                                { "instruction", std::string{ userText } }
                            }
                        };
                    }

                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::Image)
                {
                    if (completedTool(completedToolIds, "inspect_image"))
                    {
                        return std::nullopt;
                    }

                    if (toolRegistered(toolRegistry, "inspect_image"))
                    {
                        return tools::ToolRequest{
                            .toolId = "inspect_image",
                            .arguments = {
                                { "path", *path },
                                { "instruction", std::string{ userText } }
                            }
                        };
                    }

                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::Video
                    || format.kind == files::FileFormatKind::AnimatedImage)
                {
                    if (completedTool(completedToolIds, "inspect_media"))
                    {
                        return std::nullopt;
                    }

                    if (toolRegistered(toolRegistry, "inspect_media"))
                    {
                        return tools::ToolRequest{
                            .toolId = "inspect_media",
                            .arguments = {
                                { "path", *path },
                                { "instruction", std::string{ userText } }
                            }
                        };
                    }

                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::Database)
                {
                    if (completedTool(completedToolIds, "inspect_database")) return std::nullopt;
                    if (toolRegistered(toolRegistry, "inspect_database"))
                    {
                        return tools::ToolRequest{
                            .toolId = "inspect_database",
                            .arguments = { { "path", *path } }
                        };
                    }
                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::Shortcut)
                {
                    if (completedTool(completedToolIds, "inspect_shortcut")) return std::nullopt;
                    if (toolRegistered(toolRegistry, "inspect_shortcut"))
                    {
                        return tools::ToolRequest{
                            .toolId = "inspect_shortcut",
                            .arguments = { { "path", *path } }
                        };
                    }
                    return std::nullopt;
                }

                if (format.kind == files::FileFormatKind::Archive
                    && files::isZipArchiveFile(std::filesystem::path{ *path }))
                {
                    if (completedTool(completedToolIds, "list_zip_archive"))
                    {
                        return std::nullopt;
                    }

                    if (toolRegistered(toolRegistry, "list_zip_archive"))
                    {
                        return tools::ToolRequest{
                            .toolId = "list_zip_archive",
                            .arguments = { { "path", *path } }
                        };
                    }

                    return std::nullopt;
                }

                // Other recognized non-text families (archives, video, legacy
                // Office, databases, shortcuts, etc.) must also fail closed here
                // instead of being handed to read_text_file. Unknown extensions
                // remain eligible for the text reader because many source/config
                // files intentionally have uncommon or extensionless names.
                if (format.kind != files::FileFormatKind::Unknown
                    && format.kind != files::FileFormatKind::TextSource)
                {
                    return std::nullopt;
                }

                if (!completedTool(completedToolIds, "read_text_file")
                    && toolRegistered(toolRegistry, "read_text_file"))
                {
                    return tools::ToolRequest{
                        .toolId = "read_text_file",
                        .arguments = { { "path", *path } }
                    };
                }
            }
        }


        // ---------------------------------------------------------------------
        // Explicit durable memory
        // ---------------------------------------------------------------------
        const bool rememberIntent =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "memorize",
                    "remember",
                    "retain"
                });


        if (
            rememberIntent
            && !completedTool(
                completedToolIds,
                "remember_memory")
            && toolRegistered(
                toolRegistry,
                "remember_memory"))
        {
            // Deterministic recovery intentionally preserves the user's exact
            // wording. RememberMemoryTool removes only obvious request wrappers;
            // it never asks this guard to semantically rewrite personal data.
            return tools::ToolRequest{
                .toolId =
                    "remember_memory",
                .arguments = {
                    {
                        "content",
                        std::string{
                            userText
                        }
                    }
                }
            };
        }


        // ---------------------------------------------------------------------
        // Direct image generation
        // ---------------------------------------------------------------------
        const bool imageAction =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "create",
                    "draw",
                    "generate",
                    "make",
                    "render"
                });


        const bool imageObject =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "artwork",
                    "drawing",
                    "illustration",
                    "image",
                    "photo",
                    "picture"
                });


        if (
            imageAction
            && imageObject
            && !completedTool(
                completedToolIds,
                "generate_image")
            && toolRegistered(
                toolRegistry,
                "generate_image"))
        {
            // Use the original user request as the generation prompt. The image
            // backend can interpret natural language directly, and this avoids a
            // second model rewrite that could silently change the user's intent.
            return tools::ToolRequest{
                .toolId =
                    "generate_image",
                .arguments = {
                    {
                        "prompt",
                        std::string{
                            userText
                        }
                    },
                    {
                        "quality",
                        "standard"
                    },
                    {
                        "aspect_ratio",
                        "auto"
                    }
                }
            };
        }


        return std::nullopt;
    }


    std::string CapabilityRoutingGuard::buildExecutionEvidenceGuard(
        const std::size_t completedToolCount)
    {
        std::ostringstream text;

        text
            << "<rose_execution_evidence_guard>\n"
            << "completed_tool_count="
            << completedToolCount
            << "\n"
            << "The current workflow is falling back to a normal conversational "
               "response after being recognized as tool-like.\n"
            << "Do NOT claim that a file was read, an image was generated, a file "
               "was created, or any other external/tool action completed unless a "
               "tool observation in this transient context explicitly proves it.\n"
            << "Registered capability is not execution evidence.\n"
            << "If the requested action still lacks required information, ask for "
               "that information.\n"
            << "If no tool observation proves completion, describe the action as "
               "not yet executed; never say 'I generated', 'I created', 'I read', "
               "'I opened', 'I saved', or equivalent completed-action language.\n"
            << "</rose_execution_evidence_guard>";

        return text.str();
    }


} // namespace rose::agent
