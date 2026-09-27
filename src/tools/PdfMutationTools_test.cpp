#include "tools/CreatePdfDocumentTool.h"
#include "tools/EditPdfDocumentTool.h"
#include "tools/ExtractPdfPagesTool.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeService final : public rose::documents::IPdfDocumentMutationService
    {
    public:
        rose::documents::CreatePdfDocumentRequest lastCreate;
        rose::documents::EditPdfDocumentRequest lastEdit;
        rose::documents::ExtractPdfPagesRequest lastExtract;
        int createCalls{};
        int editCalls{};
        int extractCalls{};

        rose::documents::PdfMutationResult create(const rose::documents::CreatePdfDocumentRequest& request) override
        {
            lastCreate = request;
            ++createCalls;
            return { "create_pdf_document", "ok", 1 };
        }

        rose::documents::PdfMutationResult edit(const rose::documents::EditPdfDocumentRequest& request) override
        {
            lastEdit = request;
            ++editCalls;
            return { "edit_pdf_document", "ok", 1 };
        }

        rose::documents::PdfMutationResult extract(const rose::documents::ExtractPdfPagesRequest& request) override
        {
            lastExtract = request;
            ++extractCalls;
            return { "extract_pdf_pages", "ok", 3 };
        }
    };
}

int main()
{
    try
    {
        FakeService service;
        rose::tools::CreatePdfDocumentTool createTool{ service };
        rose::tools::EditPdfDocumentTool editTool{ service };
        rose::tools::ExtractPdfPagesTool extractTool{ service };

        require(createTool.descriptor().risk == rose::tools::ToolRisk::LocalWrite, "create PDF must be LocalWrite");
        require(createTool.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation, "create PDF must require confirmation");
        require(editTool.descriptor().risk == rose::tools::ToolRisk::LocalWrite, "edit PDF must be LocalWrite");
        require(extractTool.descriptor().risk == rose::tools::ToolRisk::LocalWrite, "extract PDF must be LocalWrite");

#ifdef _WIN32
        const std::string base = "C:\\Temp\\RosePdfTest.pdf";
        const std::string other = "C:\\Temp\\Other.pdf";
        const std::string extracted = "C:\\Temp\\Extracted.pdf";
#else
        const std::string base = "/tmp/RosePdfTest.pdf";
        const std::string other = "/tmp/Other.pdf";
        const std::string extracted = "/tmp/Extracted.pdf";
#endif

        auto create = createTool.execute({ .toolId = "create_pdf_document", .arguments = { { "path", base }, { "text", "Line 1\\nLine 2" } } });
        require(create.success && service.createCalls == 1, "create PDF tool should call service once");
        require(service.lastCreate.text == "Line 1\nLine 2", "create PDF should decode newline escapes");

        auto appendPage = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "append_text_page" }, { "text", "Added page" } } });
        require(appendPage.success && service.lastEdit.kind == rose::documents::PdfMutationKind::AppendTextPage, "append_text_page routing failed");

        [[maybe_unused]] const auto removedPages = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "remove_page_range" }, { "page_start", "2" }, { "page_end", "3" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::RemovePageRange && service.lastEdit.pageStart == 2 && service.lastEdit.pageEnd == 3,
                "remove_page_range should carry both bounds");

        [[maybe_unused]] const auto rotated = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "rotate_page" }, { "page", "1" }, { "rotation_degrees", "90" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::RotatePage && service.lastEdit.rotationDegrees == 90,
                "rotate_page arguments failed");

        [[maybe_unused]] const auto addedText = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "add_text_to_page" }, { "page", "1" }, { "text", "Overlay" }, { "x", "100" }, { "y", "650" }, { "font_size", "14" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::AddTextToPage && service.lastEdit.fontSize == 14.0,
                "add_text_to_page arguments failed");

        [[maybe_unused]] const auto removedObject = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "remove_page_object" }, { "page", "1" }, { "object_index", "4" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::RemovePageObject && service.lastEdit.objectIndex == 4,
                "remove_page_object arguments failed");

        [[maybe_unused]] const auto addedAnnotation = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "add_text_annotation" }, { "page", "1" }, { "text", "Review this" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::AddTextAnnotation, "add annotation routing failed");

        [[maybe_unused]] const auto removedAnnotation = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "remove_annotation" }, { "page", "1" }, { "annotation_index", "2" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::RemoveAnnotation && service.lastEdit.annotationIndex == 2,
                "remove annotation routing failed");

        [[maybe_unused]] const auto appendedPdf = editTool.execute({ .toolId = "edit_pdf_document", .arguments = { { "path", base }, { "operation", "append_pdf_pages" }, { "source_path", other }, { "pages", "1,3-4" } } });
        require(service.lastEdit.kind == rose::documents::PdfMutationKind::AppendPdfPages && service.lastEdit.sourcePath.string() == other,
                "append_pdf_pages routing failed");

        auto extract = extractTool.execute({ .toolId = "extract_pdf_pages", .arguments = { { "source_path", base }, { "destination_path", extracted }, { "pages", "1,3-4" } } });
        require(extract.success && service.extractCalls == 1 && service.lastExtract.pageRange == "1,3-4", "extract_pdf_pages routing failed");

        std::cout << "Rose PdfMutationTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Rose PdfMutationTools tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
