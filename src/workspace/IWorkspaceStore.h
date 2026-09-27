#pragma once

#include "workspace/WorkspaceTypes.h"

namespace rose::workspace
{

    // Persistence boundary for Project/Discussion metadata.
    //
    // WorkspaceRepository owns behavior and validation. A store only loads or
    // saves a complete snapshot, which keeps the repository independent from the
    // initial binary-file format and leaves a clean path to SQLite later.
    class IWorkspaceStore
    {
    public:
        virtual ~IWorkspaceStore() = default;

        [[nodiscard]]
        virtual WorkspaceSnapshot load() = 0;

        virtual void save(
            const WorkspaceSnapshot& snapshot) = 0;
    };

} // namespace rose::workspace
