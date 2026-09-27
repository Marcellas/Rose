#include "documents/OfficeDocumentMutationService.h"
#include "tools/CreateOfficeDocumentTool.h"
#include "tools/EditOfficeDocumentTool.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakeOfficeMutationService final : public rose::documents::IOfficeDocumentMutationService
    {
    public:
        rose::documents::CreateOfficeDocumentRequest lastCreate;
        rose::documents::EditOfficeDocumentRequest lastEdit;

        rose::documents::OfficeMutationResult create(
            const rose::documents::CreateOfficeDocumentRequest& request) override
        {
            lastCreate = request;
            return rose::documents::OfficeMutationResult{
                .operation = "create_word",
                .detail = request.path.string(),
                .affectedCount = 1
            };
        }

        rose::documents::OfficeMutationResult edit(
            const rose::documents::EditOfficeDocumentRequest& request) override
        {
            lastEdit = request;
            return rose::documents::OfficeMutationResult{
                .operation = "test_edit",
                .detail = request.path.string(),
                .affectedCount = 1
            };
        }
    };
}

int main()
{
    try
    {
        FakeOfficeMutationService service;
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "rose-office-mutation-test";
        const auto word = root / "notes.docx";
        const auto excel = root / "budget.xlsx";
        const auto slides = root / "brief.pptx";

        rose::tools::CreateOfficeDocumentTool create{ service };
        require(create.descriptor().risk == rose::tools::ToolRisk::LocalWrite,
            "create_office_document must be LocalWrite");
        require(create.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "create_office_document must require confirmation");

        const auto createResult = create.execute(rose::tools::ToolRequest{
            .toolId = "create_office_document",
            .arguments = {
                { "path", word.string() },
                { "kind", "word" },
                { "content", "Line one\\nLine two" }
            }
        });
        require(service.lastCreate.kind == rose::documents::OfficeDocumentKind::Word,
            "create tool should parse Word kind");
        require(service.lastCreate.content == "Line one\nLine two",
            "create tool should decode text escapes");
        require(createResult.message.find("inverse is recycle_path") != std::string::npos,
            "create tool should surface its inverse lifecycle");

        rose::tools::EditOfficeDocumentTool edit{ service };
        require(edit.descriptor().risk == rose::tools::ToolRisk::LocalWrite,
            "edit_office_document must be LocalWrite");
        require(edit.descriptor().consent == rose::tools::ToolConsent::RequiresConfirmation,
            "edit_office_document must require confirmation");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", word.string() },
                { "operation", "append_word_text" },
                { "text", "Added\\nparagraph" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::AppendWordText,
            "append_word_text should map to the paired Word append mutation");
        require(service.lastEdit.text == "Added\nparagraph",
            "edit tool should decode Word text escapes");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", word.string() },
                { "operation", "remove_word_text" },
                { "text", "Added" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::RemoveWordText,
            "Word append must have a remove counterpart");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", excel.string() },
                { "operation", "set_excel_cell" },
                { "sheet", "Budget" },
                { "cell", "B7" },
                { "text", "4225.30" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::SetExcelCell,
            "set_excel_cell should map to the Excel set mutation");
        require(service.lastEdit.sheetName == "Budget" && service.lastEdit.cellReference == "B7",
            "Excel edit should preserve exact sheet/cell addressing");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", excel.string() },
                { "operation", "clear_excel_cell" },
                { "sheet", "Budget" },
                { "cell", "B7" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::ClearExcelCell,
            "Excel set must have a clear counterpart");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", slides.string() },
                { "operation", "append_powerpoint_slide" },
                { "text", "New slide" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::AppendPowerPointSlide,
            "append_powerpoint_slide should map to PowerPoint append");

        (void)edit.execute(rose::tools::ToolRequest{
            .toolId = "edit_office_document",
            .arguments = {
                { "path", slides.string() },
                { "operation", "remove_powerpoint_slide" },
                { "slide_index", "2" }
            }
        });
        require(service.lastEdit.kind == rose::documents::OfficeMutationKind::RemovePowerPointSlide,
            "PowerPoint append must have a remove counterpart");
        require(service.lastEdit.slideIndex == 2,
            "PowerPoint remove should preserve the exact 1-based slide index");

        std::cout << "Rose OfficeMutationTools tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rose OfficeMutationTools tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
