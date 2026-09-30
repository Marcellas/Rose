#include "tools/ReadNamedPdfsTool.h"
#include "documents/ContextSafeDocumentSynthesizer.h"
#include "model/IModelProvider.h"

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

        [[nodiscard]] std::string omittedQuotedPaths(
            const std::string_view instruction)
        {
            std::ostringstream missing;
            for (std::size_t i = 0; i < instruction.size(); ++i)
            {
                if (instruction[i] != '"' && instruction[i] != '\'') continue;
                const std::size_t end = instruction.find(instruction[i], i + 1u);
                if (end == std::string_view::npos) break;
                const std::string candidate{ instruction.substr(i + 1u,
                    end - i - 1u) };
                if (absoluteWindowsPath(candidate)
                    && !lowerAscii(candidate).ends_with(".pdf"))
                    missing << candidate << ": omitted because this is not "
                        "an exact .pdf filename.\n";
                i = end;
            }
            return missing.str();
        }

        [[nodiscard]] bool namedPdfArgument(const std::string_view name)
        {
            if (name == "instruction") return true;

            for (int i = 1; i <= maximumNamedPdfFiles; ++i)
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
            for (int i = 1; i <= maximumNamedPdfFiles; ++i)
            {
                const auto found = request.arguments.find("path" + std::to_string(i));
                if (found == request.arguments.end())
                {
                    if (i <= 2) throw std::invalid_argument{
                        "read_named_pdfs requires at least two exact paths." };
                    for (int later = i + 1; later <= maximumNamedPdfFiles; ++later)
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

            for (int i = 1; i <= maximumNamedPdfFiles; ++i)
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

    ReadNamedPdfsTool::ReadNamedPdfsTool(ITool& pdfReader,
        model::IModelProvider* modelProvider)
        : pdfReader_{ pdfReader }
        , modelProvider_{ modelProvider }
        , descriptor_{
            .id = "read_named_pdfs",
            .displayName = "Read Named PDFs",
            .description = "Read two to 24 explicitly named PDFs as one confirmation-gated task. Each exact file may specify its own page window. Reports page coverage and failures; never scans parent directories.",
            .risk = ToolRisk::ReadOnly,
            .consent = ToolConsent::RequiresConfirmation,
            .parameters = {
                { .name = "path1", .description = "First exact absolute PDF path.", .type = ToolValueType::String, .required = true },
                { .name = "path2", .description = "Second exact absolute PDF path.", .type = ToolValueType::String, .required = true },
                { .name = "path3", .description = "Optional third exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path4", .description = "Optional fourth exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path5", .description = "Optional fifth exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path6", .description = "Optional sixth exact PDF path.", .type = ToolValueType::String, .required = false },
                { .name = "path1_page_start", .description = "Optional one-based start page for path1.", .type = ToolValueType::Integer, .required = false },
                { .name = "path1_page_count", .description = "Optional positive page count for path1.", .type = ToolValueType::Integer, .required = false },
                { .name = "path2_page_start", .description = "Optional one-based start page for path2.", .type = ToolValueType::Integer, .required = false },
                { .name = "path2_page_count", .description = "Optional positive page count for path2.", .type = ToolValueType::Integer, .required = false },
                { .name = "path3_page_start", .description = "Optional one-based start page for path3.", .type = ToolValueType::Integer, .required = false },
                { .name = "path3_page_count", .description = "Optional positive page count for path3.", .type = ToolValueType::Integer, .required = false },
                { .name = "path4_page_start", .description = "Optional one-based start page for path4.", .type = ToolValueType::Integer, .required = false },
                { .name = "path4_page_count", .description = "Optional positive page count for path4.", .type = ToolValueType::Integer, .required = false },
                { .name = "path5_page_start", .description = "Optional one-based start page for path5.", .type = ToolValueType::Integer, .required = false },
                { .name = "path5_page_count", .description = "Optional positive page count for path5.", .type = ToolValueType::Integer, .required = false },
                { .name = "path6_page_start", .description = "Optional one-based start page for path6.", .type = ToolValueType::Integer, .required = false },
                { .name = "path6_page_count", .description = "Optional positive page count for path6.", .type = ToolValueType::Integer, .required = false },
                { .name = "instruction", .description = "Question to guide per-document reading.", .type = ToolValueType::String, .required = false }
            }
        }
    {
        if (pdfReader_.descriptor().id != "read_pdf")
            throw std::invalid_argument{ "ReadNamedPdfsTool requires the read_pdf tool." };
        for (int i = 7; i <= maximumNamedPdfFiles; ++i)
        {
            const std::string prefix = "path" + std::to_string(i);
            descriptor_.parameters.push_back({ prefix,
                "Optional exact PDF path.", ToolValueType::String, false });
            descriptor_.parameters.push_back({ prefix + "_page_start",
                "Optional one-based start page.", ToolValueType::Integer, false });
            descriptor_.parameters.push_back({ prefix + "_page_count",
                "Optional positive page count.", ToolValueType::Integer, false });
        }
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
        std::ostringstream results;
        std::ostringstream failures;
        std::ostringstream incomplete;
        std::ostringstream coverage;
        std::ostringstream fallbackEvidence;
        std::size_t completed = 0;
        if (const auto instruction = request.arguments.find("instruction");
            instruction != request.arguments.end())
            failures << omittedQuotedPaths(instruction->second);
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
                // The full batch request remains available to the final response.
                // Repeating it (often a long timeline and all file names) for
                // every chunk of every PDF can exhaust the model context before
                // any document evidence is processed.
                read.arguments.emplace("instruction",
                    "Extract the document's material facts, dates, cited rules, "
                    "procedural steps, decisions, reasons, and limitations. "
                    "Preserve page references where available; do not infer "
                    "facts absent from the document.");
                const ToolResult result = pdfReader_.execute(read);
                if (!result.success || result.message.empty())
                    throw std::runtime_error{ "PDF reader returned no usable evidence." };
                ++completed;
                const std::size_t headerEnd =
                    result.message.find("<rose_untrusted_pdf_content>");
                const std::string_view header{ result.message.data(),
                    std::min(headerEnd, result.message.size()) };
                coverage << path << ": ";
                for (const std::string_view key : {
                        "pages=", "selected_page_start=", "selected_page_end=",
                        "pages_examined=", "extractor_truncated=",
                        "source_truncated=", "coverage=" })
                {
                    const std::size_t begin = header.find(key);
                    if (begin != std::string_view::npos)
                    {
                        const std::size_t end = header.find('\n', begin);
                        coverage << header.substr(begin, end == std::string_view::npos
                            ? end : end - begin) << ' ';
                    }
                }
                coverage << '\n';
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
                const std::size_t evidenceStart = headerEnd == std::string::npos
                    ? 0u : headerEnd + std::string_view{ "<rose_untrusted_pdf_content>" }.size();
                fallbackEvidence << "\n" << path << ": "
                    << result.message.substr(evidenceStart, 512u)
                    << "\n[Preview only; full evidence reduction unavailable.]\n";
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

        if (completed == 0u)
        {
            return ToolResult{
                .success = false,
                .message = "No named PDFs could be read:\n" + failures.str(),
                .responseMode = ToolResponseMode::AuthoritativeCompletion,
                .artifacts = {}
            };
        }

        std::string evidence = results.str();
        if (evidence.size() > maximumCombinedEvidence)
        {
            if (modelProvider_ == nullptr)
            {
                incomplete << "Combined evidence exceeds the available synthesis "
                    "window; no batch model was configured.\n";
                evidence = fallbackEvidence.str();
            }
            else
            {
                try
                {
                    documents::ContextSafeDocumentSynthesizer reducer{
                        *modelProvider_,
                        documents::ContextSafeDocumentSynthesisConfig{
                            .maximumRawObservationBytes = maximumCombinedEvidence,
                            .chunkBytes = 12u * 1024u,
                            .maximumSourceBytes = 1024u * 1024u,
                            .reductionGroupSize = 4u,
                            .maximumFinalBytes = 10u * 1024u,
                            .chunkSummaryTokens = 512,
                            .reductionTokens = 1024
                        } };
                    const auto condensed = reducer.synthesize(evidence,
                        "named pdf evidence",
                        "Preserve which named file supports each finding, its "
                        "page references, competing claims, and uncertainty. "
                        "Do not treat an unread or partial file as reviewed.");
                    evidence = condensed.text;
                    if (condensed.sourceTruncated)
                        incomplete << "Combined evidence was truncated during "
                            "batch synthesis.\n";
                }
                catch (const std::exception& exception)
                {
                    incomplete << "Batch evidence reduction failed: "
                        << exception.what() << '\n';
                    evidence = fallbackEvidence.str();
                }
            }
        }

        const bool complete = completed == paths.size()
            && incomplete.str().empty() && failures.str().empty();
        return ToolResult{
            .success = complete,
            .message = "Read " + std::to_string(completed) + " of "
                + std::to_string(paths.size()) + " named PDFs. "
                + (complete ? "Read all " + std::to_string(completed)
                    + " named PDFs. " : "This is a partial review. ")
                + "Coverage by file:\n" + coverage.str()
                + (failures.str().empty() ? "" : "Failed files:\n" + failures.str())
                + (incomplete.str().empty() ? "" : "Limits:\n" + incomplete.str())
                + "Base conclusions only on the evidence below. A condensed "
                  "summary is not an exhaustive transcript. Do not claim unread "
                  "files or pages were reviewed; distinguish user assertions "
                  "from verified document content.\n" + evidence,
            .responseMode = evidence.empty()
                ? ToolResponseMode::AuthoritativeCompletion
                : ToolResponseMode::RequiresModelSynthesis,
            .artifacts = {}
        };
    }
}
