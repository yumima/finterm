#pragma once

#ifdef HAS_QT_MULTIMEDIA

#    include <QImage>
#    include <QObject>
#    include <QOpenGLBuffer>
#    include <QOpenGLFunctions>
#    include <QOpenGLShaderProgram>
#    include <QOpenGLVertexArrayObject>
#    include <QSize>
#    include <QVideoFrame>

#    include <memory>

class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOffscreenSurface;

namespace fincept::screens::widgets {

/// GPU-side YUV→RGB conversion + scaling for NV12 video frames.
///
/// Owns a private QOffscreenSurface + QOpenGLContext — a pure GPU compute
/// resource with no visible wl_surface, which is what makes this Wayland-safe
/// (vs QVideoWidget / QOpenGLWidget, both of which create wl_subsurfaces that
/// Mutter promotes to separate xdg_toplevel windows — see commit history,
/// `df14b142` revert).
///
/// process() runs a fragment-shader pass that:
///   • samples the NV12 Y plane (R8) and UV plane (RG8) as two textures,
///   • applies BT.709 YUV→RGB conversion,
///   • renders to an FBO sized to the target widget (with letterbox aspect),
///   • reads back the small RGB result and returns a QImage to the caller.
///
/// The expensive work (~25-40 ms/frame on CPU for `QImage::scaled` at 1080p)
/// becomes ~1 ms on GPU. The remaining cost is the GL upload of the source
/// NV12 (~5-8 ms at 1080p over PCIe) plus a small readback (~1-2 ms at widget
/// size). Net ~10-15 ms/frame end to end — comfortably inside a 30 fps budget.
///
/// Fast path is NV12 only (the format Qt6 FFmpeg backend produces from NVDEC
/// on Linux). For any other QVideoFrameFormat, process() returns a null QImage
/// and the caller falls back to its existing CPU path (QVideoFrame::toImage +
/// QImage::scaled). Same fallback on any GL error — never a crash, just slower.
///
/// Threading: the context lives on whichever thread constructs the scaler;
/// process() must run on that same thread. In our case both the construction
/// and the QVideoSink::videoFrameChanged → VideoRenderWidget::present()
/// dispatch happen on the main thread, so no cross-thread context migration
/// is required.
class OffscreenVideoScaler {
  public:
    OffscreenVideoScaler();
    ~OffscreenVideoScaler();

    OffscreenVideoScaler(const OffscreenVideoScaler&)            = delete;
    OffscreenVideoScaler& operator=(const OffscreenVideoScaler&) = delete;

    /// True once the GL context, shaders, and textures are all set up.
    /// False on first use (lazy init) and on any unrecoverable failure.
    bool is_valid() const { return gl_ok_; }

    /// Process one NV12 frame.
    ///
    /// Returns the converted+scaled QImage (RGB888) sized to letterbox into
    /// `target_logical_size * dpr`. The returned QImage carries the matching
    /// devicePixelRatio so the caller can blit it 1:1 in device pixels at
    /// the logical origin.
    ///
    /// Returns a null QImage on any failure — non-NV12 frame, make-current
    /// failure, FBO incomplete, glGetError() non-zero. The caller is expected
    /// to fall back to its CPU path on null.
    QImage process(const QVideoFrame& frame, const QSize& target_logical_size, qreal dpr);

  private:
    /// Lazy initialisation on first process(). Returns true if the context
    /// + shaders + textures are ready. Sets `gl_ok_` and a flag preventing
    /// re-attempts after a failure.
    bool ensure_initialized();

    /// Allocate / resize the FBO + colour texture to fit `out_device_size`.
    bool ensure_fbo(QSize out_device_size);

    /// Compile and link the YUV→RGB shader program. Called once during init.
    bool build_shader();

    bool init_attempted_   = false;
    bool gl_ok_            = false;
    bool gl_err_warned_    = false;  ///< latch: one-shot warn on glGetError, then quiet

    // Diagnostic counters: which pixel format the sink delivers (so we know
    // whether the NV12 fast path even engages on this build) and whether
    // hwaccel handles are exposed (so we know whether zero-copy / Depth B
    // is reachable). All counts increment unconditionally — the format
    // gate happens *after* counting. First frame logs the format string
    // immediately; full summary at frames_seen_ >= 30.
    int  frames_seen_         = 0;
    int  frames_nv12_         = 0;
    int  frames_with_handle_  = 0;
    bool diag_logged_         = false;

    QOffscreenSurface*  surface_ = nullptr;
    QOpenGLContext*     context_ = nullptr;

    std::unique_ptr<QOpenGLFramebufferObject> fbo_;
    QSize fbo_device_size_;

    QOpenGLShaderProgram        program_;
    QOpenGLBuffer               vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject    vao_;

    unsigned int y_tex_  = 0;  ///< GL_R8  — luma plane, full source resolution
    unsigned int uv_tex_ = 0;  ///< GL_RG8 — chroma plane, half resolution (NV12)

    // Tracked separately from FBO size: source dimensions drive these and
    // can differ (resolution changes during a stream). When they don't
    // change frame-to-frame we use glTexSubImage2D instead of a full
    // glTexImage2D reallocation.
    QSize y_tex_size_;
    QSize uv_tex_size_;

    int loc_u_y_  = -1;
    int loc_u_uv_ = -1;
};

} // namespace fincept::screens::widgets

#endif // HAS_QT_MULTIMEDIA
