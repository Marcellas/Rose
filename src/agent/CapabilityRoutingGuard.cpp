#include "agent/CapabilityRoutingGuard.h"

#include "files/FileFormatCatalog.h"

#include "tools/ToolRegistry.h"
#include "tools/ToolTypes.h"

#include <cctype>
#include <charconv>
#include <initializer_list>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>


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

            for (const char rawCharacter : text)
            {
                const unsigned char character =
                    static_cast<unsigned char>(rawCharacter);

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
        std::optional<std::string> explicitBuildConfiguration(
            const std::string_view lowerUser)
        {
            if (containsAsciiWord(lowerUser, "relwithdebinfo")) return std::string{ "RelWithDebInfo" };
            if (containsAsciiWord(lowerUser, "minsizerel")) return std::string{ "MinSizeRel" };
            if (containsAsciiWord(lowerUser, "release")) return std::string{ "Release" };
            if (containsAsciiWord(lowerUser, "debug")) return std::string{ "Debug" };
            return std::nullopt;
        }


        [[nodiscard]]
        bool isBuildTargetCharacter(
            const unsigned char character) noexcept
        {
            return std::isalnum(character) != 0
                || character == static_cast<unsigned char>('_')
                || character == static_cast<unsigned char>('-')
                || character == static_cast<unsigned char>('.')
                || character == static_cast<unsigned char>('+')
                || character == static_cast<unsigned char>(':');
        }


        [[nodiscard]]
        std::optional<std::string> explicitBuildTarget(
            const std::string_view userText,
            const std::string_view lowerUser)
        {
            constexpr std::string_view keyword{ "target" };
            std::size_t position = lowerUser.find(keyword);

            while (position != std::string_view::npos)
            {
                const bool leftBoundary =
                    position == 0
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[position - 1]));
                const std::size_t keywordEnd = position + keyword.size();
                const bool rightBoundary =
                    keywordEnd >= lowerUser.size()
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[keywordEnd]));

                if (leftBoundary && rightBoundary)
                {
                    std::size_t valueStart = keywordEnd;
                    while (valueStart < userText.size()
                           && std::isspace(static_cast<unsigned char>(userText[valueStart])) != 0)
                    {
                        ++valueStart;
                    }
                    if (valueStart < userText.size()
                        && (userText[valueStart] == '=' || userText[valueStart] == ':'))
                    {
                        ++valueStart;
                        while (valueStart < userText.size()
                               && std::isspace(static_cast<unsigned char>(userText[valueStart])) != 0)
                        {
                            ++valueStart;
                        }
                    }

                    std::size_t valueEnd = valueStart;
                    while (valueEnd < userText.size()
                           && isBuildTargetCharacter(
                               static_cast<unsigned char>(userText[valueEnd])))
                    {
                        ++valueEnd;
                    }

                    if (valueEnd > valueStart)
                    {
                        return std::string{ userText.substr(valueStart, valueEnd - valueStart) };
                    }
                }

                position = lowerUser.find(keyword, position + 1);
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> explicitBuildJobs(
            const std::string_view lowerUser)
        {
            for (const std::string_view keyword : { std::string_view{ "jobs" }, std::string_view{ "job" } })
            {
                std::size_t position = lowerUser.find(keyword);
                while (position != std::string_view::npos)
                {
                    const bool leftBoundary =
                        position == 0
                        || !isAsciiWordCharacter(
                            static_cast<unsigned char>(lowerUser[position - 1]));
                    const std::size_t keywordEnd = position + keyword.size();
                    const bool rightBoundary =
                        keywordEnd >= lowerUser.size()
                        || !isAsciiWordCharacter(
                            static_cast<unsigned char>(lowerUser[keywordEnd]));

                    if (leftBoundary && rightBoundary)
                    {
                        std::size_t numberEnd = position;
                        while (numberEnd > 0
                               && std::isspace(static_cast<unsigned char>(lowerUser[numberEnd - 1])) != 0)
                        {
                            --numberEnd;
                        }

                        std::size_t numberStart = numberEnd;
                        while (numberStart > 0
                               && std::isdigit(static_cast<unsigned char>(lowerUser[numberStart - 1])) != 0)
                        {
                            --numberStart;
                        }

                        if (numberStart < numberEnd)
                        {
                            return std::string{ lowerUser.substr(numberStart, numberEnd - numberStart) };
                        }
                    }

                    position = lowerUser.find(keyword, position + 1);
                }
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> digitsAfterPhrase(
            const std::string_view lowerUser,
            const std::string_view phrase)
        {
            std::size_t position = lowerUser.find(phrase);

            while (position != std::string_view::npos)
            {
                const bool leftBoundary =
                    position == 0
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[position - 1]));

                std::size_t valueStart = position + phrase.size();
                const bool rightBoundary =
                    valueStart >= lowerUser.size()
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[valueStart]));

                if (leftBoundary && rightBoundary)
                {
                    while (
                        valueStart < lowerUser.size()
                        && std::isspace(
                            static_cast<unsigned char>(lowerUser[valueStart])) != 0)
                    {
                        ++valueStart;
                    }

                    if (
                        valueStart < lowerUser.size()
                        && (lowerUser[valueStart] == '=' || lowerUser[valueStart] == ':'))
                    {
                        ++valueStart;
                        while (
                            valueStart < lowerUser.size()
                            && std::isspace(
                                static_cast<unsigned char>(lowerUser[valueStart])) != 0)
                        {
                            ++valueStart;
                        }
                    }

                    std::size_t valueEnd = valueStart;
                    while (
                        valueEnd < lowerUser.size()
                        && std::isdigit(
                            static_cast<unsigned char>(lowerUser[valueEnd])) != 0)
                    {
                        ++valueEnd;
                    }

                    if (valueEnd > valueStart)
                    {
                        return std::string{
                            lowerUser.substr(valueStart, valueEnd - valueStart)
                        };
                    }
                }

                position = lowerUser.find(phrase, position + 1);
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> explicitTextStartLine(
            const std::string_view lowerUser)
        {
            for (const std::string_view phrase : {
                     std::string_view{ "start_line" },
                     std::string_view{ "starting at line" },
                     std::string_view{ "start at line" },
                     std::string_view{ "from line" },
                     std::string_view{ "line" } })
            {
                const std::optional<std::string> value =
                    digitsAfterPhrase(lowerUser, phrase);
                if (value.has_value())
                {
                    return value;
                }
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> explicitTextLineCount(
            const std::string_view lowerUser)
        {
            if (const auto explicitCount =
                    digitsAfterPhrase(lowerUser, "line_count");
                explicitCount.has_value())
            {
                return explicitCount;
            }

            // Natural language commonly says "for 40 lines" or "read 40 lines".
            // Reuse the bounded integer-before-keyword pattern without trying to
            // interpret arbitrary numeric ranges or path digits.
            std::size_t position = lowerUser.find("lines");
            while (position != std::string_view::npos)
            {
                const bool leftBoundary =
                    position == 0
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[position - 1]));
                const std::size_t end = position + 5;
                const bool rightBoundary =
                    end >= lowerUser.size()
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[end]));

                if (leftBoundary && rightBoundary)
                {
                    std::size_t numberEnd = position;
                    while (
                        numberEnd > 0
                        && std::isspace(
                            static_cast<unsigned char>(lowerUser[numberEnd - 1])) != 0)
                    {
                        --numberEnd;
                    }

                    std::size_t numberStart = numberEnd;
                    while (
                        numberStart > 0
                        && std::isdigit(
                            static_cast<unsigned char>(lowerUser[numberStart - 1])) != 0)
                    {
                        --numberStart;
                    }

                    if (numberStart < numberEnd)
                    {
                        return std::string{
                            lowerUser.substr(numberStart, numberEnd - numberStart)
                        };
                    }
                }

                position = lowerUser.find("lines", position + 1);
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::string> explicitCTestName(
            const std::string_view userText,
            const std::string_view lowerUser)
        {
            constexpr std::string_view keyword{ "test" };
            std::size_t position = lowerUser.find(keyword);

            while (position != std::string_view::npos)
            {
                const bool leftBoundary =
                    position == 0
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[position - 1]));
                const std::size_t keywordEnd = position + keyword.size();
                const bool rightBoundary =
                    keywordEnd >= lowerUser.size()
                    || !isAsciiWordCharacter(
                        static_cast<unsigned char>(lowerUser[keywordEnd]));

                if (leftBoundary && rightBoundary)
                {
                    std::size_t valueStart = keywordEnd;
                    while (valueStart < userText.size()
                           && std::isspace(static_cast<unsigned char>(userText[valueStart])) != 0)
                    {
                        ++valueStart;
                    }
                    if (valueStart < userText.size()
                        && (userText[valueStart] == '=' || userText[valueStart] == ':'))
                    {
                        ++valueStart;
                        while (valueStart < userText.size()
                               && std::isspace(static_cast<unsigned char>(userText[valueStart])) != 0)
                        {
                            ++valueStart;
                        }
                    }

                    // "test C:\\Project" names the project action, not a test
                    // called "C". Exact test filters remain optional.
                    if (valueStart + 2 < userText.size()
                        && std::isalpha(static_cast<unsigned char>(userText[valueStart])) != 0
                        && userText[valueStart + 1] == ':'
                        && (userText[valueStart + 2] == '\\' || userText[valueStart + 2] == '/'))
                    {
                        return std::nullopt;
                    }

                    std::size_t valueEnd = valueStart;
                    while (valueEnd < userText.size()
                           && isBuildTargetCharacter(
                               static_cast<unsigned char>(userText[valueEnd])))
                    {
                        ++valueEnd;
                    }

                    if (valueEnd > valueStart)
                    {
                        const std::string candidate{
                            userText.substr(valueStart, valueEnd - valueStart)
                        };
                        const std::string lowerCandidate = asciiLower(candidate);
                        if (lowerCandidate != "all"
                            && lowerCandidate != "in"
                            && lowerCandidate != "for"
                            && lowerCandidate != "on"
                            && lowerCandidate != "the"
                            && lowerCandidate != "suite"
                            && lowerCandidate != "with")
                        {
                            return candidate;
                        }
                    }
                }

                position = lowerUser.find(keyword, position + 1);
            }

            return std::nullopt;
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
        std::optional<bool> latestToolObservationSuccess(
            const std::string_view agentContext,
            const std::string_view toolId)
        {
            // Tool observations are Rose-authored wrapper blocks. Only inspect the
            // structured header fields before the free-form message payload so
            // untrusted tool output cannot manufacture a successful completion.
            constexpr std::string_view beginTag{
                "<rose_tool_observation>"
            };
            constexpr std::string_view endTag{
                "</rose_tool_observation>"
            };

            const std::string toolMarker =
                "\ntool_id="
                + std::string{ toolId }
                + "\n";

            std::optional<bool> latest;
            std::size_t searchFrom{ 0 };

            while (searchFrom < agentContext.size())
            {
                const std::size_t begin =
                    agentContext.find(beginTag, searchFrom);
                if (begin == std::string_view::npos)
                {
                    break;
                }

                const std::size_t end =
                    agentContext.find(endTag, begin + beginTag.size());
                if (end == std::string_view::npos)
                {
                    break;
                }

                const std::string_view block =
                    agentContext.substr(begin, end - begin);
                const std::size_t message =
                    block.find("\nmessage=");
                const std::string_view header =
                    block.substr(
                        0,
                        message == std::string_view::npos
                            ? std::string_view::npos
                            : message);
                const std::size_t tool = header.find(toolMarker);

                if (tool != std::string_view::npos)
                {
                    const std::size_t headerBegin =
                        tool + toolMarker.size();
                    const std::size_t success =
                        header.find("success=", headerBegin);

                    if (success != std::string_view::npos)
                    {
                        const std::size_t valueBegin =
                            success + std::string_view{ "success=" }.size();
                        const std::size_t valueEnd =
                            header.find('\n', valueBegin);
                        const std::string_view value =
                            header.substr(
                                valueBegin,
                                valueEnd == std::string_view::npos
                                    ? std::string_view::npos
                                    : valueEnd - valueBegin);

                        if (value == "true")
                        {
                            latest = true;
                        }
                        else if (value == "false")
                        {
                            latest = false;
                        }
                    }
                }

                searchFrom = end + endTag.size();
            }

            return latest;
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


#ifdef _WIN32
        [[nodiscard]]
        bool promptBoundaryAfterPathPrefix(
            const std::string_view text,
            const std::size_t end) noexcept
        {
            if (end >= text.size())
            {
                return true;
            }

            const unsigned char next =
                static_cast<unsigned char>(
                    text[end]);

            // Existing-prefix recovery is allowed only when the candidate ends
            // where path evidence can naturally end in the prompt. Without this
            // boundary check, a nonexistent path such as C:\Docs\missing.cpp
            // can be shortened all the way to the existing C:\ root, changing
            // an exact-file request into a directory request on Windows.
            return
                std::isspace(next) != 0
                || next == static_cast<unsigned char>('.')
                || next == static_cast<unsigned char>(',')
                || next == static_cast<unsigned char>(';')
                || next == static_cast<unsigned char>(':')
                || next == static_cast<unsigned char>('!')
                || next == static_cast<unsigned char>('?')
                || next == static_cast<unsigned char>(')')
                || next == static_cast<unsigned char>(']');
        }
#endif


        [[nodiscard]]
        std::optional<std::string> longestExistingWindowsPathPrefix(
            const std::string_view text,
            const std::size_t start)
        {
#ifdef _WIN32
            std::string remainder =
                decodeCommonPathEscapes(
                    text.substr(start));

            // Stop at a hard line boundary. For an unquoted Windows path followed
            // by prose on the same line, test progressively shorter character
            // prefixes until the longest existing path is found. The previous
            // implementation shortened only at spaces; in real prompts that could
            // accidentally fall past the intended final path component and settle
            // on an existing parent directory such as Desktop.
            const std::size_t lineEnd =
                remainder.find_first_of("\r\n");

            if (lineEnd != std::string::npos)
            {
                remainder.resize(lineEnd);
            }

            auto ignorableTrailingCharacter =
                [](const unsigned char character) noexcept
                {
                    return
                        std::isspace(character) != 0
                        || character == static_cast<unsigned char>('.')
                        || character == static_cast<unsigned char>(',')
                        || character == static_cast<unsigned char>(';')
                        || character == static_cast<unsigned char>('!')
                        || character == static_cast<unsigned char>('?')
                        || character == static_cast<unsigned char>(')')
                        || character == static_cast<unsigned char>(']');
                };

            std::size_t end = remainder.size();
            while (end > 0)
            {
                while (
                    end > 0
                    && ignorableTrailingCharacter(
                        static_cast<unsigned char>(remainder[end - 1u])))
                {
                    --end;
                }

                if (end == 0)
                {
                    break;
                }

                std::error_code error;
                const std::filesystem::path candidate{
                    remainder.substr(0, end)
                };

                if (
                    promptBoundaryAfterPathPrefix(remainder, end)
                    && std::filesystem::exists(candidate, error)
                    && !error)
                {
                    return candidate.lexically_normal().string();
                }

                // Remove one character, not one word. This preserves unquoted
                // paths whose own directory/file names contain spaces while still
                // allowing natural-language text after the path.
                --end;
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
        bool explicitCodingRepairIntent(
            const std::string_view userText)
        {
            const std::string lower = asciiLower(userText);

            if (containsAnyAsciiWord(
                    lower,
                    { "fix", "repair", "debug", "diagnose", "diagnosis", "resolve" }))
            {
                return true;
            }

            return lower.find("make it build") != std::string::npos
                || lower.find("get it building") != std::string::npos
                || lower.find("make the build pass") != std::string::npos
                || lower.find("fix the build") != std::string::npos
                || lower.find("fix build") != std::string::npos
                || lower.find("fix compile") != std::string::npos
                || lower.find("compile error") != std::string::npos
                || lower.find("compiler error") != std::string::npos
                || lower.find("make the tests pass") != std::string::npos
                || lower.find("make tests pass") != std::string::npos
                || lower.find("get tests passing") != std::string::npos
                || lower.find("fix the tests") != std::string::npos
                || lower.find("fix test") != std::string::npos
                || lower.find("test failure") != std::string::npos;
        }


        [[nodiscard]]
        std::optional<std::string> lineValue(
            const std::string_view block,
            const std::string_view key)
        {
            const std::string needle = std::string{ key } + "=";
            std::size_t position = block.find(needle);
            while (position != std::string_view::npos)
            {
                if (position == 0 || block[position - 1] == '\n')
                {
                    const std::size_t begin = position + needle.size();
                    const std::size_t end = block.find('\n', begin);
                    return std::string{
                        block.substr(
                            begin,
                            end == std::string_view::npos
                                ? std::string_view::npos
                                : end - begin)
                    };
                }
                position = block.find(needle, position + 1);
            }
            return std::nullopt;
        }


        [[nodiscard]]
        std::optional<std::size_t> positiveSizeValue(
            const std::string_view block,
            const std::string_view key)
        {
            const auto text = lineValue(block, key);
            if (!text.has_value() || text->empty()) return std::nullopt;

            std::size_t value{};
            const auto [end, error] = std::from_chars(
                text->data(), text->data() + text->size(), value);
            if (error != std::errc{}
                || end != text->data() + text->size()
                || value == 0)
            {
                return std::nullopt;
            }
            return value;
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
        bool isExistingNonSymlinkDirectory(
            const std::string_view rawPath) noexcept
        {
            if (rawPath.empty())
            {
                return false;
            }

            std::error_code error;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(
                    std::filesystem::path{ rawPath },
                    error);

            return
                !error
                && std::filesystem::is_directory(status)
                && !std::filesystem::is_symlink(status);
        }


        [[nodiscard]]
        bool looksLikeDevelopmentProjectDirectory(
            const std::string_view rawPath) noexcept
        {
            if (!isExistingNonSymlinkDirectory(rawPath))
            {
                return false;
            }

            const std::filesystem::path root{ rawPath };
            std::error_code error;

            // Prefer cheap, explicit project markers before enumerating anything.
            // This keeps routing deterministic and avoids recursively probing a
            // potentially huge build/vendor tree just to decide which first tool
            // should inspect the directory.
            static constexpr std::string_view markerFiles[]{
                "CMakeLists.txt",
                "Makefile",
                "meson.build",
                "Cargo.toml",
                "package.json",
                "pyproject.toml",
                "go.mod"
            };

            for (const std::string_view marker : markerFiles)
            {
                error.clear();
                if (
                    std::filesystem::is_regular_file(
                        root / std::string{ marker },
                        error)
                    && !error)
                {
                    return true;
                }
            }

            static constexpr std::string_view markerDirectories[]{
                "src",
                "include"
            };

            for (const std::string_view marker : markerDirectories)
            {
                error.clear();
                const std::filesystem::file_status status =
                    std::filesystem::symlink_status(
                        root / std::string{ marker },
                        error);

                if (
                    !error
                    && std::filesystem::is_directory(status)
                    && !std::filesystem::is_symlink(status))
                {
                    return true;
                }
            }

            // A small source-only folder may not have a build-system marker yet.
            // Inspect only one level and cap the work so routing never becomes a
            // hidden recursive directory scan.
            std::filesystem::directory_iterator iterator{
                root,
                std::filesystem::directory_options::skip_permission_denied,
                error
            };

            if (error)
            {
                return false;
            }

            constexpr std::size_t maximumEntriesToInspect{ 128 };
            std::size_t inspected{ 0 };
            const std::filesystem::directory_iterator end;

            for (
                ; iterator != end && inspected < maximumEntriesToInspect;
                iterator.increment(error), ++inspected)
            {
                if (error)
                {
                    error.clear();
                    continue;
                }

                const std::filesystem::directory_entry& entry = *iterator;

                error.clear();
                const std::filesystem::file_status status =
                    entry.symlink_status(error);

                if (
                    error
                    || std::filesystem::is_symlink(status)
                    || !std::filesystem::is_regular_file(status))
                {
                    error.clear();
                    continue;
                }

                const std::string extension =
                    asciiLower(
                        entry.path().extension().string());

                if (
                    extension == ".c"
                    || extension == ".cc"
                    || extension == ".cpp"
                    || extension == ".cxx"
                    || extension == ".h"
                    || extension == ".hpp"
                    || extension == ".hxx"
                    || extension == ".cs"
                    || extension == ".java"
                    || extension == ".rs"
                    || extension == ".go"
                    || extension == ".py"
                    || extension == ".js"
                    || extension == ".ts"
                    || extension == ".sln"
                    || extension == ".vcxproj")
                {
                    return true;
                }
            }

            return false;
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
                    "build",
                    "clear",
                    "compile",
                    "configure",
                    "reconfigure",
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
                    "rebuild",
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
                    "test",
                    "tests",
                    "validate",
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


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "build",
                "compile",
                "rebuild"
            }))
        {
            expandedUser += " build cmake project";
        }


        if (containsAnyAsciiWord(
            lowerUser,
            {
                "configure",
                "reconfigure"
            }))
        {
            expandedUser += " cmake project";
        }


        // Match meaningful capability nouns from each registered descriptor.
        // We intentionally ignore tiny/common words because this is a guard
        // against false negatives, not a full natural-language classifier.
        static constexpr std::string_view capabilityNouns[]{
            "archive",
            "build",
            "cmake",
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
            "project",
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


    bool CapabilityRoutingGuard::explicitCMakeTestExecutionIntent(
        const std::string_view userText)
    {
        const std::string lowerUser =
            asciiLower(
                userText);


        // Keep this intentionally narrower than a general semantic classifier.
        // It exists only to prove that an actual CTest execution was requested.
        // Both model-selected tool validation and deterministic recovery call the
        // same helper so phrases such as "run all tests" cannot be accepted by
        // one path and missed by the other.
        const bool testVerb =
            lowerUser.starts_with("test ")
            || lowerUser.starts_with("test:")
            || lowerUser.starts_with("run test ")
            || lowerUser.starts_with("run tests")
            || lowerUser.starts_with("run all test")
            || lowerUser.starts_with("run ctest")
            || lowerUser.find(" run test ") != std::string::npos
            || lowerUser.find(" run tests") != std::string::npos
            || lowerUser.find(" run all test") != std::string::npos
            || lowerUser.find(" run ctest") != std::string::npos
            || lowerUser.find(" execute test") != std::string::npos
            || lowerUser.find(" rerun test") != std::string::npos
            || lowerUser.find(" validate test") != std::string::npos
            || lowerUser.find(" validate the test") != std::string::npos;


        const bool explicitlyNonExecuting =
            lowerUser.find("do not test") != std::string::npos
            || lowerUser.find("don't test") != std::string::npos
            || lowerUser.find("do not run tests") != std::string::npos
            || lowerUser.find("don't run tests") != std::string::npos
            || lowerUser.find("do not run the tests") != std::string::npos
            || lowerUser.find("don't run the tests") != std::string::npos
            || lowerUser.find("without testing") != std::string::npos
            || lowerUser.find("how to test") != std::string::npos
            || lowerUser.find("how do i test") != std::string::npos
            || lowerUser.find("how to run test") != std::string::npos
            || lowerUser.find("how do i run test") != std::string::npos
            || lowerUser.find("show me how") != std::string::npos
            || lowerUser.find("what test command") != std::string::npos
            || lowerUser.starts_with("should i ")
            || lowerUser.starts_with("should we ")
            || lowerUser.starts_with("do i need to ")
            || lowerUser.starts_with("do we need to ");


        return testVerb && !explicitlyNonExecuting;
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

        // Broad project diagnosis is an execution workflow, not merely a request
        // for architectural description. Rose still performs bounded read-only
        // discovery first. Once the project is grounded, a configured CMake build
        // is a useful diagnostic action and remains confirmation-gated by policy.
        const bool projectDiagnosisIntent =
            containsAnyAsciiWord(
                lowerUser,
                { "diagnose", "diagnostic", "troubleshoot" })
            || lowerUser.find("figure out what is going wrong") != std::string::npos
            || lowerUser.find("figure out what's going wrong") != std::string::npos
            || lowerUser.find("what is going wrong") != std::string::npos
            || lowerUser.find("what's going wrong") != std::string::npos
            || lowerUser.find("find what is wrong") != std::string::npos
            || lowerUser.find("find what's wrong") != std::string::npos
            || lowerUser.find("why is this failing") != std::string::npos
            || lowerUser.find("why is it failing") != std::string::npos;

        const bool explicitlyReadOnlyDiagnosis =
            lowerUser.find("do not execute") != std::string::npos
            || lowerUser.find("don't execute") != std::string::npos
            || lowerUser.find("do not run anything") != std::string::npos
            || lowerUser.find("don't run anything") != std::string::npos
            || lowerUser.find("without executing") != std::string::npos
            || lowerUser.find("read-only") != std::string::npos
            || lowerUser.find("read only") != std::string::npos;

        const bool projectDiscoveryCompleted =
            completedTool(completedToolIds, "list_directory")
            || completedTool(completedToolIds, "scan_directory_tree")
            || completedTool(completedToolIds, "read_text_file");


        // ---------------------------------------------------------------------
        // Controlled existing-tree CMake reconfiguration
        // ---------------------------------------------------------------------
        const bool configureTopic =
            containsAnyAsciiWord(lowerUser, { "configure", "reconfigure" })
            || lowerUser.find("rerun cmake") != std::string::npos
            || lowerUser.find("run cmake configure") != std::string::npos;

        const bool explicitlyNonExecutingConfigure =
            lowerUser.find("do not configure") != std::string::npos
            || lowerUser.find("don't configure") != std::string::npos
            || lowerUser.find("do not reconfigure") != std::string::npos
            || lowerUser.find("don't reconfigure") != std::string::npos
            || lowerUser.find("without configuring") != std::string::npos
            || lowerUser.find("without reconfiguring") != std::string::npos
            || lowerUser.find("how to configure") != std::string::npos
            || lowerUser.find("how do i configure") != std::string::npos
            || lowerUser.find("how to reconfigure") != std::string::npos
            || lowerUser.find("how do i reconfigure") != std::string::npos
            || lowerUser.find("show me how") != std::string::npos
            || lowerUser.find("explain how") != std::string::npos
            || lowerUser.find("what configure command") != std::string::npos
            || lowerUser.find("what reconfigure command") != std::string::npos;

        const bool configureIntent =
            configureTopic
            && !explicitlyNonExecutingConfigure;

        if (configureIntent
            && !completedTool(completedToolIds, "reconfigure_cmake_project")
            && toolRegistered(toolRegistry, "reconfigure_cmake_project"))
        {
            const std::optional<std::string> path =
                extractAbsoluteWindowsPath(userText);

            if (path.has_value())
            {
                const std::filesystem::path source{ *path };
                std::error_code error;
                const bool ready =
                    std::filesystem::is_directory(source, error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "CMakeLists.txt", error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "build" / "CMakeCache.txt", error)
                    && !error;

                if (ready)
                {
                    return tools::ToolRequest{
                        .toolId = "reconfigure_cmake_project",
                        .arguments = { { "source_path", *path } }
                    };
                }
            }
        }

        // ---------------------------------------------------------------------
        // Controlled local CMake build
        // ---------------------------------------------------------------------
        const bool buildTopic =
            containsAnyAsciiWord(lowerUser, { "build", "compile", "rebuild" });

        const bool explicitlyNonExecutingBuild =
            lowerUser.find("do not build") != std::string::npos
            || lowerUser.find("don't build") != std::string::npos
            || lowerUser.find("without building") != std::string::npos
            || lowerUser.find("how to build") != std::string::npos
            || lowerUser.find("how do i build") != std::string::npos
            || lowerUser.find("show me how") != std::string::npos
            || lowerUser.find("what command") != std::string::npos;

        const bool diagnosisBuildIntent =
            projectDiagnosisIntent
            && projectDiscoveryCompleted
            && !explicitlyReadOnlyDiagnosis;

        const bool buildIntent =
            (buildTopic || diagnosisBuildIntent)
            && !explicitlyNonExecutingBuild;

        if (buildIntent
            && !completedTool(completedToolIds, "build_cmake_project")
            && toolRegistered(toolRegistry, "build_cmake_project"))
        {
            const std::optional<std::string> path =
                extractAbsoluteWindowsPath(userText);

            if (path.has_value())
            {
                const std::filesystem::path source{ *path };
                std::error_code error;
                const bool ready =
                    std::filesystem::is_directory(source, error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "CMakeLists.txt", error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "build" / "CMakeCache.txt", error)
                    && !error;

                if (ready)
                {
                    tools::ToolRequest request{
                        .toolId = "build_cmake_project",
                        .arguments = { { "source_path", *path } }
                    };

                    if (const std::optional<std::string> configuration =
                            explicitBuildConfiguration(lowerUser);
                        configuration.has_value())
                    {
                        request.arguments.emplace("configuration", *configuration);
                    }

                    if (const std::optional<std::string> target =
                            explicitBuildTarget(userText, lowerUser);
                        target.has_value())
                    {
                        request.arguments.emplace("target", *target);
                    }

                    if (const std::optional<std::string> jobs =
                            explicitBuildJobs(lowerUser);
                        jobs.has_value())
                    {
                        request.arguments.emplace("jobs", *jobs);
                    }

                    return request;
                }
            }
        }


        // ---------------------------------------------------------------------
        // Controlled registered CTest execution
        // ---------------------------------------------------------------------
        const bool testTopic =
            containsAnyAsciiWord(lowerUser, { "test", "tests", "ctest" })
            && (
                containsAnyAsciiWord(
                    lowerUser,
                    { "run", "execute", "rerun", "validate" })
                || lowerUser.starts_with("test ")
                || lowerUser.starts_with("test:"));

        const bool explicitlyNonExecutingTests =
            lowerUser.find("do not run tests") != std::string::npos
            || lowerUser.find("don't run tests") != std::string::npos
            || lowerUser.find("without running tests") != std::string::npos
            || lowerUser.find("without tests") != std::string::npos;

        const bool diagnosisTestIntent =
            projectDiagnosisIntent
            && projectDiscoveryCompleted
            && completedTool(completedToolIds, "build_cmake_project")
            && latestToolObservationSuccess(
                   agentContext,
                   "build_cmake_project")
                   .value_or(false)
            && !explicitlyReadOnlyDiagnosis
            && !explicitlyNonExecutingBuild
            && !explicitlyNonExecutingTests;

        const bool testIntent =
            explicitCMakeTestExecutionIntent(
                userText)
            || diagnosisTestIntent;

        if (testIntent
            && !completedTool(completedToolIds, "run_cmake_tests")
            && toolRegistered(toolRegistry, "run_cmake_tests"))
        {
            const std::optional<std::string> path =
                extractAbsoluteWindowsPath(userText);

            if (path.has_value())
            {
                const std::filesystem::path source{ *path };
                std::error_code error;
                const bool ready =
                    std::filesystem::is_directory(source, error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "CMakeLists.txt", error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "build" / "CMakeCache.txt", error)
                    && !error
                    && std::filesystem::is_regular_file(
                        source / "build" / "CTestTestfile.cmake", error)
                    && !error;

                if (ready)
                {
                    tools::ToolRequest request{
                        .toolId = "run_cmake_tests",
                        .arguments = { { "source_path", *path } }
                    };

                    if (const std::optional<std::string> configuration =
                            explicitBuildConfiguration(lowerUser);
                        configuration.has_value())
                    {
                        request.arguments.emplace("configuration", *configuration);
                    }

                    if (const std::optional<std::string> test =
                            explicitCTestName(userText, lowerUser);
                        test.has_value())
                    {
                        request.arguments.emplace("test", *test);
                    }

                    if (const std::optional<std::string> jobs =
                            explicitBuildJobs(lowerUser);
                        jobs.has_value())
                    {
                        request.arguments.emplace("jobs", *jobs);
                    }

                    return request;
                }
            }
        }


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
        const bool explicitDirectoryContentBatchIntent =
            lowerUser.find("all files") != std::string::npos
            || lowerUser.find("each file") != std::string::npos
            || lowerUser.find("every file") != std::string::npos
            || lowerUser.find("entire directory") != std::string::npos
            || lowerUser.find("whole directory") != std::string::npos
            || containsAsciiWord(lowerUser, "batch")
            || lowerUser.find("under ") != std::string::npos;

        const bool directoryBatchIntent =
            explicitDirectoryContentBatchIntent
            || containsAsciiWord(lowerUser, "directory")
            || containsAsciiWord(lowerUser, "folder");


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


        // A concrete existing directory is authoritative type evidence even when
        // the user naturally says "analyze C:\\Project" without the literal words
        // "directory" or "folder". This prevents deterministic recovery from
        // handing a directory path to read_text_file and failing after confirmation.
        const std::optional<std::string> explicitDirectoryCandidate =
            extractAbsoluteWindowsPath(
                userText);

        const bool explicitPathIsDirectory =
            explicitDirectoryCandidate.has_value()
            && isExistingNonSymlinkDirectory(
                *explicitDirectoryCandidate);

        const bool developmentProjectDirectory =
            explicitPathIsDirectory
            && explicitDirectoryCandidate.has_value()
            && looksLikeDevelopmentProjectDirectory(
                *explicitDirectoryCandidate);


        // A software project is not a document corpus. For a broad request such
        // as "Analyze C:\Rose and figure out what is going wrong", start with
        // one bounded root listing so Rose can discover grounded CMake/source/build
        // children, then return control to the normal multi-step agent loop. The
        // document-batch analyzer is terminal after one bounded excerpt batch and
        // can otherwise spend its whole budget on .vs/build/vendor artifacts.
        //
        // Explicit all/each/every-file requests remain document-batch workflows;
        // those users asked Rose to read the corpus rather than diagnose the project.
        if (
            developmentProjectDirectory
            && directoryReadIntent
            && !explicitDirectoryContentBatchIntent)
        {
            if (
                !completedTool(
                    completedToolIds,
                    "list_directory")
                && toolRegistered(
                    toolRegistry,
                    "list_directory"))
            {
                return tools::ToolRequest{
                    .toolId = "list_directory",
                    .arguments = {
                        {
                            "path",
                            *explicitDirectoryCandidate
                        }
                    }
                };
            }

            // If the model still chooses RESPOND after the root listing, one
            // one bounded recursive inventory is a safe deterministic continuation for the user's
            // still-unsatisfied project diagnosis. This is intentionally
            // only a fallback: AgentLoop asks the model first, so a more targeted
            // read/build/test decision wins when the router can make one.
            if (
                !completedTool(
                    completedToolIds,
                    "scan_directory_tree")
                && toolRegistered(
                    toolRegistry,
                    "scan_directory_tree"))
            {
                return tools::ToolRequest{
                    .toolId = "scan_directory_tree",
                    .arguments = {
                        {
                            "path",
                            *explicitDirectoryCandidate
                        },
                        {
                            "max_depth",
                            "2"
                        }
                    }
                };
            }

            // Once both bounded discovery steps have run, do not reinterpret the
            // original project path as a terminal document-batch read. The control
            // model now has Rose-owned child paths and may choose a targeted source
            // read or propose a confirmation-gated validation action.
            return std::nullopt;
        }


        if (
            (directoryBatchIntent || explicitPathIsDirectory)
            && directoryReadIntent
            && !completedTool(
                completedToolIds,
                "analyze_directory_documents")
            && toolRegistered(
                toolRegistry,
                "analyze_directory_documents"))
        {
            if (explicitDirectoryCandidate.has_value())
            {
                return tools::ToolRequest{
                    .toolId =
                        "analyze_directory_documents",
                    .arguments = {
                        {
                            "path",
                            *explicitDirectoryCandidate
                        }
                    }
                };
            }
        }


        // An existing directory must never fall through to an exact-file reader.
        // If the batch analyzer is unavailable/already satisfied, fail closed to
        // normal response rather than proposing read_text_file(path=<directory>).
        if (explicitPathIsDirectory)
        {
            return std::nullopt;
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

        // Build/test requests often contain words such as "target" or "show"
        // that are also valid read-routing cues. Do not reinterpret a CMake source
        // directory as a text file merely because the build already completed,
        // the build could not be reconstructed, or the user explicitly asked for
        // build instructions without execution. A genuine mixed request can still
        // opt into file recovery with an explicit file-reading verb.
        const bool explicitFileReadAction =
            containsAnyAsciiWord(
                lowerUser,
                {
                    "analyze",
                    "inspect",
                    "open",
                    "read",
                    "review",
                    "summarize"
                });

        const bool suppressDevelopmentPathFileRecovery =
            (configureTopic || buildTopic || testTopic)
            && !explicitFileReadAction;

        if (readIntent
            && anyFileReaderRegistered
            && !suppressDevelopmentPathFileRecovery)
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
                    tools::ToolRequest request{
                        .toolId = "read_text_file",
                        .arguments = { { "path", *path } }
                    };

                    const std::optional<std::string> startLine =
                        explicitTextStartLine(lowerUser);
                    if (startLine.has_value())
                    {
                        request.arguments.emplace(
                            "start_line",
                            *startLine);

                        const std::optional<std::string> lineCount =
                            explicitTextLineCount(lowerUser);
                        if (lineCount.has_value())
                        {
                            request.arguments.emplace(
                                "line_count",
                                *lineCount);
                        }
                    }

                    return request;
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


    std::optional<tools::ToolRequest>
    CapabilityRoutingGuard::recoverDiagnosticSourceReadRequest(
        const std::string_view userText,
        const tools::ToolRegistry& toolRegistry,
        const std::string_view trustedToolMetadata)
    {
        if (!explicitCodingRepairIntent(userText)
            || !toolRegistered(toolRegistry, "read_text_file")
            || trustedToolMetadata.empty())
        {
            return std::nullopt;
        }

        const auto kind = lineValue(trustedToolMetadata, "metadata_kind");
        const auto producer = lineValue(trustedToolMetadata, "producer_tool");
        const auto succeeded = lineValue(trustedToolMetadata, "operation_success");
        const auto path = lineValue(trustedToolMetadata, "diagnostic_path");
        const auto startLine = positiveSizeValue(
            trustedToolMetadata,
            "suggested_read_start_line");
        const auto lineCount = positiveSizeValue(
            trustedToolMetadata,
            "suggested_read_line_count");

        const bool knownProducer = producer.has_value()
            && (*producer == "build_cmake_project"
                || *producer == "reconfigure_cmake_project"
                || *producer == "run_cmake_tests");

        if (!kind.has_value() || *kind != "source_diagnostic"
            || !knownProducer
            || !succeeded.has_value() || *succeeded != "false"
            || !path.has_value() || path->empty()
            || !looksLikeAbsoluteWindowsPath(*path)
            || !startLine.has_value()
            || !lineCount.has_value()
            || *lineCount > 200)
        {
            return std::nullopt;
        }

        const files::FileFormatInfo format =
            files::classifyFileFormat(std::filesystem::path{ *path });
        if (format.kind != files::FileFormatKind::Unknown
            && format.kind != files::FileFormatKind::TextSource)
        {
            return std::nullopt;
        }

        return tools::ToolRequest{
            .toolId = "read_text_file",
            .arguments = {
                { "path", *path },
                { "start_line", std::to_string(*startLine) },
                { "line_count", std::to_string(*lineCount) }
            }
        };
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
            << "The registered capability contract is authoritative: do NOT tell "
               "the user Rose lacks a capability that is listed there, and do not "
               "redirect them to an external editor solely because routing could not "
               "yet form an exact request.\n"
            << "If the requested action still lacks required information, ask for "
               "that information.\n"
            << "If no tool observation proves completion, describe the action as "
               "not yet executed; never say 'I generated', 'I created', 'I read', "
               "'I opened', 'I saved', or equivalent completed-action language.\n"
            << "</rose_execution_evidence_guard>";

        return text.str();
    }


} // namespace rose::agent
