#include "tools/CourtFilingMetadataExtractor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]] std::string trimCopy(std::string_view value)
        {
            std::size_t begin = 0;
            std::size_t end = value.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
            return std::string{ value.substr(begin, end - begin) };
        }

        [[nodiscard]] std::string collapseWhitespace(std::string_view value)
        {
            std::ostringstream out;
            bool pendingSpace = false;
            bool wroteAny = false;
            for (const unsigned char c : value)
            {
                if (std::isspace(c) != 0)
                {
                    pendingSpace = wroteAny;
                    continue;
                }
                if (pendingSpace) out << ' ';
                out << static_cast<char>(c);
                wroteAny = true;
                pendingSpace = false;
            }
            return trimCopy(out.str());
        }

        [[nodiscard]] std::string upperAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            });
            return value;
        }

        [[nodiscard]] bool isLeap(const int year)
        {
            return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        }

        [[nodiscard]] bool validDate(const int year, const int month, const int day)
        {
            if (year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1) return false;
            static constexpr int days[]{ 31,28,31,30,31,30,31,31,30,31,30,31 };
            int maximum = days[month - 1];
            if (month == 2 && isLeap(year)) maximum = 29;
            return day <= maximum;
        }

        [[nodiscard]] std::string isoDate(const int year, const int month, const int day)
        {
            std::ostringstream out;
            out << year << '-';
            if (month < 10) out << '0';
            out << month << '-';
            if (day < 10) out << '0';
            out << day;
            return out.str();
        }

        [[nodiscard]] int monthNumber(const std::string_view token)
        {
            const std::string value = upperAscii(std::string{ token });
            static constexpr std::array<std::string_view, 12> months{
                "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
            };
            for (std::size_t i = 0; i < months.size(); ++i)
            {
                if (value.starts_with(months[i])) return static_cast<int>(i + 1);
            }
            return 0;
        }

        [[nodiscard]] std::optional<std::string> parseDateFromLine(const std::string& line)
        {
            std::smatch match;
            static const std::regex numeric{ R"((\d{1,2})\s*[/\.\-]\s*(\d{1,2})\s*[/\.\-]\s*(\d{2,4}))", std::regex::icase };
            if (std::regex_search(line, match, numeric))
            {
                int month = std::stoi(match[1].str());
                int day = std::stoi(match[2].str());
                int year = std::stoi(match[3].str());
                if (year < 100) year += 2000;
                if (validDate(year, month, day)) return isoDate(year, month, day);
            }

            static const std::regex monthFirst{ R"((JAN(?:UARY)?|FEB(?:RUARY)?|MAR(?:CH)?|APR(?:IL)?|MAY|JUN(?:E)?|JUL(?:Y)?|AUG(?:UST)?|SEP(?:TEMBER)?|OCT(?:OBER)?|NOV(?:EMBER)?|DEC(?:EMBER)?)\s*[^0-9A-Z]{0,3}\s*(\d{1,2})\s*[,\-]?\s*(\d{4}))", std::regex::icase };
            if (std::regex_search(line, match, monthFirst))
            {
                const int month = monthNumber(match[1].str());
                const int day = std::stoi(match[2].str());
                const int year = std::stoi(match[3].str());
                if (validDate(year, month, day)) return isoDate(year, month, day);
            }

            static const std::regex yearFirst{ R"((20\d{2})\s*(JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)\s*([0-3]?[0-9]))", std::regex::icase };
            if (std::regex_search(line, match, yearFirst))
            {
                const int year = std::stoi(match[1].str());
                const int month = monthNumber(match[2].str());
                const int day = std::stoi(match[3].str());
                if (validDate(year, month, day)) return isoDate(year, month, day);
            }

            static const std::regex looseMonth{ R"((JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)[^0-9]{0,6}(\d{1,2})[^0-9]{0,4}(20\d{2}))", std::regex::icase };
            if (std::regex_search(line, match, looseMonth))
            {
                const int month = monthNumber(match[1].str());
                const int day = std::stoi(match[2].str());
                const int year = std::stoi(match[3].str());
                if (validDate(year, month, day)) return isoDate(year, month, day);
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> parseTimeFromLine(const std::string& line)
        {
            std::smatch match;
            static const std::regex clock{ R"((\d{1,2})\s*:\s*(\d{2})\s*(AM|PM)?)", std::regex::icase };
            if (!std::regex_search(line, match, clock)) return std::nullopt;

            int hour = std::stoi(match[1].str());
            const int minute = std::stoi(match[2].str());
            if (minute < 0 || minute > 59) return std::nullopt;

            const std::string suffix = upperAscii(match[3].str());
            if (!suffix.empty())
            {
                if (hour < 1 || hour > 12) return std::nullopt;
                if (suffix == "AM" && hour == 12) hour = 0;
                if (suffix == "PM" && hour != 12) hour += 12;
            }
            else if (hour > 23)
            {
                return std::nullopt;
            }

            std::ostringstream out;
            if (hour < 10) out << '0';
            out << hour;
            if (minute < 10) out << '0';
            out << minute;
            return out.str();
        }

        [[nodiscard]] bool filingSignal(const std::string& line)
        {
            const std::string upper = upperAscii(line);
            return upper.find("E-FILED") != std::string::npos
                || upper.find("E FILED") != std::string::npos
                || upper == "FILED"
                || upper.starts_with("FILED ")
                || upper.find(" FILED ") != std::string::npos
                || (upper.starts_with("[SIGNAL") && upper.find("FILED") != std::string::npos);
        }

        [[nodiscard]] std::string stripSignalPrefix(std::string value)
        {
            value = trimCopy(value);
            if (value.starts_with("[signal]")) value = trimCopy(value.substr(8));
            if (value.starts_with("[signal-context]")) value = trimCopy(value.substr(16));
            return value;
        }

        [[nodiscard]] std::string titleCaseAllCaps(std::string title)
        {
            bool sawLetter = false;
            bool allUpper = true;
            for (const unsigned char c : title)
            {
                if (std::isalpha(c) == 0) continue;
                sawLetter = true;
                if (std::islower(c) != 0) allUpper = false;
            }
            if (!sawLetter || !allUpper) return title;

            static constexpr std::array<std::string_view, 13> connectors{
                "a", "an", "and", "as", "at", "by", "for", "from",
                "in", "of", "on", "the", "to"
            };

            std::istringstream words{ title };
            std::ostringstream normalized;
            std::string word;
            std::size_t index = 0;
            while (words >> word)
            {
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c)
                { return static_cast<char>(std::tolower(c)); });
                const bool connector = index > 0
                    && std::find(connectors.begin(), connectors.end(), word) != connectors.end();
                if (!connector)
                {
                    for (char& c : word)
                    {
                        if (std::isalpha(static_cast<unsigned char>(c)) != 0)
                        {
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                            break;
                        }
                    }
                }
                if (index++ > 0) normalized << ' ';
                normalized << word;
            }
            return normalized.str();
        }

        [[nodiscard]] std::optional<std::size_t> allowedAnchorStart(const std::string& value)
        {
            const std::string upper = upperAscii(value);
            static constexpr std::array<std::string_view, 15> anchors{
                "SHERIFF'S RETURN OF SERVICE", "RETURN OF SERVICE", "DECLARATION",
                "MOTION", "ORDER", "NOTICE", "PETITION", "REISSUANCE",
                "AFFIDAVIT", "CERTIFICATE", "PROOF OF", "SUMMONS",
                "MEMORANDUM", "BRIEF", "MINUTE"
            };

            for (const std::string_view anchor : anchors)
            {
                if (upper.starts_with(anchor)) return 0u;
            }

            // Captions often place a party name and the title on the same OCR
            // line, separated by a comma. Only allow anchor recovery after that
            // structural boundary; do not search arbitrary prose for legal words.
            const std::size_t comma = value.find(',');
            if (comma != std::string::npos)
            {
                const std::string tail = trimCopy(std::string_view{ value }.substr(comma + 1));
                const std::string tailUpper = upperAscii(tail);
                for (const std::string_view anchor : anchors)
                {
                    if (tailUpper.starts_with(anchor))
                    {
                        const std::size_t nonSpace = value.find_first_not_of(" \t", comma + 1);
                        return nonSpace == std::string::npos ? comma + 1 : nonSpace;
                    }
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] bool containsSentenceTail(const std::string& upper)
        {
            static constexpr std::array<std::string_view, 16> proseMarkers{
                " BECAUSE ", " NORMALLY ", " SHALL ", " WILL ", " WAS ",
                " WERE ", " IS NOT ", " ARE NOT ", " IN SUPPORT OF THIS",
                " AND OTHER PAPERS", " I ASK ", " THE COURT ", " THIS MATTER ",
                " ATTACH THIS", " SUBMITTED BY", " FILED IN SUPPORT"
            };
            for (const std::string_view marker : proseMarkers)
            {
                if (upper.find(marker) != std::string::npos) return true;
            }
            return false;
        }

        [[nodiscard]] std::string stripFooterAndCodes(std::string title)
        {
            title = collapseWhitespace(stripSignalPrefix(std::move(title)));

            // Remove page/footer material before any other interpretation. This
            // specifically prevents law-firm footer text from becoming a title.
            title = std::regex_replace(
                title,
                std::regex{ R"(\s*[-–—]?\s*PAGE\s*[|I1\d]+\s+OF\s+[|I1\d]+.*$)", std::regex::icase },
                "");

            title = std::regex_replace(
                title,
                std::regex{ R"(\s*\([A-Z0-9]{2,10}\)\s*$)" },
                "");

            // Common OCR punctuation around docket codes after a title.
            while (!title.empty() && (title.back() == '.' || title.back() == ':' || title.back() == ';' || title.back() == '}' || title.back() == '|'))
            {
                title.pop_back();
            }
            return trimCopy(title);
        }

        [[nodiscard]] std::optional<std::string> declarationCanonical(const std::string& value)
        {
            std::smatch match;
            static const std::regex role{
                R"(^DECLARATION\s+OF\s+(PETITIONER|RESPONDENT|ATTORNEY|COUNSEL|WITNESS|SERVER|SERVICE|MAILING)\b)",
                std::regex::icase };
            if (std::regex_search(value, match, role))
            {
                return titleCaseAllCaps("DECLARATION OF " + upperAscii(match[1].str()));
            }
            if (upperAscii(value) == "DECLARATION") return std::string{ "Declaration" };
            return std::nullopt;
        }

        [[nodiscard]] bool acceptableGenericTitle(const std::string& value)
        {
            const std::string upper = upperAscii(value);
            if (value.size() < 5 || value.size() > 100 || containsSentenceTail(upper)) return false;

            if (upper.starts_with("MOTION "))
            {
                return upper.starts_with("MOTION TO ")
                    || upper.starts_with("MOTION FOR ")
                    || upper.starts_with("MOTION IN ")
                    || upper.starts_with("MOTION RE ");
            }
            if (upper.starts_with("ORDER "))
            {
                static constexpr std::array<std::string_view, 16> starts{
                    "ORDER TO ", "ORDER FOR ", "ORDER OF ", "ORDER ON ",
                    "ORDER RE ", "ORDER GRANTING ", "ORDER DENYING ",
                    "ORDER APPROVING ", "ORDER DISMISSING ", "ORDER CONTINUING ",
                    "ORDER CONSOLIDATING ", "ORDER SETTING ", "ORDER APPOINTING ",
                    "ORDER MODIFYING ", "ORDER TERMINATING ", "ORDER REISSUING "
                };
                return std::any_of(starts.begin(), starts.end(), [&](const std::string_view prefix)
                { return upper.starts_with(prefix); });
            }
            if (upper.starts_with("NOTICE "))
            {
                return upper.starts_with("NOTICE OF ")
                    || upper.starts_with("NOTICE TO ")
                    || upper.starts_with("NOTICE FOR ");
            }
            if (upper.starts_with("PETITION "))
            {
                return upper.starts_with("PETITION FOR ") || upper.starts_with("PETITION TO ");
            }
            if (upper.starts_with("REISSUANCE ")) return upper.starts_with("REISSUANCE OF ");
            if (upper.starts_with("PROOF OF ")) return true;
            if (upper == "RETURN OF SERVICE" || upper == "SHERIFF'S RETURN OF SERVICE") return true;
            if (upper.starts_with("AFFIDAVIT ") || upper == "AFFIDAVIT") return true;
            if (upper.starts_with("CERTIFICATE ") || upper == "CERTIFICATE") return true;
            if (upper.starts_with("SUMMONS")) return true;
            if (upper.starts_with("MEMORANDUM ") || upper == "MEMORANDUM") return true;
            if (upper.starts_with("BRIEF ") || upper == "BRIEF") return true;
            if (upper == "MINUTE ENTRY" || upper == "MINUTES") return true;
            return false;
        }

        struct Candidate
        {
            std::string text;
            std::string key;
            int count{ 1 };
            int quality{ 0 };
        };

        [[nodiscard]] std::optional<std::string> detectTitle(const std::vector<std::string>& lines)
        {
            std::vector<Candidate> candidates;

            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                const auto consider = [&](std::string combined)
                {
                    CanonicalFilingTypeResult canonical = canonicalizeCourtFilingType(combined);
                    if (!canonical.ok) return;

                    // Preserve a useful split-line legal continuation when OCR put
                    // "OF JUDGE" on the line immediately after a Notice heading.
                    if (
                        i + 1 < lines.size()
                        && upperAscii(canonical.filingType) == "NOTICE OF DISQUALIFICATION"
                        && upperAscii(trimCopy(lines[i + 1])).starts_with("OF JUDGE"))
                    {
                        canonical.filingType = "Notice of Disqualification of Judge";
                    }

                    const std::string key = upperAscii(canonical.filingType);
                    auto found = std::find_if(candidates.begin(), candidates.end(), [&](const Candidate& item)
                    { return item.key == key; });
                    if (found == candidates.end())
                    {
                        int quality = 1;
                        if (key.find("PAGE ") == std::string::npos) ++quality;
                        if (key.starts_with("DECLARATION OF ") || key.starts_with("NOTICE OF ") || key.starts_with("MOTION ")) ++quality;
                        candidates.push_back(Candidate{ canonical.filingType, key, 1, quality });
                    }
                    else
                    {
                        ++found->count;
                    }
                };

                consider(lines[i]);
            }

            if (candidates.empty()) return std::nullopt;
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
            {
                if (a.count != b.count) return a.count > b.count;
                if (a.quality != b.quality) return a.quality > b.quality;
                // Prefer the compact canonical title when evidence strength ties.
                return a.text.size() < b.text.size();
            });
            return candidates.front().text;
        }
    }


    CanonicalFilingTypeResult canonicalizeCourtFilingType(const std::string_view candidate)
    {
        std::string value = stripFooterAndCodes(std::string{ candidate });
        if (value.empty()) return { false, {}, "empty filing type candidate" };

        const std::optional<std::size_t> start = allowedAnchorStart(value);
        if (!start.has_value())
        {
            return { false, {}, "candidate does not begin at a recognized filing-title boundary" };
        }
        if (*start > 0) value = trimCopy(std::string_view{ value }.substr(*start));

        // Normalize a few high-confidence OCR joins that affect title grammar.
        value = std::regex_replace(value, std::regex{ R"(ORDERFOR)", std::regex::icase }, "ORDER FOR");
        value = std::regex_replace(value, std::regex{ R"(OF-FEMPORARY)", std::regex::icase }, "OF TEMPORARY");
        value = collapseWhitespace(value);

        if (const auto declaration = declarationCanonical(value); declaration.has_value())
        {
            return { true, *declaration, {} };
        }

        // These highly recurring form titles have useful canonical boundaries.
        const std::string upper = upperAscii(value);
        if (upper.starts_with("NOTICE OF APPEARANCE"))
            return { true, "Notice of Appearance", {} };
        if (upper.starts_with("NOTICE OF DISQUALIFICATION OF JUDGE"))
            return { true, "Notice of Disqualification of Judge", {} };
        if (upper.starts_with("NOTICE OF DISQUALIFICATION"))
            return { true, "Notice of Disqualification", {} };
        if (upper.starts_with("SHERIFF'S RETURN OF SERVICE"))
            return { true, "Sheriff's Return of Service", {} };
        if (upper.starts_with("RETURN OF SERVICE"))
            return { true, "Return of Service", {} };

        if (!acceptableGenericTitle(value))
        {
            return { false, {}, "candidate resembles prose or does not match filing-title grammar" };
        }

        value = titleCaseAllCaps(value);
        return { true, trimCopy(value), {} };
    }


    DeterministicFilingExtraction extractCourtFilingMetadata(
        const std::string_view rawEvidence,
        const bool requireFilingTime)
    {
        DeterministicFilingExtraction result;
        result.metadata.status = FilingMetadataStatus::Ambiguous;
        result.metadata.confidence = "high";

        OcrRepairResult repaired = repairContextualOcr(rawEvidence);
        result.repairs = repaired.repairs;

        std::vector<std::string> lines;
        std::istringstream input{ repaired.text };
        std::string line;
        while (std::getline(input, line)) lines.push_back(line);

        std::optional<std::string> filingDate;
        std::optional<std::string> filingTime;
        std::size_t filingLine = lines.size();

        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            if (!filingSignal(trimCopy(lines[i]))) continue;
            filingLine = i;

            const std::size_t end = (std::min)(lines.size(), i + 5u);
            for (std::size_t j = i; j < end; ++j)
            {
                if (!filingDate.has_value()) filingDate = parseDateFromLine(lines[j]);
                if (!filingTime.has_value()) filingTime = parseTimeFromLine(lines[j]);
            }
            if (filingDate.has_value()) break;
        }

        const std::optional<std::string> title = detectTitle(lines);

        if (filingDate.has_value()) result.metadata.filingDateIso = *filingDate;
        if (filingTime.has_value()) result.metadata.filingTimeHhmm = *filingTime;
        if (title.has_value()) result.metadata.filingType = *title;

        if (!filingDate.has_value() || !title.has_value())
        {
            if (!filingDate.has_value() && !title.has_value())
                result.reason = "deterministic parser could not prove filing date or canonical filing type";
            else if (!filingDate.has_value())
                result.reason = "deterministic parser could not prove a court/clerk filing date";
            else
                result.reason = "deterministic parser could not prove a canonical document filing type";
            result.metadata.reason = result.reason;
            return result;
        }

        if (requireFilingTime && !filingTime.has_value())
        {
            result.reason = "filing time is required but not visible in the filing-stamp region";
            result.metadata.reason = result.reason;
            return result;
        }

        result.metadata.status = FilingMetadataStatus::Resolved;
        result.metadata.reason.clear();

        std::ostringstream evidence;
        evidence << "filing_signal_line=" << (filingLine < lines.size() ? trimCopy(lines[filingLine]) : "unknown")
                 << "; date=" << result.metadata.filingDateIso
                 << "; time=" << (result.metadata.filingTimeHhmm.empty() ? "UNKNOWN" : result.metadata.filingTimeHhmm)
                 << "; type=" << result.metadata.filingType
                 << "; ocr_repairs=" << result.repairs.size();
        result.evidence = evidence.str();
        return result;
    }

} // namespace rose::tools
