#pragma once

#include <memory>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/BitmapFrameSource.h"
#include "MotionStudio/export/PagExporter.h"

#include "pag/file.h"

namespace motion {
namespace pag_export {

// Owns a VideoToolbox compression session + encode pixel buffer for reuse across
// multiple EncodeVideoSequence calls when packed size / quality / fps match.
// Lifetime should span one PagExporter::Export (held by PagFileBuilder).
class PagVideoEncodeSession {
  public:
    PagVideoEncodeSession();
    ~PagVideoEncodeSession();

    PagVideoEncodeSession(const PagVideoEncodeSession &) = delete;
    PagVideoEncodeSession &operator=(const PagVideoEncodeSession &) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    friend Expected<void, PagExportError> EncodeVideoSequence(
        BitmapFrameSource *frameSource, pag::VideoSequence *sequence, FrameTime start,
        FrameTime end, int width, int height, int keyFrameInterval, int imageQuality,
        const volatile int *cancelFlag, PagVideoEncodeSession *encodeSession);
};

// Renders [start, end) via FrameSource, packs RGB|Alpha side-by-side, encodes H.264
// (VideoToolbox on Apple) into sequence->frames / headers. Sets alphaStartX=width,
// alphaStartY=0. sequence->width/height should already be the logical size.
// Calls frameSource->finish() before returning (success or failure).
// cancelFlag non-null and non-zero aborts between frames with message "cancelled".
// encodeSession non-null reuses VT resources across calls when params match; on
// failure/cancel the session is invalidated. nullptr uses a one-shot local session.
Expected<void, PagExportError> EncodeVideoSequence(BitmapFrameSource *frameSource,
                                                   pag::VideoSequence *sequence, FrameTime start,
                                                   FrameTime end, int width, int height,
                                                   int keyFrameInterval, int imageQuality,
                                                   const volatile int *cancelFlag = nullptr,
                                                   PagVideoEncodeSession *encodeSession = nullptr);

}  // namespace pag_export
}  // namespace motion
