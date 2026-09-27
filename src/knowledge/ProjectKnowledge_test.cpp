#include "documents/OpenXmlDocumentExtractor.h"
#include "files/FileFormatCatalog.h"
#include "knowledge/FileProjectKnowledgeStore.h"
#include "knowledge/ProjectKnowledgeCommand.h"
#include "knowledge/ProjectKnowledgeIndexer.h"
#include "knowledge/ProjectKnowledgeRepository.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(
        const bool condition,
        const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    void writeText(
        const std::filesystem::path& path,
        const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        if (!output)
        {
            throw std::runtime_error{ "Could not create project knowledge test source." };
        }
        output << text;
    }
}

int main()
{
    try
    {
        // File-family recognition is central rather than duplicated across
        // attachment ingestion, project indexing, and Agent tools.
        require(
            rose::files::classifyFileFormat("sample.docx").kind
                == rose::files::FileFormatKind::OfficeWordOpenXml,
            "Expected .docx recognition.");
        require(
            rose::files::classifyFileFormat("sample.xlsx").kind
                == rose::files::FileFormatKind::OfficeSpreadsheetOpenXml,
            "Expected .xlsx recognition.");
        require(
            rose::files::classifyFileFormat("sample.pptx").kind
                == rose::files::FileFormatKind::OfficePresentationOpenXml,
            "Expected .pptx recognition.");
        require(
            rose::files::classifyFileFormat("sample.webm").kind
                == rose::files::FileFormatKind::Video,
            "Expected .webm recognition for the media reader.");
        require(
            rose::files::classifyFileFormat("sample.gif").kind
                == rose::files::FileFormatKind::AnimatedImage,
            "Expected animated GIF recognition.");
        require(
            rose::files::classifyFileFormat("sample.accdb").kind
                == rose::files::FileFormatKind::Database,
            "Expected Access database recognition.");

        // Platform-neutral Office interpretation tests. These feed the same XML
        // that the Windows ZIP adapter extracts from real Office packages.
        const auto word = rose::documents::OpenXmlDocumentExtractor::extractEntries(
            ".docx",
            { { "word/document.xml",
                "<w:document><w:body><w:p><w:r><w:t>Hello Rose</w:t></w:r></w:p>"
                "<w:p><w:r><w:t>Second paragraph</w:t></w:r></w:p></w:body></w:document>" } });
        require(word.contentKind == "office/word", "Unexpected Word content kind.");
        require(!word.segments.empty() && word.segments.front().text.find("Hello Rose") != std::string::npos,
                "Expected Word text extraction.");

        const auto slides = rose::documents::OpenXmlDocumentExtractor::extractEntries(
            ".pptx",
            {
                { "ppt/slides/slide2.xml", "<p:sld><a:t>Second slide</a:t></p:sld>" },
                { "ppt/slides/slide1.xml", "<p:sld><a:t>First slide</a:t></p:sld>" }
            });
        require(slides.segments.size() == 2, "Expected two PowerPoint slide segments.");
        require(slides.segments.front().locator == "slide=1", "Expected natural PowerPoint slide ordering.");

        const auto workbook = rose::documents::OpenXmlDocumentExtractor::extractEntries(
            ".xlsx",
            {
                { "xl/sharedStrings.xml", "<sst><si><t>Name</t></si><si><t>Rose</t></si></sst>" },
                { "xl/workbook.xml", "<workbook><sheets><sheet name=\"People\" r:id=\"rId1\"/></sheets></workbook>" },
                { "xl/_rels/workbook.xml.rels", "<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/></Relationships>" },
                { "xl/worksheets/sheet1.xml", "<worksheet><sheetData><row><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row></sheetData></worksheet>" }
            });
        require(workbook.segments.size() == 1, "Expected one Excel sheet segment.");
        require(workbook.segments.front().locator == "sheet=People", "Expected Excel sheet-name provenance.");
        require(workbook.segments.front().text.find("A1=Name") != std::string::npos, "Expected Excel shared-string cell extraction.");
        require(workbook.segments.front().text.find("B1=Rose") != std::string::npos, "Expected second Excel shared-string cell extraction.");

        // Add/remove symmetry applies to dynamically registered content readers too.
        auto readerRegistry = rose::knowledge::makeDefaultProjectContentReaderRegistry();
        auto removedImageReader = readerRegistry.remove("image-ocr-v1");
        require(removedImageReader != nullptr, "Expected registered image reader removal.");
        require(readerRegistry.findReader("sample.png") == nullptr, "Removed reader remained active.");
        readerRegistry.add(std::move(removedImageReader));
        require(readerRegistry.findReader("sample.png") != nullptr, "Re-added image reader was not active.");
        require(readerRegistry.findReader("sample.webm") != nullptr, "Expected default media metadata reader.");
        require(readerRegistry.findReader("sample.gif") != nullptr, "Expected animated GIF media reader.");
        require(readerRegistry.findReader("sample.sqlite") != nullptr, "Expected default database schema reader.");
        require(readerRegistry.findReader("sample.accdb") != nullptr, "Expected Access database reader registration.");
        require(readerRegistry.findReader("sample.lnk") != nullptr, "Expected Windows shortcut metadata reader.");
        require(readerRegistry.findReader("sample.url") != nullptr, "Expected Internet Shortcut metadata reader.");

        const auto unique =
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        const std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("rose-project-knowledge-" + unique);
        const std::filesystem::path projectRoot = root / "project";
        const std::filesystem::path storePath = root / "data" / "knowledge.roseidx";

        writeText(
            projectRoot / "README.md",
            "Rose is a local-first desktop assistant. Project knowledge should remain searchable offline.\n");
        writeText(
            projectRoot / "docs" / "architecture.txt",
            "Project knowledge retrieval keeps source provenance so Rose can explain where retrieved material came from. "
            "Retrieval injects only relevant chunks into working context instead of dumping every document into every prompt.\n");
        writeText(
            projectRoot / "Rose.vcxproj",
            "<Project><PropertyGroup><LanguageStandard>stdcpp20</LanguageStandard></PropertyGroup></Project>\n");
        writeText(
            projectRoot / "src" / "worker.rs",
            "fn project_reader_registry() { /* future format adapters remain modular */ }\n");
        writeText(
            projectRoot / "media" / "demo.webm",
            "fake media fixture bytes for metadata-reader routing\n");
        std::filesystem::resize_file(
            projectRoot / "media" / "demo.webm",
            256u * 1024u);
        writeText(
            projectRoot / "build" / "generated.txt",
            "THIS GENERATED BUILD FILE MUST NOT BE INDEXED retrieval provenance secretword\n");

        rose::knowledge::ProjectKnowledgeIndexer indexer{
            rose::knowledge::ProjectKnowledgeIndexerConfig{
                .maximumDepth = 6,
                .maximumFiles = 100,
                .maximumFileBytes = 64u * 1024u,
                .maximumChunkBytes = 160,
                .chunkOverlapBytes = 24,
                .maximumWarnings = 8
            }
        };

        auto indexed = indexer.index(
            "project-test",
            { projectRoot.string() });

        require(indexed.report.rootsScanned == 1, "Expected one scanned project root.");
        require(indexed.report.documentsIndexed == 5, "Expected text/source + Visual Studio files while excluding build output.");
        require(indexed.report.chunksCreated >= 2, "Expected chunked project knowledge.");

        rose::knowledge::FileProjectKnowledgeStore store{ storePath };
        rose::knowledge::ProjectKnowledgeRepository repository{ store };
        repository.replaceProject(std::move(indexed.project));

        const auto hits = repository.search(
            "project-test",
            "retrieval source provenance",
            4);
        require(!hits.empty(), "Expected project knowledge retrieval hit.");
        require(
            hits.front().sourcePath.find("architecture.txt") != std::string::npos,
            "Expected architecture source to rank first.");
        require(
            hits.front().excerpt.find("source provenance") != std::string::npos,
            "Expected matching provenance text in excerpt.");
        require(
            hits.front().contentKind == "text/source",
            "Expected persisted content-kind provenance.");
        require(
            hits.front().readerId == "utf8-text-v1",
            "Expected persisted content-reader provenance.");
        require(
            !hits.front().sourceLocator.empty(),
            "Expected format-neutral source locator provenance.");

        const auto visualStudioHits = repository.search(
            "project-test",
            "LanguageStandard stdcpp20",
            2);
        require(!visualStudioHits.empty(), "Expected Visual Studio project-file retrieval.");
        require(
            visualStudioHits.front().sourcePath.find("Rose.vcxproj") != std::string::npos,
            "Expected .vcxproj support in the default content reader.");

        const auto languageHits = repository.search(
            "project-test",
            "project reader registry modular",
            2);
        require(!languageHits.empty(), "Expected additional programming-language source retrieval.");
        require(
            languageHits.front().sourcePath.find("worker.rs") != std::string::npos,
            "Expected Rust source support in the default content reader.");

        const auto mediaHits = repository.search(
            "project-test",
            "Media asset demo.webm",
            2);
        require(!mediaHits.empty(), "Expected media metadata retrieval.");
        require(mediaHits.front().contentKind == "media", "Expected media content-kind provenance.");
        require(mediaHits.front().readerId == "media-metadata-v1", "Expected media reader provenance.");

        const auto ignoredHits = repository.search(
            "project-test",
            "secretword",
            4);
        require(ignoredHits.empty(), "Ignored build directory leaked into project index.");

        const auto stats = repository.stats("project-test");
        require(stats.documentCount == 5, "Unexpected persisted document count.");
        require(stats.chunkCount >= 2, "Unexpected persisted chunk count.");

        // Prove durable reload, not just in-memory search.
        rose::knowledge::FileProjectKnowledgeStore reloadedStore{ storePath };
        rose::knowledge::ProjectKnowledgeRepository reloaded{ reloadedStore };
        const auto reloadedHits = reloaded.search(
            "project-test",
            "local-first searchable offline",
            2);
        require(!reloadedHits.empty(), "Reloaded project knowledge index was not searchable.");

        const auto addRoot = rose::knowledge::parseProjectKnowledgeCommand(
            "/knowledge add-root C:/Rose/Docs");
        require(addRoot.has_value(), "Expected add-root command parse.");
        require(
            addRoot->kind == rose::knowledge::ProjectKnowledgeCommandKind::AddRoot,
            "Unexpected add-root command kind.");
        require(addRoot->text == "C:/Rose/Docs", "Unexpected add-root path parse.");

        const auto removeRoot = rose::knowledge::parseProjectKnowledgeCommand(
            "/knowledge remove-root C:/Rose/Docs");
        require(removeRoot.has_value(), "Expected remove-root command parse.");
        require(
            removeRoot->kind == rose::knowledge::ProjectKnowledgeCommandKind::RemoveRoot,
            "Unexpected remove-root command kind.");
        require(removeRoot->text == "C:/Rose/Docs", "Unexpected remove-root path parse.");

        const auto search = rose::knowledge::parseProjectKnowledgeCommand(
            "/knowledge search memory provenance");
        require(search.has_value(), "Expected search command parse.");
        require(
            search->kind == rose::knowledge::ProjectKnowledgeCommandKind::Search,
            "Unexpected search command kind.");

        reloaded.clearProject("project-test");
        require(
            reloaded.stats("project-test").documentCount == 0,
            "Project knowledge clear did not remove the project cache.");

        rose::knowledge::FileProjectKnowledgeStore clearedStore{ storePath };
        rose::knowledge::ProjectKnowledgeRepository cleared{ clearedStore };
        require(
            cleared.search("project-test", "Rose", 2).empty(),
            "Cleared project knowledge reappeared after reload.");

        std::filesystem::remove_all(root);
        std::cout << "Rose ProjectKnowledge tests: PASS\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Rose ProjectKnowledge tests: FAIL: "
            << exception.what()
            << '\n';
        return 1;
    }
}
