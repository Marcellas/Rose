#include "tools/ReadNamedPdfsTool.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rose::tools
{
    namespace
    {
        [[nodiscard]] std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        [[nodiscard]] bool absoluteWindowsPath(const std::string& path)
        {
            return (path.size() >= 3 && std::isalpha(
                static_cast<unsigned char>(path[0])) && path[1] == ':'
                && (path[2] == '\\' || path[2] == '/'))
                || path.starts_with("\\\\");
        }

        [[nodiscard]] bool namedPdfArgument(const std::string_view name)
        {
            if (name == "instruction") return true;

            for (int i = 1; i <= 4; ++i)
            {
                const std::string prefix = "path" + std::to_string(i);
                if (name == prefix
                    || name == prefix + "_page_start"
                    || name == prefix + "_page_count")
                {
                    return true;
                }
            }
            return false;
        }


        [[nodiscard]] std::vector<std::string> namedPaths(const ToolRequest& request)
        {
            for (const auto& [name, value] : request.arguments)
            {
                (void)value;
                if (!namedPdfArgument(name))
                    throw std::invalid_argument{ "read_named_pdfs rejects argument '" + name + "'." };
            }

            std::vector<std::string> paths;
            for (int i = 1; i <= 4; ++i)
            {
                const auto found = request.arguments.find("path" + std::to_string(i));
                if (found == request.arguments.end())
                {
                    if (i <= 2) throw std::invalid_argument{
                        "read_named_pdfs requires at least two exact paths." };
                    for (int later = i + 1; later <= 4; ++later)
                    {
                        if (request.arguments.contains("path" + std::to_string(later)))
                            throw std::invalid_argument{ "read_named_pdfs cannot skip a path number." };
                    }
                    break;
                }
                const std::string& path = found->second;
                if (path.size() > 1024 || !absoluteWindowsPath(path)
                    || !lowerAscii(path).ends_with(".pdf"))
                    throw std::invalid_argument{
                        "read_named_pdfs requires exact absolute .pdf file paths." };
                for (const std::string& previous : paths)
                {
                    if (lowerAscii(previous) == lowerAscii(path))
                        throw std::invalid_argument{
                            "read_named_pdfs requires distinct files." };
                }
                paths.push_back(path);
            }

            for (int i = 1; i <= 4; ++i)
            {
                const std::string pathName = "path" + std::to_string(i);
                const bool hasPath = request.arguments.contains(pathName);
                const bool hasStart = request.arguments.contains(pathName + "_page_start");
                const bool hasCount = request.arguments.contains(pathName + "_page_count");
                if (!hasPath && (hasStart || hasCount))
                {
                    throw std::invalid_argument{
                        "read_named_pdfs page-window arguments require their matching path."
                    };
                }
            }

            return paths;
        }


        void forwardPageWindow(
            const ToolRequest& batchRequest,
            const int pathIndex,
            ToolRequest& singleRead)
        {
            const std::string prefix = "path" + std::to_string(pathIndex);
            const auto start =
                batchRequest.arguments.find(prefix + "_page_start");
            const auto count =
                batchRequest.arguments.find(prefix + "_page_count");

            if (start != batchRequest.arguments.end())
                singleRead.arguments.emplace("page_start", start->second);
            if (count != batchRequest.arguments.end())
                singleRead.arguments.emplace("page_count", count->second);
        }
    }

    ReadNamedPdfsTool::ReadNamedPdfsTool(ITool& pdfReader)
        : pdfReader_{ pdfReader }
        , descriptor_{
            .id = "read_named_pdfs",
            .displayName = "Read Named PDFs",
            .description = "Read two to four explicitly named PDFs as one bounded, confirmation-gated task. Each exact file may optionally specify its own one-based page window. Uses the PDF text layer or OCR, reports extraction failures and coverage limits, and never scans parent directories.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                { .name = "path1", .description = "First exact absolute PDF path.", .type = ToolValueType::String, .required = true },
                { .name = "path2", .description = "Second exact absolute PDF path.", .type = ToolValueType::String, .required = true },
                { .name = "path3", .description = "Optional third exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path4", .description = "Optional fourth exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path1_page_start", .description = "Optional one-based start page for path1.", .type = ToolValueType::Integer, .required = false },
                { .name = "path1_page_count", .description = "Optional positive page count for path1.", .type = ToolValueType::Integer, .required = false },
                { .name = "path2_page_start", .description = "Optional one-based start page for path2.", .type = ToolValueType::Integer, .required = false },
                { .name = "path2_page_count", .description = "Optional positive page count for path2.", .type = ToolValueType::Integer, .required = false },
                { .name = "path3_page_start", .description = "Optional one-based start page for path3.", .type = ToolValueType::Integer, .required = false },
                { .name = "path3_page_count", .description = "Optional positive page count for path3.", .type = ToolValueType::Integer, .required = false },
                { .name = "path4_page_start", .description = "Optional one-based start page for path4.", .type = ToolValueType::Integer, .required = false },
                { .name = "path4_page_count", .description = "Optional positive page count for path4.", .type = ToolValueType::Integer, .required = false },
                { .name = "instruction", .description = "Question to guide per-document reading.", .type = ToolValueType::String, .required = false }
            }
        }
    {
        if (pdfReader_.descriptor().id != "read_pdf")
            throw std::invalid_argument{ "ReadNamedPdfsTool requires the read_pdf tool." };
    }

    const ToolDescriptor& ReadNamedPdfsTool::descriptor() const noexcept
    {
        return descriptor_;
    }

    ToolResult ReadNamedPdfsTool::execute(const ToolRequest& request)
    {
        if (request.toolId != descriptor_.id)
            throw std::invalid_argument{ "ReadNamedPdfsTool received another tool id." };
        const std::vector<std::string> paths = namedPaths(request);
        const auto instruction = request.arguments.find("instruction");
        std::ostringstream results;
        std::ostringstream failures;
        std::ostringstream incomplete;
        std::size_t completed = 0;
        constexpr std::size_t maximumEvidencePerFile{ 24u * 1024u };
        constexpr std::size_t maximumCombinedEvidence{ 20u * 1024u };
        for (std::size_t pathOffset = 0; pathOffset < paths.size(); ++pathOffset)
        {
            const std::string& path = paths[pathOffset];
            try
            {
                ToolRequest read{ .toolId = "read_pdf", .arguments = { { "path", path } } };
                forwardPageWindow(
                    request,
                    static_cast<int>(pathOffset + 1u),
                    read);
                if (instruction != request.arguments.end())
                    read.arguments.emplace("instruction", instruction->second);
                const ToolResult result = pdfReader_.execute(read);
                if (!result.success || result.message.empty())
                    throw std::runtime_error{ "PDF reader returned no usable evidence." };
                ++completed;
                const std::size_t headerEnd =
                    result.message.find("<rose_untrusted_pdf_content>");
                const std::string_view header{ result.message.data(),
                    std::min(headerEnd, result.message.size()) };
                if (header.find("extractor_truncated=true") != std::string_view::npos
                    || header.find("source_truncated=true") != std::string_view::npos
                    || result.message.size() > maximumEvidencePerFile)
                {
                    incomplete << path << ": extracted content or result window "
                        << "was bounded; a complete review is unavailable.\n"
                        << header.substr(0, 1024) << '\n';
                }
                results << "\n<rose_named_pdf path=\"" << path << "\">\n"
                    << result.message.substr(0, maximumEvidencePerFile);
                if (result.message.size() > maximumEvidencePerFile)
                    results << "\n[PDF result cut at 24 KiB]\n"
                        << "</rose_untrusted_pdf_content>";
                results
                    << "\nresult_window_truncated="
                    << (result.message.size() > maximumEvidencePerFile ? "true" : "false")
                    << "\n</rose_named_pdf>\n";
            }
            catch (const std::exception& exception)
            {
                failures << path << ": " << exception.what() << '\n';
            }
        }

        if (completed != paths.size())
        {
            return ToolResult{
                .success = false,
                .message = "Could not complete the requested analysis of all "
                    + std::to_string(paths.size()) + " exact PDFs. Read "
                    + std::to_string(completed) + "; failed:\n" + failures.str()
                    + "No conclusion about unread files or cross-document claims was made.",
                .responseMode = ToolResponseMode::AuthoritativeCompletion,
                .artifacts = {}
            };
        }
        if (results.str().size() > maximumCombinedEvidence)
        {
            incomplete << "Combined PDF evidence exceeds the 20 KiB local-model "
                << "context allowance; use focused questions or a separate "
                << "document index.\n";
        }
        if (!incomplete.str().empty())
        {
            return ToolResult{
                .success = false,
                .message = "The exact PDF reads completed, but a full cross-document "
                    "analysis is blocked by extraction or context limits:\n"
                    + incomplete.str()
                    + "No legal outcome or conclusion about unread pages was inferred. "
                      "Use focused page ranges or a larger OCR/indexing workflow.",
                .responseMode = ToolResponseMode::AuthoritativeCompletion,
                .artifacts = {}
            };
        }
        return ToolResult{
            .success = true,
            .message = "Read all " + std::to_string(completed)
                + " named PDFs. Base conclusions only on the evidence below. "
                  "Selected-page metadata is an intentional evidence boundary; do not "
                  "claim unselected pages were reviewed. If extractor_truncated, "
                  "source_truncated, or result_window_truncated is true, explain that "
                  "even the requested page window was not fully covered. Do not infer "
                  "legal outcomes or citations absent from the excerpts.\n"
                + results.str(),
            .responseMode = ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
