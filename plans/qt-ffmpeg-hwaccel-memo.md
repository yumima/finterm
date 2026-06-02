# Qt + FFmpeg Hardware Acceleration — Investigation Memo

**Status:** Investigation paused — pending broader dependency-management decision
**Date:** 2026-05-20
**Owner:** yumima
**Related:** `fincept-qt/src/screens/dashboard/widgets/VideoPlayerWidget.cpp`, `fincept-qt/src/screens/dashboard/widgets/OffscreenVideoScaler.{h,cpp}`

---

## TL;DR

The video tile's CPU floor is **~50-80 % per 1080p stream**, and it's
entirely the PCIe upload of the decoded NV12 frame each tick. Lowering
it further requires the GPU-decoded frame to stay on the GPU through
render — *"zero-copy"*. This is **architecturally possible** on the
hardware (RTX 5070 Ti, NVDEC works) but **gated by Qt** today: the Qt
6.8.3 we use ships an FFmpeg with no hwaccel compiled in. Three viable
paths exist; all of them are *dependency-management* decisions before
they're code decisions.

---

## What we measured

Telemetry on the live video pipeline (Bloomberg HLS, 1920×1080 H.264):

| Signal | Reading |
|---|---|
| Frame format from `QVideoSink` | `Format_NV12` |
| `QVideoFrame::handleType()` over 30 frames | **0 with `RhiTextureHandle`, 30 with `NoHandle`** |
| GPU offscreen scaler engagement | 100 % (NV12 is the fast-path format) |
| Per-frame CPU upload cost (`glTexImage2D`/`SubImage2D` of NV12) | ~5–8 ms (PCIe-bound) |
| Per-frame readback (widget-sized RGB via `glReadPixels`) | ~1–2 ms |
| Shader (YUV→RGB + scale) | ~0.3 ms |
| Resulting per-stream CPU | ~50-80 % |

The shader and readback are negligible; **the upload is the floor**.

## What Qt is using

```
Path:  /home/yma/fin/finterm/.qt/6.8.3/gcc_64/   (local-to-project)
Source: aqtinstall — official Qt Company prebuilt binaries
FFmpeg: BUNDLED at /home/yma/fin/finterm/.qt/6.8.3/gcc_64/lib/
        libavcodec.so.61, libavformat.so.61, swscale.so.8, swresample.so.5
        (Qt's plugin links these in preference to system libs)
```

**Proof that bundled FFmpeg has no hwaccel:**

```bash
nm -D /home/yma/fin/finterm/.qt/6.8.3/gcc_64/lib/libavcodec.so.61 \
  | grep -iE "cuvid|nvdec|nvenc|vaapi|vdpau"
# (no output)
```

Zero symbols. The Qt Company's CI ships FFmpeg software-only because
the CI farm can't depend on NVIDIA / VAAPI being present at user
install time. **This is policy, not a build mistake.**

**`QT_FFMPEG_DECODING_HW_DEVICE_TYPES=cuda` does nothing** in this
configuration — there's no hwaccel code path to enable. Verified by
running with the env var set: telemetry unchanged (0 hwaccel handles).

## What the system has

```bash
$ ffmpeg -hwaccels
vdpau  cuda  vaapi  qsv  drm  opencl  vulkan
```

Ubuntu 25.x ships FFmpeg 8.0.1 with **every** hwaccel enabled. NVIDIA
proprietary driver is present (RTX 5070 Ti, confirmed via `nvtop`).
**The system is ready; only Qt's bundled stack is the blocker.**

## The catch — ABI mismatch

| | Qt-bundled | System |
|---|---|---|
| `libavformat` | `.so.61` | `.so.62` |
| `libavcodec` | `.so.61` | `.so.62` |

ABI break between major FFmpeg versions. We cannot `LD_PRELOAD` system
FFmpeg over the bundled one and expect Qt's `libffmpegmediaplugin.so`
to load. The plugin was linked against `.so.61` symbols and ABI.

## Three viable paths

### Path A — Rebuild only `libffmpegmediaplugin.so` (~30-60 min)

**Smallest scope.** Take Qt 6.8.3's `qtmultimedia` source tree, build
just the FFmpeg plugin, link it against system FFmpeg, drop the
resulting `.so` into the aqtinstall plugin directory.

```bash
# Sketch (untested):
git clone --branch v6.8.3 git://code.qt.io/qt/qtmultimedia.git
cd qtmultimedia
cmake -B build \
  -DCMAKE_PREFIX_PATH=/home/yma/fin/finterm/.qt/6.8.3/gcc_64 \
  -DFFMPEG_DIR=/usr \
  -DBUILD_SHARED_LIBS=ON
ninja -C build qffmpegmediaplugin   # plugin target name TBD
cp build/.../libffmpegmediaplugin.so \
   /home/yma/fin/finterm/.qt/6.8.3/gcc_64/plugins/multimedia/
```

**Risk — API delta.** Qt 6.8 plugin source was written against FFmpeg
7 (libavcodec 61). System has FFmpeg 8 (libavcodec 62). The plugin
may not compile against system headers without source patches.
**Needs a compile-test before committing.** Could be a smooth 30-min
experiment or a several-hour patching exercise.

**Risk — distribution.** The patched plugin would need to ship with
finterm releases, or instructions for users to apply the same build
to their aqtinstall Qt. That couples our release pipeline to a
non-standard Qt config.

### Path B — Full Qt 6.8 source build with FFmpeg hwaccel (~2-4 hours)

**Largest scope, most supported.** Build Qt 6.8 from source using
Qt's own FFmpeg submodule, but configure that FFmpeg with the
hwaccel flags Qt's CI doesn't set:

```bash
# Qt's FFmpeg is in qt5/qtwebengine/src/3rdparty/ffmpeg (vendored).
# qtmultimedia uses a separate FFmpeg submodule per its configure
# scripts — needs verification.

# Sketch (untested):
init-repository --module-subset=qtbase,qtdeclarative,qtmultimedia,...
cmake -GNinja -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/qt-6.8.3-hwaccel \
  -DFEATURE_ffmpeg=ON \
  ...
ninja -C build install
# Plus FFmpeg --enable-cuda --enable-cuvid --enable-nvdec --enable-vaapi
# configured by Qt's build system.
```

**Risk — long build.** Qt source build is hours of compilation +
disk space + memory.

**Risk — pinning.** Now we're maintaining a custom Qt build for the
project. CI / new-developer setup gets more complex.

**Reward — clean.** Everything in one place; the rest of Qt is
unmodified; the plugin matches the rest of Qt by construction.

### Path C — Switch to Qt 6.9+ with native system-FFmpeg support (~waiting)

**Lowest active effort, but uncertain timeline.** Qt 6.9 / 6.10 work
includes `-DQT_DEPLOY_FFMPEG=OFF` and improved support for linking
qtmultimedia against system FFmpeg rather than the bundled copy. If
that stabilises with hwaccel-aware code paths, we just upgrade Qt and
the system FFmpeg's hwaccels show through.

**Risk — not yet confirmed stable.** Need to verify Qt 6.9 release
notes and test a 6.9 install before committing.

## What's already in place to make Path A or B pay off

The `OffscreenVideoScaler` (offscreen GL context + NV12 → RGB shader
+ FBO + readback) is *the* substrate that consumes `RhiTextureHandle`
frames. Today it falls back to upload because the handle is
`NoHandle`. The day Qt starts delivering frames with
`handleType() == RhiTextureHandle`:

- The shader stays the same.
- The FBO stays the same.
- The readback stays the same.
- The upload step (`glTexImage2D` / `glTexSubImage2D`) gets replaced
  by `glBindTexture(GL_TEXTURE_2D, hw_y); ... hw_uv);` — bind the
  GPU-resident texture instead of uploading from CPU memory.

That's ~50 LOC of new code at the top of `OffscreenVideoScaler::process`
plus a small QRhi-integration path to extract the GL name from the
`QVideoFrame`'s underlying RhiTexture.

**Result if it works:** CPU per 1080p stream drops from 50-80 % to
single-digit %.

## Decision dependencies

Before picking a path, we need answers from a broader package /
distribution lens:

1. **Release distribution model.** Are we shipping a self-contained
   `finterm` installer (windeployqt/linuxdeployqt style), or
   relying on the user to have Qt installed? A custom Qt build is
   easier to bundle than to require users to assemble.
2. **CI build cost.** Adding a Qt source build to CI is a multi-hour
   tax on every release. Worth it only if hwaccel is a release-gated
   feature.
3. **Multi-distro support.** Path A's "rebuilt plugin against system
   FFmpeg" works on Ubuntu 25, but each distro ships its own FFmpeg
   ABI. The plugin would need a build per distro. Path B (full Qt
   source with bundled hwaccel-enabled FFmpeg) is portable.
4. **Cost vs benefit per stream.** 50-80 % CPU per 1080p stream is
   acceptable for 1 stream on this hardware. Becomes painful at 2+
   simultaneous streams (multi-tile dashboard) or on lower-power
   machines (laptop on battery, mid-range GPU). Decides urgency.

## Recommended order of operations (when picked back up)

1. **Decide release/dependency model first** — Path A and Path B
   depend on it.
2. **If Path A is chosen:** spend an hour on the compile-test against
   system FFmpeg. Patch or pivot based on result.
3. **If Path B is chosen:** budget half a day for the first source
   build + CI integration.
4. **If Path C is chosen:** track Qt 6.9 release notes; revisit when
   it stabilises.
5. **Once frames arrive with `RhiTextureHandle`:** add the ~50-LOC
   fast path to `OffscreenVideoScaler::process`. Telemetry already in
   place via the existing GL init log line — extend it briefly to
   confirm the new path engages.

## What is NOT a viable path

- **Workload reduction (cap fps, lower max_height).** Rejected:
  trades quality for CPU. Not acceptable.
- **Env vars on the current Qt.** Confirmed no-op — hwaccel codecs
  aren't compiled in, there's nothing to enable.
- **`QVideoWidget` / `QOpenGLWidget` direct render.** Both create
  `wl_subsurface`s that Mutter (GNOME Wayland) promotes to separate
  `xdg_toplevel`s, OOM-ing the process. Documented in
  `~/.claude/projects/.../memory/project_video_wayland_subsurface.md`.
  This is why `OffscreenVideoScaler` exists — it's the Wayland-safe
  way to use the GPU.

## Open questions

- Does Qt 6.9 ship before we ship the next finterm release? (decides
  Path C's timing)
- What's the user base GPU profile? (decides whether hwaccel is
  release-gated or a power-user toggle)
- Are there other Qt prebuild distributors (Conan, vcpkg) that *do*
  ship hwaccel-enabled FFmpeg? (might shortcut the whole question)

---

**End memo.**
