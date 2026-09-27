#include "tools/DocumentRenameMetadata.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]]
        std::string trimCopy(
            const std::string_view text)
        {
            std::size_t begin{ 0 };
            std::size_t end = text.size();

            while (
                begin < end
                && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
            {
                ++begin;
            }

            while (
                end > begin
                && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
            {
                --end;
            }

            return std::string{ text.substr(begin, end - begin) };
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
        std::string protocolKey(
            const std::string_view value)
        {
            std::string key;
            key.reserve(value.size());

            for (const unsigned char character : value)
            {
                if (std::isalnum(character) != 0)
                {
                    key.push_back(
                        static_cast<char>(std::toupper(character)));
                }
            }

            return key;
        }


        [[nodiscard]]
        std::string stripDecoration(
            std::string value)
        {
            value = trimCopy(value);

            while (
                value.size() >= 2
                && ((value.front() == '"' && value.back() == '"')
                    || (value.front() == '\'' && value.back() == '\'')
                    || (value.front() == '`' && value.back() == '`')))
            {
                value = trimCopy(
                    std::string_view{ value }.substr(1, value.size() - 2));
            }

            while (
                value.size() >= 4
                && value.starts_with("**")
                && value.ends_with("**"))
            {
                value = trimCopy(
                    std::string_view{ value }.substr(2, value.size() - 4));
            }

            return value;
        }


        [[nodiscard]]
        bool parseUnsigned(
            const std::string_view text,
            int& value)
        {
            value = 0;
            if (text.empty())
            {
                return false;
            }

            const char* begin = text.data();
            const char* end = begin + text.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, value);

            return parsed.ec == std::errc{} && parsed.ptr == end;
        }


        [[nodiscard]]
        bool isLeapYear(
            const int year) noexcept
        {
            return
                (year % 4 == 0 && year % 100 != 0)
                || year % 400 == 0;
        }


        [[nodiscard]]
        bool validDate(
            const int year,
            const int month,
            const int day) noexcept
        {
            if (year < 1600 || year > 9999 || month < 1 || month > 12 || day < 1)
            {
                return false;
            }

            static constexpr int daysPerMonth[]{
                31, 28, 31, 30, 31, 30,
                31, 31, 30, 31, 30, 31
            };

            int maximumDay = daysPerMonth[month - 1];
            if (month == 2 && isLeapYear(year))
            {
                maximumDay = 29;
            }

            return day <= maximumDay;
        }


        [[nodiscard]]
        std::vector<std::string_view> splitOn(
            const std::string_view text,
            const char separator)
        {
            std::vector<std::string_view> parts;
            std::size_t begin{ 0 };

            while (begin <= text.size())
            {
                const std::size_t end = text.find(separator, begin);
                parts.push_back(
                    text.substr(
                        begin,
                        end == std::string_view::npos
                            ? std::string_view::npos
                            : end - begin));

                if (end == std::string_view::npos)
                {
                    break;
                }

                begin = end + 1;
            }

            return parts;
        }


        [[nodiscard]]
        std::optional<std::string> normalizeDate(
            std::string value)
        {
            value = stripDecoration(std::move(value));

            for (char& character : value)
            {
                if (character == '/' || character == '.')
                {
                    character = '-';
                }
            }

            const std::vector<std::string_view> parts = splitOn(value, '-');
            if (parts.size() != 3)
            {
                return std::nullopt;
            }

            int first{ 0 };
            int second{ 0 };
            int third{ 0 };

            if (
                !parseUnsigned(trimCopy(parts[0]), first)
                || !parseUnsigned(trimCopy(parts[1]), second)
                || !parseUnsigned(trimCopy(parts[2]), third))
            {
                return std::nullopt;
            }

            int year{ 0 };
            int month{ 0 };
            int day{ 0 };

            // Preferred protocol is ISO, but accept the common US court-stamp
            // representation too so a model echoing source punctuation is harmless.
            if (parts[0].size() == 4)
            {
                year = first;
                month = second;
                day = third;
            }
            else
            {
                month = first;
                day = second;
                year = third;
            }

            if (!validDate(year, month, day))
            {
                return std::nullopt;
            }

            std::string normalized;
            normalized.reserve(10);
            normalized += std::to_string(year);
            normalized.push_back('-');
            if (month < 10)
            {
                normalized.push_back('0');
            }
            normalized += std::to_string(month);
            normalized.push_back('-');
            if (day < 10)
            {
                normalized.push_back('0');
            }
            normalized += std::to_string(day);
            return normalized;
        }


        [[nodiscard]]
        std::optional<std::string> normalizeTime(
            std::string value)
        {
            value = stripDecoration(std::move(value));
            const std::string lower = lowerAscii(value);

            if (
                lower.empty()
                || lower == "unknown"
                || lower == "none"
                || lower == "n/a"
                || lower == "na"
                || lower == "not shown"
                || lower == "unavailable")
            {
                return std::string{};
            }

            bool pm{ false };
            bool am{ false };
            std::string upper = value;
            std::transform(
                upper.begin(),
                upper.end(),
                upper.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::toupper(character));
                });

            if (upper.find("PM") != std::string::npos)
            {
                pm = true;
            }
            if (upper.find("AM") != std::string::npos)
            {
                am = true;
            }

            std::string digits;
            digits.reserve(value.size());
            for (const unsigned char character : value)
            {
                if (std::isdigit(character) != 0)
                {
                    digits.push_back(static_cast<char>(character));
                }
            }

            if (digits.size() == 3)
            {
                digits.insert(digits.begin(), '0');
            }

            if (digits.size() != 4)
            {
                return std::nullopt;
            }

            int hour{ 0 };
            int minute{ 0 };
            if (
                !parseUnsigned(std::string_view{ digits }.substr(0, 2), hour)
                || !parseUnsigned(std::string_view{ digits }.substr(2, 2), minute)
                || minute < 0
                || minute > 59)
            {
                return std::nullopt;
            }

            if (am || pm)
            {
                if (hour < 1 || hour > 12)
                {
                    return std::nullopt;
                }

                if (am && hour == 12)
                {
                    hour = 0;
                }
                else if (pm && hour != 12)
                {
                    hour += 12;
                }
            }
            else if (hour < 0 || hour > 23)
            {
                return std::nullopt;
            }

            std::string normalized;
            normalized.reserve(4);
            if (hour < 10)
            {
                normalized.push_back('0');
            }
            normalized += std::to_string(hour);
            if (minute < 10)
            {
                normalized.push_back('0');
            }
            normalized += std::to_string(minute);
            return normalized;
        }


        [[nodiscard]]
        bool containsInvalidWindowsFilenameCharacter(
            const std::string_view value) noexcept
        {
            for (const unsigned char character : value)
            {
                if (
                    character < 32u
                    || character == '<'
                    || character == '>'
                    || character == ':'
                    || character == '"'
                    || character == '/'
                    || character == '\\'
                    || character == '|'
                    || character == '?'
                    || character == '*')
                {
                    return true;
                }
            }

            return false;
        }


        [[nodiscard]]
        bool isAllUppercasePhrase(
            const std::string_view value)
        {
            bool sawLetter{ false };

            for (const unsigned char character : value)
            {
                if (std::isalpha(character) == 0)
                {
                    continue;
                }

                sawLetter = true;
                if (std::islower(character) != 0)
                {
                    return false;
                }
            }

            return sawLetter;
        }


        [[nodiscard]]
        bool isLowercaseConnectorWord(
            const std::string_view word)
        {
            static constexpr std::string_view words[]{
                "a", "an", "and", "as", "at", "by", "for", "from",
                "in", "of", "on", "or", "the", "to", "with"
            };

            return std::find(std::begin(words), std::end(words), word)
                != std::end(words);
        }


        [[nodiscard]]
        std::string titleCaseAllUppercasePhrase(
            const std::string_view value)
        {
            std::istringstream words{ std::string{ value } };
            std::ostringstream result;
            std::string word;
            std::size_t index{ 0 };

            while (words >> word)
            {
                std::string lowered = lowerAscii(word);

                if (!(index > 0 && isLowercaseConnectorWord(lowered)))
                {
                    for (char& character : lowered)
                    {
                        const unsigned char byte = static_cast<unsigned char>(character);
                        if (std::isalpha(byte) != 0)
                        {
                            character = static_cast<char>(std::toupper(byte));
                            break;
                        }
                    }
                }

                if (index > 0)
                {
                    result << ' ';
                }

                result << lowered;
                ++index;
            }

            return result.str();
        }


        [[nodiscard]]
        std::optional<std::string> normalizeFilingType(
            std::string value)
        {
            value = stripDecoration(std::move(value));

            const std::string lower = lowerAscii(value);
            if (lower.size() > 4 && lower.ends_with(".pdf"))
            {
                value.erase(value.size() - 4);
            }

            for (char& character : value)
            {
                if (character == '_')
                {
                    character = ' ';
                }
            }

            std::string normalized;
            normalized.reserve(value.size());
            bool previousWhitespace{ true };

            for (const unsigned char character : value)
            {
                if (std::isspace(character) != 0)
                {
                    if (!previousWhitespace)
                    {
                        normalized.push_back(' ');
                        previousWhitespace = true;
                    }
                    continue;
                }

                normalized.push_back(static_cast<char>(character));
                previousWhitespace = false;
            }

            while (!normalized.empty() && normalized.back() == ' ')
            {
                normalized.pop_back();
            }

            while (!normalized.empty() && (normalized.back() == '.' || normalized.back() == ' '))
            {
                normalized.pop_back();
            }

            if (
                normalized.empty()
                || normalized == "."
                || normalized == ".."
                || containsInvalidWindowsFilenameCharacter(normalized))
            {
                return std::nullopt;
            }

            // Court captions and OCR frequently emit headings in ALL CAPS.  The
            // model should identify the semantic filing type; deterministic C++
            // owns presentation consistency.  Only normalize phrases that are
            // entirely uppercase so normal mixed-case legal names are untouched.
            if (isAllUppercasePhrase(normalized))
            {
                normalized = titleCaseAllUppercasePhrase(normalized);
            }

            return normalized;
        }


        [[nodiscard]]
        std::string normalizeConfidence(
            std::string value)
        {
            value = lowerAscii(stripDecoration(std::move(value)));

            if (value == "high" || value == "medium" || value == "low")
            {
                return value;
            }

            if (value == "confident" || value == "certain")
            {
                return "high";
            }

            if (value == "moderate")
            {
                return "medium";
            }

            if (value == "uncertain")
            {
                return "low";
            }

            return "unspecified";
        }


        void addField(
            std::unordered_map<std::string, std::string>& fields,
            const std::string_view key,
            std::string value)
        {
            const std::string normalizedKey = protocolKey(key);
            if (normalizedKey.empty())
            {
                return;
            }

            fields.insert_or_assign(
                normalizedKey,
                stripDecoration(std::move(value)));
        }


        [[nodiscard]]
        std::unordered_map<std::string, std::string> parseNamedFields(
            const std::string_view text)
        {
            std::unordered_map<std::string, std::string> fields;

            const std::size_t firstNewline = text.find('\n');
            const std::string firstLine = trimCopy(
                text.substr(
                    0,
                    firstNewline == std::string_view::npos
                        ? std::string_view::npos
                        : firstNewline));

            const std::vector<std::string_view> compactParts = splitOn(firstLine, '|');
            if (compactParts.size() >= 2)
            {
                for (std::size_t index{ 1 }; index < compactParts.size(); ++index)
                {
                    const std::string_view part = compactParts[index];
                    const std::size_t separator = part.find('=');
                    if (separator != std::string_view::npos)
                    {
                        addField(
                            fields,
                            part.substr(0, separator),
                            std::string{ part.substr(separator + 1) });
                    }
                }
            }

            std::size_t offset{ 0 };
            while (offset <= text.size())
            {
                const std::size_t end = text.find('\n', offset);
                std::string line = trimCopy(
                    text.substr(
                        offset,
                        end == std::string_view::npos
                            ? std::string_view::npos
                            : end - offset));

                while (!line.empty() && (line.front() == '-' || line.front() == '*'))
                {
                    line.erase(line.begin());
                    line = trimCopy(line);
                }

                if (!line.empty() && !line.starts_with("```"))
                {
                    std::size_t separator = line.find('=');
                    if (separator == std::string::npos)
                    {
                        separator = line.find(':');
                    }

                    if (separator != std::string::npos)
                    {
                        addField(
                            fields,
                            std::string_view{ line }.substr(0, separator),
                            line.substr(separator + 1));
                    }
                }

                if (end == std::string_view::npos)
                {
                    break;
                }

                offset = end + 1;
            }

            return fields;
        }


        [[nodiscard]]
        std::optional<std::string> fieldValue(
            const std::unordered_map<std::string, std::string>& fields,
            const std::string_view key)
        {
            const auto found = fields.find(protocolKey(key));
            if (found == fields.end())
            {
                return std::nullopt;
            }

            return found->second;
        }


        [[nodiscard]]
        std::string compactStatus(
            const std::string_view value)
        {
            return protocolKey(value);
        }


        [[nodiscard]]
        FilingMetadata metadataFailure(
            const FilingMetadataStatus status,
            std::string reason)
        {
            FilingMetadata metadata;
            metadata.status = status;
            metadata.reason = std::move(reason);
            return metadata;
        }


        [[nodiscard]]
        FilingBasenameResult basenameFailure(
            std::string reason)
        {
            FilingBasenameResult result;
            result.ok = false;
            result.reason = std::move(reason);
            return result;
        }


    } // namespace


    FilingMetadata parseFilingMetadataProtocol(
        const std::string_view raw)
    {
        const std::string text = trimCopy(raw);
        if (text.empty())
        {
            return metadataFailure(
                FilingMetadataStatus::Malformed,
                "classifier returned no visible structured metadata");
        }

        const std::size_t firstNewline = text.find('\n');
        const std::string firstLine = trimCopy(
            std::string_view{ text }.substr(
                0,
                firstNewline == std::string::npos
                    ? std::string_view::npos
                    : firstNewline));

        std::string statusToken;
        const std::size_t pipe = firstLine.find('|');
        if (pipe != std::string::npos)
        {
            statusToken = compactStatus(
                std::string_view{ firstLine }.substr(0, pipe));
        }

        const std::unordered_map<std::string, std::string> fields =
            parseNamedFields(text);

        if (statusToken.empty())
        {
            if (const std::optional<std::string> status = fieldValue(fields, "STATUS"))
            {
                statusToken = compactStatus(*status);
            }
        }

        if (statusToken.empty())
        {
            // Tolerate a model that omitted STATUS but still supplied all core named
            // fields.  This remains structured output; free-form prose is rejected.
            if (
                fieldValue(fields, "DATE").has_value()
                && fieldValue(fields, "TYPE").has_value())
            {
                statusToken = "OK";
            }
        }

        if (
            statusToken == "AMBIGUOUS"
            || statusToken == "UNCERTAIN"
            || statusToken == "UNKNOWN")
        {
            std::string reason = fieldValue(fields, "REASON").value_or(
                "classifier marked filing metadata ambiguous");

            if (const std::optional<std::string> missing = fieldValue(fields, "MISSING"))
            {
                if (!missing->empty())
                {
                    reason += "; missing=" + *missing;
                }
            }

            return metadataFailure(
                FilingMetadataStatus::Ambiguous,
                std::move(reason));
        }

        if (statusToken != "OK" && statusToken != "SUCCESS" && statusToken != "RESOLVED")
        {
            return metadataFailure(
                FilingMetadataStatus::Malformed,
                "classifier output did not contain a recognized structured status");
        }

        const std::optional<std::string> rawDate = fieldValue(fields, "DATE");
        const std::optional<std::string> rawType = fieldValue(fields, "TYPE");
        const std::optional<std::string> rawTime = fieldValue(fields, "TIME");
        const std::optional<std::string> rawConfidence = fieldValue(fields, "CONFIDENCE");

        if (!rawDate.has_value() || !rawType.has_value())
        {
            return metadataFailure(
                FilingMetadataStatus::Malformed,
                "resolved classifier output omitted DATE or TYPE");
        }

        const std::optional<std::string> date = normalizeDate(*rawDate);
        if (!date.has_value())
        {
            return metadataFailure(
                FilingMetadataStatus::Malformed,
                "classifier returned an invalid filing date");
        }

        const std::optional<std::string> filingType = normalizeFilingType(*rawType);
        if (!filingType.has_value())
        {
            return metadataFailure(
                FilingMetadataStatus::Malformed,
                "classifier returned an invalid filing type");
        }

        std::string time;
        if (rawTime.has_value())
        {
            const std::optional<std::string> normalizedTime = normalizeTime(*rawTime);
            if (!normalizedTime.has_value())
            {
                return metadataFailure(
                    FilingMetadataStatus::Malformed,
                    "classifier returned an invalid filing time");
            }

            time = *normalizedTime;
        }

        return FilingMetadata{
            .status = FilingMetadataStatus::Resolved,
            .filingDateIso = *date,
            .filingTimeHhmm = std::move(time),
            .filingType = *filingType,
            .confidence = normalizeConfidence(
                rawConfidence.value_or("unspecified")),
            .reason = {}
        };
    }


    FilingDateTimeMetadata parseFilingDateTimeProtocol(
        const std::string_view raw)
    {
        const std::string text = trimCopy(raw);
        if (text.empty())
        {
            return FilingDateTimeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingDateIso = {},
                .filingTimeHhmm = {},
                .confidence = {},
                .reason = "date/time classifier returned no visible structured metadata"
            };
        }

        const std::size_t firstNewline = text.find('\n');
        const std::string firstLine = trimCopy(
            std::string_view{ text }.substr(
                0,
                firstNewline == std::string_view::npos
                    ? std::string_view::npos
                    : firstNewline));

        std::string statusToken;
        const std::size_t pipe = firstLine.find('|');
        if (pipe != std::string::npos)
        {
            statusToken = compactStatus(
                std::string_view{ firstLine }.substr(0, pipe));
        }

        const std::unordered_map<std::string, std::string> fields =
            parseNamedFields(text);

        if (statusToken.empty())
        {
            if (const std::optional<std::string> status = fieldValue(fields, "STATUS"))
            {
                statusToken = compactStatus(*status);
            }
        }

        if (statusToken.empty() && fieldValue(fields, "DATE").has_value())
        {
            statusToken = "OK";
        }

        if (
            statusToken == "AMBIGUOUS"
            || statusToken == "UNCERTAIN"
            || statusToken == "UNKNOWN")
        {
            std::string reason = fieldValue(fields, "REASON").value_or(
                "date/time classifier marked filing stamp metadata ambiguous");

            if (const std::optional<std::string> missing = fieldValue(fields, "MISSING"))
            {
                if (!missing->empty())
                {
                    reason += "; missing=" + *missing;
                }
            }

            return FilingDateTimeMetadata{
                .status = FilingMetadataStatus::Ambiguous,
                .filingDateIso = {},
                .filingTimeHhmm = {},
                .confidence = {},
                .reason = std::move(reason)
            };
        }

        if (statusToken != "OK" && statusToken != "SUCCESS" && statusToken != "RESOLVED")
        {
            return FilingDateTimeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingDateIso = {},
                .filingTimeHhmm = {},
                .confidence = {},
                .reason = "date/time classifier output did not contain a recognized structured status"
            };
        }

        const std::optional<std::string> rawDate = fieldValue(fields, "DATE");
        if (!rawDate.has_value())
        {
            return FilingDateTimeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingDateIso = {},
                .filingTimeHhmm = {},
                .confidence = {},
                .reason = "resolved date/time classifier output omitted DATE"
            };
        }

        const std::optional<std::string> date = normalizeDate(*rawDate);
        if (!date.has_value())
        {
            return FilingDateTimeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingDateIso = {},
                .filingTimeHhmm = {},
                .confidence = {},
                .reason = "date/time classifier returned an invalid filing date"
            };
        }

        std::string time;
        if (const std::optional<std::string> rawTime = fieldValue(fields, "TIME"))
        {
            const std::optional<std::string> normalized = normalizeTime(*rawTime);
            if (!normalized.has_value())
            {
                return FilingDateTimeMetadata{
                    .status = FilingMetadataStatus::Malformed,
                    .filingDateIso = {},
                    .filingTimeHhmm = {},
                    .confidence = {},
                    .reason = "date/time classifier returned an invalid filing time"
                };
            }

            time = *normalized;
        }

        return FilingDateTimeMetadata{
            .status = FilingMetadataStatus::Resolved,
            .filingDateIso = *date,
            .filingTimeHhmm = std::move(time),
            .confidence = normalizeConfidence(
                fieldValue(fields, "CONFIDENCE").value_or("unspecified")),
            .reason = {}
        };
    }


    FilingTypeMetadata parseFilingTypeProtocol(
        const std::string_view raw)
    {
        const std::string text = trimCopy(raw);
        if (text.empty())
        {
            return FilingTypeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingType = {},
                .confidence = {},
                .reason = "filing-type classifier returned no visible structured metadata"
            };
        }

        const std::size_t firstNewline = text.find('\n');
        const std::string firstLine = trimCopy(
            std::string_view{ text }.substr(
                0,
                firstNewline == std::string_view::npos
                    ? std::string_view::npos
                    : firstNewline));

        std::string statusToken;
        const std::size_t pipe = firstLine.find('|');
        if (pipe != std::string::npos)
        {
            statusToken = compactStatus(
                std::string_view{ firstLine }.substr(0, pipe));
        }

        const std::unordered_map<std::string, std::string> fields =
            parseNamedFields(text);

        if (statusToken.empty())
        {
            if (const std::optional<std::string> status = fieldValue(fields, "STATUS"))
            {
                statusToken = compactStatus(*status);
            }
        }

        if (statusToken.empty() && fieldValue(fields, "TYPE").has_value())
        {
            statusToken = "OK";
        }

        if (
            statusToken == "AMBIGUOUS"
            || statusToken == "UNCERTAIN"
            || statusToken == "UNKNOWN")
        {
            std::string reason = fieldValue(fields, "REASON").value_or(
                "filing-type classifier marked document type ambiguous");

            if (const std::optional<std::string> missing = fieldValue(fields, "MISSING"))
            {
                if (!missing->empty())
                {
                    reason += "; missing=" + *missing;
                }
            }

            return FilingTypeMetadata{
                .status = FilingMetadataStatus::Ambiguous,
                .filingType = {},
                .confidence = {},
                .reason = std::move(reason)
            };
        }

        if (statusToken != "OK" && statusToken != "SUCCESS" && statusToken != "RESOLVED")
        {
            return FilingTypeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingType = {},
                .confidence = {},
                .reason = "filing-type classifier output did not contain a recognized structured status"
            };
        }

        const std::optional<std::string> rawType = fieldValue(fields, "TYPE");
        if (!rawType.has_value())
        {
            return FilingTypeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingType = {},
                .confidence = {},
                .reason = "resolved filing-type classifier output omitted TYPE"
            };
        }

        const std::optional<std::string> filingType = normalizeFilingType(*rawType);
        if (!filingType.has_value())
        {
            return FilingTypeMetadata{
                .status = FilingMetadataStatus::Malformed,
                .filingType = {},
                .confidence = {},
                .reason = "filing-type classifier returned an invalid filing type"
            };
        }

        return FilingTypeMetadata{
            .status = FilingMetadataStatus::Resolved,
            .filingType = *filingType,
            .confidence = normalizeConfidence(
                fieldValue(fields, "CONFIDENCE").value_or("unspecified")),
            .reason = {}
        };
    }


    FilingBasenameResult formatFilingBasename(
        const FilingMetadata& metadata,
        const bool requireFilingTime,
        const std::size_t maximumBytes)
    {
        if (metadata.status != FilingMetadataStatus::Resolved)
        {
            return basenameFailure(
                metadata.reason.empty()
                    ? "filing metadata is not resolved"
                    : metadata.reason);
        }

        if (metadata.confidence == "low")
        {
            return basenameFailure("classifier confidence is low");
        }

        const std::optional<std::string> date = normalizeDate(metadata.filingDateIso);
        const std::optional<std::string> filingType = normalizeFilingType(metadata.filingType);
        const std::optional<std::string> time = normalizeTime(metadata.filingTimeHhmm);

        if (!date.has_value() || !filingType.has_value() || !time.has_value())
        {
            return basenameFailure(
                "filing metadata failed deterministic validation");
        }

        if (requireFilingTime && time->empty())
        {
            return basenameFailure(
                "the naming instruction requires a filing time but none is supported by the evidence");
        }

        const std::vector<std::string_view> dateParts = splitOn(*date, '-');
        if (dateParts.size() != 3)
        {
            return basenameFailure(
                "canonical filing date could not be formatted");
        }

        int year{ 0 };
        int month{ 0 };
        int day{ 0 };
        if (
            !parseUnsigned(dateParts[0], year)
            || !parseUnsigned(dateParts[1], month)
            || !parseUnsigned(dateParts[2], day))
        {
            return basenameFailure(
                "canonical filing date could not be formatted");
        }

        std::string basename =
            std::to_string(month)
            + "."
            + std::to_string(day)
            + "."
            + std::to_string(year);

        if (!time->empty())
        {
            basename += "_" + *time;
        }

        basename += " " + *filingType;

        while (!basename.empty() && (basename.back() == '.' || basename.back() == ' '))
        {
            basename.pop_back();
        }

        if (
            basename.empty()
            || basename.size() > maximumBytes
            || containsInvalidWindowsFilenameCharacter(basename))
        {
            return basenameFailure(
                "deterministic filing filename is invalid or exceeds Rose's configured length limit");
        }

        return FilingBasenameResult{
            .ok = true,
            .basename = std::move(basename),
            .reason = {}
        };
    }

} // namespace rose::tools
