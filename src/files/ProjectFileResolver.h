#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rose::files
{
    enum class ProjectFileResolutionStatus
    {
        Unique,
        Ambiguous,
        NotFound,
        SearchLimitReached
    };


    struct ProjectFileResolution
    {
        std::string requestedReference;
        ProjectFileResolutionStatus status{
            ProjectFileResolutionStatus::NotFound
        };

        // Populated only for an unambiguous match. Ambiguous candidate paths stay
        // private to the resolver so the control model cannot silently choose one.
        std::string absolutePath;

        std::size_t matchCount{ 0 };
        std::size_t scannedEntries{ 0 };
    };


    struct ProjectFileResolverConfig
    {
        // Recursive fallback is bounded so a broad approved root cannot stall the
        // conversation worker indefinitely. Direct root/reference matches are
        // checked before recursive enumeration and do not consume this budget.
        std::size_t maximumRecursiveEntries{ 50'000 };
        std::size_t maximumMatchesPerReference{ 8 };
    };


    // Resolve user-supplied relative/bare file references ONLY beneath the active
    // Project's explicitly approved roots.
    //
    // This is discovery, not authority: resolving a path never grants a file-read
    // permission and never bypasses ToolExecutionPolicy confirmation.
    class ProjectFileResolver final
    {
    public:
        explicit ProjectFileResolver(
            ProjectFileResolverConfig config = {});

        [[nodiscard]]
        std::vector<ProjectFileResolution> resolve(
            std::string_view userText,
            const std::vector<std::string>& approvedRoots) const;

        [[nodiscard]]
        static std::string buildTransientContext(
            const std::vector<ProjectFileResolution>& resolutions);

    private:
        ProjectFileResolverConfig config_;
    };

} // namespace rose::files
