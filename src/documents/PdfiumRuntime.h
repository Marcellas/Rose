#pragma once

namespace rose::documents
{
    // PDFium exposes process-global initialization. Rose has multiple independent
    // consumers (reader/OCR and mutation), so keep a tiny reference-counted lease
    // instead of letting each subsystem call Init/Destroy independently.
    void acquirePdfiumRuntime();
    void releasePdfiumRuntime() noexcept;

    class PdfiumRuntimeLease final
    {
    public:
        PdfiumRuntimeLease();
        ~PdfiumRuntimeLease();

        PdfiumRuntimeLease(const PdfiumRuntimeLease&) = delete;
        PdfiumRuntimeLease& operator=(const PdfiumRuntimeLease&) = delete;
        PdfiumRuntimeLease(PdfiumRuntimeLease&&) = delete;
        PdfiumRuntimeLease& operator=(PdfiumRuntimeLease&&) = delete;
    };
}
