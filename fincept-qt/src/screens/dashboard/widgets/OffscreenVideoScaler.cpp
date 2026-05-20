#include "screens/dashboard/widgets/OffscreenVideoScaler.h"

#ifdef HAS_QT_MULTIMEDIA

#    include "core/logging/Logger.h"

#    include <QOffscreenSurface>
#    include <QOpenGLContext>
#    include <QOpenGLFramebufferObject>
#    include <QOpenGLFramebufferObjectFormat>
#    include <QSurfaceFormat>
#    include <QVideoFrameFormat>

namespace fincept::screens::widgets {

namespace {

// Fullscreen quad in clip space. UV uses the natural mapping (UV.y tracks
// clip.y), which intentionally renders the source upside-down to the FBO.
// glReadPixels then reads the FBO bottom-up into QImage's top-down memory,
// undoing the flip exactly. Net effect: QImage scanline 0 == source TOP.
// No CPU mirror needed.
constexpr float kQuadVerts[] = {
    // pos.xy        // uv.xy (natural — flip happens via readback orientation)
    -1.f, -1.f,      0.f, 0.f,
     1.f, -1.f,      1.f, 0.f,
    -1.f,  1.f,      0.f, 1.f,
     1.f,  1.f,      1.f, 1.f,
};

constexpr const char* kVertShader = R"(
#version 150 core
in vec2 a_pos;
in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// BT.709 full-range YUV→RGB. Most live HLS streams from Bloomberg / CNBC /
// Yahoo flag BT.709; if a stream is BT.601 colors will be slightly off but
// not visibly wrong on a news ticker. Worth revisiting if any stream looks
// noticeably off (would dispatch by frame.surfaceFormat().yCbCrColorSpace()).
constexpr const char* kFragShader = R"(
#version 150 core
in vec2 v_uv;
out vec4 frag_color;
uniform sampler2D u_y;
uniform sampler2D u_uv;
void main() {
    float y = texture(u_y, v_uv).r;
    vec2 uv = texture(u_uv, v_uv).rg - vec2(0.5);
    vec3 rgb = vec3(
        y + 1.5748 * uv.y,
        y - 0.1873 * uv.x - 0.4681 * uv.y,
        y + 1.8556 * uv.x
    );
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

QSize aspect_fit(QSize source, QSize bound) {
    if (source.isEmpty() || bound.isEmpty()) return {};
    const double sa = double(source.width()) / source.height();
    const double ba = double(bound.width())  / bound.height();
    if (sa > ba) {
        // wider than bound — fit width
        return QSize(bound.width(), int(bound.width() / sa));
    } else {
        return QSize(int(bound.height() * sa), bound.height());
    }
}

} // namespace

OffscreenVideoScaler::OffscreenVideoScaler() = default;

OffscreenVideoScaler::~OffscreenVideoScaler() {
    if (context_ && context_->isValid() && surface_) {
        context_->makeCurrent(surface_);
        // RAII for VBO / VAO / program; explicit only for raw GL handles.
        auto* f = context_->functions();
        if (y_tex_)  f->glDeleteTextures(1, &y_tex_);
        if (uv_tex_) f->glDeleteTextures(1, &uv_tex_);
        fbo_.reset();
        program_.removeAllShaders();
        vbo_.destroy();
        vao_.destroy();
        context_->doneCurrent();
    }
    delete context_;
    delete surface_;
}

bool OffscreenVideoScaler::ensure_initialized() {
    if (init_attempted_) return gl_ok_;
    init_attempted_ = true;

    // Match the global default share context's format so resource sharing
    // with the rest of the app's GL works if we ever need it. Core 3.2 is
    // the minimum that supports GLSL 150 and GL_RG8 / single-channel
    // floating textures we use for NV12 planes.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    if (fmt.majorVersion() < 3 || (fmt.majorVersion() == 3 && fmt.minorVersion() < 2)) {
        fmt.setVersion(3, 2);
    }

    surface_ = new QOffscreenSurface();
    surface_->setFormat(fmt);
    surface_->create();
    if (!surface_->isValid()) {
        LOG_ERROR("VideoScaler", "QOffscreenSurface::create failed; falling back to CPU path");
        return false;
    }

    context_ = new QOpenGLContext();
    context_->setFormat(fmt);
    // Share with any existing global context so we could swap textures with
    // the rest of the app's GL state in the future. Safe even if there
    // isn't one — globalShareContext() returns nullptr and we get a fresh
    // unshared context.
    if (auto* share = QOpenGLContext::globalShareContext())
        context_->setShareContext(share);
    if (!context_->create()) {
        LOG_ERROR("VideoScaler", "QOpenGLContext::create failed; falling back to CPU path");
        return false;
    }

    if (!context_->makeCurrent(surface_)) {
        LOG_ERROR("VideoScaler", "makeCurrent on offscreen surface failed; falling back to CPU path");
        return false;
    }

    auto* f = context_->functions();

    if (!build_shader()) {
        context_->doneCurrent();
        return false;
    }

    // Quad geometry — 4 vertices, interleaved pos.xy + uv.xy.
    if (!vao_.create()) {
        LOG_ERROR("VideoScaler", "QOpenGLVertexArrayObject::create failed");
        context_->doneCurrent();
        return false;
    }
    vao_.bind();

    vbo_.create();
    vbo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    vbo_.bind();
    vbo_.allocate(kQuadVerts, sizeof(kQuadVerts));

    const int stride = 4 * sizeof(float);
    program_.bind();
    program_.enableAttributeArray("a_pos");
    program_.setAttributeBuffer("a_pos", GL_FLOAT, 0,            2, stride);
    program_.enableAttributeArray("a_uv");
    program_.setAttributeBuffer("a_uv",  GL_FLOAT, 2 * sizeof(float), 2, stride);
    program_.release();

    vbo_.release();
    vao_.release();

    // Y + UV plane textures. Sizes are set on first upload (texImage2D);
    // here we just allocate handles and pick non-mipped linear filtering.
    f->glGenTextures(1, &y_tex_);
    f->glBindTexture(GL_TEXTURE_2D, y_tex_);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    f->glGenTextures(1, &uv_tex_);
    f->glBindTexture(GL_TEXTURE_2D, uv_tex_);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    loc_u_y_  = program_.uniformLocation("u_y");
    loc_u_uv_ = program_.uniformLocation("u_uv");

    context_->doneCurrent();
    gl_ok_ = true;
    LOG_INFO("VideoScaler", "offscreen GL scaler ready (NV12 fast path)");
    return true;
}

bool OffscreenVideoScaler::build_shader() {
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertShader)) {
        LOG_ERROR("VideoScaler", "vertex shader compile failed: " + program_.log());
        return false;
    }
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragShader)) {
        LOG_ERROR("VideoScaler", "fragment shader compile failed: " + program_.log());
        return false;
    }
    if (!program_.link()) {
        LOG_ERROR("VideoScaler", "shader link failed: " + program_.log());
        return false;
    }
    return true;
}

bool OffscreenVideoScaler::ensure_fbo(QSize out_device_size) {
    if (out_device_size.isEmpty()) return false;
    if (fbo_ && fbo_device_size_ == out_device_size) return true;

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setInternalTextureFormat(GL_RGB8);
    // No depth / stencil — we render a single 2D quad with no Z testing.
    fbo_.reset(new QOpenGLFramebufferObject(out_device_size, fmt));
    if (!fbo_->isValid()) {
        LOG_ERROR("VideoScaler", "FBO allocation incomplete");
        fbo_.reset();
        return false;
    }
    fbo_device_size_ = out_device_size;
    return true;
}

QImage OffscreenVideoScaler::process(const QVideoFrame& frame_in,
                                      const QSize& target_logical_size,
                                      qreal dpr) {
    if (!ensure_initialized()) return {};
    if (!frame_in.isValid() || target_logical_size.isEmpty()) return {};

    // Fast path is NV12 only. Anything else, return null and let the caller
    // fall back to QVideoFrame::toImage() + QImage::scaled() on CPU.
    if (frame_in.pixelFormat() != QVideoFrameFormat::Format_NV12) {
        return {};
    }

    // QVideoFrame::map is non-const — work on a copy.
    QVideoFrame f = frame_in;
    if (!f.map(QVideoFrame::ReadOnly)) return {};

    const int src_w = f.width();
    const int src_h = f.height();
    if (src_w <= 0 || src_h <= 0 || (src_w & 1) || (src_h & 1)) {
        // NV12 requires even dimensions for the half-size UV plane.
        f.unmap();
        return {};
    }

    if (!context_->makeCurrent(surface_)) {
        f.unmap();
        return {};
    }

    auto* fns = context_->functions();

    // Target FBO size: source frame letterboxed into the widget's device-pixel
    // rect. Matches how the caller's paintEvent expects scaled_image_ to be
    // sized — the surrounding letterbox is filled black at paint time.
    const QSize bound_device(qMax(1, int(target_logical_size.width()  * dpr)),
                             qMax(1, int(target_logical_size.height() * dpr)));
    const QSize out_device = aspect_fit(QSize(src_w, src_h), bound_device);
    if (out_device.isEmpty() || !ensure_fbo(out_device)) {
        context_->doneCurrent();
        f.unmap();
        return {};
    }

    // Upload Y plane (R8, full resolution). Allocate-then-update so the
    // per-frame cost is just the texel transfer, not a full reallocation.
    fns->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    fns->glPixelStorei(GL_UNPACK_ROW_LENGTH, f.bytesPerLine(0));
    fns->glBindTexture(GL_TEXTURE_2D, y_tex_);
    if (y_tex_size_ != QSize(src_w, src_h)) {
        fns->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, src_w, src_h, 0,
                          GL_RED, GL_UNSIGNED_BYTE, f.bits(0));
        y_tex_size_ = QSize(src_w, src_h);
    } else {
        fns->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h,
                             GL_RED, GL_UNSIGNED_BYTE, f.bits(0));
    }

    // Upload UV plane (RG8, half resolution — interleaved U,V).
    fns->glPixelStorei(GL_UNPACK_ROW_LENGTH, f.bytesPerLine(1) / 2);
    fns->glBindTexture(GL_TEXTURE_2D, uv_tex_);
    const QSize uv_size(src_w / 2, src_h / 2);
    if (uv_tex_size_ != uv_size) {
        fns->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uv_size.width(), uv_size.height(), 0,
                          GL_RG, GL_UNSIGNED_BYTE, f.bits(1));
        uv_tex_size_ = uv_size;
    } else {
        fns->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uv_size.width(), uv_size.height(),
                             GL_RG, GL_UNSIGNED_BYTE, f.bits(1));
    }
    fns->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    f.unmap();

    // Render quad to FBO with the YUV→RGB shader.
    fbo_->bind();
    fns->glViewport(0, 0, out_device.width(), out_device.height());
    fns->glDisable(GL_DEPTH_TEST);
    fns->glDisable(GL_BLEND);

    program_.bind();
    fns->glActiveTexture(GL_TEXTURE0);
    fns->glBindTexture(GL_TEXTURE_2D, y_tex_);
    fns->glActiveTexture(GL_TEXTURE1);
    fns->glBindTexture(GL_TEXTURE_2D, uv_tex_);
    program_.setUniformValue(loc_u_y_, 0);
    program_.setUniformValue(loc_u_uv_, 1);

    vao_.bind();
    fns->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vao_.release();

    program_.release();

    // Readback. RGBA (4 bytes/pixel) is always 4-byte row-aligned, which
    // matches QImage::Format_RGBA8888's row layout regardless of width —
    // unlike Format_RGB888 which pads rows to 4-byte boundaries and would
    // slide each row by a few bytes against glReadPixels' tight-packed
    // output, producing chroma-shifted stripes.
    QImage out(out_device, QImage::Format_RGBA8888);
    fns->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    fns->glReadPixels(0, 0, out_device.width(), out_device.height(),
                      GL_RGBA, GL_UNSIGNED_BYTE, out.bits());

    fbo_->release();
    fns->glBindTexture(GL_TEXTURE_2D, 0);

    // Surface any GL error from this pass — caller will use CPU fallback.
    // Latch the warning so a persistent error doesn't spam the log at 30+ fps;
    // one line per session is enough to know the fast path is degraded.
    const GLenum err = fns->glGetError();
    context_->doneCurrent();
    if (err != GL_NO_ERROR) {
        if (!gl_err_warned_) {
            LOG_WARN("VideoScaler", QString("glGetError 0x%1; CPU fallback engaged "
                                            "for affected frames (suppressing further warnings)")
                                        .arg(err, 0, 16));
            gl_err_warned_ = true;
        }
        return {};
    }

    out.setDevicePixelRatio(dpr);
    return out;
}

} // namespace fincept::screens::widgets

#endif // HAS_QT_MULTIMEDIA
