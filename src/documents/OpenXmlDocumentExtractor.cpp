#include "documents/OpenXmlDocumentExtractor.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::documents
{
    namespace
    {
        [[nodiscard]]
        std::string lowerAscii(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return value;
        }

        [[nodiscard]]
        std::string normalizeExtension(std::string_view extension)
        {
            std::string result{ extension };
            result = lowerAscii(std::move(result));
            if (!result.empty() && result.front() != '.')
            {
                result.insert(result.begin(), '.');
            }
            return result;
        }

        void replaceAll(
            std::string& value,
            const std::string_view needle,
            const std::string_view replacement)
        {
            std::size_t position{ 0 };
            while ((position = value.find(needle, position)) != std::string::npos)
            {
                value.replace(position, needle.size(), replacement);
                position += replacement.size();
            }
        }

        [[nodiscard]]
        std::string decodeXmlText(std::string value)
        {
            replaceAll(value, "&lt;", "<");
            replaceAll(value, "&gt;", ">");
            replaceAll(value, "&quot;", "\"");
            replaceAll(value, "&apos;", "'");
            replaceAll(value, "&amp;", "&");
            return value;
        }

        [[nodiscard]]
        std::string trim(std::string value)
        {
            const auto first = std::find_if(
                value.begin(), value.end(),
                [](const unsigned char c) { return std::isspace(c) == 0; });
            if (first == value.end()) return {};
            const auto last = std::find_if(
                value.rbegin(), value.rend(),
                [](const unsigned char c) { return std::isspace(c) == 0; }).base();
            return std::string{ first, last };
        }

        [[nodiscard]]
        std::vector<std::string> collectTagText(
            const std::string_view xml,
            const std::string_view tagName)
        {
            std::vector<std::string> values;
            const std::string open = "<" + std::string{ tagName };
            const std::string close = "</" + std::string{ tagName } + ">";
            std::size_t cursor{ 0 };

            while (cursor < xml.size())
            {
                const std::size_t begin = xml.find(open, cursor);
                if (begin == std::string_view::npos) break;
                const std::size_t body = xml.find('>', begin + open.size());
                if (body == std::string_view::npos) break;
                const std::size_t end = xml.find(close, body + 1);
                if (end == std::string_view::npos) break;

                values.push_back(
                    decodeXmlText(
                        std::string{ xml.substr(body + 1, end - body - 1) }));
                cursor = end + close.size();
            }
            return values;
        }

        [[nodiscard]]
        std::optional<std::string> attributeValue(
            const std::string_view tag,
            const std::string_view attribute)
        {
            const std::string pattern = std::string{ attribute } + "=\"";
            const std::size_t begin = tag.find(pattern);
            if (begin == std::string_view::npos) return std::nullopt;
            const std::size_t valueBegin = begin + pattern.size();
            const std::size_t end = tag.find('"', valueBegin);
            if (end == std::string_view::npos) return std::nullopt;
            return decodeXmlText(std::string{ tag.substr(valueBegin, end - valueBegin) });
        }

        [[nodiscard]]
        const OpenXmlPackageEntry* findEntry(
            const std::vector<OpenXmlPackageEntry>& entries,
            const std::string_view path)
        {
            const auto found = std::find_if(
                entries.begin(), entries.end(),
                [&](const OpenXmlPackageEntry& entry)
                {
                    return entry.path == path;
                });
            return found == entries.end() ? nullptr : &*found;
        }

        [[nodiscard]]
        std::string wordLikeText(
            const std::string_view xml,
            const std::string_view textTag,
            const std::string_view paragraphClose)
        {
            std::string output;
            std::size_t cursor{ 0 };
            const std::string open = "<" + std::string{ textTag };
            const std::string close = "</" + std::string{ textTag } + ">";

            while (cursor < xml.size())
            {
                const std::size_t textBegin = xml.find(open, cursor);
                const std::size_t paragraph = xml.find(paragraphClose, cursor);

                if (paragraph != std::string_view::npos
                    && (textBegin == std::string_view::npos || paragraph < textBegin))
                {
                    if (!output.empty() && output.back() != '\n') output.push_back('\n');
                    cursor = paragraph + paragraphClose.size();
                    continue;
                }
                if (textBegin == std::string_view::npos) break;

                const std::size_t body = xml.find('>', textBegin + open.size());
                if (body == std::string_view::npos) break;
                const std::size_t textEnd = xml.find(close, body + 1);
                if (textEnd == std::string_view::npos) break;

                output += decodeXmlText(
                    std::string{ xml.substr(body + 1, textEnd - body - 1) });
                cursor = textEnd + close.size();
            }

            return trim(std::move(output));
        }

        [[nodiscard]]
        int trailingNumber(
            const std::string_view path,
            const std::string_view stem)
        {
            const std::size_t position = path.rfind(stem);
            if (position == std::string_view::npos) return 0;
            const std::size_t begin = position + stem.size();
            std::size_t end = begin;
            while (end < path.size() && std::isdigit(static_cast<unsigned char>(path[end])) != 0)
            {
                ++end;
            }
            int value{ 0 };
            const auto parsed = std::from_chars(path.data() + begin, path.data() + end, value);
            return parsed.ec == std::errc{} ? value : 0;
        }

        [[nodiscard]]
        ExtractedOpenXmlDocument extractWord(
            const std::vector<OpenXmlPackageEntry>& entries)
        {
            ExtractedOpenXmlDocument result;
            result.contentKind = "office/word";

            if (const auto* document = findEntry(entries, "word/document.xml"))
            {
                const std::string text = wordLikeText(document->xml, "w:t", "</w:p>");
                if (!text.empty()) result.segments.push_back({ "document", text });
            }

            std::vector<const OpenXmlPackageEntry*> extras;
            for (const auto& entry : entries)
            {
                if (entry.path.starts_with("word/header") || entry.path.starts_with("word/footer"))
                {
                    extras.push_back(&entry);
                }
            }
            std::sort(extras.begin(), extras.end(), [](const auto* a, const auto* b) { return a->path < b->path; });
            for (const auto* entry : extras)
            {
                const std::string text = wordLikeText(entry->xml, "w:t", "</w:p>");
                if (!text.empty()) result.segments.push_back({ entry->path, text });
            }
            return result;
        }

        [[nodiscard]]
        ExtractedOpenXmlDocument extractPowerPoint(
            const std::vector<OpenXmlPackageEntry>& entries)
        {
            ExtractedOpenXmlDocument result;
            result.contentKind = "office/powerpoint";

            std::vector<const OpenXmlPackageEntry*> slides;
            for (const auto& entry : entries)
            {
                if (entry.path.starts_with("ppt/slides/slide") && entry.path.ends_with(".xml"))
                {
                    slides.push_back(&entry);
                }
            }
            std::sort(
                slides.begin(), slides.end(),
                [](const auto* left, const auto* right)
                {
                    return trailingNumber(left->path, "slide") < trailingNumber(right->path, "slide");
                });

            for (const auto* slide : slides)
            {
                const auto runs = collectTagText(slide->xml, "a:t");
                std::string text;
                for (const auto& run : runs)
                {
                    if (!text.empty()) text.push_back('\n');
                    text += run;
                }
                text = trim(std::move(text));
                if (!text.empty())
                {
                    result.segments.push_back({
                        "slide=" + std::to_string(trailingNumber(slide->path, "slide")),
                        std::move(text)
                    });
                }
            }
            return result;
        }

        [[nodiscard]]
        std::vector<std::string> sharedStrings(
            const std::vector<OpenXmlPackageEntry>& entries)
        {
            const auto* shared = findEntry(entries, "xl/sharedStrings.xml");
            if (shared == nullptr) return {};

            std::vector<std::string> strings;
            std::size_t cursor{ 0 };
            while (cursor < shared->xml.size())
            {
                const std::size_t begin = shared->xml.find("<si", cursor);
                if (begin == std::string::npos) break;
                const std::size_t body = shared->xml.find('>', begin + 3);
                if (body == std::string::npos) break;
                const std::size_t end = shared->xml.find("</si>", body + 1);
                if (end == std::string::npos) break;

                const std::string_view si{ shared->xml.data() + body + 1, end - body - 1 };
                const auto pieces = collectTagText(si, "t");
                std::string combined;
                for (const auto& piece : pieces) combined += piece;
                strings.push_back(std::move(combined));
                cursor = end + 5;
            }
            return strings;
        }

        [[nodiscard]]
        std::unordered_map<std::string, std::string> workbookSheetPaths(
            const std::vector<OpenXmlPackageEntry>& entries)
        {
            std::unordered_map<std::string, std::string> relationships;
            if (const auto* rels = findEntry(entries, "xl/_rels/workbook.xml.rels"))
            {
                std::size_t cursor{ 0 };
                while ((cursor = rels->xml.find("<Relationship", cursor)) != std::string::npos)
                {
                    const std::size_t end = rels->xml.find('>', cursor);
                    if (end == std::string::npos) break;
                    const std::string_view tag{ rels->xml.data() + cursor, end - cursor + 1 };
                    const auto id = attributeValue(tag, "Id");
                    const auto target = attributeValue(tag, "Target");
                    if (id && target)
                    {
                        std::string normalized = *target;
                        while (normalized.starts_with("../")) normalized.erase(0, 3);
                        while (normalized.starts_with('/')) normalized.erase(0, 1);
                        if (!normalized.starts_with("xl/")) normalized = "xl/" + normalized;
                        relationships[*id] = std::move(normalized);
                    }
                    cursor = end + 1;
                }
            }

            std::unordered_map<std::string, std::string> sheetNames;
            if (const auto* workbook = findEntry(entries, "xl/workbook.xml"))
            {
                std::size_t cursor{ 0 };
                while ((cursor = workbook->xml.find("<sheet", cursor)) != std::string::npos)
                {
                    const std::size_t end = workbook->xml.find('>', cursor);
                    if (end == std::string::npos) break;
                    const std::string_view tag{ workbook->xml.data() + cursor, end - cursor + 1 };
                    const auto name = attributeValue(tag, "name");
                    const auto id = attributeValue(tag, "r:id");
                    if (name && id)
                    {
                        const auto relationship = relationships.find(*id);
                        if (relationship != relationships.end())
                        {
                            sheetNames[relationship->second] = *name;
                        }
                    }
                    cursor = end + 1;
                }
            }
            return sheetNames;
        }

        [[nodiscard]]
        std::optional<std::string> firstTagText(
            const std::string_view xml,
            const std::string_view tag)
        {
            auto values = collectTagText(xml, tag);
            if (values.empty()) return std::nullopt;
            return values.front();
        }

        [[nodiscard]]
        ExtractedOpenXmlDocument extractExcel(
            const std::vector<OpenXmlPackageEntry>& entries)
        {
            ExtractedOpenXmlDocument result;
            result.contentKind = "office/excel";
            const auto strings = sharedStrings(entries);
            const auto names = workbookSheetPaths(entries);

            std::vector<const OpenXmlPackageEntry*> sheets;
            for (const auto& entry : entries)
            {
                if (entry.path.starts_with("xl/worksheets/sheet") && entry.path.ends_with(".xml"))
                {
                    sheets.push_back(&entry);
                }
            }
            std::sort(
                sheets.begin(), sheets.end(),
                [](const auto* left, const auto* right)
                {
                    return trailingNumber(left->path, "sheet") < trailingNumber(right->path, "sheet");
                });

            for (const auto* sheet : sheets)
            {
                const auto nameIt = names.find(sheet->path);
                const std::string sheetName = nameIt == names.end()
                    ? "Sheet" + std::to_string(trailingNumber(sheet->path, "sheet"))
                    : nameIt->second;

                std::ostringstream text;
                std::size_t cursor{ 0 };
                while ((cursor = sheet->xml.find("<c", cursor)) != std::string::npos)
                {
                    const std::size_t tagEnd = sheet->xml.find('>', cursor);
                    if (tagEnd == std::string::npos) break;
                    const std::string_view tag{ sheet->xml.data() + cursor, tagEnd - cursor + 1 };
                    const std::size_t cellEnd = sheet->xml.find("</c>", tagEnd + 1);
                    if (cellEnd == std::string::npos) break;
                    const std::string_view body{ sheet->xml.data() + tagEnd + 1, cellEnd - tagEnd - 1 };

                    const std::string cell = attributeValue(tag, "r").value_or("?");
                    const std::string type = attributeValue(tag, "t").value_or("");
                    std::string value;

                    if (type == "inlineStr")
                    {
                        const auto pieces = collectTagText(body, "t");
                        for (const auto& piece : pieces) value += piece;
                    }
                    else if (const auto raw = firstTagText(body, "v"))
                    {
                        value = *raw;
                        if (type == "s")
                        {
                            std::size_t index{ 0 };
                            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
                            if (parsed.ec == std::errc{} && index < strings.size()) value = strings[index];
                        }
                    }

                    const auto formula = firstTagText(body, "f");
                    if (!value.empty() || formula.has_value())
                    {
                        text << cell << '=';
                        if (formula) text << "formula(" << *formula << ") ";
                        text << value << '\n';
                    }
                    cursor = cellEnd + 4;
                }

                std::string rendered = trim(text.str());
                if (!rendered.empty())
                {
                    result.segments.push_back({ "sheet=" + sheetName, std::move(rendered) });
                }
            }
            return result;
        }

#ifdef _WIN32
        [[nodiscard]]
        std::wstring quoteWindowsArgument(const std::wstring& argument)
        {
            if (argument.empty()) return L"\"\"";
            if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;

            std::wstring result{ L"\"" };
            std::size_t slashes{ 0 };
            for (const wchar_t character : argument)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (character == L'\"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                    slashes = 0;
                    continue;
                }
                result.append(slashes, L'\\');
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        class TemporaryOpenXmlPackage final
        {
        public:
            TemporaryOpenXmlPackage(
                const std::span<const std::uint8_t> bytes,
                const std::string_view extension,
                const std::size_t maximumExtractedXmlBytes)
            {
                wchar_t tempBuffer[MAX_PATH + 1]{};
                const DWORD length = GetTempPathW(MAX_PATH, tempBuffer);
                if (length == 0 || length > MAX_PATH)
                {
                    throw std::runtime_error{ "Could not locate the Windows temporary directory." };
                }

                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                root_ = std::filesystem::path{ tempBuffer }
                    / (L"RoseOpenXml-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(stamp));
                std::filesystem::create_directories(root_ / "out");

                package_ = root_ / (L"package" + std::filesystem::path{ std::string{ extension } }.wstring());
                std::ofstream packageStream{ package_, std::ios::binary | std::ios::trunc };
                packageStream.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!packageStream) throw std::runtime_error{ "Could not create Rose's temporary Office package." };
                packageStream.close();

                const std::filesystem::path script = root_ / "extract.ps1";
                std::ofstream scriptStream{ script, std::ios::binary | std::ios::trunc };
                scriptStream
                    << "$ErrorActionPreference='Stop'\n"
                    << "Add-Type -AssemblyName System.IO.Compression\n"
                    << "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
                    << "$zip=[IO.Compression.ZipFile]::OpenRead($args[0])\n"
                    << "$root=[IO.Path]::GetFullPath($args[1])\n"
                    << "$prefix=$root.TrimEnd('\\') + '\\'\n"
                    << "$max=[int64]" << maximumExtractedXmlBytes << "\n"
                    << "$total=[int64]0\n"
                    << "try { foreach($e in $zip.Entries) {\n"
                    << "  $n=$e.FullName.Replace('\\','/')\n"
                    << "  $ok=($n -eq 'word/document.xml') -or ($n -like 'word/header*.xml') -or ($n -like 'word/footer*.xml') -or "
                       "($n -like 'ppt/slides/slide*.xml') -or ($n -eq 'xl/workbook.xml') -or ($n -eq 'xl/_rels/workbook.xml.rels') -or "
                       "($n -eq 'xl/sharedStrings.xml') -or ($n -like 'xl/worksheets/sheet*.xml')\n"
                    << "  if(-not $ok){ continue }\n"
                    << "  $total += $e.Length; if($e.Length -gt $max -or $total -gt $max){ throw 'Office XML exceeds Rose extraction limit.' }\n"
                    << "  $dest=[IO.Path]::GetFullPath((Join-Path $root $n))\n"
                    << "  if(-not $dest.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){ throw 'Unsafe Office package entry path.' }\n"
                    << "  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest)) | Out-Null\n"
                    << "  $input=$e.Open(); try { $output=[IO.File]::Create($dest); try { $input.CopyTo($output) } finally { $output.Dispose() } } finally { $input.Dispose() }\n"
                    << "} } finally { $zip.Dispose() }\n";
                if (!scriptStream) throw std::runtime_error{ "Could not create Rose's temporary Office extraction script." };
                scriptStream.close();

                std::wstring command = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ";
                command += quoteWindowsArgument(script.wstring());
                command += L" ";
                command += quoteWindowsArgument(package_.wstring());
                command += L" ";
                command += quoteWindowsArgument((root_ / "out").wstring());

                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION process{};
                std::vector<wchar_t> mutableCommand(command.begin(), command.end());
                mutableCommand.push_back(L'\0');

                if (!CreateProcessW(
                        nullptr,
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup,
                        &process))
                {
                    throw std::runtime_error{ "Could not launch PowerShell for Office package extraction." };
                }

                CloseHandle(process.hThread);
                const DWORD wait = WaitForSingleObject(process.hProcess, 120000);
                DWORD exitCode{ 1 };
                if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exitCode);
                else TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hProcess);

                if (wait != WAIT_OBJECT_0 || exitCode != 0)
                {
                    throw std::runtime_error{ "Windows could not unpack the Office Open XML document." };
                }
            }

            ~TemporaryOpenXmlPackage()
            {
                std::error_code error;
                std::filesystem::remove_all(root_, error);
            }

            [[nodiscard]]
            std::vector<OpenXmlPackageEntry> entries() const
            {
                std::vector<OpenXmlPackageEntry> result;
                const auto outputRoot = root_ / "out";
                std::error_code error;
                for (std::filesystem::recursive_directory_iterator it{
                         outputRoot,
                         std::filesystem::directory_options::skip_permission_denied,
                         error }, end;
                     it != end;
                     it.increment(error))
                {
                    if (error) { error.clear(); continue; }
                    if (!it->is_regular_file(error) || error) { error.clear(); continue; }
                    std::ifstream input{ it->path(), std::ios::binary };
                    std::ostringstream buffer;
                    buffer << input.rdbuf();
                    result.push_back({
                        std::filesystem::relative(it->path(), outputRoot).generic_string(),
                        buffer.str()
                    });
                }
                return result;
            }

        private:
            std::filesystem::path root_;
            std::filesystem::path package_;
        };
#endif
    }

    OpenXmlDocumentExtractor::OpenXmlDocumentExtractor(
        const OpenXmlDocumentExtractorConfig config)
        : config_{ config }
    {
        if (config_.maximumPackageBytes == 0 || config_.maximumExtractedXmlBytes == 0)
        {
            throw std::invalid_argument{ "OpenXmlDocumentExtractor limits must be greater than zero." };
        }
    }

    ExtractedOpenXmlDocument OpenXmlDocumentExtractor::extract(
        const std::span<const std::uint8_t> packageBytes,
        const std::string_view sourceExtension) const
    {
        if (packageBytes.empty())
        {
            throw std::runtime_error{ "The Office Open XML document is empty." };
        }
        if (packageBytes.size() > config_.maximumPackageBytes)
        {
            throw std::runtime_error{ "The Office Open XML document exceeds Rose's package-size limit." };
        }

#ifdef _WIN32
        TemporaryOpenXmlPackage package{
            packageBytes,
            normalizeExtension(sourceExtension),
            config_.maximumExtractedXmlBytes
        };
        return extractEntries(sourceExtension, package.entries());
#else
        (void)sourceExtension;
        throw std::runtime_error{
            "Office Open XML extraction is currently implemented for Rose's Windows target only."
        };
#endif
    }

    ExtractedOpenXmlDocument OpenXmlDocumentExtractor::extractEntries(
        const std::string_view sourceExtension,
        const std::vector<OpenXmlPackageEntry>& entries)
    {
        const std::string extension = normalizeExtension(sourceExtension);
        if (extension == ".docx" || extension == ".docm") return extractWord(entries);
        if (extension == ".pptx" || extension == ".pptm") return extractPowerPoint(entries);
        if (extension == ".xlsx" || extension == ".xlsm") return extractExcel(entries);
        throw std::invalid_argument{ "Unsupported Office Open XML extension: " + extension };
    }
}
