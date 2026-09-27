#pragma once

#include "tools/ITool.h"
#include "tools/PdfTextExtractor.h"

#include <cstddef>
#include <memory>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::tools
{

    struct AnalyzeDirectoryDocumentsToolConfig
    {
        // One tool execution must fit inside the hidden routing model's context.
        // The tool therefore returns compact per-document excerpts instead of
        // dumping entire files into one prompt.
        std::size_t maximumFilesPerBatch{ 20 };
        std::size_t maximumObservationBytes{ 18u * 1024u };
        std::size_t maximumExcerptBytesPerFile{ 900u };
        std::size_t maximumDepth{ 8 };
        std::size_t maximumTextFileBytes{ 64u * 1024u };
        std::size_t maximumPdfBytes{ 64u * 1024u * 1024u };
    };


    // Read-only, bounded document-batch analysis primitive.
    //
    // The authorization boundary is the explicitly confirmed root directory.
    // Once ToolExecutionPolicy authorizes this exact request, the tool may read
    // supported regular files beneath that root without asking for one confirmation
    // per file. It never mutates anything, never follows symbolic links, and returns
    // only compact excerpts for model synthesis.
    class AnalyzeDirectoryDocumentsTool final : public ITool
    {
    public:
        AnalyzeDirectoryDocumentsTool(
            std::unique_ptr<ocr::IOcrEngine> ocrEngine,
            AnalyzeDirectoryDocumentsToolConfig config = {});

        ~AnalyzeDirectoryDocumentsTool() override;

        AnalyzeDirectoryDocumentsTool(
            const AnalyzeDirectoryDocumentsTool&) = delete;

        AnalyzeDirectoryDocumentsTool& operator=(
            const AnalyzeDirectoryDocumentsTool&) = delete;

        [[nodiscard]]
        const ToolDescriptor& descriptor() const noexcept override;

        [[nodiscard]]
        ToolResult execute(
            const ToolRequest& request) override;

    private:
        AnalyzeDirectoryDocumentsToolConfig config_;
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
        PdfTextExtractor pdfTextExtractor_;
        ToolDescriptor descriptor_;
    };

} // namespace rose::tools
