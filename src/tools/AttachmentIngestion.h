#pragma once

#include "input/UserSubmission.h"
#include "tools/PdfTextExtractor.h"

#include <memory>
#include <string>

namespace rose::ocr
{
    class IOcrEngine;
}

namespace rose::vision
{
    class IVisionProvider;
}

namespace rose::permissions
{
    class PermissionSystem;
}

namespace rose::tools
{

    class ReadFileTool;

    struct IngestedUserSubmission
    {
        // Canonical user text persisted into Conversation.
        std::string userText;

        // Request-local source material. This is supplied to the model for this
        // generation but must NOT be persisted as if the user typed it.
        std::string transientContext;
    };


    // Turns user-selected attachments into transient model context.
    //
    // Dispatch today:
    //
    //     .pdf                         -> PDFium + OCR fallback
    //     png/jpg/jpeg/bmp/tif/tiff   -> semantic vision + OCR
    //     everything else             -> UTF-8/source reader
    //
    // OCR and semantic vision are deliberately separate capabilities. The OCR
    // provider answers "what text is visible?" while the vision provider answers
    // broader pixel-grounded questions.
    class AttachmentIngestion final
    {
    public:
        // Compatibility constructor: preserves the OCR-only behavior when a vision
        // provider is not configured yet.
        AttachmentIngestion(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            std::unique_ptr<ocr::IOcrEngine> ocrEngine);

        AttachmentIngestion(
            permissions::PermissionSystem& permissions,
            ReadFileTool& readFileTool,
            std::unique_ptr<ocr::IOcrEngine> ocrEngine,
            std::unique_ptr<vision::IVisionProvider> visionProvider);

        ~AttachmentIngestion();

        AttachmentIngestion(const AttachmentIngestion&) = delete;
        AttachmentIngestion& operator=(const AttachmentIngestion&) = delete;

        [[nodiscard]]
        IngestedUserSubmission ingest(
            input::UserSubmission submission);

    private:
        permissions::PermissionSystem& permissions_;
        ReadFileTool& readFileTool_;
        std::unique_ptr<ocr::IOcrEngine> ocrEngine_;
        std::unique_ptr<vision::IVisionProvider> visionProvider_;
        PdfTextExtractor pdfTextExtractor_;
    };

} // namespace rose::tools
