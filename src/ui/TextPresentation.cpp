#include "ui/TextPresentation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>


namespace rose::ui
{

    namespace
    {
        [[nodiscard]]
        bool isAsciiLetter(
            const char value) noexcept
        {
            const unsigned char character =
                static_cast<unsigned char>(value);

            return std::isalpha(character) != 0;
        }


        [[nodiscard]]
        bool isAsciiSpace(
            const char value) noexcept
        {
            const unsigned char character =
                static_cast<unsigned char>(value);

            return std::isspace(character) != 0;
        }


        [[nodiscard]]
        std::string_view trimView(
            std::string_view text) noexcept
        {
            while (
                !text.empty()
                && isAsciiSpace(text.front()))
            {
                text.remove_prefix(1);
            }

            while (
                !text.empty()
                && isAsciiSpace(text.back()))
            {
                text.remove_suffix(1);
            }

            return text;
        }


        struct BracedText
        {
            std::string_view content;
            std::size_t nextOffset{ 0 };
        };


        // Read one balanced {...} group.
        //
        // This tiny brace reader is enough for commands such as \frac{a}{b} and
        // \sqrt{x}. It deliberately does not attempt to implement TeX grammar.
        [[nodiscard]]
        std::optional<BracedText> readBracedText(
            const std::string_view text,
            const std::size_t openingBrace)
        {
            if (
                openingBrace >= text.size()
                || text[openingBrace] != '{')
            {
                return std::nullopt;
            }

            std::size_t depth{ 1 };

            for (
                std::size_t index = openingBrace + 1;
                index < text.size();
                ++index)
            {
                if (text[index] == '{')
                {
                    ++depth;
                }
                else if (text[index] == '}')
                {
                    --depth;

                    if (depth == 0)
                    {
                        return BracedText{
                            .content = text.substr(
                                openingBrace + 1,
                                index - openingBrace - 1),
                            .nextOffset = index + 1
                        };
                    }
                }
            }

            return std::nullopt;
        }


        [[nodiscard]]
        std::string normalizeMath(
            std::string_view text);


        [[nodiscard]]
        std::optional<std::string_view> replacementForLatexCommand(
            const std::string_view command) noexcept
        {
            using Entry =
                std::pair<std::string_view, std::string_view>;

            static constexpr std::array<Entry, 39> replacements{
                Entry{ "alpha", "α" },
                Entry{ "beta", "β" },
                Entry{ "gamma", "γ" },
                Entry{ "delta", "δ" },
                Entry{ "epsilon", "ε" },
                Entry{ "theta", "θ" },
                Entry{ "lambda", "λ" },
                Entry{ "mu", "μ" },
                Entry{ "nu", "ν" },
                Entry{ "pi", "π" },
                Entry{ "rho", "ρ" },
                Entry{ "sigma", "σ" },
                Entry{ "tau", "τ" },
                Entry{ "phi", "φ" },
                Entry{ "chi", "χ" },
                Entry{ "psi", "ψ" },
                Entry{ "omega", "ω" },
                Entry{ "Gamma", "Γ" },
                Entry{ "Delta", "Δ" },
                Entry{ "Theta", "Θ" },
                Entry{ "Lambda", "Λ" },
                Entry{ "Pi", "Π" },
                Entry{ "Sigma", "Σ" },
                Entry{ "Phi", "Φ" },
                Entry{ "Psi", "Ψ" },
                Entry{ "Omega", "Ω" },
                Entry{ "times", "×" },
                Entry{ "cdot", "·" },
                Entry{ "pm", "±" },
                Entry{ "leq", "≤" },
                Entry{ "geq", "≥" },
                Entry{ "neq", "≠" },
                Entry{ "approx", "≈" },
                Entry{ "infty", "∞" },
                Entry{ "rightarrow", "→" },
                Entry{ "leftarrow", "←" },
                Entry{ "leftrightarrow", "↔" },
                Entry{ "degree", "°" },
                Entry{ "partial", "∂" }
            };

            const auto found =
                std::find_if(
                    replacements.begin(),
                    replacements.end(),
                    [command](const Entry& entry)
                    {
                        return entry.first == command;
                    });

            if (found == replacements.end())
            {
                return std::nullopt;
            }

            return found->second;
        }


        [[nodiscard]]
        std::optional<std::string_view> superscriptDigit(
            const char value) noexcept
        {
            switch (value)
            {
            case '0': return "⁰";
            case '1': return "¹";
            case '2': return "²";
            case '3': return "³";
            case '4': return "⁴";
            case '5': return "⁵";
            case '6': return "⁶";
            case '7': return "⁷";
            case '8': return "⁸";
            case '9': return "⁹";
            case '-': return "⁻";
            case '+': return "⁺";
            default: return std::nullopt;
            }
        }


        [[nodiscard]]
        std::optional<std::string> makeSuperscript(
            const std::string_view text)
        {
            if (text.empty())
            {
                return std::nullopt;
            }

            std::string result;

            for (const char value : text)
            {
                const auto replacement =
                    superscriptDigit(value);

                if (!replacement)
                {
                    return std::nullopt;
                }

                result += *replacement;
            }

            return result;
        }


        [[nodiscard]]
        std::string normalizeMath(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (
                std::size_t index{ 0 };
                index < text.size();)
            {
                const char value =
                    text[index];

                // Remove common inline/display-math delimiters. The mathematical
                // content itself remains visible.
                if (value == '$')
                {
                    ++index;
                    continue;
                }


                // A small, safe subset of superscripts is worth rendering nicely.
                if (
                    value == '^'
                    && index + 1 < text.size())
                {
                    if (text[index + 1] == '{')
                    {
                        const auto group =
                            readBracedText(
                                text,
                                index + 1);

                        if (group)
                        {
                            const auto superscript =
                                makeSuperscript(
                                    group->content);

                            if (superscript)
                            {
                                result += *superscript;
                                index = group->nextOffset;
                                continue;
                            }
                        }
                    }
                    else
                    {
                        const auto superscript =
                            superscriptDigit(
                                text[index + 1]);

                        if (superscript)
                        {
                            result += *superscript;
                            index += 2;
                            continue;
                        }
                    }
                }


                if (value != '\\')
                {
                    result.push_back(value);
                    ++index;
                    continue;
                }


                // TeX also uses a backslash to escape ordinary punctuation.
                if (index + 1 < text.size())
                {
                    const char escaped =
                        text[index + 1];

                    if (
                        escaped == '%'
                        || escaped == '_'
                        || escaped == '#'
                        || escaped == '&'
                        || escaped == '$'
                        || escaped == '{'
                        || escaped == '}')
                    {
                        result.push_back(escaped);
                        index += 2;
                        continue;
                    }

                    // \( ... \) and \[ ... \] math delimiters.
                    if (
                        escaped == '('
                        || escaped == ')'
                        || escaped == '['
                        || escaped == ']')
                    {
                        index += 2;
                        continue;
                    }
                }


                std::size_t commandEnd =
                    index + 1;

                while (
                    commandEnd < text.size()
                    && isAsciiLetter(
                        text[commandEnd]))
                {
                    ++commandEnd;
                }

                if (commandEnd == index + 1)
                {
                    // Unknown backslash usage: preserve it rather than silently
                    // deleting model output.
                    result.push_back(value);
                    ++index;
                    continue;
                }


                const std::string_view command =
                    text.substr(
                        index + 1,
                        commandEnd - index - 1);


                // \left and \right are sizing hints, not user-visible content.
                if (
                    command == "left"
                    || command == "right")
                {
                    index = commandEnd;
                    continue;
                }


                // \frac{numerator}{denominator}
                if (command == "frac")
                {
                    const auto numerator =
                        readBracedText(
                            text,
                            commandEnd);

                    if (numerator)
                    {
                        const auto denominator =
                            readBracedText(
                                text,
                                numerator->nextOffset);

                        if (denominator)
                        {
                            result += '(';
                            result += normalizeMath(
                                numerator->content);
                            result += " / ";
                            result += normalizeMath(
                                denominator->content);
                            result += ')';

                            index =
                                denominator->nextOffset;

                            continue;
                        }
                    }
                }


                // \sqrt{value}
                if (command == "sqrt")
                {
                    const auto radicand =
                        readBracedText(
                            text,
                            commandEnd);

                    if (radicand)
                    {
                        result += "√(";
                        result += normalizeMath(
                            radicand->content);
                        result += ')';

                        index =
                            radicand->nextOffset;

                        continue;
                    }
                }


                // Text-like TeX commands can be flattened safely into plain text.
                if (
                    command == "text"
                    || command == "mathrm"
                    || command == "mathbf"
                    || command == "mathit")
                {
                    const auto content =
                        readBracedText(
                            text,
                            commandEnd);

                    if (content)
                    {
                        result += normalizeMath(
                            content->content);

                        index =
                            content->nextOffset;

                        continue;
                    }
                }


                if (const auto replacement =
                        replacementForLatexCommand(
                            command))
                {
                    result += *replacement;
                    index = commandEnd;
                    continue;
                }


                // Keep unknown commands intact. Readability is important, but data
                // preservation wins whenever this lightweight formatter is unsure.
                result.append(
                    text.substr(
                        index,
                        commandEnd - index));

                index = commandEnd;
            }

            return result;
        }


        [[nodiscard]]
        std::string flattenMarkdownLinks(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (
                std::size_t index{ 0 };
                index < text.size();)
            {
                if (text[index] != '[')
                {
                    result.push_back(
                        text[index]);
                    ++index;
                    continue;
                }

                const std::size_t labelEnd =
                    text.find(
                        ']',
                        index + 1);

                if (
                    labelEnd == std::string_view::npos
                    || labelEnd + 1 >= text.size()
                    || text[labelEnd + 1] != '(')
                {
                    result.push_back(
                        text[index]);
                    ++index;
                    continue;
                }

                const std::size_t urlEnd =
                    text.find(
                        ')',
                        labelEnd + 2);

                if (urlEnd == std::string_view::npos)
                {
                    result.push_back(
                        text[index]);
                    ++index;
                    continue;
                }

                const std::string_view label =
                    text.substr(
                        index + 1,
                        labelEnd - index - 1);

                const std::string_view url =
                    text.substr(
                        labelEnd + 2,
                        urlEnd - labelEnd - 2);

                result.append(label);

                if (!url.empty())
                {
                    result += " (";
                    result.append(url);
                    result += ')';
                }

                index = urlEnd + 1;
            }

            return result;
        }


        [[nodiscard]]
        std::string removeConservativeEmphasis(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (
                std::size_t index{ 0 };
                index < text.size();)
            {
                if (
                    index + 1 < text.size()
                    && (
                        text.substr(index, 2) == "**"
                        || text.substr(index, 2) == "__"))
                {
                    index += 2;
                    continue;
                }

                result.push_back(
                    text[index]);
                ++index;
            }

            return result;
        }


        [[nodiscard]]
        std::string transformNormalSegment(
            const std::string_view text)
        {
            const std::string linksFlattened =
                flattenMarkdownLinks(text);

            const std::string mathNormalized =
                normalizeMath(
                    linksFlattened);

            return removeConservativeEmphasis(
                mathNormalized);
        }


        // Preserve inline-code content byte-for-byte while removing only the Markdown
        // backtick delimiters around it.
        [[nodiscard]]
        std::string transformInlineText(
            const std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            std::size_t segmentBegin{ 0 };
            bool inInlineCode{ false };

            for (
                std::size_t index{ 0 };
                index < text.size();
                ++index)
            {
                if (text[index] != '`')
                {
                    continue;
                }

                const std::string_view segment =
                    text.substr(
                        segmentBegin,
                        index - segmentBegin);

                if (inInlineCode)
                {
                    result.append(segment);
                }
                else
                {
                    result += transformNormalSegment(
                        segment);
                }

                inInlineCode =
                    !inInlineCode;

                segmentBegin =
                    index + 1;
            }

            const std::string_view remainder =
                text.substr(segmentBegin);

            if (inInlineCode)
            {
                // Unmatched opening backtick: preserve the delimiter because we cannot
                // know whether the model intended code or ordinary punctuation.
                result.push_back('`');
                result.append(remainder);
            }
            else
            {
                result += transformNormalSegment(
                    remainder);
            }

            return result;
        }


        [[nodiscard]]
        bool isFenceDelimiter(
            const std::string_view line) noexcept
        {
            const std::string_view trimmed =
                trimView(line);

            return
                trimmed.starts_with("```")
                || trimmed.starts_with("~~~");
        }


        [[nodiscard]]
        std::string transformNormalLine(
            std::string_view line)
        {
            // Markdown headings become ordinary text. We intentionally do not invent
            // typography here; a future rich renderer can style the preserved source.
            std::size_t headingMarkers{ 0 };

            while (
                headingMarkers < line.size()
                && headingMarkers < 6
                && line[headingMarkers] == '#')
            {
                ++headingMarkers;
            }

            if (
                headingMarkers > 0
                && headingMarkers < line.size()
                && line[headingMarkers] == ' ')
            {
                line.remove_prefix(
                    headingMarkers + 1);
            }


            // Plain-text block quotes are easier to read without a raw Markdown marker.
            if (line.starts_with("> "))
            {
                line.remove_prefix(2);
            }


            return transformInlineText(line);
        }

    } // namespace


    std::string makeReadableChatText(
        const std::string_view text)
    {
        std::string result;
        result.reserve(text.size());

        bool inCodeFence{ false };
        std::size_t lineBegin{ 0 };

        while (lineBegin <= text.size())
        {
            const std::size_t newline =
                text.find(
                    '\n',
                    lineBegin);

            const bool hasNewline =
                newline != std::string_view::npos;

            const std::size_t lineEnd =
                hasNewline
                    ? newline
                    : text.size();

            std::string_view line =
                text.substr(
                    lineBegin,
                    lineEnd - lineBegin);

            if (
                !line.empty()
                && line.back() == '\r')
            {
                line.remove_suffix(1);
            }


            if (isFenceDelimiter(line))
            {
                inCodeFence =
                    !inCodeFence;

                // Preserve block separation while removing the raw ``` / ~~~ marker.
                if (hasNewline)
                {
                    result.push_back('\n');
                }
            }
            else if (inCodeFence)
            {
                // Code blocks are presentation-sensitive source material. Do not run
                // Markdown or LaTeX substitutions through them.
                result.append(line);

                if (hasNewline)
                {
                    result.push_back('\n');
                }
            }
            else
            {
                result += transformNormalLine(line);

                if (hasNewline)
                {
                    result.push_back('\n');
                }
            }


            if (!hasNewline)
            {
                break;
            }

            lineBegin =
                newline + 1;
        }

        return result;
    }

} // namespace rose::ui
