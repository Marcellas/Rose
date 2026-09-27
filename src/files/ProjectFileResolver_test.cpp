#include "files/ProjectFileResolver.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    void writeFile(const std::filesystem::path& path)
    {
        std::ofstream file{ path, std::ios::binary | std::ios::trunc };
        file << "test";
    }
}

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / "rose_project_file_resolver_test";

    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root / "nested", error);
    require(!error, "test directory creation should succeed");

    writeFile(root / "Dunamis.docx");
    writeFile(root / "nested" / "Billing.xlsx");

    rose::files::ProjectFileResolver resolver;
    const std::vector<std::string> roots{ root.string() };

    const auto direct = resolver.resolve(
        "Read Dunamis.docx and summarize it.",
        roots);

    require(direct.size() == 1, "one bare filename should be detected");
    require(
        direct.front().status == rose::files::ProjectFileResolutionStatus::Unique,
        "direct approved-root filename should resolve uniquely");
    require(
        std::filesystem::path{ direct.front().absolutePath }.filename()
            == "Dunamis.docx",
        "resolved direct filename should be preserved");

    const auto recursive = resolver.resolve(
        "Review Billing.xlsx.",
        roots);
    require(recursive.size() == 1, "recursive filename should be detected");
    require(
        recursive.front().status == rose::files::ProjectFileResolutionStatus::Unique,
        "nested filename should resolve recursively beneath an approved root");

    writeFile(root / "Billing.xlsx");
    const auto preferredDirect = resolver.resolve(
        "Review Billing.xlsx.",
        roots);
    require(
        preferredDirect.front().status == rose::files::ProjectFileResolutionStatus::Unique,
        "a direct approved-root match should win before recursive ambiguity");
    require(
        std::filesystem::path{ preferredDirect.front().absolutePath }.parent_path()
            == std::filesystem::weakly_canonical(root),
        "direct root match should be preferred");

    std::filesystem::remove(root / "Billing.xlsx", error);
    std::filesystem::create_directories(root / "other", error);
    writeFile(root / "other" / "Billing.xlsx");

    const auto ambiguous = resolver.resolve(
        "Review Billing.xlsx.",
        roots);
    require(
        ambiguous.front().status == rose::files::ProjectFileResolutionStatus::Ambiguous,
        "multiple recursive matches should be reported as ambiguous");
    require(
        ambiguous.front().absolutePath.empty(),
        "ambiguous resolution must not expose one candidate as authoritative");

    const auto quoted = resolver.resolve(
        "Read \"Missing Design.docx\" and summarize it.",
        roots);
    require(quoted.size() == 1, "quoted filenames with spaces should be detected");
    require(
        quoted.front().status == rose::files::ProjectFileResolutionStatus::NotFound,
        "missing quoted filename should report not_found");

    const std::string context =
        rose::files::ProjectFileResolver::buildTransientContext(direct);
    require(
        context.find("status=unique") != std::string::npos
            && context.find("absolute_path=") != std::string::npos,
        "unique transient context should expose the grounded absolute path");

    std::filesystem::remove_all(root, error);

    std::cout << "Rose ProjectFileResolver tests: PASS\n";
    return 0;
}
