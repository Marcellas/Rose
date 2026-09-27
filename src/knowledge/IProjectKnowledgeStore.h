#pragma once

#include "knowledge/ProjectKnowledgeTypes.h"

namespace rose::knowledge
{
    class IProjectKnowledgeStore
    {
    public:
        virtual ~IProjectKnowledgeStore() = default;

        [[nodiscard]]
        virtual ProjectKnowledgeSnapshot load() = 0;

        virtual void save(
            const ProjectKnowledgeSnapshot& snapshot) = 0;
    };
}
