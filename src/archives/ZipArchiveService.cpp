#include "archives/ZipArchiveService.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace rose::archives
{
    namespace
    {
        [[nodiscard]]
        bool asciiLetter(const char value) noexcept
        {
            return (value >= 'A' && value <= 'Z')
                || (value >= 'a' && value <= 'z');
        }

        [[nodiscard]]
        std::string lowerAscii(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        [[nodiscard]]
        bool isZipPath(const std::filesystem::path& path)
        {
            return lowerAscii(path.extension().string()) == ".zip";
        }

        void requireAbsoluteZipPath(
            const std::filesystem::path& path,
            const std::string_view operation)
        {
            if (!path.is_absolute())
            {
                throw std::invalid_argument{
                    std::string{ operation }
                    + " requires an absolute .zip path."
                };
            }
            if (!isZipPath(path))
            {
                throw std::invalid_argument{
                    std::string{ operation }
                    + " currently supports .zip archives only: "
                    + path.string()
                };
            }
        }

        [[nodiscard]]
        bool isFilesystemRoot(const std::filesystem::path& path)
        {
            const std::filesystem::path normalized = path.lexically_normal();
            return !normalized.root_path().empty()
                && normalized == normalized.root_path();
        }

#ifdef _WIN32
        [[nodiscard]]
        std::wstring quoteWindowsArgument(const std::wstring& argument)
        {
            // CreateProcess receives a single command line. This implements the
            // standard Windows quoting rules for one argument so paths containing
            // spaces/quotes cannot alter the generated PowerShell invocation.
            if (argument.empty()) return L"\"\"";

            bool needsQuotes{ false };
            for (const wchar_t character : argument)
            {
                if (character == L' ' || character == L'\t' || character == L'\"')
                {
                    needsQuotes = true;
                    break;
                }
            }
            if (!needsQuotes) return argument;

            std::wstring result{ L'\"' };
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

        class TemporaryDirectory final
        {
        public:
            explicit TemporaryDirectory(const std::string_view purpose)
            {
                static std::atomic<std::uint64_t> sequence{ 0 };
                const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                root_ = std::filesystem::temp_directory_path()
                    / ("Rose-" + std::string{ purpose } + "-"
                       + std::to_string(now) + "-"
                       + std::to_string(sequence.fetch_add(1)));
                std::error_code error;
                if (!std::filesystem::create_directories(root_, error) || error)
                {
                    throw std::runtime_error{
                        "Could not create Rose's temporary ZIP workspace: "
                        + root_.string()
                    };
                }
            }

            ~TemporaryDirectory()
            {
                std::error_code error;
                std::filesystem::remove_all(root_, error);
            }

            [[nodiscard]]
            const std::filesystem::path& path() const noexcept
            {
                return root_;
            }

        private:
            std::filesystem::path root_;
        };

        void runPowerShellScript(
            const std::filesystem::path& script,
            const std::vector<std::filesystem::path>& pathArguments,
            const std::vector<std::string>& scalarArguments,
            const DWORD timeoutMilliseconds)
        {
            std::wstring command =
                L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ";
            command += quoteWindowsArgument(script.wstring());

            for (const auto& argument : pathArguments)
            {
                command.push_back(L' ');
                command += quoteWindowsArgument(argument.wstring());
            }
            for (const auto& argument : scalarArguments)
            {
                const std::wstring wide(argument.begin(), argument.end());
                command.push_back(L' ');
                command += quoteWindowsArgument(wide);
            }

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
                throw std::runtime_error{
                    "Could not launch Windows PowerShell for ZIP operation."
                };
            }

            CloseHandle(process.hThread);
            const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
            DWORD exitCode{ 1 };
            if (wait == WAIT_OBJECT_0)
            {
                (void)GetExitCodeProcess(process.hProcess, &exitCode);
            }
            else
            {
                (void)TerminateProcess(process.hProcess, 1);
            }
            CloseHandle(process.hProcess);

            if (wait != WAIT_OBJECT_0 || exitCode != 0)
            {
                throw std::runtime_error{
                    "Windows PowerShell ZIP operation failed."
                };
            }
        }

        void writeUtf8File(
            const std::filesystem::path& path,
            const std::string_view text)
        {
            std::ofstream output{ path, std::ios::binary | std::ios::trunc };
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!output)
            {
                throw std::runtime_error{
                    "Could not create Rose ZIP helper file: " + path.string()
                };
            }
        }

        [[nodiscard]]
        std::uint64_t parseUint64(
            const std::string_view value,
            const std::string_view field)
        {
            std::uint64_t parsed{ 0 };
            const auto result = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            {
                throw std::runtime_error{
                    "Could not parse ZIP " + std::string{ field } + "."
                };
            }
            return parsed;
        }

        [[nodiscard]]
        std::vector<std::string_view> splitTabs(const std::string_view line)
        {
            std::vector<std::string_view> fields;
            std::size_t start{ 0 };
            while (start <= line.size())
            {
                const std::size_t end = line.find('\t', start);
                fields.push_back(
                    line.substr(start, end == std::string_view::npos
                        ? std::string_view::npos
                        : end - start));
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            return fields;
        }

        [[nodiscard]]
        std::string sanitizedManifestPath(std::string value)
        {
            for (char& character : value)
            {
                if (character == '\t' || character == '\r' || character == '\n')
                {
                    character = ' ';
                }
            }
            return value;
        }

        struct CreateManifestEntry
        {
            char type{ 'F' };
            std::filesystem::path source;
            std::string archivePath;
        };

        [[nodiscard]]
        std::vector<CreateManifestEntry> collectCreateManifest(
            const std::filesystem::path& source,
            const ZipArchiveServiceConfig& config)
        {
            std::error_code error;
            const bool sourceIsDirectory = std::filesystem::is_directory(source, error);
            if (error)
            {
                throw std::runtime_error{
                    "Could not inspect ZIP source: " + source.string()
                };
            }

            std::vector<CreateManifestEntry> entries;
            std::uint64_t totalBytes{ 0 };

            const auto appendFile = [&](const std::filesystem::path& file,
                                        const std::string& archivePath)
            {
                std::error_code sizeError;
                const std::uintmax_t size = std::filesystem::file_size(file, sizeError);
                if (sizeError)
                {
                    throw std::runtime_error{
                        "Could not inspect source file while creating ZIP: "
                        + file.string()
                    };
                }
                if (size > config.maximumEntryBytes)
                {
                    throw std::runtime_error{
                        "ZIP creation source file exceeds Rose's per-entry limit: "
                        + file.string()
                    };
                }
                if (size > (std::numeric_limits<std::uint64_t>::max)() - totalBytes
                    || totalBytes + static_cast<std::uint64_t>(size) > config.maximumTotalBytes)
                {
                    throw std::runtime_error{
                        "ZIP creation source exceeds Rose's total-size limit."
                    };
                }
                totalBytes += static_cast<std::uint64_t>(size);
                entries.push_back({ 'F', file, archivePath });
            };

            if (!sourceIsDirectory)
            {
                if (!std::filesystem::is_regular_file(source, error) || error)
                {
                    throw std::runtime_error{
                        "ZIP creation source must be a regular file or directory: "
                        + source.string()
                    };
                }
                appendFile(source, source.filename().generic_string());
                return entries;
            }

            std::filesystem::recursive_directory_iterator iterator{
                source,
                std::filesystem::directory_options::skip_permission_denied,
                error
            };
            if (error)
            {
                throw std::runtime_error{
                    "Could not enumerate ZIP source directory: " + source.string()
                };
            }
            const std::filesystem::recursive_directory_iterator end;

            for (; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    throw std::runtime_error{
                        "Could not continue enumerating ZIP source directory."
                    };
                }

                if (entries.size() >= config.maximumArchiveEntries)
                {
                    throw std::runtime_error{
                        "ZIP creation source exceeds Rose's entry-count limit."
                    };
                }

                const std::filesystem::directory_entry& entry = *iterator;
                const std::filesystem::file_status status = entry.symlink_status(error);
                if (error)
                {
                    throw std::runtime_error{
                        "Could not inspect ZIP source entry: " + entry.path().string()
                    };
                }

                if (std::filesystem::is_symlink(status))
                {
                    if (entry.is_directory(error) && !error)
                    {
                        iterator.disable_recursion_pending();
                    }
                    continue;
                }

                std::filesystem::path relative = std::filesystem::relative(
                    entry.path(), source, error);
                if (error || relative.empty())
                {
                    throw std::runtime_error{
                        "Could not build ZIP source-relative path: "
                        + entry.path().string()
                    };
                }
                std::string archivePath = relative.generic_string();
                if (!isSafeZipEntryPath(archivePath))
                {
                    throw std::runtime_error{
                        "ZIP source produced an unsafe relative path: " + archivePath
                    };
                }

                if (std::filesystem::is_directory(status))
                {
                    entries.push_back({ 'D', {}, archivePath + "/" });
                }
                else if (std::filesystem::is_regular_file(status))
                {
                    appendFile(entry.path(), archivePath);
                }
            }

            return entries;
        }
#endif
    } // namespace


    bool isSafeZipEntryPath(
        const std::string_view entryPath) noexcept
    {
        if (entryPath.empty()) return false;
        if (entryPath.front() == '/' || entryPath.front() == '\\') return false;
        if (entryPath.size() >= 2 && asciiLetter(entryPath[0]) && entryPath[1] == ':')
        {
            return false;
        }

        std::size_t start{ 0 };
        while (start < entryPath.size())
        {
            const std::size_t end = entryPath.find_first_of("/\\", start);
            const std::string_view part = entryPath.substr(
                start,
                end == std::string_view::npos
                    ? std::string_view::npos
                    : end - start);

            if (part == "..") return false;
            if (part.find(':') != std::string_view::npos) return false;

            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return true;
    }


    WindowsZipArchiveService::WindowsZipArchiveService(
        const ZipArchiveServiceConfig config)
        : config_{ config }
    {
        if (config_.maximumListedEntries == 0
            || config_.maximumArchiveEntries == 0
            || config_.maximumEntryBytes == 0
            || config_.maximumTotalBytes == 0)
        {
            throw std::invalid_argument{
                "WindowsZipArchiveService limits must be greater than zero."
            };
        }
    }


    const ZipArchiveServiceConfig& WindowsZipArchiveService::config() const noexcept
    {
        return config_;
    }


    ZipArchiveListing WindowsZipArchiveService::list(
        const std::filesystem::path& archivePath) const
    {
        requireAbsoluteZipPath(archivePath, "ZIP listing");
        std::error_code error;
        if (!std::filesystem::is_regular_file(archivePath, error) || error)
        {
            throw std::runtime_error{
                "ZIP archive does not exist or is not a readable file: "
                + archivePath.string()
            };
        }

#ifdef _WIN32
        TemporaryDirectory temporary{ "zip-list" };
        const std::filesystem::path script = temporary.path() / "list.ps1";
        const std::filesystem::path output = temporary.path() / "listing.tsv";

        std::ostringstream ps;
        ps
            << "$ErrorActionPreference='Stop'\n"
            << "Add-Type -AssemblyName System.IO.Compression\n"
            << "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
            << "$archive=$args[0]; $out=$args[1]; $max=[int]$args[2]; $maxScan=[int]$args[3]\n"
            << "$zip=[IO.Compression.ZipFile]::OpenRead($archive)\n"
            << "try {\n"
            << "  $lines=New-Object 'System.Collections.Generic.List[string]'\n"
            << "  $count=0; [UInt64]$total=0; $unsafe=$false\n"
            << "  $root=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'RoseZipSafetyRoot'))\n"
            << "  $prefix=$root.TrimEnd('\\')+'\\'\n"
            << "  foreach($e in $zip.Entries){\n"
            << "    $count++; if($count -gt $maxScan){ throw 'Archive exceeds Rose entry-count limit.' }; [UInt64]$total += [UInt64]$e.Length\n"
            << "    $name=$e.FullName; $normalized=$name.Replace('/','\\')\n"
            << "    $safe=$true\n"
            << "    if([IO.Path]::IsPathRooted($normalized) -or $normalized.Contains(':')){ $safe=$false }\n"
            << "    else { try { $dest=[IO.Path]::GetFullPath((Join-Path $root $normalized)); if(-not $dest.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){ $safe=$false } } catch { $safe=$false } }\n"
            << "    if(-not $safe){ $unsafe=$true }\n"
            << "    if($lines.Count -lt $max){\n"
            << "      $display=$name.Replace(\"`t\",' ').Replace(\"`r\",' ').Replace(\"`n\",' '); if($display.Length -gt 4096){ $display=$display.Substring(0,4096) + '...[name truncated]' }\n"
            << "      $isDir=if($name.EndsWith('/')){1}else{0}\n"
            << "      $safeInt=if($safe){1}else{0}\n"
            << "      $lines.Add(('ENTRY' + \"`t\" + $isDir + \"`t\" + $safeInt + \"`t\" + $e.Length + \"`t\" + $e.CompressedLength + \"`t\" + $display))\n"
            << "    }\n"
            << "  }\n"
            << "  $unsafeInt=if($unsafe){1}else{0}\n"
            << "  $all=New-Object 'System.Collections.Generic.List[string]'\n"
            << "  $all.Add(('SUMMARY' + \"`t\" + $count + \"`t\" + $total + \"`t\" + $unsafeInt))\n"
            << "  foreach($line in $lines){ $all.Add($line) }\n"
            << "  [IO.File]::WriteAllLines($out,$all,[Text.UTF8Encoding]::new($false))\n"
            << "} finally { $zip.Dispose() }\n";
        writeUtf8File(script, ps.str());

        runPowerShellScript(
            script,
            { archivePath, output },
            {
                std::to_string(config_.maximumListedEntries),
                std::to_string(config_.maximumArchiveEntries)
            },
            120000);

        std::ifstream input{ output, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "ZIP listing did not produce output." };
        }

        ZipArchiveListing listing;
        std::string line;
        bool sawSummary{ false };
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto fields = splitTabs(line);
            if (fields.empty()) continue;

            if (fields[0] == "SUMMARY")
            {
                if (fields.size() != 4)
                {
                    throw std::runtime_error{ "ZIP listing summary was malformed." };
                }
                listing.totalEntryCount = static_cast<std::size_t>(
                    parseUint64(fields[1], "entry count"));
                listing.totalUncompressedBytes = parseUint64(fields[2], "total bytes");
                listing.containsUnsafePaths = fields[3] == "1";
                listing.truncated = listing.totalEntryCount > config_.maximumListedEntries;
                sawSummary = true;
                continue;
            }

            if (fields[0] == "ENTRY")
            {
                if (fields.size() < 6) continue;
                listing.entries.push_back(
                    ZipArchiveEntry{
                        .path = std::string{ fields[5] },
                        .uncompressedBytes = parseUint64(fields[3], "entry bytes"),
                        .compressedBytes = parseUint64(fields[4], "compressed bytes"),
                        .directory = fields[1] == "1",
                        .safeRelativePath = fields[2] == "1"
                    });
            }
        }

        if (!sawSummary)
        {
            throw std::runtime_error{ "ZIP listing output was incomplete." };
        }
        return listing;
#else
        throw std::runtime_error{
            "ZIP archive operations are currently implemented for Rose's Windows target only."
        };
#endif
    }


    void WindowsZipArchiveService::extract(
        const std::filesystem::path& archivePath,
        const std::filesystem::path& destinationDirectory) const
    {
        requireAbsoluteZipPath(archivePath, "ZIP extraction");
        if (!destinationDirectory.is_absolute())
        {
            throw std::invalid_argument{
                "ZIP extraction requires an absolute destination directory."
            };
        }

        std::error_code error;
        if (!std::filesystem::is_regular_file(archivePath, error) || error)
        {
            throw std::runtime_error{
                "ZIP archive does not exist or is not readable: " + archivePath.string()
            };
        }
        error.clear();
        if (std::filesystem::exists(destinationDirectory, error))
        {
            throw std::runtime_error{
                "ZIP extraction will not overwrite an existing destination: "
                + destinationDirectory.string()
            };
        }
        if (error)
        {
            throw std::system_error{ error, "Could not inspect ZIP extraction destination" };
        }
        const std::filesystem::path parent = destinationDirectory.parent_path();
        error.clear();
        if (parent.empty()
            || !std::filesystem::is_directory(parent, error)
            || error)
        {
            throw std::runtime_error{
                "ZIP extraction requires an existing destination parent directory: "
                + parent.string()
            };
        }

#ifdef _WIN32
        TemporaryDirectory temporary{ "zip-extract" };
        const std::filesystem::path script = temporary.path() / "extract.ps1";
        std::ostringstream ps;
        ps
            << "$ErrorActionPreference='Stop'\n"
            << "Add-Type -AssemblyName System.IO.Compression\n"
            << "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
            << "$archive=$args[0]; $dest=$args[1]; $maxEntries=[int]$args[2]; [UInt64]$maxEntry=$args[3]; [UInt64]$maxTotal=$args[4]\n"
            << "if([IO.Directory]::Exists($dest) -or [IO.File]::Exists($dest)){ throw 'Destination already exists.' }\n"
            << "$root=[IO.Path]::GetFullPath($dest); $prefix=$root.TrimEnd('\\')+'\\'\n"
            << "$zip=[IO.Compression.ZipFile]::OpenRead($archive)\n"
            << "try {\n"
            << "  $seen=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)\n"
            << "  $planned=[System.Collections.Generic.List[object]]::new()\n"
            << "  $count=0; [UInt64]$total=0\n"
            << "  foreach($e in $zip.Entries){\n"
            << "    $count++; if($count -gt $maxEntries){ throw 'Archive exceeds Rose entry-count limit.' }\n"
            << "    if([UInt64]$e.Length -gt $maxEntry){ throw 'Archive entry exceeds Rose per-entry size limit.' }\n"
            << "    [UInt64]$total += [UInt64]$e.Length; if($total -gt $maxTotal){ throw 'Archive exceeds Rose total extraction-size limit.' }\n"
            << "    $name=$e.FullName; $normalized=$name.Replace('/','\\')\n"
            << "    if([string]::IsNullOrWhiteSpace($normalized) -or [IO.Path]::IsPathRooted($normalized) -or $normalized.Contains(':')){ throw 'Unsafe ZIP entry path.' }\n"
            << "    $full=[IO.Path]::GetFullPath((Join-Path $root $normalized))\n"
            << "    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){ throw 'ZIP entry escapes extraction root.' }\n"
            << "    if(-not $seen.Add($full)){ throw 'ZIP contains duplicate destination paths.' }\n"
            << "    $planned.Add([PSCustomObject]@{ Entry=$e; Path=$full; IsDir=$name.EndsWith('/') })\n"
            << "  }\n"
            << "  [IO.Directory]::CreateDirectory($root) | Out-Null\n"
            << "  try {\n"
            << "    foreach($p in $planned){\n"
            << "      if($p.IsDir){ [IO.Directory]::CreateDirectory($p.Path) | Out-Null; continue }\n"
            << "      $parent=[IO.Path]::GetDirectoryName($p.Path); if($parent){ [IO.Directory]::CreateDirectory($parent) | Out-Null }\n"
            << "      $input=$p.Entry.Open(); try { $output=New-Object IO.FileStream($p.Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None); try { $input.CopyTo($output) } finally { $output.Dispose() } } finally { $input.Dispose() }\n"
            << "    }\n"
            << "  } catch { if([IO.Directory]::Exists($root)){ [IO.Directory]::Delete($root,$true) }; throw }\n"
            << "} finally { $zip.Dispose() }\n";
        writeUtf8File(script, ps.str());
        runPowerShellScript(
            script,
            { archivePath, destinationDirectory },
            {
                std::to_string(config_.maximumArchiveEntries),
                std::to_string(config_.maximumEntryBytes),
                std::to_string(config_.maximumTotalBytes)
            },
            300000);
#else
        throw std::runtime_error{
            "ZIP archive operations are currently implemented for Rose's Windows target only."
        };
#endif
    }


    void WindowsZipArchiveService::create(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destinationArchive) const
    {
        if (!sourcePath.is_absolute())
        {
            throw std::invalid_argument{ "ZIP creation requires an absolute source path." };
        }
        if (isFilesystemRoot(sourcePath))
        {
            throw std::invalid_argument{
                "ZIP creation will not archive an entire filesystem root."
            };
        }
        requireAbsoluteZipPath(destinationArchive, "ZIP creation");

        std::error_code error;
        if (!std::filesystem::exists(sourcePath, error) || error)
        {
            throw std::runtime_error{
                "ZIP creation source does not exist or cannot be inspected: "
                + sourcePath.string()
            };
        }
        error.clear();
        if (std::filesystem::exists(destinationArchive, error))
        {
            throw std::runtime_error{
                "ZIP creation will not overwrite an existing destination: "
                + destinationArchive.string()
            };
        }
        if (error)
        {
            throw std::system_error{ error, "Could not inspect ZIP destination" };
        }
        const std::filesystem::path destinationParent = destinationArchive.parent_path();
        error.clear();
        if (destinationParent.empty()
            || !std::filesystem::is_directory(destinationParent, error)
            || error)
        {
            throw std::runtime_error{
                "ZIP creation requires an existing destination parent directory: "
                + destinationParent.string()
            };
        }

#ifdef _WIN32
        const std::filesystem::path canonicalSource =
            std::filesystem::weakly_canonical(sourcePath, error);
        if (error)
        {
            throw std::runtime_error{ "Could not resolve ZIP source path." };
        }
        const auto manifestEntries = collectCreateManifest(canonicalSource, config_);
        TemporaryDirectory temporary{ "zip-create" };
        const std::filesystem::path manifest = temporary.path() / "manifest.tsv";
        const std::filesystem::path script = temporary.path() / "create.ps1";

        std::ostringstream manifestText;
        for (const auto& entry : manifestEntries)
        {
            manifestText << entry.type << '\t';
            if (entry.type == 'F') manifestText << sanitizedManifestPath(entry.source.string());
            manifestText << '\t' << sanitizedManifestPath(entry.archivePath) << '\n';
        }
        writeUtf8File(manifest, manifestText.str());

        std::ostringstream ps;
        ps
            << "$ErrorActionPreference='Stop'\n"
            << "Add-Type -AssemblyName System.IO.Compression\n"
            << "Add-Type -AssemblyName System.IO.Compression.FileSystem\n"
            << "$manifest=$args[0]; $dest=$args[1]\n"
            << "if([IO.File]::Exists($dest) -or [IO.Directory]::Exists($dest)){ throw 'ZIP destination already exists.' }\n"
            << "$zip=[IO.Compression.ZipFile]::Open($dest,[IO.Compression.ZipArchiveMode]::Create)\n"
            << "try {\n"
            << "  foreach($line in [IO.File]::ReadAllLines($manifest,[Text.Encoding]::UTF8)){\n"
            << "    if([string]::IsNullOrWhiteSpace($line)){ continue }\n"
            << "    $parts=$line.Split([char]9); if($parts.Length -lt 3){ throw 'Malformed Rose ZIP creation manifest.' }\n"
            << "    $type=$parts[0]; $source=$parts[1]; $name=$parts[2].Replace('\\','/')\n"
            << "    if($type -eq 'D'){ if(-not $name.EndsWith('/')){ $name += '/' }; $null=$zip.CreateEntry($name); continue }\n"
            << "    if($type -ne 'F'){ throw 'Unknown Rose ZIP creation manifest entry.' }\n"
            << "    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$source,$name,[IO.Compression.CompressionLevel]::Optimal) | Out-Null\n"
            << "  }\n"
            << "} catch { $zip.Dispose(); if([IO.File]::Exists($dest)){ [IO.File]::Delete($dest) }; throw } finally { if($zip){ $zip.Dispose() } }\n";
        writeUtf8File(script, ps.str());

        runPowerShellScript(
            script,
            { manifest, destinationArchive },
            {},
            300000);
#else
        throw std::runtime_error{
            "ZIP archive operations are currently implemented for Rose's Windows target only."
        };
#endif
    }
}
