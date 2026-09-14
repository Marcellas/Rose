#pragma once

#include <filesystem>
#include <string>
#include <optional>

namespace rose::artifacts
{

    enum class ArtifactKind
    {
        File,
        Image,
        Document,
        Code,
        Archive,
        Other
    };


    // A lightweight, copyable description of a file Rose has produced.
    //
    // Artifact does NOT own file bytes. The file's lifetime is provided by the
    // backing artifact store. Keeping this type value-only makes it safe to move
    // through ChatBridge between the worker and UI threads.
    struct Artifact
    {
        std::filesystem::path path;
        std::string displayName;
        std::string mediaType;
        ArtifactKind kind{ ArtifactKind::File };

        // Optional local provenance/sidecar file. The UI does not need to read it
        // to display the artifact, but later memory/indexing code can.
        std::optional<std::filesystem::path> metadataPath;
    };

} // namespace rose::artifacts
