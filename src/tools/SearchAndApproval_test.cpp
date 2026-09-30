#include "agent/ScopedReadApproval.h"
#include "agent/ReadWindowArguments.h"
#include "agent/CapabilityRoutingGuard.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/SearchLocalFilesTool.h"
#include "tools/ReadNamedPdfsTool.h"
#include "tools/ToolRegistry.h"
#include "model/IModelProvider.h"
#include "ui/TextPresentation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    void check(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }

    class FakePdfReader final : public rose::tools::ITool
    {
    public:
        std::vector<std::string> readPaths;
        std::vector<rose::tools::ToolRequest> readRequests;
        std::string failingPath;
        std::string partialPath;
        bool longResult{ false };
        rose::tools::ToolDescriptor descriptor_{
            .id = "read_pdf", .displayName = "Read PDF", .description = "Test reader",
            .risk = rose::tools::ToolRisk::ReadOnly,
            .consent = rose::tools::ToolConsent::RequiresConfirmation,
            .parameters = {} };

        const rose::tools::ToolDescriptor& descriptor() const noexcept override
        {
            return descriptor_;
        }

        rose::tools::ToolResult execute(const rose::tools::ToolRequest& request) override
        {
            const std::string path = request.arguments.at("path");
            readPaths.push_back(path);
            readRequests.push_back(request);
            if (path == failingPath) throw std::runtime_error{ "OCR unavailable" };
            return { .success = true, .message = "Read PDF: " + path
                + (path == partialPath ? "\nextractor_truncated=true"
                    : "\nextractor_truncated=false")
                + "\ncoverage=full_extracted_content"
                + (longResult ? "\n<rose_untrusted_pdf_content>\n"
                    + std::string(2600, 'x')
                    + "\n</rose_untrusted_pdf_content>" : ""), .trustedMetadata = {},
                .sourceWindowEvidence = std::nullopt,
                .responseMode = rose::tools::ToolResponseMode::RequiresModelSynthesis,
                .artifacts = {} };
        }
    };

    class BatchSummaryProvider final : public rose::model::IModelProvider
    {
    public:
        int calls{ 0 };
        bool fail{ false };
        rose::model::ModelContextUsage inspectContext(
            const rose::model::ModelRequest& request) const override
        {
            return { 1000, 16384, request.maxGeneratedTokens };
        }
        rose::model::ModelResponse generate(
            const rose::model::ModelRequest&) override
        {
            ++calls;
            if (fail) throw std::runtime_error{ "model reducer unavailable" };
            return { "Condensed cross-file evidence with source names.", {}, 12,
                rose::model::ModelFinishReason::EndOfGeneration };
        }
    };
}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("rose-search-approval-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "nested");
    struct Cleanup
    {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{ root };

    { std::ofstream(root / "nested" / "example.cpp") << "// forall exists\n"; }
    { std::ofstream(root / "nested" / "AgentLoop.cpp") << "void agentLoop() {}\n"; }
    { std::ofstream(root / "unusual.the-longest-possible-ending") << "binary-ish\n"; }
    { std::ofstream(root / "extensionless") << "Agent loop\n"; }
    { std::ofstream binary(root / "opaque.data", std::ios::binary);
      binary.write("\0Agent loop\n", 12); }
    const auto outside = root.parent_path()
        / (root.filename().string() + "-outside.cpp");
    { std::ofstream(outside) << "external"; }
    struct RemoveOutside
    {
        fs::path path;
        ~RemoveOutside() { std::error_code error; fs::remove(path, error); }
    } removeOutside{ outside };

    rose::tools::SearchLocalFilesTool search;
    rose::tools::ToolRequest request{
        .toolId = "search_local_files",
        .arguments = { { "path", root.string() }, { "query", "FORALL" } }
    };
    const auto contents = search.execute(request);
    check(contents.success, "offline content search failed");
    check(contents.message.find("example.cpp:1") != std::string::npos,
        "offline search did not find the source line");
    request.arguments["query"] = "longest-possible-ending";
    check(search.execute(request).message.find("[filename]") != std::string::npos,
        "unusual extension was not discoverable by filename");
    request.arguments["query"] = "binary-ish";
    check(search.execute(request).message.find("unusual.the-longest-possible-ending:1")
        != std::string::npos, "unusual extension text was skipped");
    request.arguments["query"] = "Agent loop";
    const auto broadText = search.execute(request).message;
    check(broadText.find("extensionless:1") != std::string::npos,
        "extensionless text was skipped");
    check(broadText.find("opaque.data") == std::string::npos,
        "binary contents were treated as plain text");
    request.arguments["query"] = "Agent loop";
    const auto spacedTerm = search.execute(request).message;
    check(spacedTerm.find("AgentLoop.cpp [filename, separator-folded]")
        != std::string::npos
        && spacedTerm.find("AgentLoop.cpp:1") != std::string::npos,
        "spaced search term did not find the CamelCase code name");
    check(search.execute(request).responseMode
        == rose::tools::ToolResponseMode::AuthoritativeCompletion,
        "offline search result must reach the user without model speculation");

    rose::tools::ToolRegistry registry;
    registry.registerTool(std::make_unique<rose::tools::SearchLocalFilesTool>());
    const auto exact = rose::agent::CapabilityRoutingGuard::explicitOfflineSearchRequest(
        R"(Search for "Agent loop" in "C:\Users\chris\OneDrive\Desktop\Rose")",
        registry);
    check(exact.has_value() && exact->toolId == "search_local_files",
        "explicit offline search did not route to the local tool");
    check(exact->arguments.at("path") == R"(C:\Users\chris\OneDrive\Desktop\Rose)"
        && exact->arguments.at("query") == "Agent loop",
        "offline search changed the exact user-supplied folder or term");
    const std::string_view completed[] = { "search_local_files" };
    check(!rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
        R"(Search for "Agent loop" in "C:\Users\chris\OneDrive\Desktop\Rose")",
        registry, completed).has_value(),
        "offline search should not repeat after completion");

    auto pdfReader = std::make_unique<FakePdfReader>();
    FakePdfReader& fake = *pdfReader;
    rose::tools::ToolRegistry documentRegistry;
    documentRegistry.registerTool(std::move(pdfReader));
    documentRegistry.registerTool(std::make_unique<rose::tools::ReadNamedPdfsTool>(fake));
    const std::string namedPdfPrompt =
        R"(Analyze these three files: "C:\Legal\AO (26 pgs).pdf", "C:\Legal\Navy BCNR\APPLICATION.pdf" and "C:\Desktop\Decision (7 pgs).pdf". Do not scan directories.)";
    const auto named = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        namedPdfPrompt, documentRegistry);
    check(named && named->toolId == "read_named_pdfs"
        && named->arguments.at("path1") == R"(C:\Legal\AO (26 pgs).pdf)"
        && named->arguments.at("path2") == R"(C:\Legal\Navy BCNR\APPLICATION.pdf)"
        && named->arguments.at("path3") == R"(C:\Desktop\Decision (7 pgs).pdf)",
        "explicit named PDFs did not retain their exact file paths");
    const auto batch = documentRegistry.execute(*named);
    check(batch.success && fake.readPaths.size() == 3
        && batch.message.find("Read all 3 named PDFs") != std::string::npos,
        "named PDF batch failed to read all exact files");
    check(fake.readRequests.size() == 3
        && fake.readRequests[0].arguments.at("instruction").size() < 300
        && fake.readRequests[0].arguments.at("instruction").find("C:\\Legal")
            == std::string::npos,
        "named PDF batch repeated the full user request into each PDF read");
    auto missingExtension = *named;
    missingExtension.arguments["instruction"] +=
        R"( and "C:\Legal\DoDI Referral".)";
    const auto omitted = documentRegistry.execute(missingExtension);
    check(!omitted.success && omitted.message.find("DoDI Referral: omitted")
        != std::string::npos,
        "a quoted PDF path without its extension must not disappear silently");
    fake.readPaths.clear();
    const auto sixNamed = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        R"(Analyze all six PDFs: "C:\Legal\A.pdf", "C:\Legal\B.pdf", "C:\Legal\C.pdf", "C:\Legal\D.pdf", "C:\Legal\E.pdf", and "C:\Legal\F.pdf".)",
        documentRegistry);
    check(sixNamed && sixNamed->toolId == "read_named_pdfs"
        && sixNamed->arguments.at("path6") == R"(C:\Legal\F.pdf)",
        "six exact PDFs must route as one named batch");
    const auto sixResult = documentRegistry.execute(*sixNamed);
    check(sixResult.success && fake.readPaths.size() == 6
        && sixResult.message.find("Read all 6 named PDFs") != std::string::npos,
        "six-file batch must read each requested PDF");
    BatchSummaryProvider batchProvider;
    rose::tools::ReadNamedPdfsTool largeBatch{ fake, &batchProvider };
    rose::tools::ToolRequest many{ .toolId = "read_named_pdfs" };
    std::string manyPrompt = "Review these PDFs: ";
    for (int i = 1; i <= 12; ++i)
    {
        const std::string path = "C:\\Legal\\File" + std::to_string(i) + ".pdf";
        many.arguments.emplace("path" + std::to_string(i), path);
        manyPrompt += "\"" + path + "\" ";
    }
    check(largeBatch.descriptor().parameters.size() >= 72,
        "large batch must advertise all exact path and page arguments");
    const auto routedMany = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        manyPrompt, documentRegistry);
    check(routedMany && routedMany->arguments.at("path12") == many.arguments.at("path12"),
        "large named PDF request lost an exact path");
    fake.longResult = true;
    const auto manyResult = largeBatch.execute(many);
    check(manyResult.success && batchProvider.calls > 0
        && manyResult.message.find("Condensed cross-file evidence") != std::string::npos
        && manyResult.message.find("File12.pdf") != std::string::npos,
        "large named PDF batch must retain coverage and reduce evidence");
    batchProvider.fail = true;
    const auto reducerFailure = largeBatch.execute(many);
    check(!reducerFailure.success
        && reducerFailure.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis
        && reducerFailure.message.find("model reducer unavailable") != std::string::npos
        && reducerFailure.message.find("File12.pdf") != std::string::npos
        && reducerFailure.message.find("Preview only") != std::string::npos,
        "reducer failure must preserve scoped previews of completed reads");
    batchProvider.fail = false;
    fake.longResult = false;
    fake.readPaths.clear();
    fake.failingPath = R"(C:\Legal\Navy BCNR\APPLICATION.pdf)";
    const auto incomplete = documentRegistry.execute(*named);
    check(!incomplete.success
        && incomplete.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis
        && incomplete.message.find("OCR unavailable") != std::string::npos
        && incomplete.message.find("partial review") != std::string::npos
        && incomplete.message.find("C:\\Legal\\AO (26 pgs).pdf") != std::string::npos,
        "failed named PDF read must retain the successfully read evidence");
    fake.failingPath.clear();
    fake.partialPath = R"(C:\Legal\Navy BCNR\APPLICATION.pdf)";
    const auto bounded = documentRegistry.execute(*named);
    check(!bounded.success
        && bounded.responseMode == rose::tools::ToolResponseMode::RequiresModelSynthesis
        && bounded.message.find("complete review is unavailable")
            != std::string::npos,
        "bounded OCR result must not become a full legal analysis");
    const auto recovered = rose::agent::CapabilityRoutingGuard::recoverDirectToolRequest(
        namedPdfPrompt, documentRegistry, {});
    check(recovered && recovered->toolId == "read_named_pdfs",
        "multi-PDF recovery must not analyze a parent directory");

    // A page-window instruction immediately before one named PDF must become
    // structured tool arguments for that file only. This reproduces the legal
    // review request that asked Rose to read two complete PDFs plus only the
    // first 25 pages of a much larger third PDF.
    const std::string rangedPdfPrompt =
        "Read and analyze these PDFs in entirety: \"C:\\Legal\\Decision.pdf\"\n"
        "\"C:\\Legal\\AO.pdf\"\n"
        "Read the first 25 pages of this file and assume quoted evidence is present: \"C:\\Legal\\Application.pdf\"";
    const auto ranged = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        rangedPdfPrompt, documentRegistry);
    check(ranged && ranged->toolId == "read_named_pdfs"
        && ranged->arguments.at("path3_page_start") == "1"
        && ranged->arguments.at("path3_page_count") == "25"
        && !ranged->arguments.contains("path1_page_count")
        && !ranged->arguments.contains("path2_page_count"),
        "per-file PDF page-window routing did not preserve first-25-page intent");

    fake.readRequests.clear();
    const auto rangedBatch = documentRegistry.execute(*ranged);
    check(rangedBatch.success && fake.readRequests.size() == 3
        && fake.readRequests[2].arguments.at("page_start") == "1"
        && fake.readRequests[2].arguments.at("page_count") == "25"
        && !fake.readRequests[0].arguments.contains("page_count")
        && !fake.readRequests[1].arguments.contains("page_count"),
        "read_named_pdfs did not forward the page window to only the selected PDF");

    const auto singleRanged = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        R"(Review the first 25 pages of "C:\Legal\Large Application.pdf".)",
        documentRegistry);
    check(singleRanged && singleRanged->toolId == "read_pdf"
        && singleRanged->arguments.at("page_start") == "1"
        && singleRanged->arguments.at("page_count") == "25",
        "single named PDF first-N-page intent did not become a bounded read window");

    const auto explicitRange = rose::agent::CapabilityRoutingGuard::explicitNamedPdfRequest(
        R"(Review "C:\Legal\Large Application.pdf", pages 20-44.)",
        documentRegistry);
    check(explicitRange && explicitRange->toolId == "read_pdf"
        && explicitRange->arguments.at("page_start") == "20"
        && explicitRange->arguments.at("page_count") == "25",
        "single named PDF explicit page range did not become a bounded read window");

    rose::tools::ToolRequest confirmedScan{
        .toolId = "scan_directory_tree",
        .arguments = { { "path", root.string() } }
    };
    rose::tools::ToolDescriptor readDescriptor{
        .id = "scan_directory_tree",
        .displayName = "Scan Directory Tree",
        .description = "Test descriptor",
        .risk = rose::tools::ToolRisk::ReadOnly,
        .consent = rose::tools::ToolConsent::RequiresConfirmation,
        .parameters = {}
    };
    const auto scope = rose::agent::approvedReadScope(confirmedScan, readDescriptor);
    check(scope.has_value() && scope->directory, "confirmed scope missing");

    readDescriptor.id = "read_text_file";
    rose::tools::ToolRequest inside{
        .toolId = "read_text_file",
        .arguments = { { "path", (root / "nested" / "example.cpp").string() } }
    };
    check(rose::agent::coveredByReadScope(*scope, inside, readDescriptor),
        "nested read was not covered");
    inside.arguments["path"] = (root / ".." / outside.filename()).string();
    check(!rose::agent::coveredByReadScope(*scope, inside, readDescriptor),
        "escaped path was incorrectly covered");

    rose::permissions::ToolExecutionPolicy policy;
    check(policy.evaluate(readDescriptor,
        rose::permissions::ToolConfirmationState::ScopedReadApproved).allowed(),
        "scoped read rejected by policy");
    readDescriptor.risk = rose::tools::ToolRisk::LocalWrite;
    check(!policy.evaluate(readDescriptor,
        rose::permissions::ToolConfirmationState::ScopedReadApproved).allowed(),
        "scoped approval allowed a write");

    const auto math = rose::ui::makeReadableChatText(
        R"(\forall x \in A, \exists y: x \leq y \land y \neq 0)");
    check(math.find("∀") != std::string::npos, "forall conversion missing");
    check(math.find("∃") != std::string::npos, "exists conversion missing");
    check(math.find("∈") != std::string::npos, "membership conversion missing");

    rose::tools::ToolRequest collapsed{
        .toolId = "read_text_file",
        .arguments = { { "path", (root / "nested" / "example.cpp").string() },
                       { "start_line", "820,line_count=1000" } }
    };
    check(rose::agent::normalizeReadWindowArguments(collapsed),
        "compressed read arguments were rejected");
    check(collapsed.arguments.at("start_line") == "820", "wrong start line");
    check(collapsed.arguments.at("line_count") == "200", "range not bounded");

    std::cout << "Rose offline search, named PDFs, scoped approval, math: PASS\n";
}
