#pragma once

#include "documents/PdfiumRuntime.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace rose::documents
{
    enum class PdfMutationKind
    {
        AppendTextPage,
        RemovePageRange,
        RotatePage,
        AddTextToPage,
        RemovePageObject,
        AddTextAnnotation,
        RemoveAnnotation,
        AppendPdfPages
    };

    struct CreatePdfDocumentRequest
    {
        std::filesystem::path path;
        std::string text;
    };

    struct EditPdfDocumentRequest
    {
        std::filesystem::path path;
        PdfMutationKind kind{ PdfMutationKind::AppendTextPage };
        std::string text;
        std::filesystem::path sourcePath;
        std::string pageRange;
        std::size_t pageIndex{};        // 1-based
        std::size_t pageStart{};        // 1-based inclusive
        std::size_t pageEnd{};          // 1-based inclusive
        std::size_t objectIndex{};      // 1-based
        std::size_t annotationIndex{};  // 1-based
        int rotationDegrees{};          // 90/180/270 clockwise delta
        double x{ 72.0 };
        double y{ 720.0 };
        double fontSize{ 12.0 };
    };

    struct ExtractPdfPagesRequest
    {
        std::filesystem::path sourcePath;
        std::filesystem::path destinationPath;
        std::string pageRange;
    };

    struct PdfMutationResult
    {
        std::string operation;
        std::string detail;
        std::size_t affectedCount{};
    };

    class IPdfDocumentMutationService
    {
    public:
        virtual ~IPdfDocumentMutationService() = default;

        virtual PdfMutationResult create(const CreatePdfDocumentRequest& request) = 0;
        virtual PdfMutationResult edit(const EditPdfDocumentRequest& request) = 0;
        virtual PdfMutationResult extract(const ExtractPdfPagesRequest& request) = 0;
    };

    // Local PDFium writer/editor. All existing-document edits are transactional:
    // Rose loads the original read-only, writes a sibling temporary PDF, and only
    // swaps it into place after a successful PDFium save.
    class LocalPdfDocumentMutationService final : public IPdfDocumentMutationService
    {
    public:
        LocalPdfDocumentMutationService();
        ~LocalPdfDocumentMutationService() override;

        LocalPdfDocumentMutationService(const LocalPdfDocumentMutationService&) = delete;
        LocalPdfDocumentMutationService& operator=(const LocalPdfDocumentMutationService&) = delete;

        PdfMutationResult create(const CreatePdfDocumentRequest& request) override;
        PdfMutationResult edit(const EditPdfDocumentRequest& request) override;
        PdfMutationResult extract(const ExtractPdfPagesRequest& request) override;

    private:
        PdfiumRuntimeLease runtimeLease_;
    };
}
