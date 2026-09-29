#include "agent/ScopedReadApproval.h"
#include "agent/ReadWindowArguments.h"
#include "agent/CapabilityRoutingGuard.h"
#include "permissions/ToolExecutionPolicy.h"
#include "tools/SearchLocalFilesTool.h"
#include "tools/ToolRegistry.h"
#include "ui/TextPresentation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    void check(const bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error{ message };
    }
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
    { std::ofstream(root / "unusual.the-longest-possible-ending") << "binary-ish"; }
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

    std::cout << "Rose offline search, scoped approval, math: PASS\n";
}
