#include "documents/OfficeDocumentMutationService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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
        constexpr std::size_t maximumTextBytes{ 2u * 1024u * 1024u };
        constexpr std::size_t maximumSheetNameBytes{ 128u };

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
        std::string extensionLower(const std::filesystem::path& path)
        {
            return lowerAscii(path.extension().string());
        }

        [[nodiscard]]
        std::string kindName(const OfficeDocumentKind kind)
        {
            switch (kind)
            {
            case OfficeDocumentKind::Word: return "word";
            case OfficeDocumentKind::Excel: return "excel";
            case OfficeDocumentKind::PowerPoint: return "powerpoint";
            }
            return "unknown";
        }

        [[nodiscard]]
        bool extensionMatchesKind(
            const std::filesystem::path& path,
            const OfficeDocumentKind kind)
        {
            const std::string extension = extensionLower(path);
            switch (kind)
            {
            case OfficeDocumentKind::Word:
                return extension == ".docx";
            case OfficeDocumentKind::Excel:
                return extension == ".xlsx";
            case OfficeDocumentKind::PowerPoint:
                return extension == ".pptx";
            }
            return false;
        }

        [[nodiscard]]
        bool validExcelCellReference(const std::string_view value)
        {
            if (value.empty()) return false;

            std::size_t cursor{ 0 };
            std::uint32_t column{ 0 };
            while (cursor < value.size()
                && value[cursor] >= 'A'
                && value[cursor] <= 'Z')
            {
                column = column * 26u
                    + static_cast<std::uint32_t>(value[cursor] - 'A' + 1);
                ++cursor;
                if (cursor > 3) return false;
            }
            if (cursor == 0 || column == 0 || column > 16384u) return false;
            if (cursor >= value.size() || value[cursor] == '0') return false;

            std::uint32_t row{ 0 };
            for (; cursor < value.size(); ++cursor)
            {
                const char c = value[cursor];
                if (c < '0' || c > '9') return false;
                row = row * 10u + static_cast<std::uint32_t>(c - '0');
                if (row > 1048576u) return false;
            }
            return row >= 1u;
        }

        void validateTextBound(const std::string_view text, const std::string_view name)
        {
            if (text.size() > maximumTextBytes)
            {
                throw std::invalid_argument{
                    std::string{ name } + " exceeds Rose's 2 MiB Office mutation bound."
                };
            }
        }

        void validateCreateRequest(const CreateOfficeDocumentRequest& request)
        {
            if (!request.path.is_absolute())
            {
                throw std::invalid_argument{ "Office document creation requires an absolute destination path." };
            }
            if (!extensionMatchesKind(request.path, request.kind))
            {
                throw std::invalid_argument{
                    "Office document kind '" + kindName(request.kind)
                    + "' does not match destination extension: "
                    + request.path.extension().string()
                };
            }
            validateTextBound(request.content, "Office initial content");
            if (request.sheetName.empty() || request.sheetName.size() > maximumSheetNameBytes)
            {
                throw std::invalid_argument{ "Excel sheet name must be between 1 and 128 UTF-8 bytes." };
            }
            if (std::filesystem::exists(request.path))
            {
                throw std::runtime_error{
                    "Rose will not overwrite an existing Office document: "
                    + request.path.string()
                };
            }
            const auto parent = request.path.parent_path();
            if (parent.empty() || !std::filesystem::is_directory(parent))
            {
                throw std::runtime_error{
                    "Office document destination parent does not exist: "
                    + parent.string()
                };
            }
        }

        void validateEditRequest(const EditOfficeDocumentRequest& request)
        {
            if (!request.path.is_absolute())
            {
                throw std::invalid_argument{ "Office document editing requires an absolute file path." };
            }
            if (!std::filesystem::is_regular_file(request.path))
            {
                throw std::runtime_error{
                    "Office document to edit does not exist: " + request.path.string()
                };
            }

            const std::string extension = extensionLower(request.path);
            switch (request.kind)
            {
            case OfficeMutationKind::AppendWordText:
            case OfficeMutationKind::RemoveWordText:
            case OfficeMutationKind::ReplaceWordText:
                if (extension != ".docx")
                {
                    throw std::invalid_argument{ "Word text mutations currently support .docx files only." };
                }
                break;
            case OfficeMutationKind::SetExcelCell:
            case OfficeMutationKind::ClearExcelCell:
                if (extension != ".xlsx")
                {
                    throw std::invalid_argument{ "Excel cell mutations currently support .xlsx files only." };
                }
                if (request.sheetName.empty())
                {
                    throw std::invalid_argument{ "Excel cell mutation requires a sheet name." };
                }
                if (!validExcelCellReference(request.cellReference))
                {
                    throw std::invalid_argument{
                        "Excel cell reference must be an uppercase A1-style address within XFD1048576."
                    };
                }
                break;
            case OfficeMutationKind::AppendPowerPointSlide:
            case OfficeMutationKind::RemovePowerPointSlide:
                if (extension != ".pptx")
                {
                    throw std::invalid_argument{ "PowerPoint slide mutations currently support .pptx files only." };
                }
                break;
            }

            validateTextBound(request.text, "Office edit text");
            validateTextBound(request.findText, "Office find text");
            validateTextBound(request.replacementText, "Office replacement text");

            if (request.kind == OfficeMutationKind::RemoveWordText && request.text.empty())
            {
                throw std::invalid_argument{ "remove_word_text requires non-empty text." };
            }
            if (request.kind == OfficeMutationKind::ReplaceWordText && request.findText.empty())
            {
                throw std::invalid_argument{ "replace_word_text requires non-empty find text." };
            }
            if (request.kind == OfficeMutationKind::RemovePowerPointSlide && request.slideIndex == 0)
            {
                throw std::invalid_argument{ "remove_powerpoint_slide requires a 1-based slide index." };
            }
        }

#ifdef _WIN32
        [[nodiscard]]
        std::wstring quoteWindowsArgument(const std::wstring& argument)
        {
            if (argument.empty()) return L"\"\"";
            bool needsQuotes{ false };
            for (const wchar_t c : argument)
            {
                if (c == L' ' || c == L'\t' || c == L'\"')
                {
                    needsQuotes = true;
                    break;
                }
            }
            if (!needsQuotes) return argument;

            std::wstring result{ L'\"' };
            std::size_t slashes{ 0 };
            for (const wchar_t c : argument)
            {
                if (c == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (c == L'\"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                    result.push_back(L'\"');
                    slashes = 0;
                    continue;
                }
                result.append(slashes, L'\\');
                slashes = 0;
                result.push_back(c);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        class TemporaryDirectory final
        {
        public:
            TemporaryDirectory()
            {
                static std::atomic<std::uint64_t> sequence{ 0 };
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                path_ = std::filesystem::temp_directory_path()
                    / ("Rose-OfficeMutation-" + std::to_string(stamp) + "-"
                       + std::to_string(sequence.fetch_add(1)));
                std::error_code error;
                if (!std::filesystem::create_directories(path_, error) || error)
                {
                    throw std::runtime_error{ "Could not create Rose's temporary Office mutation workspace." };
                }
            }

            ~TemporaryDirectory()
            {
                std::error_code error;
                std::filesystem::remove_all(path_, error);
            }

            [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

        private:
            std::filesystem::path path_;
        };

        [[nodiscard]]
        std::string base64Encode(const std::string_view input)
        {
            static constexpr char alphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string output;
            output.reserve(((input.size() + 2u) / 3u) * 4u);

            std::size_t cursor{ 0 };
            while (cursor + 3u <= input.size())
            {
                const std::uint32_t value =
                    (static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor])) << 16u)
                    | (static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor + 1u])) << 8u)
                    | static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor + 2u]));
                output.push_back(alphabet[(value >> 18u) & 0x3Fu]);
                output.push_back(alphabet[(value >> 12u) & 0x3Fu]);
                output.push_back(alphabet[(value >> 6u) & 0x3Fu]);
                output.push_back(alphabet[value & 0x3Fu]);
                cursor += 3u;
            }

            const std::size_t remaining = input.size() - cursor;
            if (remaining == 1u)
            {
                const std::uint32_t value =
                    static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor])) << 16u;
                output.push_back(alphabet[(value >> 18u) & 0x3Fu]);
                output.push_back(alphabet[(value >> 12u) & 0x3Fu]);
                output += "==";
            }
            else if (remaining == 2u)
            {
                const std::uint32_t value =
                    (static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor])) << 16u)
                    | (static_cast<std::uint32_t>(static_cast<unsigned char>(input[cursor + 1u])) << 8u);
                output.push_back(alphabet[(value >> 18u) & 0x3Fu]);
                output.push_back(alphabet[(value >> 12u) & 0x3Fu]);
                output.push_back(alphabet[(value >> 6u) & 0x3Fu]);
                output.push_back('=');
            }
            return output;
        }

        [[nodiscard]]
        std::string officePowerShellBody()
        {
            // MSVC limits the size of one string literal. Keep Rose's embedded
            // PowerShell helper in compiler-safe chunks and concatenate them here.
            std::string script;
            script.reserve(17299u);
            script += R"PS(
param(
    [Parameter(Mandatory=$true)][string]$Mode,
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [string]$Kind = '',
    [string]$Operation = '',
    [string]$Content64 = '',
    [string]$Text64 = '',
    [string]$Find64 = '',
    [string]$Replacement64 = '',
    [string]$Sheet64 = '',
    [string]$Cell = '',
    [int]$SlideIndex = 0
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Decode-Utf8([string]$s) {
    if ([string]::IsNullOrEmpty($s)) { return '' }
    return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($s))
}
function Xml-Escape([string]$s) {
    return [Security.SecurityElement]::Escape($s)
}
function Read-ZipText([IO.Compression.ZipArchive]$zip,[string]$name) {
    $entry=$zip.GetEntry($name)
    if ($null -eq $entry) { throw "Office package is missing required part: $name" }
    $reader=[IO.StreamReader]::new($entry.Open(), [Text.UTF8Encoding]::new($false), $true)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
function Write-ZipText([IO.Compression.ZipArchive]$zip,[string]$name,[string]$text) {
    $old=$zip.GetEntry($name)
    if ($null -ne $old) { $old.Delete() }
    $entry=$zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
    $writer=[IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
    try { $writer.Write($text) } finally { $writer.Dispose() }
}
function New-EmptyZip([string]$path) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    return [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Update,$false)
}
function Open-ZipUpdate([string]$path) {
    $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    return [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Update,$false)
}
function New-WordPackage([string]$path,[string]$text) {
    $zip=New-EmptyZip $path
    try {
        Write-ZipText $zip '[Content_Types].xml' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>'
        Write-ZipText $zip '_rels/.rels' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>'
        $paragraphs=''
        foreach ($line in ($text -split '\r?\n')) {
            $paragraphs += '<w:p><w:r><w:t xml:space="preserve">'+(Xml-Escape $line)+'</w:t></w:r></w:p>'
        }
        $document='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>'+$paragraphs+'<w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr></w:body></w:document>'
        Write-ZipText $zip 'word/document.xml' $document
    } finally { $zip.Dispose() }
}
function New-ExcelPackage([string]$path,[string]$sheetName,[string]$text) {
    $zip=New-EmptyZip $path
    try {
        Write-ZipText $zip '[Content_Types].xml' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>'
        Write-ZipText $zip '_rels/.rels' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'
        $workbook='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="'+(Xml-Escape $sheetName)+'" sheetId="1" r:id="rId1"/></sheets></workbook>'
        Write-ZipText $zip 'xl/workbook.xml' $workbook
        Write-ZipText $zip 'xl/_rels/workbook.xml.rels' '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>'
        $cell=''
        if (-not [string]::IsNullOrEmpty($text)) { $cell='<row r="1"><c r="A1" t="inlineStr"><is><t xml:space="preserve">'+(Xml-Escape $text)+'</t></is></c></row>' }
        Write-ZipText $zip 'xl/worksheets/sheet1.xml' ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'+$cell+'</sheetData></worksheet>')
    } finally { $zip.Dispose() }
}
function Invoke-PowerPointCreate([string]$path,[string]$text) {
    $app=$null; $presentation=$null
    try {
        $app=New-Object -ComObject PowerPoint.Application
        $presentation=$app.Presentations.Add()
        $slide=$presentation.Slides.Add(1,12)
        if (-not [string]::IsNullOrEmpty($text)) {
            $shape=$slide.Shapes.AddTextbox(1,36,36,648,360)
            $shape.TextFrame.TextRange.Text=$text
        }
        $presentation.SaveAs($path,24)
    } catch {
        throw "PowerPoint creation requires the local Microsoft PowerPoint COM application: $($_.Exception.Message)"
    } finally {
        if ($null -ne $presentation) { try { $presentation.Close() } catch {} ; [Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation) | Out-Null }
        if ($null -ne $app) { try { $app.Quit() } catch {} ; [Runtime.InteropServices.Marshal]::FinalReleaseComObject($app) | Out-Null }
        [GC]::Collect(); [GC]::WaitForPendingFinalizers()
    }
}
function Invoke-PowerPointEdit([string]$path,[string]$operation,[string]$text,[int]$slideIndex) {
    $app=$null; $presentation=$null
    try {
        $app=New-Object -ComObject PowerPoint.Application
        $presentation=$app.Presentations.Open($path,$false,$false,$false)
        if ($operation -eq 'append_powerpoint_slide') {
            $index=$presentation.Slides.Count+1
            $slide=$presentation.Slides.Add($index,12)
            if (-not [string]::IsNullOrEmpty($text)) {
                $shape=$slide.Shapes.AddTextbox(1,36,36,648,360)
                $shape.TextFrame.TextRange.Text=$text
            }
            $script:affected=1
        } elseif ($operation -eq 'remove_powerpoint_slide') {
            if ($slideIndex -lt 1 -or $slideIndex -gt $presentation.Slides.Count) { throw "Slide index $slideIndex is outside 1..$($presentation.Slides.Count)." }
            $presentation.Slides.Item($slideIndex).Delete()
            $script:affected=1
        } else { throw "Unsupported PowerPoint operation: $operation" }
        $presentation.Save()
    } catch {
        throw "PowerPoint slide mutation requires the local Microsoft PowerPoint COM application: $($_.Exception.Message)"
    } finally {
        if ($null -ne $presentation) { try { $presentation.Close() } catch {} ; [Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation) | Out-Null }
        if ($null -ne $app) { try { $app.Quit() } catch {} ; [Runtime.InteropServices.Marshal]::FinalReleaseComObject($app) | Out-Null }
        [GC]::Collect(); [GC]::WaitForPendingFinalizers()
    }
}
)PS";
            script += R"PS(function Resolve-WorksheetPart([IO.Compression.ZipArchive]$zip,[string]$sheetName) {
    [xml]$wb=Read-ZipText $zip 'xl/workbook.xml'
    $ns=New-Object Xml.XmlNamespaceManager($wb.NameTable)
    $ns.AddNamespace('x','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
    $ns.AddNamespace('r','http://schemas.openxmlformats.org/officeDocument/2006/relationships')
    $sheet=$null
    foreach ($candidate in $wb.SelectNodes('//x:sheet',$ns)) { if ($candidate.GetAttribute('name') -eq $sheetName) { $sheet=$candidate; break } }
    if ($null -eq $sheet) { throw "Excel worksheet not found: $sheetName" }
    $rid=$sheet.GetAttribute('id','http://schemas.openxmlformats.org/officeDocument/2006/relationships')
    [xml]$rels=Read-ZipText $zip 'xl/_rels/workbook.xml.rels'
    $rns=New-Object Xml.XmlNamespaceManager($rels.NameTable)
    $rns.AddNamespace('p','http://schemas.openxmlformats.org/package/2006/relationships')
    $rel=$null
    foreach ($candidate in $rels.SelectNodes('//p:Relationship',$rns)) { if ($candidate.GetAttribute('Id') -eq $rid) { $rel=$candidate; break } }
    if ($null -eq $rel) { throw "Excel worksheet relationship not found for $sheetName" }
    $target=$rel.GetAttribute('Target').Replace('\\','/')
    if ($target.StartsWith('/')) { $target=$target.TrimStart('/') }
    elseif (-not $target.StartsWith('xl/')) { $target='xl/'+$target.TrimStart('./') }
    return $target
}
function Cell-ColumnIndex([string]$reference) {
    $v=0
    foreach ($c in $reference.ToCharArray()) {
        if ($c -lt 'A' -or $c -gt 'Z') { break }
        $v=($v*26)+([int][char]$c-[int][char]'A'+1)
    }
    return $v
}
function Cell-RowIndex([string]$reference) {
    $digits=($reference -replace '[A-Z]','')
    return [int]$digits
}

$content=Decode-Utf8 $Content64
$text=Decode-Utf8 $Text64
$find=Decode-Utf8 $Find64
$replacement=Decode-Utf8 $Replacement64
$sheet=Decode-Utf8 $Sheet64
$script:affected=0

if ($Mode -eq 'create') {
    if ($Kind -eq 'word') { New-WordPackage $PackagePath $content }
    elseif ($Kind -eq 'excel') { New-ExcelPackage $PackagePath $sheet $content }
    elseif ($Kind -eq 'powerpoint') { Invoke-PowerPointCreate $PackagePath $content }
    else { throw "Unsupported Office create kind: $Kind" }
    $script:affected=1
}
elseif ($Mode -eq 'edit') {
    if ($Operation -eq 'append_word_text' -or $Operation -eq 'remove_word_text' -or $Operation -eq 'replace_word_text') {
        $zip=Open-ZipUpdate $PackagePath
        try {
            [xml]$doc=Read-ZipText $zip 'word/document.xml'
            $ns=New-Object Xml.XmlNamespaceManager($doc.NameTable)
            $ns.AddNamespace('w','http://schemas.openxmlformats.org/wordprocessingml/2006/main')
            if ($Operation -eq 'append_word_text') {
                $body=$doc.SelectSingleNode('//w:body',$ns)
                if ($null -eq $body) { throw 'Word document has no w:body.' }
                $section=$body.SelectSingleNode('./w:sectPr',$ns)
                foreach ($line in ($text -split '\r?\n')) {
                    $p=$doc.CreateElement('w','p','http://schemas.openxmlformats.org/wordprocessingml/2006/main')
                    $r=$doc.CreateElement('w','r','http://schemas.openxmlformats.org/wordprocessingml/2006/main')
                    $t=$doc.CreateElement('w','t','http://schemas.openxmlformats.org/wordprocessingml/2006/main')
                    $space=$doc.CreateAttribute('xml','space','http://www.w3.org/XML/1998/namespace'); $space.Value='preserve'; $null=$t.Attributes.Append($space)
                    $t.InnerText=$line; $null=$r.AppendChild($t); $null=$p.AppendChild($r)
                    if ($null -ne $section) { $null=$body.InsertBefore($p,$section) } else { $null=$body.AppendChild($p) }
                    $script:affected++
                }
            } else {
                foreach ($node in $doc.SelectNodes('//w:t',$ns)) {
                    $before=$node.InnerText
                    if ($Operation -eq 'remove_word_text') { $after=$before.Replace($text,'') }
                    else { $after=$before.Replace($find,$replacement) }
                    if ($after -ne $before) { $node.InnerText=$after; $script:affected++ }
                }
            }
            Write-ZipText $zip 'word/document.xml' $doc.OuterXml
        } finally { $zip.Dispose() }
    }
    elseif ($Operation -eq 'set_excel_cell' -or $Operation -eq 'clear_excel_cell') {
        $zip=Open-ZipUpdate $PackagePath
        try {
            $part=Resolve-WorksheetPart $zip $sheet
            [xml]$doc=Read-ZipText $zip $part
            $ns=New-Object Xml.XmlNamespaceManager($doc.NameTable)
            $ns.AddNamespace('x','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
            $sheetData=$doc.SelectSingleNode('//x:sheetData',$ns)
            if ($null -eq $sheetData) { throw "Worksheet $sheet has no sheetData element." }
            $rowNumber=Cell-RowIndex $Cell
            $row=$null
            foreach ($candidate in $sheetData.SelectNodes('./x:row',$ns)) { if ([int]$candidate.GetAttribute('r') -eq $rowNumber) { $row=$candidate; break } }
            if ($null -eq $row -and $Operation -eq 'set_excel_cell') {
                $row=$doc.CreateElement('row','http://schemas.openxmlformats.org/spreadsheetml/2006/main'); $row.SetAttribute('r',[string]$rowNumber)
                $inserted=$false
                foreach ($candidate in @($sheetData.SelectNodes('./x:row',$ns))) {
                    if ([int]$candidate.GetAttribute('r') -gt $rowNumber) { $null=$sheetData.InsertBefore($row,$candidate); $inserted=$true; break }
                }
                if (-not $inserted) { $null=$sheetData.AppendChild($row) }
            }
            $cellNode=$null
            if ($null -ne $row) { foreach ($candidate in $row.SelectNodes('./x:c',$ns)) { if ($candidate.GetAttribute('r') -eq $Cell) { $cellNode=$candidate; break } } }
            if ($Operation -eq 'clear_excel_cell') {
                if ($null -ne $cellNode) {
                    while ($cellNode.HasChildNodes) { $null=$cellNode.RemoveChild($cellNode.FirstChild) }
                    $cellNode.RemoveAttribute('t')
                    $script:affected=1
                }
            } else {
                if ($null -eq $cellNode) {
                    $cellNode=$doc.CreateElement('c','http://schemas.openxmlformats.org/spreadsheetml/2006/main'); $cellNode.SetAttribute('r',$Cell)
                    $newCol=Cell-ColumnIndex $Cell; $inserted=$false
                    foreach ($candidate in @($row.SelectNodes('./x:c',$ns))) {
                        if ((Cell-ColumnIndex $candidate.GetAttribute('r')) -gt $newCol) { $null=$row.InsertBefore($cellNode,$candidate); $inserted=$true; break }
                    }
                    if (-not $inserted) { $null=$row.AppendChild($cellNode) }
                }
                while ($cellNode.HasChildNodes) { $null=$cellNode.RemoveChild($cellNode.FirstChild) }
                if ($text.StartsWith('=')) {
                    $cellNode.RemoveAttribute('t')
                    $f=$doc.CreateElement('f','http://schemas.openxmlformats.org/spreadsheetml/2006/main'); $f.InnerText=$text.Substring(1); $null=$cellNode.AppendChild($f)
                } else {
                    $number=0.0
                    if ([double]::TryParse($text,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$number)) {
                        $cellNode.RemoveAttribute('t')
                        $v=$doc.CreateElement('v','http://schemas.openxmlformats.org/spreadsheetml/2006/main'); $v.InnerText=$number.ToString([Globalization.CultureInfo]::InvariantCulture); $null=$cellNode.AppendChild($v)
                    } else {
                        $cellNode.SetAttribute('t','inlineStr')
                        $is=$doc.CreateElement('is','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
                        $t=$doc.CreateElement('t','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
                        $space=$doc.CreateAttribute('xml','space','http://www.w3.org/XML/1998/namespace'); $space.Value='preserve'; $null=$t.Attributes.Append($space)
                        $t.InnerText=$text; $null=$is.AppendChild($t); $null=$cellNode.AppendChild($is)
                    }
                }
                $script:affected=1
            }
            Write-ZipText $zip $part $doc.OuterXml
        } finally { $zip.Dispose() }
    }
    elseif ($Operation -eq 'append_powerpoint_slide' -or $Operation -eq 'remove_powerpoint_slide') {
        Invoke-PowerPointEdit $PackagePath $Operation $text $SlideIndex
    }
    else { throw "Unsupported Office edit operation: $Operation" }
}
else { throw "Unsupported Office mutation mode: $Mode" }

Write-Output ('affected_count='+$script:affected)
)PS";
            return script;
        }

        [[nodiscard]]
        std::filesystem::path writeScript(const TemporaryDirectory& temporary)
        {
            const auto path = temporary.path() / "office-mutate.ps1";
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error{ "Could not write Rose's temporary Office mutation helper." };
            }
            output << officePowerShellBody();
            return path;
        }

        struct PowerShellResult
        {
            DWORD exitCode{};
            std::string output;
        };

        [[nodiscard]]
        PowerShellResult runPowerShell(
            const std::filesystem::path& script,
            const std::vector<std::wstring>& arguments,
            const DWORD timeoutMilliseconds)
        {
            std::wstring command = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ";
            command += quoteWindowsArgument(script.wstring());
            for (const auto& argument : arguments)
            {
                command.push_back(L' ');
                command += quoteWindowsArgument(argument);
            }

            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.bInheritHandle = TRUE;
            HANDLE readPipe{ nullptr };
            HANDLE writePipe{ nullptr };
            if (!CreatePipe(&readPipe, &writePipe, &attributes, 0))
            {
                throw std::runtime_error{ "Could not create Office mutation output pipe." };
            }
            (void)SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = writePipe;
            startup.hStdError = writePipe;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION process{};
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');

            if (!CreateProcessW(
                    nullptr,
                    mutableCommand.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    nullptr,
                    &startup,
                    &process))
            {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                throw std::runtime_error{ "Could not launch Windows PowerShell for Office mutation." };
            }
            CloseHandle(writePipe);
            CloseHandle(process.hThread);

            const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
            if (wait != WAIT_OBJECT_0)
            {
                (void)TerminateProcess(process.hProcess, 1);
            }

            std::string captured;
            char buffer[4096];
            DWORD bytesRead{ 0 };
            while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                captured.append(buffer, buffer + bytesRead);
                if (captured.size() > 64u * 1024u) break;
            }
            CloseHandle(readPipe);

            DWORD exitCode{ 1 };
            if (wait == WAIT_OBJECT_0)
            {
                (void)GetExitCodeProcess(process.hProcess, &exitCode);
            }
            CloseHandle(process.hProcess);
            return PowerShellResult{ .exitCode = exitCode, .output = std::move(captured) };
        }

        [[nodiscard]]
        std::wstring wideUtf8Base64(const std::string_view value)
        {
            const std::string encoded = base64Encode(value);
            return std::wstring(encoded.begin(), encoded.end());
        }

        [[nodiscard]]
        std::size_t parseAffectedCount(const std::string_view output)
        {
            const std::string_view marker{ "affected_count=" };
            const std::size_t begin = output.rfind(marker);
            if (begin == std::string_view::npos) return 0;
            std::size_t cursor = begin + marker.size();
            std::size_t value{ 0 };
            bool any{ false };
            while (cursor < output.size() && output[cursor] >= '0' && output[cursor] <= '9')
            {
                any = true;
                value = value * 10u + static_cast<std::size_t>(output[cursor] - '0');
                ++cursor;
            }
            return any ? value : 0;
        }

        [[nodiscard]]
        std::filesystem::path siblingTemporaryPath(
            const std::filesystem::path& finalPath,
            const std::string_view label)
        {
            static std::atomic<std::uint64_t> sequence{ 0 };
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            return finalPath.parent_path()
                / (finalPath.stem().string() + ".rose-" + std::string{ label } + "-"
                   + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1))
                   + finalPath.extension().string());
        }

        class RemoveOnExit final
        {
        public:
            explicit RemoveOnExit(std::filesystem::path path)
                : path_{ std::move(path) }
            {
            }
            ~RemoveOnExit()
            {
                if (!active_) return;
                std::error_code error;
                std::filesystem::remove(path_, error);
            }
            void release() noexcept { active_ = false; }
        private:
            std::filesystem::path path_;
            bool active_{ true };
        };

        void replaceFileTransactionally(
            const std::filesystem::path& original,
            const std::filesystem::path& edited)
        {
            const auto backup = siblingTemporaryPath(original, "backup");
            RemoveOnExit backupCleanup{ backup };

            std::error_code error;
            std::filesystem::rename(original, backup, error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not stage original Office document for replacement: " + error.message()
                };
            }

            error.clear();
            std::filesystem::rename(edited, original, error);
            if (error)
            {
                std::error_code rollbackError;
                std::filesystem::rename(backup, original, rollbackError);
                throw std::runtime_error{
                    "Could not replace Office document after mutation: " + error.message()
                };
            }

            error.clear();
            std::filesystem::remove(backup, error);
            backupCleanup.release();
        }
#endif
    }

    OfficeMutationResult LocalOfficeDocumentMutationService::create(
        const CreateOfficeDocumentRequest& request)
    {
        validateCreateRequest(request);

#ifdef _WIN32
        TemporaryDirectory temporary;
        const auto script = writeScript(temporary);
        const auto staged = siblingTemporaryPath(request.path, "new");
        RemoveOnExit stagedCleanup{ staged };

        const std::string kind = kindName(request.kind);
        const std::wstring wideKind(kind.begin(), kind.end());
        const auto result = runPowerShell(
            script,
            {
                L"-Mode", L"create",
                L"-PackagePath", staged.wstring(),
                L"-Kind", wideKind,
                L"-Content64", wideUtf8Base64(request.content),
                L"-Sheet64", wideUtf8Base64(request.sheetName)
            },
            120000u);

        if (result.exitCode != 0 || !std::filesystem::is_regular_file(staged))
        {
            throw std::runtime_error{
                "Office document creation failed. " + result.output
            };
        }

        std::error_code error;
        std::filesystem::rename(staged, request.path, error);
        if (error)
        {
            throw std::runtime_error{
                "Could not move completed Office document into destination: " + error.message()
            };
        }
        stagedCleanup.release();

        return OfficeMutationResult{
            .operation = "create_" + kindName(request.kind),
            .detail = request.path.lexically_normal().string(),
            .affectedCount = 1
        };
#else
        (void)request;
        throw std::runtime_error{ "Office document mutation is currently implemented for Windows only." };
#endif
    }

    OfficeMutationResult LocalOfficeDocumentMutationService::edit(
        const EditOfficeDocumentRequest& request)
    {
        validateEditRequest(request);

#ifdef _WIN32
        TemporaryDirectory temporary;
        const auto script = writeScript(temporary);
        const auto staged = siblingTemporaryPath(request.path, "edit");
        RemoveOnExit stagedCleanup{ staged };

        std::error_code copyError;
        std::filesystem::copy_file(
            request.path,
            staged,
            std::filesystem::copy_options::none,
            copyError);
        if (copyError)
        {
            throw std::runtime_error{
                "Could not create Rose's private Office edit copy: " + copyError.message()
            };
        }

        std::string operation;
        switch (request.kind)
        {
        case OfficeMutationKind::AppendWordText: operation = "append_word_text"; break;
        case OfficeMutationKind::RemoveWordText: operation = "remove_word_text"; break;
        case OfficeMutationKind::ReplaceWordText: operation = "replace_word_text"; break;
        case OfficeMutationKind::SetExcelCell: operation = "set_excel_cell"; break;
        case OfficeMutationKind::ClearExcelCell: operation = "clear_excel_cell"; break;
        case OfficeMutationKind::AppendPowerPointSlide: operation = "append_powerpoint_slide"; break;
        case OfficeMutationKind::RemovePowerPointSlide: operation = "remove_powerpoint_slide"; break;
        }

        const auto result = runPowerShell(
            script,
            {
                L"-Mode", L"edit",
                L"-PackagePath", staged.wstring(),
                L"-Operation", std::wstring(operation.begin(), operation.end()),
                L"-Text64", wideUtf8Base64(request.text),
                L"-Find64", wideUtf8Base64(request.findText),
                L"-Replacement64", wideUtf8Base64(request.replacementText),
                L"-Sheet64", wideUtf8Base64(request.sheetName),
                L"-Cell", std::wstring(request.cellReference.begin(), request.cellReference.end()),
                L"-SlideIndex", std::to_wstring(request.slideIndex)
            },
            120000u);

        if (result.exitCode != 0)
        {
            throw std::runtime_error{ "Office document edit failed. " + result.output };
        }

        replaceFileTransactionally(request.path, staged);
        stagedCleanup.release();

        return OfficeMutationResult{
            .operation = operation,
            .detail = request.path.lexically_normal().string(),
            .affectedCount = parseAffectedCount(result.output)
        };
#else
        (void)request;
        throw std::runtime_error{ "Office document mutation is currently implemented for Windows only." };
#endif
    }
}
