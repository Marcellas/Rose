#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace rose::documents
{
    enum class OfficeDocumentKind
    {
        Word,
        Excel,
        PowerPoint
    };

    enum class OfficeMutationKind
    {
        AppendWordText,
        RemoveWordText,
        ReplaceWordText,
        SetExcelCell,
        ClearExcelCell,
        AppendPowerPointSlide,
        RemovePowerPointSlide
    };

    struct CreateOfficeDocumentRequest
    {
        std::filesystem::path path;
        OfficeDocumentKind kind{ OfficeDocumentKind::Word };
        std::string content;
        std::string sheetName{ "Sheet1" };
    };

    struct EditOfficeDocumentRequest
    {
        std::filesystem::path path;
        OfficeMutationKind kind{ OfficeMutationKind::AppendWordText };
        std::string text;
        std::string findText;
        std::string replacementText;
        std::string sheetName;
        std::string cellReference;
        std::size_t slideIndex{};
    };

    struct OfficeMutationResult
    {
        std::string operation;
        std::string detail;
        std::size_t affectedCount{};
    };

    class IOfficeDocumentMutationService
    {
    public:
        virtual ~IOfficeDocumentMutationService() = default;

        virtual OfficeMutationResult create(
            const CreateOfficeDocumentRequest& request) = 0;

        virtual OfficeMutationResult edit(
            const EditOfficeDocumentRequest& request) = 0;
    };

    // Windows-local Office writer/editor.
    //
    // Word and Excel operations mutate the Open XML package directly through
    // Windows PowerShell/.NET ZIP + XmlDocument support. The user's original is
    // never edited in-place: Rose works on a sibling temporary copy and swaps it
    // into place only after the helper succeeds.
    //
    // PowerPoint structural slide creation/removal uses the installed PowerPoint
    // COM automation surface in this first mutation checkpoint. If PowerPoint is
    // unavailable, Rose reports that dependency instead of corrupting the package.
    // A later backend can replace this without changing the tool interface.
    class LocalOfficeDocumentMutationService final : public IOfficeDocumentMutationService
    {
    public:
        OfficeMutationResult create(
            const CreateOfficeDocumentRequest& request) override;

        OfficeMutationResult edit(
            const EditOfficeDocumentRequest& request) override;
    };
}
