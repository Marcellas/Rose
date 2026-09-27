#include "memory/MemoryFactNormalizer.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace rose::memory
{
    namespace
    {
        [[nodiscard]]
        std::string_view trimAsciiWhitespace(
            const std::string_view text) noexcept
        {
            constexpr std::string_view whitespace{ " \t\r\n" };

            const std::size_t first =
                text.find_first_not_of(whitespace);

            if (first == std::string_view::npos)
            {
                return {};
            }

            const std::size_t last =
                text.find_last_not_of(whitespace);

            return text.substr(first, last - first + 1);
        }


        [[nodiscard]]
        std::string canonicalText(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            bool previousWhitespace{ false };

            for (const unsigned char raw : trimAsciiWhitespace(text))
            {
                if (std::isspace(raw) != 0)
                {
                    if (!previousWhitespace && !result.empty())
                    {
                        result.push_back(' ');
                    }

                    previousWhitespace = true;
                    continue;
                }

                previousWhitespace = false;

                unsigned char character = raw;
                if (character >= 'A' && character <= 'Z')
                {
                    character = static_cast<unsigned char>(
                        character - 'A' + 'a');
                }

                result.push_back(static_cast<char>(character));
            }

            while (!result.empty())
            {
                const char last = result.back();
                if (
                    last == '.'
                    || last == '!'
                    || last == '?'
                    || last == ','
                    || last == ';'
                    || last == ':')
                {
                    result.pop_back();
                    continue;
                }

                break;
            }

            return result;
        }


        [[nodiscard]]
        bool startsWith(
            const std::string_view text,
            const std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size()
                && text.substr(0, prefix.size()) == prefix;
        }


        [[nodiscard]]
        bool endsWith(
            const std::string_view text,
            const std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size()
                && text.substr(text.size() - suffix.size()) == suffix;
        }


        [[nodiscard]]
        std::string lastAlphanumericToken(
            const std::string_view text)
        {
            std::vector<std::string> tokens;
            std::string current;

            const auto flush = [&]()
            {
                if (!current.empty())
                {
                    tokens.push_back(current);
                    current.clear();
                }
            };

            for (const unsigned char character : text)
            {
                if (std::isalnum(character) != 0)
                {
                    current.push_back(static_cast<char>(character));
                }
                else
                {
                    flush();
                }
            }

            flush();

            if (tokens.empty())
            {
                return {};
            }

            return tokens.back();
        }


        [[nodiscard]]
        std::optional<NormalizedMemoryFact> preferenceFactFromValue(
            const std::string_view value)
        {
            const std::string canonicalValue = canonicalText(value);
            if (canonicalValue.empty())
            {
                return std::nullopt;
            }

            // A single final content token is a deliberately conservative slot
            // approximation. It lets "dark roast coffee" and "medium roast
            // coffee" share preference:coffee while avoiding a full NLP stack.
            const std::string category =
                lastAlphanumericToken(canonicalValue);

            if (category.empty())
            {
                return std::nullopt;
            }

            return NormalizedMemoryFact{
                .key = "preference:" + category,
                .value = canonicalValue
            };
        }
    }


    std::optional<NormalizedMemoryFact> normalizeMemoryFact(
        const std::string_view text)
    {
        const std::string canonical = canonicalText(text);

        if (canonical.empty())
        {
            return std::nullopt;
        }

        static constexpr std::string_view preferencePrefixes[]{
            "i prefer ",
            "i usually prefer ",
            "i generally prefer ",
            "i tend to prefer ",
            "my preference is "
        };

        for (const std::string_view prefix : preferencePrefixes)
        {
            if (startsWith(canonical, prefix))
            {
                return preferenceFactFromValue(
                    std::string_view{ canonical }.substr(prefix.size()));
            }
        }

        static constexpr std::string_view preferenceSuffix{
            " is my preference"
        };

        if (endsWith(canonical, preferenceSuffix))
        {
            return preferenceFactFromValue(
                std::string_view{ canonical }.substr(
                    0,
                    canonical.size() - preferenceSuffix.size()));
        }

        static constexpr std::string_view namePrefix{
            "my name is "
        };

        if (startsWith(canonical, namePrefix))
        {
            const std::string value = canonicalText(
                std::string_view{ canonical }.substr(namePrefix.size()));

            if (!value.empty())
            {
                return NormalizedMemoryFact{
                    .key = "identity:user-name",
                    .value = value
                };
            }
        }

        static constexpr std::string_view callMePrefix{
            "call me "
        };

        if (startsWith(canonical, callMePrefix))
        {
            const std::string value = canonicalText(
                std::string_view{ canonical }.substr(callMePrefix.size()));

            if (!value.empty())
            {
                return NormalizedMemoryFact{
                    .key = "identity:user-name",
                    .value = value
                };
            }
        }

        static constexpr std::string_view locationPrefixes[]{
            "i live in ",
            "i am based in ",
            "i'm based in "
        };

        for (const std::string_view prefix : locationPrefixes)
        {
            if (startsWith(canonical, prefix))
            {
                const std::string value = canonicalText(
                    std::string_view{ canonical }.substr(prefix.size()));

                if (!value.empty())
                {
                    return NormalizedMemoryFact{
                        .key = "identity:location",
                        .value = value
                    };
                }
            }
        }

        static constexpr std::string_view timezonePrefixes[]{
            "my timezone is ",
            "my time zone is "
        };

        for (const std::string_view prefix : timezonePrefixes)
        {
            if (startsWith(canonical, prefix))
            {
                const std::string value = canonicalText(
                    std::string_view{ canonical }.substr(prefix.size()));

                if (!value.empty())
                {
                    return NormalizedMemoryFact{
                        .key = "identity:timezone",
                        .value = value
                    };
                }
            }
        }

        return std::nullopt;
    }

} // namespace rose::memory
