#pragma once

#include "jobs/JobTypes.h"

namespace rose::jobs
{
    class IJobStore
    {
    public:
        virtual ~IJobStore() = default;

        [[nodiscard]] virtual JobSnapshot load() = 0;
        virtual void save(const JobSnapshot& snapshot) = 0;
    };
}
