#include "documents/PdfiumRuntime.h"

#if defined(ROSE_HAS_PDFIUM)
#include <fpdfview.h>
#endif

#include <cstddef>
#include <mutex>

namespace rose::documents
{
    namespace
    {
#if defined(ROSE_HAS_PDFIUM)
        std::mutex gPdfiumMutex;
        std::size_t gPdfiumLeaseCount{ 0 };
#endif
    }

    void acquirePdfiumRuntime()
    {
#if defined(ROSE_HAS_PDFIUM)
        const std::scoped_lock lock{ gPdfiumMutex };
        if (gPdfiumLeaseCount == 0)
        {
            FPDF_InitLibrary();
        }
        ++gPdfiumLeaseCount;
#endif
    }

    void releasePdfiumRuntime() noexcept
    {
#if defined(ROSE_HAS_PDFIUM)
        const std::scoped_lock lock{ gPdfiumMutex };
        if (gPdfiumLeaseCount == 0)
        {
            return;
        }

        --gPdfiumLeaseCount;
        if (gPdfiumLeaseCount == 0)
        {
            FPDF_DestroyLibrary();
        }
#endif
    }

    PdfiumRuntimeLease::PdfiumRuntimeLease()
    {
        acquirePdfiumRuntime();
    }

    PdfiumRuntimeLease::~PdfiumRuntimeLease()
    {
        releasePdfiumRuntime();
    }
}
