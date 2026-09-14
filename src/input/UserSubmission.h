#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rose::input
{

    // One file the user explicitly attached to the current submission.
    //
    // IMPORTANT:
    // The path is data, not a standing filesystem permission. The worker grants
    // a one-shot read permission only while it processes this submission.
    struct FileAttachment
    {
        std::filesystem::path path;
        std::string displayName;
    };


    // UI -> worker message payload.
    //
    // Keeping attachments beside the user's text prevents us from smuggling file
    // paths or file contents into the canonical chat string just to cross threads.
    struct UserSubmission
    {
        std::string text;
        std::vector<FileAttachment> attachments;
    };

} // namespace rose::input
