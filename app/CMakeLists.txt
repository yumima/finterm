cmake_minimum_required(VERSION 3.27)

# ── RC compiler override (clang-cl + Windows only) ───────────────────────────
# When building with clang-cl, CMake's Windows-Clang module auto-enables the
# two-step `cmake -E cmake_llvm_rc` wrapper: preprocess the .rc with clang-cl
# then pipe to rc.exe. That wrapper inherits incompatible C flags (-MD, -FS,
# /showIncludes) and fails silently with exit 1 on Windows SDK rc.exe. Our
# app.rc is a trivial ICON declaration with no macros or #includes, so we
# bypass the wrapper and use rc.exe directly. Tracked as CMake issue #20198.
# Must run before project() so CMAKE_RC_COMPILER is set before RC language init.
if(WIN32 AND CMAKE_HOST_WIN32)
    # Heuristic: if CC/CXX env vars point to clang-cl, or the generator is likely
    # to pick up clang-cl from PATH, force MSVC rc.exe. Harmless on pure MSVC too.
    find_program(_finterm_rc_exe NAMES rc rc.exe
        HINTS
            "$ENV{WindowsSdkVerBinPath}x64"
            "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x64"
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
    if(_finterm_rc_exe)
        set(CMAKE_RC_COMPILER "${_finterm_rc_exe}" CACHE FILEPATH "Windows resource compiler" FORCE)
        set(CMAKE_RC_COMPILER_INIT "${_finterm_rc_exe}")
    endif()
endif()

project(finterm VERSION 4.1.0 LANGUAGES C CXX)

# Strip C/C++ flags out of RC invocations — rc.exe only understands /D, /I, /fo.
if(WIN32)
    set(CMAKE_RC_FLAGS "/nologo" CACHE STRING "" FORCE)
    set(CMAKE_RC_FLAGS_RELEASE "" CACHE STRING "" FORCE)
    set(CMAKE_RC_FLAGS_DEBUG "" CACHE STRING "" FORCE)
    set(CMAKE_RC_FLAGS_RELWITHDEBINFO "" CACHE STRING "" FORCE)
    set(CMAKE_RC_FLAGS_MINSIZEREL "" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Pinned toolchain enforcement ─────────────────────────────────────────────
# finterm ships on 3 platforms and cannot tolerate compiler-version
# drift. These checks fail the build early with an actionable message rather
# than letting the user hit a cryptic template error 10 minutes into compile.
if(MSVC AND MSVC_VERSION LESS 1938)
    message(FATAL_ERROR
        "MSVC 19.38 (Visual Studio 2022 17.8) or newer required.\n"
        "Detected MSVC_VERSION: ${MSVC_VERSION}\n"
        "Install VS 2022 17.8+ from https://visualstudio.microsoft.com/downloads/")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.3)
    message(FATAL_ERROR
        "GCC 12.3 or newer required. Detected: ${CMAKE_CXX_COMPILER_VERSION}\n"
        "Ubuntu 22.04+ / Debian 12+ ship a compatible GCC.")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15.0)
    message(FATAL_ERROR
        "Clang 15.0 or newer required (Apple Clang 15.0 = Xcode 15.2).\n"
        "Detected: ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# ── Compiler cache (ccache / sccache) ────────────────────────────────────────
# Detected at configure time. If present, routes every C/C++ compilation
# through the cache. Cuts clean rebuilds by 5-20x when source hasn't changed.
#
# PCH SUPPORT — without this, ccache rejects 99% of calls with
# "Could not use precompiled header" because MSVC-style PCH files include
# a serialised path that defeats the cache key. The sloppiness flags +
# CCACHE_PCH_EXTSUM tell ccache to hash the PCH's contents instead of its
# path. This must be set both at configure time (when CMake records the
# launcher script) and at build time (when the launcher actually runs),
# so we wrap ccache in a CMake-generated script that exports the env vars
# unconditionally.
find_program(FINTERM_CCACHE_PROGRAM NAMES ccache sccache)
if(FINTERM_CCACHE_PROGRAM)
    # Expose the same env vars to interactive `ccache -s` runs by writing a
    # ccache.conf in the cache dir. This is idempotent — ccache reads it on
    # every invocation, so changes here propagate without a clean rebuild.
    if(WIN32)
        set(_finterm_ccache_dir "$ENV{LOCALAPPDATA}/ccache")
    elseif(APPLE)
        set(_finterm_ccache_dir "$ENV{HOME}/Library/Caches/ccache")
    else()
        set(_finterm_ccache_dir "$ENV{HOME}/.ccache")
    endif()
    if(_finterm_ccache_dir AND IS_DIRECTORY "${_finterm_ccache_dir}")
        # ccache.conf format: KEY = VALUE per line. We write only the keys we
        # need; ccache merges with defaults for everything else.
        file(WRITE "${_finterm_ccache_dir}/ccache.conf"
            "sloppiness = pch_defines,time_macros,include_file_mtime,include_file_ctime,locale\n"
            "pch_external_checksum = true\n"
            "depend_mode = true\n"
            "compiler_check = content\n"
            "max_size = 5G\n")
        message(STATUS "Wrote ccache config to ${_finterm_ccache_dir}/ccache.conf")
    endif()

    # Wrap ccache in a launcher script that exports the env vars unconditionally
    # — belt-and-braces in case ccache.conf isn't picked up (e.g. when the user
    # has CCACHE_DIR set to a non-default location).
    if(WIN32)
        set(_finterm_ccache_launcher "${CMAKE_BINARY_DIR}/ccache_launcher.cmd")
        file(WRITE "${_finterm_ccache_launcher}"
            "@echo off\r\n"
            "set CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime,locale\r\n"
            "set CCACHE_PCH_EXTSUM=true\r\n"
            "set CCACHE_DEPEND=1\r\n"
            "set CCACHE_COMPILERCHECK=content\r\n"
            "\"${FINTERM_CCACHE_PROGRAM}\" %*\r\n")
    else()
        set(_finterm_ccache_launcher "${CMAKE_BINARY_DIR}/ccache_launcher.sh")
        file(WRITE "${_finterm_ccache_launcher}"
            "#!/bin/sh\n"
            "export CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime,locale\n"
            "export CCACHE_PCH_EXTSUM=true\n"
            "export CCACHE_DEPEND=1\n"
            "export CCACHE_COMPILERCHECK=content\n"
            "exec \"${FINTERM_CCACHE_PROGRAM}\" \"$@\"\n")
        execute_process(COMMAND chmod +x "${_finterm_ccache_launcher}")
    endif()

    set(CMAKE_CXX_COMPILER_LAUNCHER "${_finterm_ccache_launcher}")
    set(CMAKE_C_COMPILER_LAUNCHER   "${_finterm_ccache_launcher}")
    message(STATUS "Compiler cache enabled: ${FINTERM_CCACHE_PROGRAM} (PCH-aware launcher)")
endif()

# ── macOS rpath configuration ────────────────────────────────────────────────
# Set install rpaths so the binary looks inside its own bundle for frameworks.
# Without this, CMake embeds build-tree rpaths (e.g. _deps/qgeoview-build/lib)
# and CPack IFW fails trying to delete them after macdeployqt already rewrote them.
if(APPLE)
    # If not explicitly provided by the caller/preset, pin the minimum
    # supported macOS version so builds on newer runners do not accidentally
    # produce bundles that require that newer host OS at runtime.
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING
            "Minimum macOS version supported by finterm" FORCE)
    endif()
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build performance ─────────────────────────────────────────────────────────
# Unity build for infrastructure layers (safe — no local symbol conflicts)
# Screens are excluded below via SKIP_UNITY_BUILD_INCLUSION (too many local statics/functions)
set(CMAKE_UNITY_BUILD ON)
# Batch size 14: sweet spot for MSVC — keeps each unity TU under ~30s to compile.
# 32 was too large: one TU change forced a 32-file recompile; 14 limits blast radius.
set(CMAKE_UNITY_BUILD_BATCH_SIZE 14)

# FetchContent: default to skipping the update step after first download to cut
# configure time by 5-15s on incremental configures. Not FORCEd so developers
# can toggle it off (-DFETCHCONTENT_UPDATES_DISCONNECTED=OFF) when a GIT_TAG is
# bumped to a commit not yet present in the local clone — otherwise the build
# wedges with "Requested git ref ... is not present locally". CMake docs:
# https://cmake.org/cmake/help/latest/module/FetchContent.html#variable:FETCHCONTENT_UPDATES_DISCONNECTED
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Skip FetchContent update step after initial download")

# ── LTO (whole-program optimisation) ─────────────────────────────────────────
# /GL + /LTCG produce the smallest, fastest binary but make every incremental
# link a full whole-program rebuild over 400+ object files — easily 30-90s per
# link on a developer machine. Default OFF so daily iteration is fast; CI and
# explicit release builds opt in.
option(FINTERM_ENABLE_LTO
    "Enable MSVC /GL + /LTCG and equivalent on GCC/Clang (slow link, smaller/faster binary)"
    OFF)
if(DEFINED ENV{CI})
    set(FINTERM_ENABLE_LTO ON CACHE BOOL "" FORCE)
endif()

# MSVC compiler flags for maximum build throughput:
#   /Z7        — embed debug info in .obj instead of shared PDB → no PDB write
#                contention (major speedup with parallel compilation)
#   /Oi        — enable intrinsics (faster codegen, no extra passes)
#   /bigobj    — allow >65535 sections (needed by large unity TUs and MOC output)
#
# /MP is intentionally NOT set: Ninja already parallelises at the cl.exe level
# (one cl.exe per TU, capped at host CPU count). Adding /MP makes each cl.exe
# fork N more workers, oversubscribing the box and *slowing* builds. /MP is
# only useful under MSBuild, which parallelises by project not by TU.
if(MSVC)
    # /permissive- : enforces ISO C++ conformance — catches MS-specific non-portable
    #                code that would break on GCC/Clang (two-phase lookup, etc.)
    # /W4          : level-4 warnings (highest practical without /Wall noise)
    # /WX only in CI so local iteration isn't blocked by new warnings
    # /Zc:preprocessor: the conforming preprocessor — __VA_OPT__ (logging and
    #   SlowOpTimer macros) is C++20 and the legacy MSVC one rejects it.
    # /wd4456-4459: name-shadowing warnings. GCC and Clang builds here don't
    #   enable -Wshadow, so these fired on Windows only, and under CI's /WX
    #   made code that builds everywhere else fail. /wd4305: double→float
    #   literal truncation in Qt float APIs (setAlphaF and friends).
    add_compile_options(/Z7 /Oi /bigobj /permissive- /W4 /Zc:preprocessor /wd4456 /wd4457 /wd4458 /wd4459 /wd4305)
    if(DEFINED ENV{CI})
        add_compile_options(/WX)
    endif()
    # Replace /Zi and /ZI (shared PDB) with /Z7 in all configs
    foreach(flag_var
            CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REPLACE "/Zi" "/Z7" ${flag_var} "${${flag_var}}")
        string(REPLACE "/ZI" "/Z7" ${flag_var} "${${flag_var}}")
    endforeach()

    # Whole-program optimisation. /GL marks each .obj as IL; /LTCG merges at
    # link time. Both required together — using one without the other warns
    # and silently disables. Gated behind FINTERM_ENABLE_LTO so incremental
    # dev links stay fast (incremental linking is incompatible with /LTCG).
    if(FINTERM_ENABLE_LTO)
        add_compile_options($<$<CONFIG:Release>:/GL>)
        add_link_options($<$<CONFIG:Release>:/LTCG:incremental>)
    else()
        # /INCREMENTAL with debug info needs /DEBUG to emit a usable PDB even
        # though we use /Z7 (objects carry debug). Keep linker incremental for
        # fast Release-mode iteration. CMake's default Release link flags set
        # /INCREMENTAL:NO; override here.
        add_link_options($<$<CONFIG:Release>:/INCREMENTAL>)
        add_link_options($<$<CONFIG:RelWithDebInfo>:/INCREMENTAL>)
        # Replace any /INCREMENTAL:NO that CMake's defaults inserted.
        foreach(flag_var
                CMAKE_EXE_LINKER_FLAGS_RELEASE
                CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
                CMAKE_SHARED_LINKER_FLAGS_RELEASE
                CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO
                CMAKE_MODULE_LINKER_FLAGS_RELEASE
                CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO)
            string(REPLACE "/INCREMENTAL:NO" "/INCREMENTAL" ${flag_var} "${${flag_var}}")
        endforeach()
    endif()
endif()

# ── GCC / Clang warning flags ────────────────────────────────────────────────
# Mirror MSVC /W4 + /permissive- behavior on Unix compilers.
# -Werror only in CI so local iteration isn't blocked.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(DEFINED ENV{CI})
        add_compile_options(-Werror)
    endif()
endif()

# Suppress Windows min/max macros that break std::min/std::max in unity batches
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

# ── Test suite (opt-in) ──────────────────────────────────────────────────────
# OFF by default so a normal build is unchanged. Turn on with
# -DFINTERM_BUILD_TESTS=ON to compile the Qt Test executables under
# app/tests/. Run via `ctest` in the build directory.
option(FINTERM_BUILD_TESTS "Build the finterm test suite" OFF)

# ── Qt packages ──────────────────────────────────────────────────────────────
# FINTERM_QT_VERSION is the version the project is *tested and shipped* against.
# By default the build accepts any Qt >= that major.minor (so a system/distro
# Qt — e.g. apt's qt6-* or Homebrew's qt6 — is used as-is, no vendored copy).
# Release/installer builds set -DFINTERM_QT_STRICT=ON to lock the exact tested
# patch release, guaranteeing binary/ABI stability of the shipped installer.
# The Dockerfile and release CI set STRICT; dev boxes and PR CI do not.
# (The old ALLOW_QT_DRIFT flag is gone — the floor IS the default now;
#  pass -DFINTERM_QT_STRICT=ON for the exact pin instead.)
set(FINTERM_QT_VERSION 6.8.3 CACHE STRING "Qt version the project is tested/shipped against")
option(FINTERM_QT_STRICT "Require exactly FINTERM_QT_VERSION (release/ship builds)" OFF)

# Resolve the version constraint passed to every find_package(Qt6 ...) below.
# STRICT  -> exact patch pin (ship builds).  Default -> major.minor floor.
if(FINTERM_QT_STRICT)
    set(_finterm_qt_match ${FINTERM_QT_VERSION} EXACT)
else()
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _finterm_qt_floor "${FINTERM_QT_VERSION}")
    set(_finterm_qt_match ${_finterm_qt_floor})
endif()

# Qt 6.8 FindWrapOpenGL.cmake hard-codes "-framework AGL" as a fallback when
# find_library(AGL) fails. Apple removed AGL in macOS 14+ (Sonoma), causing
# linker errors on modern SDKs. Pre-create WrapOpenGL::WrapOpenGL without AGL
# so Qt's FindWrapOpenGL takes its early-return path (target already exists).
if(APPLE AND NOT TARGET WrapOpenGL::WrapOpenGL)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
        get_target_property(_finterm_ogl_loc OpenGL::GL IMPORTED_LOCATION)
        if(_finterm_ogl_loc AND NOT _finterm_ogl_loc MATCHES "/([^/]+)\\.framework$")
            get_filename_component(_finterm_ogl_path "${_finterm_ogl_loc}" DIRECTORY)
        endif()
        if(NOT _finterm_ogl_path)
            set(_finterm_ogl_path "-framework OpenGL")
        endif()
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "${_finterm_ogl_path}")
        set(WrapOpenGL_FOUND ON)
    endif()
endif()

find_package(Qt6 ${_finterm_qt_match} REQUIRED COMPONENTS
    Widgets Charts PrintSupport Network Sql Concurrent Multimedia)

# Optional: WebSockets (not always installed)
find_package(Qt6 ${_finterm_qt_match} QUIET COMPONENTS WebSockets)
if(Qt6WebSockets_FOUND)
    set(HAS_WEBSOCKETS TRUE)
endif()

# Optional: MultimediaWidgets (for inline video/stream playback)
# Qt6::Multimedia is already required above (needed for QMediaPlayer in AiChatBubble / VideoPlayerWidget).
find_package(Qt6 ${_finterm_qt_match} QUIET COMPONENTS MultimediaWidgets)
if(Qt6MultimediaWidgets_FOUND)
    set(HAS_MULTIMEDIA TRUE)
endif()

# Optional: TextToSpeech (cross-platform TTS — SAPI/AVSpeech/speech-dispatcher)
find_package(Qt6 ${_finterm_qt_match} QUIET COMPONENTS TextToSpeech)
if(Qt6TextToSpeech_FOUND)
    set(HAS_TTS TRUE)
endif()

# macOS 14+ / Xcode 15+ removes the AGL framework. Qt6's WrapOpenGL target (pulled in
# by Qt6::Gui) hardcodes a fallback to `-framework AGL` if it isn't found on the system.
# We strip it from the INTERFACE_LINK_LIBRARIES to prevent `ld: framework 'AGL' not found`.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_gl_libs)
        set(_new_gl_libs "")
        foreach(_lib IN LISTS _gl_libs)
            if(NOT _lib MATCHES "AGL")
                list(APPEND _new_gl_libs "${_lib}")
            endif()
        endforeach()
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_new_gl_libs}")
    endif()
endif()

# ── FetchContent dependencies ─────────────────────────────────────────────────
# Guard: prevent all FetchContent subdirectories from registering install()
# rules that would pollute CPack and cause "file not found" errors on packaging.
# Use CACHE FORCE so the flag is visible inside FetchContent sub-scopes
# (plain set() only propagates into add_subdirectory, not FetchContent processes).
include(FetchContent)
set(CMAKE_SKIP_INSTALL_RULES TRUE CACHE BOOL "" FORCE)

# ── Qt6::GuiPrivate availability check ───────────────────────────────────────
# QXlsx requires Qt6::GuiPrivate (uses private/qzipreader_p.h internally).
# The Qt6::GuiPrivate target is created as a side-effect of loading Qt6::Gui
# (which is pulled in by Qt6::Widgets above). There is NO separate cmake
# package called "Qt6GuiPrivate" — so find_package(Qt6 COMPONENTS GuiPrivate)
# always fails. The correct check is: if(TARGET Qt6::GuiPrivate).
#
# The official Qt installer (jurplel/install-qt-action, aqtinstall) always
# includes private headers. System Qt packages require an extra package:
#   Ubuntu/Debian : sudo apt-get install qt6-base-private-dev
#   Fedora        : sudo dnf install qt6-qtbase-private-devel
#   Arch Linux    : qt6-base (private headers included by default)
#   macOS Homebrew: included by default
# When private headers are absent, QXlsx is disabled and Excel export shows
# a "not available" message rather than crashing or failing to build.
if(TARGET Qt6::GuiPrivate)
    set(FINTERM_HAS_PRIVATE_HEADERS TRUE)
else()
    set(FINTERM_HAS_PRIVATE_HEADERS FALSE)
    message(WARNING
        "\n"
        "  Qt6::GuiPrivate not found — Excel export (QXlsx) will be disabled.\n"
        "  To enable it, install Qt6 private headers:\n"
        "    Ubuntu/Debian : sudo apt-get install qt6-base-private-dev\n"
        "    Fedora        : sudo dnf install qt6-qtbase-private-devel\n"
        "    Arch Linux    : qt6-base (already includes private headers)\n"
        "    macOS Homebrew: already included\n"
        "    CI/official   : jurplel/install-qt-action always includes them\n"
    )
endif()

# ── QXlsx (Excel file I/O) ────────────────────────────────────────────────────
# Only fetched and built when Qt6::GuiPrivate is available.
# A stub INTERFACE target is created when unavailable so all
# target_link_libraries(... QXlsx::QXlsx) calls still compile cleanly.
if(FINTERM_HAS_PRIVATE_HEADERS)
    FetchContent_Declare(
        QXlsx
        GIT_REPOSITORY https://github.com/QtExcel/QXlsx.git
        GIT_TAG        3192b808a283f8eb18c97b9a087bf085054e9bb0  # v1.4.9
        SOURCE_SUBDIR  QXlsx
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(QXlsx)

    # QXlsx uses implicit const char* -> QString conversions which are banned
    # by Qt6's strict cast macros. Undefine them for this target only.
    # Also silence upstream warnings so CI's -Werror doesn't fail on third-party
    # code. /WX- is critical on MSVC: /W0 suppresses warnings but does NOT
    # disable the directory-inherited /WX (warnings-as-errors) that promotes
    # any surviving warning to a build-breaking error.
    if(MSVC)
        target_compile_options(QXlsx PRIVATE
            /UQT_NO_CAST_FROM_ASCII /UQT_NO_CAST_FROM_BYTEARRAY
            /UQT_NO_CAST_TO_ASCII   /UQT_NO_URL_CAST_FROM_STRING
            /W0 /WX-
        )
    else()
        target_compile_options(QXlsx PRIVATE
            -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_FROM_BYTEARRAY
            -UQT_NO_CAST_TO_ASCII   -UQT_NO_URL_CAST_FROM_STRING
            -w -Wno-error
        )
    endif()
else()
    # Stub target — satisfies target_link_libraries without pulling in QXlsx
    add_library(QXlsx_stub INTERFACE)
    add_library(QXlsx::QXlsx ALIAS QXlsx_stub)
endif()
# NOTE: FINTERM_HAS_QXLSX compile definition is applied to finterm
# after add_executable() — see the target_compile_definitions block below.

# ── Auto-update service ─────────────────────────────────────────────────────
# The updater is implemented in-tree (src/services/updater/UpdateService.cpp)
# using Qt6::Network directly — no third-party dependency. See the class
# docstring for the manifest schema.

# ── md4c (fast CommonMark markdown parser → HTML) ────────────────────────────
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        729e6b8b320caa96328968ab27d7db2235e4fb47  # release-0.5.2
    GIT_SHALLOW    TRUE
)
# Patch md4c's CMakeLists.txt before FetchContent_MakeAvailable processes
# it. md4c adds -Wdeclaration-after-statement at directory scope (a C-only
# warning), which then bleeds into the CXX-compiled MOC files we generate
# for the same targets — gcc emits "valid for C/ObjC but not for C++" on
# every CXX file. -w on the targets doesn't catch it because cc1plus
# emits the message before user warning machinery activates. Modifying
# the directory's COMPILE_OPTIONS post-MakeAvailable is also too late
# because targets snapshot directory props at creation time. So we patch
# the source file directly: comment the line out before subdirectory
# processing. Idempotent — re-runs of cmake configure no-op.
FetchContent_GetProperties(md4c)
if(NOT md4c_POPULATED)
    FetchContent_Populate(md4c)
    set(_md4c_lists "${md4c_SOURCE_DIR}/CMakeLists.txt")
    if(EXISTS "${_md4c_lists}")
        file(READ "${_md4c_lists}" _md4c_text)
        string(REPLACE
            "add_compile_options(-Wdeclaration-after-statement)"
            "# patched out by parent finterm CMakeLists (CXX-incompatible flag)\n#    add_compile_options(-Wdeclaration-after-statement)"
            _md4c_text "${_md4c_text}")
        file(WRITE "${_md4c_lists}" "${_md4c_text}")
    endif()
    add_subdirectory("${md4c_SOURCE_DIR}" "${md4c_BINARY_DIR}")
endif()
# md4c builds md4c (parser) and md4c-html (HTML renderer) as static libs.
# Disable any example/test targets it may expose.
set_target_properties(md4c PROPERTIES UNITY_BUILD OFF)
if(TARGET md4c-html)
    set_target_properties(md4c-html PROPERTIES UNITY_BUILD OFF)
endif()
# Silence upstream warnings so CI's -Werror doesn't fail on third-party code.
# /WX- / -Wno-error needed in addition to /W0 / -w because CI adds -Werror (/WX)
# at the directory level, which suppression flags alone don't override.
foreach(_md4c_tgt md4c md4c-html md2html)
    if(TARGET ${_md4c_tgt})
        if(MSVC)
            target_compile_options(${_md4c_tgt} PRIVATE /W0 /WX-)
        else()
            target_compile_options(${_md4c_tgt} PRIVATE -w -Wno-error)
        endif()
    endif()
endforeach()

# md4c's src/CMakeLists.txt installs from ${CMAKE_BINARY_DIR}/src/md4c.pc but
# configure_file() writes to ${CMAKE_CURRENT_BINARY_DIR}/md4c.pc (the subbuild
# dir). On CI these are different paths, causing CPack's "file INSTALL cannot
# find md4c.pc: File exists" error. Copy the generated .pc files at configure
# time to where md4c's install rules expect them.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/src")
foreach(_pc md4c.pc md4c-html.pc)
    if(EXISTS "${md4c_BINARY_DIR}/src/${_pc}")
        configure_file(
            "${md4c_BINARY_DIR}/src/${_pc}"
            "${CMAKE_BINARY_DIR}/src/${_pc}"
            COPYONLY
        )
    endif()
endforeach()

# ── QGeoView (interactive map widget with OSM tiles) ─────────────────────────
# PATCH_COMMAND removes -framework AGL which was deleted in macOS 14 (Sonoma).
# Uses a portable CMake script instead of sed (sed is unavailable in MSVC
# Developer Command Prompt on Windows and is fragile across platforms).
FetchContent_Declare(
    QGeoView
    # An untagged commit: a shallow git clone can only reach a commit that is a
    # branch or tag tip, so once master moved on every fresh configure failed
    # ("unable to read tree"). The commit's archive, pinned by hash, can't drift.
    URL      https://github.com/AmonRaNet/QGeoView/archive/4b2c52c80940e1073c259ded1a57df83036ba1c9.tar.gz  # master @ 2026-04-11
    URL_HASH SHA256=e347fa62d5a77650b8587415f6e98071497eb27e5f695e46dc554780dc47ebac
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND}
        -DFILE=<SOURCE_DIR>/lib/CMakeLists.txt
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/qgeoview_remove_agl.cmake"
)
set(QGV_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(QGV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QGeoView)
# QGeoView has anonymous-namespace symbol clashes under unity build
set_target_properties(qgeoview PROPERTIES UNITY_BUILD OFF)
# Silence upstream warnings so CI's -Werror doesn't fail on third-party code.
if(MSVC)
    target_compile_options(qgeoview PRIVATE /W0 /WX-)
else()
    target_compile_options(qgeoview PRIVATE -w -Wno-error)
endif()

# Qt 6.8.3's FindWrapOpenGL.cmake adds "-framework AGL" which was removed in
# macOS 14 (Sonoma). The patch in setup.sh fixes this at the Qt install level.
# As a safety net, scrub AGL from qgeoview's resolved link libraries too.
# See GitHub issue #140.
if(APPLE)
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_libs qgeoview ${_prop})
        if(_libs)
            list(REMOVE_ITEM _libs "-framework AGL" "AGL")
            set_target_properties(qgeoview PROPERTIES ${_prop} "${_libs}")
        endif()
    endforeach()
endif()

# ── SingleApplication (single-process enforcement + new-window IPC) ──────────
# When the user re-launches the exe, the secondary instance sends "--new-window"
# to the primary, which opens a new MainWindow instead of a second process.
# QAPPLICATION_CLASS must be set before FetchContent_MakeAvailable so the lib
# compiles inheriting QApplication (not QCoreApplication).
set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
set(QAPPLICATION_CLASS QApplication CACHE STRING "" FORCE)
FetchContent_Declare(
    SingleApplication
    GIT_REPOSITORY https://github.com/itay-grudev/SingleApplication.git
    GIT_TAG        a8da87d78238ac9ac92d2cda41793d4879c188b3  # v3.5.4
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(SingleApplication)
# ── Patch SingleApplication for Qt 6.8 QSharedMemory compat ────────────────────
# Qt 6.8 changed the default QSharedMemory backend to PosixRealtime, but macOS's
# shm_open rejects the keys that SingleApplication generates ("bad name").
# Fix: use SystemV type which works on macOS (and Linux) with Qt 6.6+.
if(UNIX)
    file(READ ${SingleApplication_SOURCE_DIR}/singleapplication.cpp _sa_src)
    string(REPLACE
        "d->memory = new QSharedMemory( QNativeIpcKey( d->blockServerName ) );"
        "d->memory = new QSharedMemory(); d->memory->setNativeKey(d->blockServerName, QNativeIpcKey::Type::SystemV);"
        _sa_src "${_sa_src}")
    file(WRITE ${SingleApplication_SOURCE_DIR}/singleapplication.cpp "${_sa_src}")
endif()
if(MSVC)
    target_compile_options(SingleApplication PRIVATE /W0 /WX-)
else()
    target_compile_options(SingleApplication PRIVATE -w -Wno-error)
endif()

# ── Qt Advanced Docking System (detachable panels) ───────────────────────────
# Panels become CDockWidgets — drag to float, dock, move to any monitor.
# LGPL — compatible with AGPL. Used in Qt Creator itself.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC   ON  CACHE BOOL "" FORCE)  # static lib: simpler deployment, no extra DLL
set(QT_VERSION_MAJOR 6 CACHE STRING "" FORCE)
FetchContent_Declare(
    QtADS
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        87cffe5d40307c61141c88b88c10c5e7d181d880  # 4.5.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(QtADS)
# ADS target name is version-stamped: qtadvanceddocking-qt6
# Disable unity build — ADS uses anonymous namespaces internally
set_target_properties(qtadvanceddocking-qt6 PROPERTIES UNITY_BUILD OFF)

# Restore install rules for the main application targets
set(CMAKE_SKIP_INSTALL_RULES FALSE CACHE BOOL "" FORCE)
# Third-party code: silence warnings so CI's -Werror doesn't turn a benign
# upstream warning (e.g. -Wunused-but-set-variable on Clang) into a build failure.
if(MSVC)
    target_compile_options(qtadvanceddocking-qt6 PRIVATE /W0 /WX-)
else()
    target_compile_options(qtadvanceddocking-qt6 PRIVATE -w -Wno-error)
endif()

# ── Source files by module ────────────────────────────────────────────────────

# Core
set(CORE_SOURCES
    src/core/config/AppConfig.cpp
    src/core/config/AppPaths.cpp
    src/core/config/BuildInfo.cpp
    src/core/config/LayoutMigrations.cpp
    src/core/config/ProfileManager.cpp
    src/core/logging/Logger.cpp
    src/core/events/EventBus.cpp
    src/core/session/SessionManager.cpp
    src/core/session/ScreenStateManager.cpp
    src/core/keys/KeyConfigManager.cpp
    src/core/keys/WindowCycler.cpp
    src/core/net/NetSpeedMeter.cpp
    src/core/symbol/SymbolContext.cpp
    src/core/symbol/SymbolGroupRegistry.cpp
    src/core/symbol/SymbolMime.cpp
    src/core/symbol/SymbolDragSource.cpp
    src/core/components/ComponentCatalog.cpp
    src/core/components/PopularityTracker.cpp
    src/core/crash/CrashHandler.cpp
    src/core/util/AppLifecycle.cpp
)

# Network
set(NETWORK_SOURCES
    src/network/http/HttpClient.cpp
)
# WebSocketClient is always compiled; it stubs out when HAS_QT_WEBSOCKETS is not defined
list(APPEND NETWORK_SOURCES src/network/websocket/WebSocketClient.cpp)

# DataHub (Phase 0 scaffolding — see app/DATAHUB_ARCHITECTURE.md)
# Headers are always built so Q_OBJECT MOC runs; the .cpp bodies are only
# active when the feature flag is ON. Phase 1 will flesh out the stub.
set(DATAHUB_SOURCES
    src/datahub/DataHub.cpp
    src/datahub/DataHubMetaTypes.cpp
)

# Auth
set(AUTH_SOURCES
    src/auth/AuthManager.cpp
    src/auth/AuthService.cpp
    src/auth/PinManager.cpp
    src/auth/InactivityGuard.cpp
    src/auth/SecurityAuditLog.cpp
)

# Storage
set(STORAGE_SOURCES
    # Core databases
    src/storage/sqlite/Database.cpp
    src/storage/sqlite/CacheDatabase.cpp
    src/storage/sweep/RetiredStorage.cpp

    # Migration system
    src/storage/sqlite/migrations/MigrationRunner.cpp
    src/storage/sqlite/migrations/v001_initial.cpp
    src/storage/sqlite/migrations/v002_llm_chat.cpp
    src/storage/sqlite/migrations/v003_data_mcp_agents.cpp
    src/storage/sqlite/migrations/v004_cleanup.cpp
    src/storage/sqlite/migrations/v005_dashboard_layout.cpp
    src/storage/sqlite/migrations/v006_portfolio_multi.cpp
    src/storage/sqlite/migrations/v007_news_baselines.cpp

    # Storage manager
    src/storage/StorageManager.cpp
    # Cache + tab sessions
    src/storage/cache/CacheManager.cpp
    src/storage/cache/ScreenStateStore.cpp

    # Secure storage (unchanged)
    src/storage/secure/SecureStorage.cpp
    src/storage/secure/LlmSecureKeys.cpp

    # Repositories
    src/storage/repositories/SettingsRepository.cpp
    src/storage/repositories/WatchlistRepository.cpp
    src/storage/repositories/NotesRepository.cpp
    src/storage/repositories/PaperTradingRepository.cpp
    src/storage/repositories/NewsMonitorRepository.cpp
    src/storage/repositories/NewsArticleRepository.cpp
    src/storage/repositories/ChatRepository.cpp
    src/storage/repositories/AgentTraceRepository.cpp
    src/storage/repositories/TeamRepository.cpp
    src/storage/repositories/ChatArtefactRepository.cpp
    src/storage/repositories/AgentFeedbackRepository.cpp
    src/storage/repositories/ToolKillswitchRepository.cpp
    src/storage/repositories/LlmConfigRepository.cpp
    src/storage/repositories/EngineLinkRepository.cpp
    src/storage/repositories/LlmProfileRepository.cpp
    src/storage/repositories/DataSourceRepository.cpp
    src/storage/repositories/McpServerRepository.cpp
    src/storage/repositories/AgentConfigRepository.cpp
    src/storage/repositories/PortfolioRepository.cpp
    src/storage/repositories/WorkflowRepository.cpp
    src/storage/repositories/AiPredictionRepository.cpp
    src/storage/repositories/EarningsSignalRepository.cpp
    src/storage/repositories/CustomIndexRepository.cpp
    src/storage/repositories/DataMappingRepository.cpp
    src/storage/repositories/AccountRepository.cpp

    # Workflow migration
    src/storage/sqlite/migrations/v008_workflows.cpp

    # Instrument master contract (Phase 1)
    src/storage/sqlite/migrations/v009_instruments.cpp

    # LLM Profiles — multi-provider support
    src/storage/sqlite/migrations/v010_llm_profiles.cpp

    # Custom Indices — user-defined index creation
    src/storage/sqlite/migrations/v011_custom_indices.cpp

    # Data normalization — mapping persistence and normalized data storage
    src/storage/sqlite/migrations/v012_data_normalization.cpp

    # News articles — dedicated news article storage
    src/storage/sqlite/migrations/v013_news_articles.cpp

    # LLM tools toggle
    src/storage/sqlite/migrations/v014_llm_tools_toggle.cpp

    # Screen UI state — crash-safe per-screen state in cache.db
    src/storage/sqlite/migrations/v015_screen_state.cpp

    # Multi-account broker support
    src/storage/sqlite/migrations/v016_broker_accounts.cpp
    src/storage/sqlite/migrations/v017_perf_indexes.cpp
    src/storage/sqlite/migrations/v018_widget_config.cpp
    src/storage/sqlite/migrations/v019_sector_cache.cpp
    src/storage/sqlite/migrations/v020_security_audit_log.cpp
    src/storage/sqlite/migrations/v021_max_tokens_default.cpp
    src/storage/sqlite/migrations/v022_llm_secure_keys.cpp
    src/storage/sqlite/migrations/v023_llm_profile_runtime.cpp
    src/storage/sqlite/migrations/v024_mcp_marketplace_seed.cpp
    src/storage/sqlite/migrations/v025_mcp_http_transport.cpp
    src/storage/sqlite/migrations/v026_quant_critic_agent.cpp
    src/storage/sqlite/migrations/v027_agent_schedule.cpp
    src/storage/sqlite/migrations/v028_named_agents.cpp
    src/storage/sqlite/migrations/v029_agent_traces.cpp
    src/storage/sqlite/migrations/v030_tool_killswitch.cpp
    src/storage/sqlite/migrations/v031_untrusted_directive.cpp
    src/storage/sqlite/migrations/v032_memory_entries.cpp
    src/storage/sqlite/migrations/v033_mcp_oauth.cpp
    src/storage/sqlite/migrations/v034_chat_artefacts.cpp
    src/storage/sqlite/migrations/v035_agent_feedback.cpp
    src/storage/sqlite/migrations/v036_agent_trace_tool_calls.cpp
    src/storage/sqlite/migrations/v037_teams.cpp
    src/storage/sqlite/migrations/v038_team_mode.cpp
    src/storage/sqlite/migrations/v039_artefact_supersedes.cpp
    src/storage/sqlite/migrations/v040_memory_embeddings.cpp
    src/storage/sqlite/migrations/v041_migrate_fincept_provider.cpp
    src/storage/sqlite/migrations/v042_fix_llm_base_url.cpp
    src/storage/sqlite/migrations/v043_chat_session_persona.cpp
    src/storage/sqlite/migrations/v044_ai_predictions.cpp
    src/storage/sqlite/migrations/v045_physical_ai_watchlist.cpp
    src/storage/sqlite/migrations/v046_earnings_signal_records.cpp
    src/storage/sqlite/migrations/v047_earnings_predicted_move.cpp
    src/storage/sqlite/migrations/v048_snapshot_source.cpp
    src/storage/sqlite/migrations/v049_opening_balances.cpp
    src/storage/sqlite/migrations/v050_llm_assignment_dedupe.cpp
    src/storage/sqlite/migrations/v051_hearth_provider_rename.cpp
    src/storage/sqlite/migrations/v052_earnings_outlook.cpp
    src/storage/sqlite/migrations/v053_agent_instructions_backfill.cpp
    src/storage/sqlite/migrations/v054_iv_history_expiry_kind.cpp
    src/storage/sqlite/migrations/v055_chat_session_context.cpp
    src/storage/sqlite/migrations/v056_ai_prediction_source.cpp
    src/storage/sqlite/migrations/v057_saved_strategies.cpp
    src/storage/sqlite/migrations/v058_drop_context_recordings.cpp
    src/storage/sqlite/migrations/v059_engine_link.cpp
)

# Python runner
set(PYTHON_SOURCES
    src/python/PythonRunner.cpp
    src/python/PythonSetupManager.cpp
    src/python/PythonWorker.cpp
)

# AI Chat
set(AI_CHAT_SOURCES
    src/ai_chat/LlmService.cpp
    src/ai_chat/EmbeddingsClient.cpp
    src/ai_chat/HearthService.cpp
    src/ai_chat/HearthInstaller.cpp
    src/ai_chat/ModelCatalog.cpp
    src/ai_chat/ChatPane.cpp
    src/ai_chat/ElicitPrompter.cpp
    src/ai_chat/AiChatBubble.cpp
)

# MCP — Model Context Protocol system
set(MCP_SOURCES
    # Core
    src/mcp/McpProvider.cpp
    src/mcp/McpService.cpp
    src/mcp/McpClient.cpp
    src/mcp/McpHttpClient.cpp
    src/mcp/McpOAuth.cpp
    src/mcp/McpOAuthAuthCode.cpp
    src/mcp/McpManager.cpp
    src/mcp/McpInit.cpp
    src/mcp/UntrustedContent.cpp

    # Tool modules
    src/mcp/tools/NavigationTools.cpp
    src/mcp/tools/MarketsTools.cpp
    src/mcp/tools/WatchlistTools.cpp
    src/mcp/tools/NewsTools.cpp
    src/mcp/tools/MemoryTools.cpp
    src/mcp/tools/ArtefactTools.cpp
    src/mcp/tools/NotesTools.cpp
    src/mcp/tools/FileManagerTools.cpp
    src/mcp/tools/AiChatTools.cpp
    src/mcp/tools/PortfolioTools.cpp
    src/mcp/tools/CryptoTradingTools.cpp
    src/mcp/tools/PaperTradingTools.cpp
    src/mcp/tools/EdgarTools.cpp
    src/mcp/tools/FinancialDatasetsTools.cpp
    src/mcp/tools/QuantNarratorTools.cpp
    src/mcp/tools/MAAnalyticsTools.cpp
    src/mcp/tools/AltInvestmentsTools.cpp
    src/mcp/tools/DataSourcesTools.cpp
    src/mcp/tools/ProfileTools.cpp
    src/mcp/tools/SettingsTools.cpp
    src/mcp/tools/PythonTools.cpp
    src/mcp/tools/SystemTools.cpp
    src/mcp/tools/DataHubTools.cpp
    src/mcp/tools/ReportBuilderTools.cpp
    src/mcp/tools/AgenticMemoryTools.cpp
    src/mcp/tools/AgentsTools.cpp
    src/mcp/tools/AgentsTools_Discovery.cpp
    src/mcp/tools/AgentsTools_Execution.cpp
    src/mcp/tools/AgentsTools_Repos.cpp
    src/mcp/tools/DBnomicsTools.cpp
    src/mcp/tools/EquityResearchTools.cpp
    src/mcp/tools/FuturesTools.cpp
    src/mcp/tools/GeopoliticsTools.cpp
    src/mcp/tools/GovDataTools.cpp
    src/mcp/tools/MetaTools.cpp
    src/mcp/tools/QuantLabTools.cpp
    src/mcp/tools/SurfaceAnalyticsTools.cpp
    src/mcp/SchemaValidator.cpp
    src/mcp/ToolRetriever.cpp
)

# Trading
set(TRADING_SOURCES
    src/trading/PaperTrading.cpp
    src/trading/OrderMatcher.cpp
    src/trading/BrokerRegistry.cpp
    src/trading/UnifiedTrading.cpp
    src/trading/AccountManager.cpp
    src/trading/AccountDataStream.cpp
    src/trading/DataStreamManager.cpp
    src/trading/ExchangeService.cpp
    src/trading/ExchangeSession.cpp
    src/trading/ExchangeSessionManager.cpp
    src/trading/ExchangeDaemonPool.cpp
    src/trading/brokers/BrokerHttp.cpp
    # Brokers
    src/trading/brokers/alpaca/AlpacaBroker.cpp
    src/trading/brokers/ibkr/IBKRBroker.cpp
    src/trading/brokers/tradier/TradierBroker.cpp
    src/trading/brokers/saxo/SaxoBankBroker.cpp

    # Instrument system (Phase 1)
)

# Services
set(SERVICE_SOURCES
    src/services/calendar/CompanyEventsFeed.cpp
    src/services/screener/ScreenerService.cpp
    src/services/social/SocialBackend.cpp
    src/services/notebook/NotebookKernel.cpp
    src/services/calendar/EarningsCalendarFeed.cpp
    src/services/calendar/MacroCalendarFeed.cpp
    src/services/akshare/AkShareService.cpp
    src/services/alpha_arena/AlphaArenaService.cpp
    src/services/asia_markets/AsiaMarketsService.cpp
    src/services/crypto/TotpService.cpp
    src/services/python_cli/PythonCliService.cpp
    src/services/markets/LiveQuoteFollower.cpp
    src/services/markets/ExtendedHoursService.cpp
    src/services/markets/MarketDataService.cpp
    src/services/markets/IndexFutures.cpp
    src/services/markets/ConditionService.cpp
    src/services/futures/FuturesService.cpp
    src/services/futures/FuturesHub.cpp
    src/services/data_normalization/DataNormalizationService.cpp
    src/services/news/NewsService.cpp
    src/services/news/NewsClusterService.cpp
    src/services/news/NewsMonitorService.cpp
    src/services/news/NewsNlpService.cpp
    src/services/databento/DatabentoService.cpp
    src/services/dbnomics/DBnomicsService.cpp
    src/services/gov_data/GovDataService.cpp
    src/services/equity/EarningsRefresh.cpp
    src/services/equity/EarningsSignal.cpp
    src/services/equity/EquityResearchService.cpp
    src/services/equity/ReportService.cpp
    src/services/equity/TechnicalRating.cpp
    src/services/finnhub/FinnhubService.cpp
    src/services/video/LiveHlsProxy.cpp
    src/services/query/QueryStore.cpp
    src/services/ma_analytics/MAAnalyticsService.cpp
    src/services/geopolitics/GeopoliticsService.cpp
    src/services/maritime/MaritimeService.cpp
    src/services/ai_quant_lab/AIQuantLabService.cpp
    src/services/backtesting/BacktestingService.cpp
    src/services/algo_trading/AlgoTradingService.cpp
    src/services/portfolio/PortfolioFx.cpp
    src/services/portfolio/PortfolioLedger.cpp
    src/services/portfolio/BrokerCsv.cpp
    src/services/portfolio/PortfolioReturns.cpp
    src/services/portfolio/PortfolioService.cpp
    src/services/portfolio/PortfolioAnalyticsService.cpp
    src/services/app_context/AppContextService.cpp
    src/services/quantlib/QuantLibClient.cpp
    src/services/economics/EconomicsService.cpp
    src/services/agents/AgentService.cpp
    src/services/agents/AgentScheduler.cpp
    src/services/agents/BudgetService.cpp
    src/services/agents/ElicitBridge.cpp
    src/services/agents/SkillProposalService.cpp
    src/services/agents/SkillStore.cpp
    src/services/file_manager/FileManagerService.cpp

    src/services/sectors/SectorResolver.cpp
    src/services/workflow/NodeRegistry.cpp
    src/services/workflow/WorkflowService.cpp
    src/services/workflow/WorkflowExecutor.cpp
    src/services/workflow/ExpressionEngine.cpp

    src/services/workspace/WorkspaceSerializer.cpp
    src/services/workspace/WorkspaceManager.cpp
    src/services/workspace/WorkspaceTemplates.cpp
    src/services/pushpins/PushpinService.cpp
    src/services/workflow/nodes/TriggerNodes.cpp
    src/services/workflow/nodes/ControlFlowNodes.cpp
    src/services/workflow/nodes/UtilityNodes.cpp
    src/services/workflow/nodes/MarketDataNodes.cpp
    src/services/workflow/nodes/TradingNodes.cpp
    src/services/workflow/nodes/AnalyticsNodes.cpp
    src/services/workflow/nodes/SafetyNodes.cpp
    src/services/workflow/nodes/NotificationNodes.cpp
    src/services/workflow/nodes/AgentNodes.cpp
    src/services/workflow/nodes/FileNodes.cpp
    src/services/workflow/nodes/DataFormatNodes.cpp
    src/services/workflow/nodes/IntegrationNodes.cpp

    # Workflow infrastructure
    src/services/workflow/ExecutionHooks.cpp
    src/services/workflow/AuditLogger.cpp
    src/services/workflow/ConfirmationService.cpp
    src/services/workflow/adapters/ServiceBridges.cpp
    src/services/polymarket/PolymarketService.cpp
    src/services/polymarket/PolymarketWebSocket.cpp

    # Prediction Markets — exchange-agnostic abstraction (Polymarket, Kalshi, …)
    src/services/prediction/PredictionExchangeAdapter.cpp
    src/services/prediction/PredictionExchangeRegistry.cpp
    src/services/prediction/PredictionCredentialStore.cpp
    src/services/prediction/polymarket/PolymarketTypeMap.cpp
    src/services/prediction/polymarket/PolymarketAdapter.cpp
    src/services/prediction/kalshi/KalshiRestClient.cpp
    src/services/prediction/kalshi/KalshiWsClient.cpp
    src/services/prediction/kalshi/KalshiAdapter.cpp

    # Speech-to-text service (local Whisper or Deepgram, scripts/voice)
    src/services/stt/SpeechService.cpp

    # Text-to-speech service (local Piper)
    src/services/tts/TtsService.cpp

    # Modular notification system — service + 15 providers
    src/services/notifications/NotificationService.cpp
    # User alerts (price crossings, day moves, insider clusters) — delivered
    # through NotificationService; pure rules in services/alerts/AlertRules.h
    src/services/alerts/AlertService.cpp
    src/services/notifications/providers/TelegramProvider.cpp
    src/services/notifications/providers/DiscordProvider.cpp
    src/services/notifications/providers/SlackProvider.cpp
    src/services/notifications/providers/EmailProvider.cpp
    src/services/notifications/providers/WhatsAppProvider.cpp
    src/services/notifications/providers/PushoverProvider.cpp
    src/services/notifications/providers/NtfyProvider.cpp
    src/services/notifications/providers/PushbulletProvider.cpp
    src/services/notifications/providers/GotifyProvider.cpp
    src/services/notifications/providers/MattermostProvider.cpp
    src/services/notifications/providers/TeamsProvider.cpp
    src/services/notifications/providers/WebhookProvider.cpp
    src/services/notifications/providers/PagerDutyProvider.cpp
    src/services/notifications/providers/OpsgenieProvider.cpp
    src/services/notifications/providers/SMSProvider.cpp
)

# UI framework
set(UI_SOURCES
    src/ui/formatting/CsvWriter.cpp
    src/ui/markdown/MarkdownRenderer.cpp
    src/ui/notifications/ToastService.cpp
    src/ui/theme/Theme.cpp
    src/ui/theme/ThemeManager.cpp
    src/ui/theme/StyleSheets.cpp
    src/ui/theme/Tooltips.cpp
    src/ui/widgets/GroupBadge.cpp
    src/ui/pushpins/PushpinBar.cpp
    src/ui/components/ComponentCard.cpp
    src/ui/components/ComponentBrowserDialog.cpp
    src/ui/widgets/WorldMapWidget.cpp
    src/ui/widgets/LoadingOverlay.cpp
    src/ui/widgets/StatusPill.cpp
    src/ui/widgets/SpeedSparkline.cpp
    src/ui/tables/DataTable.cpp
    src/ui/tables/TickFlashDelegate.cpp
    src/ui/charts/ChartFactory.cpp
    src/ui/charts/InlineSparkline.cpp
    src/ui/navigation/FKeyBar.cpp
    src/ui/navigation/StatusBar.cpp
    src/ui/navigation/ToolBar.cpp
    src/ui/navigation/CommandBar.cpp
    src/ui/navigation/DockToolBar.cpp
    src/ui/navigation/DockStatusBar.cpp

    src/ui/workspace/WorkspaceNewDialog.cpp
    src/ui/workspace/WorkspaceOpenDialog.cpp
    src/ui/workspace/WorkspaceSaveAsDialog.cpp

    # In-app notification widgets
    src/ui/widgets/NotifToast.cpp
    src/ui/widgets/NotifBell.cpp
    src/ui/widgets/UpdateIndicator.cpp
    src/ui/widgets/EngineIndicator.cpp
    src/ui/widgets/NotifPanel.cpp
    src/ui/alerts/AlertsDialog.cpp
)

# Screens
set(SCREEN_SOURCES
    src/screens/auth/LoginScreen.cpp
    src/screens/auth/RegisterScreen.cpp
    src/screens/auth/LockScreen.cpp
    src/screens/devtools/DataHubInspector.cpp
    src/screens/profile/ProfileScreen.cpp
    src/screens/settings/SettingsScreen.cpp
    src/screens/settings/LlmConfigSection.cpp
    src/screens/settings/EnginePanel.cpp
    src/screens/settings/AiRolesSection.cpp
    src/screens/settings/AiConfigSection.cpp
    src/screens/settings/McpServersSection.cpp
    src/screens/settings/KeybindingsSection.cpp
    src/screens/settings/PythonEnvSection.cpp
    src/screens/settings/VoiceConfigSection.cpp
    src/screens/settings/PriceSourceSection.cpp
    src/screens/settings/CredentialsSection.cpp
    src/screens/settings/StoredCredentialsSection.cpp
    src/screens/settings/AiSystemSection.cpp
    src/screens/settings/SchedulerSection.cpp
    src/screens/settings/ArtefactsSection.cpp
    src/screens/settings/ActivityPanelSection.cpp
    src/screens/settings/SkillDiffsSection.cpp
    src/screens/about/AboutScreen.cpp
    src/screens/trade_viz/TradeVizScreen.cpp
    src/screens/docs/DocsScreen.cpp
    src/screens/workbench/WorkbenchScreen.cpp
    src/screens/setup/SetupScreen.cpp
    src/screens/info/ContactScreen.cpp
    src/screens/info/TermsScreen.cpp
    src/screens/info/PrivacyScreen.cpp
    src/screens/info/TrademarksScreen.cpp
    src/screens/info/HelpScreen.cpp
    src/screens/knowledge/AbbreviationsColumn.cpp
    src/screens/knowledge/CalculatorPanel.cpp
    src/screens/knowledge/CategoryColumn.cpp
    src/screens/knowledge/ContentLoader.cpp
    src/screens/knowledge/EntryBodyView.cpp
    src/screens/knowledge/ExposurePanel.cpp
    src/screens/knowledge/GroupedPane.cpp
    src/screens/knowledge/HelpHint.cpp
    src/screens/knowledge/HistoryChartPanel.cpp
    src/screens/knowledge/KnowledgeScreen.cpp
    src/screens/knowledge/LiveDataPanel.cpp
    src/screens/knowledge/PeerChartPanel.cpp
    src/screens/knowledge/RailWidget.cpp
    src/screens/file_manager/FileManagerScreen.cpp
    src/screens/relationship_map/RelationshipMapScreen.cpp
    src/screens/relationship_map/RelationshipGraphScene.cpp
    src/services/relationship_map/RelationshipMapService.cpp
    src/services/report_builder/ReportBuilderService.cpp
    src/services/report_builder/ReportBuilderTemplates.cpp
    src/screens/markets/MarketsScreen.cpp
    src/screens/markets/MarketChartDialog.cpp
    src/screens/markets/world/WorldMarketsView.cpp
    src/screens/dashboard/widgets/ConditionAddDialog.cpp
    src/screens/dashboard/widgets/ConditionTableWidget.cpp
    src/screens/dashboard/widgets/YieldCurveWidget.cpp
    src/screens/dashboard/widgets/CorrelationWidget.cpp
    src/screens/dashboard/widgets/MacroOddsWidget.cpp
    src/screens/dashboard/widgets/MarketChartWidget.cpp
    src/screens/dashboard/widgets/MarketsWidgets.cpp
    src/screens/markets/overview/MarketScreenPage.cpp
    src/screens/markets/overview/MarketTemplates.cpp
    src/screens/futures/FuturesWidgets.cpp
    src/screens/dashboard/canvas/SelectionLink.cpp
    src/screens/dashboard/DashboardScreen.cpp
    src/screens/dashboard/DashboardToolBar.cpp
    src/screens/dashboard/MarketPulsePanel.cpp
    src/ui/widgets/TickerBar.cpp
    src/screens/dashboard/canvas/DashboardCanvas.cpp
    src/screens/dashboard/canvas/WidgetTile.cpp
    src/screens/dashboard/canvas/PlaceholderOverlay.cpp
    src/screens/dashboard/canvas/DashboardTemplates.cpp
    src/screens/dashboard/canvas/WidgetRegistry.cpp
    src/screens/dashboard/canvas/AddWidgetDialog.cpp
    src/screens/dashboard/canvas/TemplatePicker.cpp
    src/screens/dashboard/widgets/BaseWidget.cpp
    src/screens/dashboard/widgets/QuoteTableWidget.cpp
    src/screens/dashboard/widgets/TopMoversWidget.cpp
    src/screens/dashboard/widgets/NewsWidget.cpp
    src/screens/dashboard/widgets/SectorHeatmapWidget.cpp
    src/screens/dashboard/widgets/StockQuoteWidget.cpp
    src/screens/dashboard/widgets/WatchlistWidget.cpp
    src/screens/dashboard/widgets/PerformanceWidget.cpp
    src/screens/dashboard/widgets/MarketSentimentWidget.cpp
    src/screens/dashboard/widgets/EarningsCalendarWidget.cpp
    src/screens/dashboard/widgets/EconomicCalendarWidget.cpp
    src/screens/dashboard/widgets/IpoCalendarWidget.cpp
    src/screens/dashboard/widgets/OwnershipCalendarWidget.cpp
    src/screens/dashboard/widgets/ScreenerWidget.cpp
    src/screens/dashboard/widgets/RiskMetricsWidget.cpp
    src/screens/dashboard/widgets/PortfolioSummaryWidget.cpp
    src/screens/dashboard/widgets/QuickTradeWidget.cpp
    src/screens/dashboard/widgets/OpenPositionsWidget.cpp
    src/screens/dashboard/widgets/OrderBookMiniWidget.cpp
    src/screens/dashboard/widgets/MarginUsageWidget.cpp
    src/screens/dashboard/widgets/TodayPnLWidget.cpp
    src/screens/dashboard/widgets/BrokerHoldingsWidget.cpp
    src/screens/dashboard/widgets/MarketQuoteStripWidget.cpp
    src/screens/dashboard/widgets/CryptoTickerWidget.cpp
    src/screens/dashboard/widgets/PolymarketPriceWidget.cpp
    src/screens/dashboard/widgets/AgentErrorsWidget.cpp
    src/screens/dashboard/widgets/SparklineStripWidget.cpp
    src/screens/dashboard/widgets/TradeTapeWidget.cpp
    src/screens/dashboard/widgets/NewsCategoryWidget.cpp
    src/screens/dashboard/widgets/RecentFilesWidget.cpp
    src/screens/dashboard/widgets/VideoPlayerWidget.cpp
    src/screens/dashboard/widgets/OffscreenVideoScaler.cpp
    src/screens/dashboard/widgets/WebScraperWidget.cpp
    src/screens/report_builder/ReportBuilderScreen.cpp
    src/screens/report_builder/ComponentToolbar.cpp
    src/screens/report_builder/DocumentCanvas.cpp
    src/screens/report_builder/PropertiesPanel.cpp
    src/screens/news/NewsScreen.cpp
    src/screens/news/NewsCommandBar.cpp
    src/screens/news/NewsSidePanel.cpp
    src/screens/news/WhatsOnPanel.cpp
    src/screens/news/WeeklyBriefPanel.cpp
    src/screens/news/NewsPeekPane.cpp
    src/screens/news/NewsFeedPanel.cpp
    src/screens/news/NewsFeedModel.cpp
    src/screens/news/NewsFeedDelegate.cpp
    src/screens/news/NewsDetailPanel.cpp
    src/screens/news/NewsTickerStrip.cpp
    src/screens/social/SocialScreen.cpp
    src/screens/social/BriefsPanel.cpp
    src/screens/crypto_trading/CryptoTradingScreen.cpp
    src/screens/crypto_trading/CryptoTickerBar.cpp
    src/screens/crypto_trading/CryptoWatchlist.cpp
    src/screens/crypto_trading/CryptoChart.cpp
    src/screens/crypto_trading/CryptoOrderEntry.cpp
    src/screens/crypto_trading/CryptoOrderBook.cpp
    src/screens/crypto_trading/CryptoBottomPanel.cpp
    src/screens/crypto_trading/CryptoCredentials.cpp
    src/screens/crypto_trading/CryptoTimeSales.cpp
    src/screens/crypto_trading/CryptoDepthChart.cpp
    src/screens/equity_trading/EquityTradingScreen.cpp
    src/screens/equity_trading/EquityTickerBar.cpp
    src/screens/equity_trading/EquityWatchlist.cpp
    src/screens/equity_trading/EquityChart.cpp
    src/screens/equity_trading/EquityOrderEntry.cpp
    src/screens/equity_trading/EquityOrderBook.cpp
    src/screens/equity_trading/EquityBottomPanel.cpp
    src/screens/equity_trading/AccountManagementDialog.cpp
    src/screens/equity_trading/BroadcastOrderDialog.cpp
    src/screens/data_sources/DataSourcesScreen.cpp
    src/screens/data_sources/ConnectorRegistry.cpp
    src/screens/data_sources/connectors/RelationalDatabases.cpp
    src/screens/data_sources/connectors/NoSqlDatabases.cpp
    src/screens/data_sources/connectors/TimeSeriesDatabases.cpp
    src/screens/data_sources/connectors/FileSources.cpp
    src/screens/data_sources/connectors/ApiStreaming.cpp
    src/screens/data_sources/connectors/CloudStorage.cpp
    src/screens/data_sources/connectors/MarketData.cpp
    src/screens/data_sources/connectors/SearchWarehouse.cpp
    src/screens/data_sources/connectors/AlternativeData.cpp
    src/screens/data_sources/connectors/OpenBanking.cpp
    src/screens/notes/NotesScreen.cpp
    src/screens/watchlist/WatchlistScreen.cpp
    src/screens/surface_analytics/SurfaceEquityMath.cpp
    src/screens/surface_analytics/SurfaceCsvImporter.cpp
    src/screens/surface_analytics/Surface3DWidget.cpp
    src/screens/surface_analytics/SurfaceTableWidget.cpp
    src/screens/surface_analytics/SurfaceLineWidget.cpp
    src/screens/surface_analytics/SurfaceControlPanel.cpp
    src/screens/surface_analytics/SurfaceDataInspector.cpp
    src/screens/surface_analytics/SurfaceAnalyticsScreen.cpp
    src/screens/dbnomics/DBnomicsDataTable.cpp
    src/screens/dbnomics/DBnomicsChartWidget.cpp
    src/screens/dbnomics/DBnomicsSelectionPanel.cpp
    src/screens/dbnomics/DBnomicsScreen.cpp

    # Code Editor (Python Colab)
    src/screens/code_editor/CodeEditorScreen.cpp

    # Excel Spreadsheet
    src/screens/excel/ExcelScreen.cpp
    src/screens/excel/SpreadsheetWidget.cpp

    # Government Data
    src/screens/gov_data/GovDataScreen.cpp
    src/screens/gov_data/GovDataProviderPanel.cpp
    src/screens/gov_data/GovDataTreasuryPanel.cpp
    src/screens/gov_data/GovDataCongressPanel.cpp
    src/screens/gov_data/GovDataFrancePanel.cpp
    src/screens/gov_data/GovDataHKPanel.cpp
    src/screens/gov_data/GovDataUKPanel.cpp
    src/screens/gov_data/GovDataAustraliaPanel.cpp

    # Derivatives Pricing
    src/screens/derivatives/DerivativesScreen.cpp

    # AkShare Data Explorer
    src/screens/akshare/AkShareScreen.cpp

    # Asia Markets
    src/screens/asia_markets/AsiaMarketsScreen.cpp

    # Geopolitics
    src/screens/geopolitics/GeopoliticsScreen.cpp
    src/screens/geopolitics/ConflictMonitorPanel.cpp
    src/screens/geopolitics/HDXDataPanel.cpp
    src/screens/geopolitics/RelationshipPanel.cpp
    src/screens/geopolitics/TradeAnalysisPanel.cpp

    # Maritime Intelligence
    src/screens/maritime/MaritimeScreen.cpp

    # AI Quant Lab
    src/screens/ai_quant_lab/AIQuantLabScreen.cpp
    src/screens/ai_quant_lab/QuantModulePanel.cpp

    # Backtesting
    src/screens/backtesting/BacktestingScreen.cpp

    # Algo Trading
    src/screens/algo_trading/AlgoTradingScreen.cpp
    src/screens/algo_trading/StrategyBuilderPanel.cpp
    src/screens/algo_trading/StrategyListPanel.cpp
    src/screens/algo_trading/ScannerPanel.cpp
    src/screens/algo_trading/DeploymentDashboard.cpp

    # Data Mapping
    src/screens/data_mapping/DataMappingScreen.cpp

    # Economics
    src/screens/economics/EconomicsScreen.cpp
    src/screens/economics/panels/EconPanelBase.cpp
    src/screens/economics/panels/WorldBankPanel.cpp
    src/screens/economics/panels/ImfPanel.cpp
    src/screens/economics/panels/FredPanel.cpp
    src/screens/economics/panels/FredAnalyticsPanel.cpp
    src/screens/economics/panels/OecdPanel.cpp
    src/screens/economics/panels/EcbPanel.cpp
    src/screens/economics/panels/EurostatPanel.cpp
    src/screens/economics/panels/BlsPanel.cpp
    src/screens/economics/panels/CensusPanel.cpp
    src/screens/economics/panels/EconDbPanel.cpp
    src/screens/economics/panels/TradingEconomicsPanel.cpp
    src/screens/economics/panels/EconomicCalendarPanel.cpp
    src/screens/economics/panels/BcbPanel.cpp
    src/screens/economics/panels/AkShareChinaPanel.cpp
    src/screens/economics/panels/OnsPanel.cpp
    src/screens/economics/panels/GlobalCentralBanksPanel.cpp
    src/screens/economics/panels/FederalReservePanel.cpp
    src/screens/economics/panels/FiscalDataPanel.cpp
    src/screens/economics/panels/OwIdPanel.cpp
    src/screens/economics/panels/StatCanPanel.cpp
    src/screens/economics/panels/IlostatPanel.cpp
    src/screens/economics/panels/UnComtradePanel.cpp
    src/screens/economics/panels/WorldBankHealthPanel.cpp
    src/screens/economics/panels/BisPanel.cpp
    src/screens/economics/panels/AdbPanel.cpp
    src/screens/economics/panels/CftcPanel.cpp
    src/screens/economics/panels/EiaPanel.cpp
    src/screens/economics/panels/WtoPanel.cpp
    src/screens/economics/panels/UnescoPanel.cpp
    src/screens/economics/panels/BeaPanel.cpp

    # Polymarket / Prediction Markets
    src/screens/polymarket/PolymarketScreen.cpp
    src/screens/polymarket/PolymarketCommandBar.cpp
    src/screens/polymarket/PolymarketMarketCard.cpp
    src/screens/polymarket/PolymarketBrowsePanel.cpp
    src/screens/polymarket/PolymarketDetailPanel.cpp
    src/screens/polymarket/PolymarketOrderBook.cpp
    src/screens/polymarket/PolymarketPriceChart.cpp
    src/screens/polymarket/PolymarketLeaderboard.cpp
    src/screens/polymarket/PolymarketActivityFeed.cpp
    src/screens/polymarket/PolymarketStatusBar.cpp
    src/screens/polymarket/PredictionAccountDialog.cpp
    src/screens/polymarket/ExchangePresentation.cpp
    src/screens/polymarket/PolymarketOrderBlotter.cpp

    # Alpha Arena
    src/screens/alpha_arena/AlphaArenaScreen.cpp

    # Private Market (screen id pre_ipo): marketplace prices + SEC dossiers + IPO calendar
    src/screens/pre_ipo/PreIpoScreen.cpp
    src/screens/pre_ipo/IpoWatchView.cpp
    src/screens/pre_ipo/PrivateMarketCharts.cpp
    src/screens/pre_ipo/PrivateMarketWidgets.cpp
    src/screens/ownership/OwnershipScreen.cpp
    src/screens/screener/ScreenerScreen.cpp
    src/screens/futures/FuturesCharts.cpp
    src/screens/ownership/StockOwnershipPanel.cpp
    src/screens/ownership/OwnershipHeader.cpp
    src/screens/ownership/HoldersTable.cpp
    src/screens/ownership/InsidersPanel.cpp
    src/screens/ownership/InsiderTimeline.cpp
    src/screens/ownership/ShortInterestPanel.cpp
    src/screens/ownership/ScanPanels.cpp
    src/screens/ownership/FirmDetailPanel.cpp
    src/screens/ownership/Form4Dialog.cpp
    src/services/ownership/OwnershipService.cpp
    src/services/pre_ipo/PreIpoService.cpp
    src/services/pre_ipo/ClarityService.cpp
    src/services/pre_ipo/IpoDealsFeed.cpp

    # Alternative Investments
    src/screens/alt_investments/AltInvestmentsScreen.cpp

    # MCP Servers
    src/screens/mcp_servers/McpServersScreen.cpp

    # QuantLib Suite
    src/screens/quantlib/QuantLibScreen.cpp

    # M&A Analytics
    src/screens/ma_analytics/MAAnalyticsScreen.cpp
    src/screens/ma_analytics/MAModulePanel.cpp

    # Equity Research
    src/screens/equity_research/EquityResearchScreen.cpp
    src/screens/equity_research/EquityOverviewTab.cpp
    src/screens/equity_research/PriceRangeStrip.cpp
    src/screens/equity_research/EquityFinancialsTab.cpp
    src/screens/equity_research/EquityAiTab.cpp
    src/screens/equity_research/EarningsReactionChart.cpp
    src/screens/equity_research/EquityAnalysisTab.cpp
    src/screens/equity_research/EquityEarningsTab.cpp
    src/screens/equity_research/EarningsCallAudio.cpp
    src/screens/equity_research/EarningsCallWebPanel.cpp
    src/screens/equity_research/EquityTechnicalsTab.cpp
    src/screens/equity_research/EquityOptionsTab.cpp
    src/screens/equity_research/EquityTalippTab.cpp
    src/screens/equity_research/EquityPeersTab.cpp
    src/screens/equity_research/EquityReportTab.cpp
    src/screens/equity_research/ReportBuilder.cpp
    src/screens/equity_research/ReportCharts.cpp
    src/screens/equity_research/EquityNewsTab.cpp

    # Portfolio
    src/screens/portfolio/PortfolioScreen.cpp
    src/screens/portfolio/PortfolioCommandBar.cpp
    src/screens/portfolio/PortfolioStatsRibbon.cpp
    src/screens/portfolio/PortfolioStatusBar.cpp
    src/screens/portfolio/PortfolioDialogs.cpp
    src/screens/portfolio/BrokerCsvDialog.cpp
    src/screens/portfolio/PortfolioHeatmap.cpp
    src/screens/portfolio/PortfolioPerfChart.cpp
    src/screens/portfolio/PortfolioSectorPanel.cpp
    src/screens/portfolio/PortfolioBlotter.cpp
    src/screens/portfolio/PortfolioTxnPanel.cpp
    src/screens/portfolio/PortfolioSparkline.cpp
    src/screens/portfolio/PortfolioOrderPanel.cpp
    src/screens/portfolio/PortfolioDetailWrapper.cpp
    src/screens/portfolio/views/AnalyticsSectorsView.cpp
    src/screens/portfolio/views/PerformanceRiskView.cpp
    src/screens/portfolio/views/RiskManagementView.cpp
    src/screens/portfolio/views/PortfolioOptimizationView.cpp
    src/screens/portfolio/views/QuantStatsView.cpp
    src/screens/portfolio/views/ReportsView.cpp
    src/screens/portfolio/views/CustomIndexView.cpp
    src/screens/portfolio/views/PlanningView.cpp
    src/screens/portfolio/views/EconomicsView.cpp
    src/screens/portfolio/PortfolioFFNView.cpp
    src/screens/portfolio/PortfolioFuturesView.cpp
    src/screens/portfolio/PortfolioInsightsPanel.cpp

    src/screens/agent_config/AgentConfigScreen.cpp
    src/screens/agent_config/AgentsViewPanel.cpp
    src/screens/agent_config/SystemViewPanel.cpp
    src/screens/agent_config/CreateAgentPanel.cpp
    src/screens/agent_config/ToolsViewPanel.cpp
    src/screens/agent_config/AgentChatPanel.cpp
    src/screens/agent_config/TeamsViewPanel.cpp
    src/screens/agent_config/WorkflowsViewPanel.cpp
    src/screens/agent_config/PlannerViewPanel.cpp

    # Node Editor
    src/screens/node_editor/NodeEditorScreen.cpp
    src/screens/node_editor/canvas/NodeCanvas.cpp
    src/screens/node_editor/canvas/NodeScene.cpp
    src/screens/node_editor/canvas/NodeItem.cpp
    src/screens/node_editor/canvas/PortItem.cpp
    src/screens/node_editor/canvas/EdgeItem.cpp
    src/screens/node_editor/canvas/TempEdge.cpp
    src/screens/node_editor/canvas/MiniMap.cpp
    src/screens/node_editor/palette/NodePalette.cpp
    src/screens/node_editor/properties/NodePropertiesPanel.cpp
    src/screens/node_editor/properties/ParameterWidgets.cpp
    src/screens/node_editor/properties/ExecutionResultsPanel.cpp
    src/screens/node_editor/toolbar/NodeEditorToolbar.cpp
    src/screens/node_editor/toolbar/DeployDialog.cpp

)

# App
set(APP_SOURCES
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/DockScreenRouter.cpp
)

# All screen files compiled individually — they have local statics/functions that
# collide when batched, and many use QLineEdit/etc without explicit includes.
# Infrastructure layers (core, network, auth, storage, services) still benefit from unity.
set_source_files_properties(
    src/screens/about/AboutScreen.cpp
    src/screens/agent_config/AgentChatPanel.cpp
    src/screens/agent_config/AgentConfigScreen.cpp
    src/screens/agent_config/AgentsViewPanel.cpp
    src/screens/agent_config/CreateAgentPanel.cpp
    src/screens/agent_config/PlannerViewPanel.cpp
    src/screens/agent_config/SystemViewPanel.cpp
    src/screens/agent_config/TeamsViewPanel.cpp
    src/screens/agent_config/ToolsViewPanel.cpp
    src/screens/agent_config/WorkflowsViewPanel.cpp
    src/screens/ai_quant_lab/AIQuantLabScreen.cpp
    src/screens/ai_quant_lab/QuantModulePanel.cpp
    src/screens/akshare/AkShareScreen.cpp
    src/screens/algo_trading/AlgoTradingScreen.cpp
    src/screens/algo_trading/DeploymentDashboard.cpp
    src/screens/algo_trading/ScannerPanel.cpp
    src/screens/algo_trading/StrategyBuilderPanel.cpp
    src/screens/algo_trading/StrategyListPanel.cpp
    src/screens/alpha_arena/AlphaArenaScreen.cpp
    src/screens/alt_investments/AltInvestmentsScreen.cpp
    src/screens/pre_ipo/PreIpoScreen.cpp
    src/screens/pre_ipo/IpoWatchView.cpp
    src/screens/pre_ipo/PrivateMarketCharts.cpp
    src/screens/pre_ipo/PrivateMarketWidgets.cpp
    src/screens/ownership/OwnershipScreen.cpp
    src/screens/screener/ScreenerScreen.cpp
    src/screens/futures/FuturesCharts.cpp
    src/screens/ownership/StockOwnershipPanel.cpp
    src/screens/ownership/OwnershipHeader.cpp
    src/screens/ownership/HoldersTable.cpp
    src/screens/ownership/InsidersPanel.cpp
    src/screens/ownership/InsiderTimeline.cpp
    src/screens/ownership/ShortInterestPanel.cpp
    src/screens/ownership/ScanPanels.cpp
    src/screens/ownership/FirmDetailPanel.cpp
    src/screens/ownership/Form4Dialog.cpp
    src/services/ownership/OwnershipService.cpp
    src/services/pre_ipo/PreIpoService.cpp
    src/services/pre_ipo/ClarityService.cpp
    src/services/pre_ipo/IpoDealsFeed.cpp
    src/screens/asia_markets/AsiaMarketsScreen.cpp
    src/screens/auth/LockScreen.cpp
    src/screens/auth/LoginScreen.cpp
    src/screens/auth/RegisterScreen.cpp
    src/screens/backtesting/BacktestingScreen.cpp
    src/screens/code_editor/CodeEditorScreen.cpp
    src/screens/crypto_trading/CryptoBottomPanel.cpp
    src/screens/crypto_trading/CryptoChart.cpp
    src/screens/crypto_trading/CryptoCredentials.cpp
    src/screens/crypto_trading/CryptoOrderBook.cpp
    src/screens/crypto_trading/CryptoOrderEntry.cpp
    src/screens/crypto_trading/CryptoTickerBar.cpp
    src/screens/crypto_trading/CryptoTimeSales.cpp
    src/screens/crypto_trading/CryptoTradingScreen.cpp
    src/screens/crypto_trading/CryptoWatchlist.cpp
    src/screens/crypto_trading/CryptoDepthChart.cpp
    src/screens/equity_trading/EquityTradingScreen.cpp
    src/screens/equity_trading/EquityTickerBar.cpp
    src/screens/equity_trading/EquityWatchlist.cpp
    src/screens/equity_trading/EquityChart.cpp
    src/screens/equity_trading/EquityOrderEntry.cpp
    src/screens/equity_trading/EquityOrderBook.cpp
    src/screens/equity_trading/EquityBottomPanel.cpp
    src/screens/equity_trading/AccountManagementDialog.cpp
    src/screens/equity_trading/BroadcastOrderDialog.cpp
    src/screens/data_sources/DataSourcesScreen.cpp
    src/screens/data_sources/ConnectorRegistry.cpp
    src/screens/data_sources/connectors/RelationalDatabases.cpp
    src/screens/data_sources/connectors/NoSqlDatabases.cpp
    src/screens/data_sources/connectors/TimeSeriesDatabases.cpp
    src/screens/data_sources/connectors/FileSources.cpp
    src/screens/data_sources/connectors/ApiStreaming.cpp
    src/screens/data_sources/connectors/CloudStorage.cpp
    src/screens/data_sources/connectors/MarketData.cpp
    src/screens/data_sources/connectors/SearchWarehouse.cpp
    src/screens/data_sources/connectors/AlternativeData.cpp
    src/screens/data_sources/connectors/OpenBanking.cpp
    src/screens/dashboard/DashboardScreen.cpp
    src/screens/dashboard/DashboardToolBar.cpp
    src/screens/dashboard/MarketPulsePanel.cpp
    src/ui/widgets/TickerBar.cpp
    src/screens/dashboard/canvas/AddWidgetDialog.cpp
    src/screens/dashboard/canvas/DashboardCanvas.cpp
    src/screens/dashboard/canvas/DashboardTemplates.cpp
    src/screens/dashboard/canvas/PlaceholderOverlay.cpp
    src/screens/dashboard/canvas/TemplatePicker.cpp
    src/screens/dashboard/canvas/WidgetRegistry.cpp
    src/screens/dashboard/canvas/WidgetTile.cpp
    src/screens/dashboard/widgets/BaseWidget.cpp
    src/screens/dashboard/widgets/EarningsCalendarWidget.cpp
    src/screens/dashboard/widgets/EconomicCalendarWidget.cpp
    src/services/calendar/CompanyEventsFeed.cpp
    src/services/screener/ScreenerService.cpp
    src/services/social/SocialBackend.cpp
    src/services/notebook/NotebookKernel.cpp
    src/services/calendar/EarningsCalendarFeed.cpp
    src/services/calendar/MacroCalendarFeed.cpp
    src/screens/dashboard/widgets/IpoCalendarWidget.cpp
    src/screens/dashboard/widgets/OwnershipCalendarWidget.cpp
    src/screens/dashboard/widgets/MarketSentimentWidget.cpp
    src/screens/dashboard/widgets/NewsWidget.cpp
    src/screens/dashboard/widgets/PerformanceWidget.cpp
    src/screens/dashboard/widgets/PortfolioSummaryWidget.cpp
    src/screens/dashboard/widgets/QuickTradeWidget.cpp
    src/screens/dashboard/widgets/OpenPositionsWidget.cpp
    src/screens/dashboard/widgets/OrderBookMiniWidget.cpp
    src/screens/dashboard/widgets/MarginUsageWidget.cpp
    src/screens/dashboard/widgets/TodayPnLWidget.cpp
    src/screens/dashboard/widgets/BrokerHoldingsWidget.cpp
    src/screens/dashboard/widgets/MarketQuoteStripWidget.cpp
    src/screens/dashboard/widgets/CryptoTickerWidget.cpp
    src/screens/dashboard/widgets/PolymarketPriceWidget.cpp
    src/screens/dashboard/widgets/AgentErrorsWidget.cpp
    src/screens/dashboard/widgets/SparklineStripWidget.cpp
    src/screens/dashboard/widgets/TradeTapeWidget.cpp
    src/screens/dashboard/widgets/NewsCategoryWidget.cpp
    src/screens/dashboard/widgets/QuoteTableWidget.cpp
    src/screens/dashboard/widgets/RiskMetricsWidget.cpp
    src/screens/dashboard/widgets/ScreenerWidget.cpp
    src/screens/dashboard/widgets/SectorHeatmapWidget.cpp
    src/screens/dashboard/widgets/StockQuoteWidget.cpp
    src/screens/dashboard/widgets/TopMoversWidget.cpp
    src/screens/dashboard/widgets/RecentFilesWidget.cpp
    src/screens/dashboard/widgets/VideoPlayerWidget.cpp
    src/screens/dashboard/widgets/OffscreenVideoScaler.cpp
    src/screens/dashboard/widgets/WatchlistWidget.cpp
    src/screens/dashboard/widgets/WebScraperWidget.cpp
    src/screens/data_mapping/DataMappingScreen.cpp
    src/screens/dbnomics/DBnomicsChartWidget.cpp
    src/screens/dbnomics/DBnomicsDataTable.cpp
    src/screens/dbnomics/DBnomicsScreen.cpp
    src/screens/dbnomics/DBnomicsSelectionPanel.cpp
    src/screens/docs/DocsScreen.cpp
    src/screens/derivatives/DerivativesScreen.cpp
    src/screens/economics/EconomicsScreen.cpp
    src/screens/equity_research/EquityAiTab.cpp
    src/screens/equity_research/EquityAnalysisTab.cpp
    src/screens/equity_research/EarningsReactionChart.cpp
    src/screens/equity_research/EquityEarningsTab.cpp
    src/screens/equity_research/EarningsCallAudio.cpp
    src/screens/equity_research/EarningsCallWebPanel.cpp
    src/screens/equity_research/EquityFinancialsTab.cpp
    src/screens/equity_research/EquityNewsTab.cpp
    src/screens/equity_research/EquityOverviewTab.cpp
    src/screens/equity_research/PriceRangeStrip.cpp
    src/screens/equity_research/EquityPeersTab.cpp
    src/screens/equity_research/EquityReportTab.cpp
    src/screens/equity_research/ReportBuilder.cpp
    src/screens/equity_research/ReportCharts.cpp
    src/screens/equity_research/EquityResearchScreen.cpp
    src/screens/equity_research/EquityOptionsTab.cpp
    src/screens/equity_research/EquityTalippTab.cpp
    src/screens/equity_research/EquityTechnicalsTab.cpp
    src/screens/excel/ExcelScreen.cpp
    src/screens/excel/SpreadsheetWidget.cpp
    src/screens/file_manager/FileManagerScreen.cpp
    src/screens/geopolitics/ConflictMonitorPanel.cpp
    src/screens/geopolitics/GeopoliticsScreen.cpp
    src/screens/geopolitics/HDXDataPanel.cpp
    src/screens/geopolitics/RelationshipPanel.cpp
    src/screens/geopolitics/TradeAnalysisPanel.cpp
    src/screens/gov_data/GovDataCongressPanel.cpp
    src/screens/gov_data/GovDataProviderPanel.cpp
    src/screens/gov_data/GovDataScreen.cpp
    src/screens/gov_data/GovDataTreasuryPanel.cpp
    src/screens/gov_data/GovDataFrancePanel.cpp
    src/screens/gov_data/GovDataHKPanel.cpp
    src/screens/gov_data/GovDataUKPanel.cpp
    src/screens/gov_data/GovDataAustraliaPanel.cpp
    src/screens/info/ContactScreen.cpp
    src/screens/info/HelpScreen.cpp
    src/screens/info/PrivacyScreen.cpp
    src/screens/info/TermsScreen.cpp
    src/screens/info/TrademarksScreen.cpp
    src/screens/knowledge/AbbreviationsColumn.cpp
    src/screens/knowledge/CalculatorPanel.cpp
    src/screens/knowledge/CategoryColumn.cpp
    src/screens/knowledge/ContentLoader.cpp
    src/screens/knowledge/EntryBodyView.cpp
    src/screens/knowledge/ExposurePanel.cpp
    src/screens/knowledge/GroupedPane.cpp
    src/screens/knowledge/HelpHint.cpp
    src/screens/knowledge/HistoryChartPanel.cpp
    src/screens/knowledge/KnowledgeScreen.cpp
    src/screens/knowledge/LiveDataPanel.cpp
    src/screens/knowledge/PeerChartPanel.cpp
    src/screens/knowledge/RailWidget.cpp
    src/screens/ma_analytics/MAAnalyticsScreen.cpp
    src/screens/ma_analytics/MAModulePanel.cpp
    src/screens/maritime/MaritimeScreen.cpp
    src/screens/markets/MarketChartDialog.cpp
    src/screens/markets/MarketsScreen.cpp
    src/screens/markets/world/WorldMarketsView.cpp
    src/screens/dashboard/widgets/ConditionAddDialog.cpp
    src/screens/dashboard/widgets/ConditionTableWidget.cpp
    src/screens/dashboard/widgets/YieldCurveWidget.cpp
    src/screens/dashboard/widgets/CorrelationWidget.cpp
    src/screens/dashboard/widgets/MacroOddsWidget.cpp
    src/screens/dashboard/widgets/MarketChartWidget.cpp
    src/screens/dashboard/widgets/MarketsWidgets.cpp
    src/screens/markets/overview/MarketScreenPage.cpp
    src/screens/markets/overview/MarketTemplates.cpp
    src/screens/futures/FuturesWidgets.cpp
    src/screens/dashboard/canvas/SelectionLink.cpp
    src/screens/mcp_servers/McpServersScreen.cpp
    src/screens/news/NewsCommandBar.cpp
    src/screens/news/NewsDetailPanel.cpp
    src/screens/news/NewsFeedDelegate.cpp
    src/screens/news/NewsFeedModel.cpp
    src/screens/news/NewsFeedPanel.cpp
    src/screens/news/NewsScreen.cpp
    src/screens/news/NewsSidePanel.cpp
    src/screens/news/WhatsOnPanel.cpp
    src/screens/news/WeeklyBriefPanel.cpp
    src/screens/news/NewsPeekPane.cpp
    src/screens/news/NewsTickerStrip.cpp
    src/screens/node_editor/NodeEditorScreen.cpp
    src/screens/node_editor/canvas/EdgeItem.cpp
    src/screens/node_editor/canvas/MiniMap.cpp
    src/screens/node_editor/canvas/NodeCanvas.cpp
    src/screens/node_editor/canvas/NodeItem.cpp
    src/screens/node_editor/canvas/NodeScene.cpp
    src/screens/node_editor/canvas/PortItem.cpp
    src/screens/node_editor/canvas/TempEdge.cpp
    src/screens/node_editor/palette/NodePalette.cpp
    src/screens/node_editor/properties/ExecutionResultsPanel.cpp
    src/screens/node_editor/properties/NodePropertiesPanel.cpp
    src/screens/node_editor/properties/ParameterWidgets.cpp
    src/screens/node_editor/toolbar/DeployDialog.cpp
    src/screens/node_editor/toolbar/NodeEditorToolbar.cpp
    src/screens/notes/NotesScreen.cpp
    src/screens/portfolio/PortfolioInsightsPanel.cpp
    src/screens/portfolio/PortfolioBlotter.cpp
    src/screens/portfolio/PortfolioCommandBar.cpp
    src/screens/portfolio/PortfolioDetailWrapper.cpp
    src/screens/portfolio/PortfolioDialogs.cpp
    src/screens/portfolio/BrokerCsvDialog.cpp
    src/screens/portfolio/PortfolioFFNView.cpp
    src/screens/portfolio/PortfolioFuturesView.cpp
    src/screens/portfolio/PortfolioHeatmap.cpp
    src/screens/portfolio/PortfolioOrderPanel.cpp
    src/screens/portfolio/PortfolioPerfChart.cpp
    src/screens/portfolio/PortfolioScreen.cpp
    src/screens/portfolio/PortfolioTxnPanel.cpp
    src/screens/portfolio/PortfolioSectorPanel.cpp
    src/screens/portfolio/PortfolioSparkline.cpp
    src/screens/portfolio/PortfolioStatsRibbon.cpp
    src/screens/portfolio/PortfolioStatusBar.cpp
    src/screens/portfolio/views/AnalyticsSectorsView.cpp
    src/screens/portfolio/views/CustomIndexView.cpp
    src/screens/portfolio/views/EconomicsView.cpp
    src/screens/portfolio/views/PerformanceRiskView.cpp
    src/screens/portfolio/views/PlanningView.cpp
    src/screens/portfolio/views/PortfolioOptimizationView.cpp
    src/screens/portfolio/views/QuantStatsView.cpp
    src/screens/portfolio/views/ReportsView.cpp
    src/screens/portfolio/views/RiskManagementView.cpp
    src/screens/profile/ProfileScreen.cpp
    src/screens/relationship_map/RelationshipGraphScene.cpp
    src/screens/relationship_map/RelationshipMapScreen.cpp
    src/screens/report_builder/ComponentToolbar.cpp
    src/screens/report_builder/DocumentCanvas.cpp
    src/screens/report_builder/PropertiesPanel.cpp
    src/screens/report_builder/ReportBuilderScreen.cpp
    src/screens/settings/KeybindingsSection.cpp
    src/screens/settings/PythonEnvSection.cpp
    src/screens/settings/LlmConfigSection.cpp
    src/screens/settings/AiRolesSection.cpp
    src/screens/settings/AiConfigSection.cpp
    src/screens/settings/McpServersSection.cpp
    src/screens/settings/SettingsScreen.cpp
    src/screens/setup/SetupScreen.cpp
    src/screens/social/SocialScreen.cpp
    src/screens/social/BriefsPanel.cpp
    src/screens/surface_analytics/Surface3DWidget.cpp
    src/screens/surface_analytics/SurfaceAnalyticsScreen.cpp
    src/screens/surface_analytics/SurfaceControlPanel.cpp
    src/screens/surface_analytics/SurfaceCsvImporter.cpp
    src/screens/surface_analytics/SurfaceDataInspector.cpp
    src/screens/surface_analytics/SurfaceEquityMath.cpp
    src/screens/surface_analytics/SurfaceLineWidget.cpp
    src/screens/surface_analytics/SurfaceTableWidget.cpp
    src/screens/trade_viz/TradeVizScreen.cpp
    src/screens/watchlist/WatchlistScreen.cpp
    # Storage repositories — each defines kCols in namespace finterm
    src/storage/repositories/AgentConfigRepository.cpp
    src/storage/repositories/AgentTraceRepository.cpp
    src/storage/repositories/TeamRepository.cpp
    src/storage/repositories/ChatArtefactRepository.cpp
    src/storage/repositories/AgentFeedbackRepository.cpp
    src/storage/repositories/ToolKillswitchRepository.cpp
    src/storage/repositories/ChatRepository.cpp
    src/storage/repositories/DataSourceRepository.cpp
    src/storage/repositories/LlmConfigRepository.cpp
    src/storage/repositories/McpServerRepository.cpp
    src/storage/repositories/NewsMonitorRepository.cpp
    src/storage/repositories/NewsArticleRepository.cpp
    src/storage/repositories/NotesRepository.cpp
    src/storage/repositories/PaperTradingRepository.cpp
    src/storage/repositories/PortfolioRepository.cpp
    src/storage/repositories/SettingsRepository.cpp
    src/storage/repositories/WatchlistRepository.cpp
    src/storage/repositories/WorkflowRepository.cpp
    src/storage/repositories/AiPredictionRepository.cpp
    src/storage/repositories/CustomIndexRepository.cpp
    src/storage/repositories/DataMappingRepository.cpp
    src/storage/repositories/AccountRepository.cpp
    # Migration files — each defines sql() helper in anonymous namespace
    src/storage/sqlite/migrations/v001_initial.cpp
    src/storage/sqlite/migrations/v002_llm_chat.cpp
    src/storage/sqlite/migrations/v003_data_mcp_agents.cpp
    src/storage/sqlite/migrations/v004_cleanup.cpp
    src/storage/sqlite/migrations/v005_dashboard_layout.cpp
    src/storage/sqlite/migrations/v006_portfolio_multi.cpp
    src/storage/sqlite/migrations/v007_news_baselines.cpp
    src/storage/sqlite/migrations/v008_workflows.cpp
    src/storage/sqlite/migrations/v009_instruments.cpp
    src/storage/sqlite/migrations/v010_llm_profiles.cpp
    src/storage/sqlite/migrations/v011_custom_indices.cpp
    src/storage/sqlite/migrations/v012_data_normalization.cpp
    src/storage/sqlite/migrations/v013_news_articles.cpp
    src/storage/sqlite/migrations/v014_llm_tools_toggle.cpp
    src/storage/sqlite/migrations/v015_screen_state.cpp
    src/storage/sqlite/migrations/v016_broker_accounts.cpp
    src/storage/sqlite/migrations/v017_perf_indexes.cpp
    src/storage/sqlite/migrations/v018_widget_config.cpp
    src/storage/sqlite/migrations/v019_sector_cache.cpp
    src/storage/sqlite/migrations/v020_security_audit_log.cpp
    src/storage/sqlite/migrations/v021_max_tokens_default.cpp
    src/storage/sqlite/migrations/v022_llm_secure_keys.cpp
    src/storage/sqlite/migrations/v023_llm_profile_runtime.cpp
    src/storage/sqlite/migrations/v024_mcp_marketplace_seed.cpp
    src/storage/sqlite/migrations/v025_mcp_http_transport.cpp
    src/storage/sqlite/migrations/v026_quant_critic_agent.cpp
    src/storage/sqlite/migrations/v027_agent_schedule.cpp
    src/storage/sqlite/migrations/v028_named_agents.cpp
    src/storage/sqlite/migrations/v029_agent_traces.cpp
    src/storage/sqlite/migrations/v030_tool_killswitch.cpp
    src/storage/sqlite/migrations/v031_untrusted_directive.cpp
    src/storage/sqlite/migrations/v032_memory_entries.cpp
    src/storage/sqlite/migrations/v033_mcp_oauth.cpp
    src/storage/sqlite/migrations/v034_chat_artefacts.cpp
    src/storage/sqlite/migrations/v035_agent_feedback.cpp
    src/storage/sqlite/migrations/v036_agent_trace_tool_calls.cpp
    src/storage/sqlite/migrations/v037_teams.cpp
    src/storage/sqlite/migrations/v038_team_mode.cpp
    src/storage/sqlite/migrations/v039_artefact_supersedes.cpp
    src/storage/sqlite/migrations/v040_memory_embeddings.cpp
    src/storage/sqlite/migrations/v041_migrate_fincept_provider.cpp
    src/storage/sqlite/migrations/v042_fix_llm_base_url.cpp
    src/storage/sqlite/migrations/v043_chat_session_persona.cpp
    src/storage/sqlite/migrations/v044_ai_predictions.cpp
    src/storage/sqlite/migrations/v045_physical_ai_watchlist.cpp
    # Polymarket screen files — each defines static fmt_* helpers in same namespace
    src/screens/polymarket/PolymarketScreen.cpp
    src/screens/polymarket/PolymarketCommandBar.cpp
    src/screens/polymarket/PolymarketMarketCard.cpp
    src/screens/polymarket/PolymarketBrowsePanel.cpp
    src/screens/polymarket/PolymarketDetailPanel.cpp
    src/screens/polymarket/PolymarketOrderBook.cpp
    src/screens/polymarket/PolymarketPriceChart.cpp
    src/screens/polymarket/PolymarketLeaderboard.cpp
    src/screens/polymarket/PolymarketActivityFeed.cpp
    src/screens/polymarket/PolymarketStatusBar.cpp
    src/screens/polymarket/PredictionAccountDialog.cpp
    # Trading broker files — each defines static now_ts() in same namespace
    src/trading/brokers/alpaca/AlpacaBroker.cpp
    src/trading/brokers/ibkr/IBKRBroker.cpp
    src/trading/brokers/tradier/TradierBroker.cpp
    src/trading/brokers/saxo/SaxoBankBroker.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)

# NetSpeedMeter needs winsock2.h before <windows.h>; in a unity batch another
# file's <windows.h> would come first and the IP helper headers fail to parse.
set_source_files_properties(src/core/net/NetSpeedMeter.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)

# UpdateInstaller includes <windows.h> (the helper's process): its macros stay
# in its own file.
set_source_files_properties(src/services/updater/UpdateInstaller.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)

# User alerts: file-local helpers in anonymous namespaces (kCategory, money…).
set_source_files_properties(src/services/alerts/AlertService.cpp src/ui/alerts/AlertsDialog.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)

# ADS files use anonymous namespaces and heavy Qt meta-object includes —
# exclude from unity to avoid ODR / symbol clashes in large batches
set_source_files_properties(
    src/app/MainWindow.cpp
    src/app/DockScreenRouter.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)

# MCP core + tools: each defines TAG or kTimeoutMs in shared namespaces —
# must stay excluded from unity (batch size 1 = each file its own TU)
set_source_files_properties(
    src/mcp/McpProvider.cpp
    src/mcp/McpService.cpp
    src/mcp/McpClient.cpp
    src/mcp/McpHttpClient.cpp
    src/mcp/McpOAuth.cpp
    src/mcp/McpOAuthAuthCode.cpp
    src/mcp/McpManager.cpp
    src/mcp/McpInit.cpp
    src/mcp/UntrustedContent.cpp
    src/mcp/tools/NavigationTools.cpp
    src/mcp/tools/MarketsTools.cpp
    src/mcp/tools/WatchlistTools.cpp
    src/mcp/tools/NewsTools.cpp
    src/mcp/tools/MemoryTools.cpp
    src/mcp/tools/ArtefactTools.cpp
    src/mcp/tools/NotesTools.cpp
    src/mcp/tools/FileManagerTools.cpp
    src/mcp/tools/AiChatTools.cpp
    src/mcp/tools/PortfolioTools.cpp
    src/mcp/tools/CryptoTradingTools.cpp
    src/mcp/tools/PaperTradingTools.cpp
    src/mcp/tools/EdgarTools.cpp
    src/mcp/tools/FinancialDatasetsTools.cpp
    src/mcp/tools/QuantNarratorTools.cpp
    src/mcp/tools/MAAnalyticsTools.cpp
    src/mcp/tools/AltInvestmentsTools.cpp
    src/mcp/tools/DataSourcesTools.cpp
    src/mcp/tools/ProfileTools.cpp
    src/mcp/tools/SettingsTools.cpp
    src/mcp/tools/PythonTools.cpp
    src/mcp/tools/SystemTools.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)

# Trading brokers: each defines static now_ts() / TAG at file scope —
# ODR collision if batched, must stay excluded from unity
set_source_files_properties(
    src/trading/brokers/alpaca/AlpacaBroker.cpp
    src/trading/brokers/ibkr/IBKRBroker.cpp
    src/trading/brokers/tradier/TradierBroker.cpp
    src/trading/brokers/saxo/SaxoBankBroker.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE
)

# Storage repositories: each defines kCols — safe to batch in groups of 3
set_source_files_properties(
    src/storage/repositories/AgentConfigRepository.cpp
    src/storage/repositories/AgentTraceRepository.cpp
    src/storage/repositories/TeamRepository.cpp
    src/storage/repositories/ChatArtefactRepository.cpp
    src/storage/repositories/AgentFeedbackRepository.cpp
    src/storage/repositories/ToolKillswitchRepository.cpp
    src/storage/repositories/ChatRepository.cpp
    src/storage/repositories/DataSourceRepository.cpp
    src/storage/repositories/LlmConfigRepository.cpp
    src/storage/repositories/McpServerRepository.cpp
    src/storage/repositories/NewsMonitorRepository.cpp
    src/storage/repositories/NewsArticleRepository.cpp
    src/storage/repositories/NotesRepository.cpp
    src/storage/repositories/PaperTradingRepository.cpp
    src/storage/repositories/PortfolioRepository.cpp
    src/storage/repositories/SettingsRepository.cpp
    src/storage/repositories/WatchlistRepository.cpp
    src/storage/repositories/WorkflowRepository.cpp
    src/storage/repositories/AiPredictionRepository.cpp
    src/storage/repositories/CustomIndexRepository.cpp
    src/storage/repositories/DataMappingRepository.cpp
    PROPERTIES UNITY_BUILD_BATCH_SIZE 3
)

# ── Executable ────────────────────────────────────────────────────────────────
# WIN32     — suppress console window on Windows
# MACOSX_BUNDLE — create a proper .app bundle on macOS
if(WIN32)
    set(_FINTERM_EXE_FLAGS WIN32)
    # Generate app.rc from app.rc.in so FILEVERSION / PRODUCTVERSION / string
    # table reflect the project() VERSION. Without this the exe's PE version
    # resource is 0.0.0.0 and Windows Error Reporting cannot triage crashes
    # by app version (see issue #215). The .ico reference is relative to the
    # generated file's directory, so copy finterm.ico next to it.
    set(FINTERM_VER_MAJOR ${PROJECT_VERSION_MAJOR})
    set(FINTERM_VER_MINOR ${PROJECT_VERSION_MINOR})
    set(FINTERM_VER_PATCH ${PROJECT_VERSION_PATCH})
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/finterm.ico"
        "${CMAKE_CURRENT_BINARY_DIR}/finterm.ico"
        COPYONLY)
    set(_FINTERM_PLATFORM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
elseif(APPLE)
    set(_FINTERM_EXE_FLAGS MACOSX_BUNDLE)
    set(_FINTERM_PLATFORM_SOURCES "")
else()
    set(_FINTERM_EXE_FLAGS "")
    set(_FINTERM_PLATFORM_SOURCES "")
endif()

add_executable(finterm ${_FINTERM_EXE_FLAGS}
    ${CORE_SOURCES}
    ${NETWORK_SOURCES}
    ${DATAHUB_SOURCES}
    ${AUTH_SOURCES}
    ${STORAGE_SOURCES}
    ${PYTHON_SOURCES}
    ${AI_CHAT_SOURCES}
    ${MCP_SOURCES}
    ${TRADING_SOURCES}
    ${SERVICE_SOURCES}
    ${UI_SOURCES}
    ${SCREEN_SOURCES}
    ${APP_SOURCES}
    src/services/updater/UpdateService.cpp
    src/services/updater/UpdatePolicy.cpp
    src/services/updater/UpdateInstaller.cpp
    ${_FINTERM_PLATFORM_SOURCES}
)

target_include_directories(finterm PRIVATE
    src
    ${SingleApplication_SOURCE_DIR}
)

# ── Emitted artifact name ─────────────────────────────────────────────────────
# The CMake target token "finterm" is an internal handle — never seen in
# any shipped artifact (like the C++ namespace finterm). Only the produced
# binary carries the brand:
#   Linux -> finterm.bin    Windows -> finterm.exe    macOS -> finterm.app
if(WIN32)
    set_target_properties(finterm PROPERTIES OUTPUT_NAME "finterm")
elseif(APPLE)
    set_target_properties(finterm PROPERTIES OUTPUT_NAME "finterm")
else()
    set_target_properties(finterm PROPERTIES OUTPUT_NAME "finterm.bin")
endif()

# ── macOS bundle properties ───────────────────────────────────────────────────
if(APPLE)
    # CMake's own template plus the microphone's purpose: without one macOS ends
    # an app that opens it.
    set_target_properties(finterm PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST        "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER    "io.github.yumima.finterm"
        MACOSX_BUNDLE_BUNDLE_NAME       "finterm"
        MACOSX_BUNDLE_BUNDLE_VERSION    "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE         "finterm.icns"
        MACOSX_BUNDLE_COPYRIGHT         "Copyright © 2024-2026 finterm contributors"
    )
    # The plugin is a static library in every Qt; qt_add_executable() would link
    # it. The release checks that it is there.
    if(TARGET Qt6::QDarwinMicrophonePermissionPlugin)
        target_link_libraries(finterm PRIVATE Qt6::QDarwinMicrophonePermissionPlugin)
        target_compile_definitions(finterm PRIVATE FINTERM_IMPORT_MICROPHONE_PERMISSION)
    endif()
    # Copy .icns into the bundle Resources folder at build time
    set(_FINTERM_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/finterm.icns")
    if(EXISTS "${_FINTERM_ICNS}")
        target_sources(finterm PRIVATE "${_FINTERM_ICNS}")
        set_source_files_properties("${_FINTERM_ICNS}"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    else()
        message(WARNING "macOS icon not found at ${_FINTERM_ICNS} — app will show generic icon. "
                        "Generate it with: iconutil -c icns -o resources/finterm.icns <iconset>")
    endif()
endif()

# ── macOS Qt deployment (equivalent of windeployqt) ──────────────────────────
if(APPLE AND DEPLOY_QT)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "$ENV{QTDIR}/bin"
    )
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET finterm POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                "$<TARGET_BUNDLE_DIR:finterm>"
                -no-strip -always-overwrite
            COMMENT "Running macdeployqt to bundle Qt frameworks..."
        )
        # Remove unused SQL driver plugins that reference libraries not present
        # on typical macOS installs (ODBC, PostgreSQL, Mimer). These cause
        # harmless but noisy "ERROR: no file at ..." warnings from macdeployqt.
        add_custom_command(TARGET finterm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f
                "$<TARGET_BUNDLE_DIR:finterm>/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib"
                "$<TARGET_BUNDLE_DIR:finterm>/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib"
                "$<TARGET_BUNDLE_DIR:finterm>/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"
            COMMENT "Removing unused SQL driver plugins (ODBC/PostgreSQL/Mimer)..."
        )
    else()
        message(WARNING "macdeployqt not found — Qt frameworks will not be bundled. "
                        "Pass -DCMAKE_PREFIX_PATH=<Qt install dir> to help CMake find it.")
    endif()
endif()

# Precompiled header — Qt + std headers parsed once, reused across all TUs
# QDebug excluded — it adds overhead in Release and is only needed for debugging
target_precompile_headers(finterm PRIVATE
    # ── Core Qt objects ───────────────────────────────────────────────────────
    <QObject>
    <QWidget>
    <QString>
    <QStringList>
    <QByteArray>
    <QList>
    <QVector>
    <QMap>
    <QHash>
    <QSet>
    <QVariant>
    <QPointer>
    # ── JSON ──────────────────────────────────────────────────────────────────
    <QJsonObject>
    <QJsonArray>
    <QJsonDocument>
    <QJsonValue>
    # ── Layouts ───────────────────────────────────────────────────────────────
    <QVBoxLayout>
    <QHBoxLayout>
    <QGridLayout>
    <QFormLayout>
    <QStackedLayout>
    # ── Common widgets ────────────────────────────────────────────────────────
    <QLabel>
    <QPushButton>
    <QToolButton>
    <QLineEdit>
    <QPlainTextEdit>
    <QTextEdit>
    <QComboBox>
    <QSpinBox>
    <QDoubleSpinBox>
    <QCheckBox>
    <QRadioButton>
    <QSlider>
    <QProgressBar>
    <QScrollArea>
    <QScrollBar>
    <QFrame>
    <QSplitter>
    <QTabWidget>
    <QStackedWidget>
    <QTableWidget>
    <QTableView>
    <QTreeWidget>
    <QTreeView>
    <QListWidget>
    <QListView>
    <QGroupBox>
    <QMessageBox>
    <QDialog>
    <QHeaderView>
    <QAbstractItemView>
    # ── Item models ───────────────────────────────────────────────────────────
    <QStandardItemModel>
    <QStandardItem>
    <QSortFilterProxyModel>
    # ── Timers & dates ────────────────────────────────────────────────────────
    <QTimer>
    <QDateTime>
    <QDate>
    <QTime>
    # ── Network ───────────────────────────────────────────────────────────────
    <QNetworkAccessManager>
    <QNetworkRequest>
    <QNetworkReply>
    <QUrl>
    <QUrlQuery>
    # ── Concurrency & process ─────────────────────────────────────────────────
    <QProcess>
    <QtConcurrent/QtConcurrent>
    <QThread>
    <QMutex>
    <QMutexLocker>
    <QReadWriteLock>
    <QAtomicInt>
    <QFuture>
    <QFutureWatcher>
    # ── I/O & paths ───────────────────────────────────────────────────────────
    <QSettings>
    <QStandardPaths>
    <QDir>
    <QFile>
    <QFileInfo>
    <QSaveFile>
    <QTemporaryFile>
    <QIODevice>
    # ── Graphics & painting ───────────────────────────────────────────────────
    <QPainter>
    <QPen>
    <QBrush>
    <QColor>
    <QFont>
    <QFontMetrics>
    <QPixmap>
    <QIcon>
    <QPalette>
    <QCursor>
    # ── Events ───────────────────────────────────────────────────────────────
    <QEvent>
    <QKeyEvent>
    <QMouseEvent>
    <QResizeEvent>
    <QWheelEvent>
    # ── Charts (Qt6::Charts — required dep) ──────────────────────────────────
    <QChart>
    <QChartView>
    <QLineSeries>
    <QBarSeries>
    <QBarSet>
    <QValueAxis>
    <QDateTimeAxis>
    <QBarCategoryAxis>
    <QCategoryAxis>
    # ── Multimedia (always available — required dep) ───────────────────────────
    <QAudioFormat>
    <QMediaDevices>
    # ── Misc Qt ──────────────────────────────────────────────────────────────
    <QUuid>
    <QRegularExpression>
    <QSqlQuery>
    <QApplication>
    <QAbstractScrollArea>
    # ── std ──────────────────────────────────────────────────────────────────
    <functional>
    <memory>
    <vector>
    <string>
    <optional>
    <variant>
    <atomic>
    <mutex>
    <algorithm>
    <numeric>
    <cmath>
    <cstdint>
    <utility>
    <tuple>
    <array>
    <span>
)

# ── OpenSSL — needed for Google Sheets JWT signing ────────────────────────────
# OPENSSL_ROOT_DIR can be set via:
#   - cmake -DOPENSSL_ROOT_DIR=<path>          (CI: passed from workflow)
#   - environment variable OPENSSL_ROOT_DIR    (CI: set by setup-openssl action)
#   - vcpkg on dev machine (auto-detected below if not already set)
if(WIN32 AND NOT OPENSSL_ROOT_DIR AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
    # Dev machine vcpkg fallback
    if(DEFINED ENV{VCPKG_INSTALLED_DIR} AND EXISTS "$ENV{VCPKG_INSTALLED_DIR}/x64-windows/lib/libcrypto.lib")
        set(OPENSSL_ROOT_DIR "$ENV{VCPKG_INSTALLED_DIR}/x64-windows" CACHE PATH "" FORCE)
    elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/lib/libcrypto.lib")
        set(OPENSSL_ROOT_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows" CACHE PATH "" FORCE)
    endif()
endif()

if(OPENSSL_ROOT_DIR)
    message(STATUS "OPENSSL_ROOT_DIR: ${OPENSSL_ROOT_DIR}")
endif()

find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL found: ${OPENSSL_VERSION} — include: ${OPENSSL_INCLUDE_DIR}")

target_include_directories(finterm PRIVATE
    "${OPENSSL_INCLUDE_DIR}"
    "${md4c_SOURCE_DIR}/src"
)
# WebEngineWidgets: try find_package first; fall back to direct lib probe
# because the CMake dependency chain for WebEngine is fragile when modules
# like QtQuick are installed as part of the base Qt rather than as extras.
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
if(NOT Qt6WebEngineWidgets_FOUND)
    set(_we_lib "${CMAKE_PREFIX_PATH}/lib/libQt6WebEngineWidgets.so")
    set(_we_inc "${CMAKE_PREFIX_PATH}/include/QtWebEngineWidgets")
    if(EXISTS "${_we_lib}" AND EXISTS "${_we_inc}")
        # Direct approach: add include paths and link all Qt deps of WebEngine.
        # Creating IMPORTED targets without their full transitive dep chain
        # causes linker errors; listing the .so files directly avoids that.
        target_include_directories(finterm PRIVATE
            "${CMAKE_PREFIX_PATH}/include/QtWebEngineWidgets"
            "${CMAKE_PREFIX_PATH}/include/QtWebEngineCore")
        foreach(_lib WebEngineWidgets WebEngineCore Positioning WebChannel
                     Qml QmlMeta QmlModels QmlWorkerScript Quick)
            set(_so "${CMAKE_PREFIX_PATH}/lib/libQt6${_lib}.so")
            if(EXISTS "${_so}")
                target_link_libraries(finterm PRIVATE "${_so}")
            endif()
        endforeach()
        set(Qt6WebEngineWidgets_FOUND TRUE)
        message(STATUS "Qt6 WebEngineWidgets found via direct probe — enabling WebEngine video path")
    else()
        message(STATUS "Qt6 WebEngineWidgets not found — WebEngine video path disabled")
    endif()
else()
    message(STATUS "Qt6 WebEngineWidgets found — enabling WebEngine video path")
endif()

target_link_libraries(finterm PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::OpenGLWidgets
    Qt6::PrintSupport
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Multimedia
    QXlsx::QXlsx
    qgeoview
    md4c
    md4c-html
    SingleApplication::SingleApplication
    qtadvanceddocking-qt6
    OpenSSL::SSL
    OpenSSL::Crypto
)

# macOS: link Security.framework + CoreFoundation for SecItem API in SecureStorage
if(APPLE)
    target_link_libraries(finterm PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

# Linux: optionally link libsecret for cryptographically-secure credential
# storage (Secret Service / GNOME Keyring / KWallet via the freedesktop
# compatibility layer). Without libsecret, SecureStorage falls back to
# XOR-obfuscated QSettings which is NOT secure. Install libsecret-1-dev
# (Debian/Ubuntu), libsecret-devel (Fedora), or libsecret (Arch) and
# reconfigure to enable.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
        if(LIBSECRET_FOUND)
            target_link_libraries(finterm PRIVATE PkgConfig::LIBSECRET)
            target_compile_definitions(finterm PRIVATE FINTERM_HAVE_LIBSECRET)
            message(STATUS "libsecret found — Linux SecureStorage will use Secret Service backend")
        else()
            message(WARNING
                "libsecret-1 not found — Linux credential storage falls back to "
                "XOR-obfuscated QSettings (NOT cryptographically secure). "
                "Install libsecret-1-dev and rebuild for production deployments.")
        endif()
    endif()
endif()

# Windows: link dbghelp for the crash-handler minidump writer (see
# src/core/crash/CrashHandler.cpp). Also link Advapi32/Crypt32 explicitly for
# SecureStorage — they're normally pulled in transitively, but when MSVC runs
# /OPT:REF aggressively on release builds the indirect reference via wincred.h
# can get stripped, producing late-link errors.
if(WIN32)
    target_link_libraries(finterm PRIVATE dbghelp Advapi32 Crypt32)
endif()

# Copy OpenSSL DLLs to build output — Windows only
if(WIN32)
    get_target_property(OPENSSL_CRYPTO_DLL OpenSSL::Crypto IMPORTED_LOCATION)
    get_target_property(OPENSSL_SSL_DLL    OpenSSL::SSL    IMPORTED_LOCATION)

    # IMPORTED_LOCATION may point to the .lib on Windows — find the matching .dll
    if(OPENSSL_CRYPTO_DLL MATCHES "\\.lib$" OR NOT OPENSSL_CRYPTO_DLL)
        # Search common locations for the DLL
        find_file(OPENSSL_CRYPTO_DLL_FILE
            NAMES libcrypto-3-x64.dll libcrypto-3.dll libcrypto.dll
            HINTS
                "${OPENSSL_ROOT_DIR}/bin"
                "$ENV{QT_ROOT_DIR}/bin"
                "C:/Program Files/OpenSSL/bin"
                "C:/Program Files/OpenSSL-Win64/bin"
        )
        find_file(OPENSSL_SSL_DLL_FILE
            NAMES libssl-3-x64.dll libssl-3.dll libssl.dll
            HINTS
                "${OPENSSL_ROOT_DIR}/bin"
                "$ENV{QT_ROOT_DIR}/bin"
                "C:/Program Files/OpenSSL/bin"
                "C:/Program Files/OpenSSL-Win64/bin"
        )
    else()
        set(OPENSSL_CRYPTO_DLL_FILE "${OPENSSL_CRYPTO_DLL}")
        set(OPENSSL_SSL_DLL_FILE    "${OPENSSL_SSL_DLL}")
    endif()

    if(OPENSSL_CRYPTO_DLL_FILE AND OPENSSL_SSL_DLL_FILE)
        add_custom_command(TARGET finterm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSSL_CRYPTO_DLL_FILE}"
                "$<TARGET_FILE_DIR:finterm>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPENSSL_SSL_DLL_FILE}"
                "$<TARGET_FILE_DIR:finterm>"
            COMMENT "Copying OpenSSL DLLs..."
        )
        message(STATUS "OpenSSL DLLs to deploy: ${OPENSSL_CRYPTO_DLL_FILE}, ${OPENSSL_SSL_DLL_FILE}")
    else()
        message(WARNING "OpenSSL DLLs not found for POST_BUILD copy — deploy manually")
    endif()
endif()

# ── In-place update helpers, embedded (UpdateInstaller writes them out) ──────
qt_add_resources(finterm "updater_scripts"
    PREFIX "/updater"
    BASE "src/services/updater/scripts"
    FILES
        src/services/updater/scripts/apply-update.sh
        src/services/updater/scripts/apply-update.ps1)

# ── Application version (single source of truth) ─────────────────────────────
# FINTERM_VERSION_STRING is consumed by main.cpp so QApplication::applicationVersion()
# always matches the project() VERSION declared at the top of this file.
#
# Only the release workflow builds a RELEASE: -DFINTERM_RELEASE_BUILD=ON. Every
# other build (finterm.sh build, a dev tree, CI branch builds) is "<version>-dev",
# which the updater refuses (strict x.y.z): a dev build never checks for, offers
# or installs a release over itself, never touches the shared update note, and
# says what it is in the status bar and About. Packaging reads the project
# VERSION, not this string.
option(FINTERM_RELEASE_BUILD "Build an official release (enables the updater)" OFF)
if(FINTERM_RELEASE_BUILD)
    set(FINTERM_APP_VERSION "${CMAKE_PROJECT_VERSION}")
else()
    set(FINTERM_APP_VERSION "${CMAKE_PROJECT_VERSION}-dev")
endif()
# Which build of the version a release is (core/config/BuildInfo): a version can
# be released again from a newer commit, and the updater and About tell the two
# apart by it. A release build that can't say would never be offered the next.
if(FINTERM_RELEASE_BUILD)
    if(NOT FINTERM_BUILD_COMMIT OR NOT FINTERM_BUILD_TIME)
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=*" log -1 "--format=%H;%ct"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE _finterm_git OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
        list(LENGTH _finterm_git _finterm_git_parts)
        if(NOT _finterm_git_parts EQUAL 2)
            message(FATAL_ERROR "A release build needs its commit: build from a git checkout, or set "
                                "FINTERM_BUILD_COMMIT (hash) and FINTERM_BUILD_TIME (seconds since 1970)")
        endif()
        list(GET _finterm_git 0 FINTERM_BUILD_COMMIT)
        list(GET _finterm_git 1 FINTERM_BUILD_TIME)
    endif()
    message(STATUS "finterm build: ${FINTERM_BUILD_COMMIT} (${FINTERM_BUILD_TIME})")
    set_source_files_properties(src/core/config/BuildInfo.cpp PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION TRUE
        COMPILE_DEFINITIONS "FINTERM_BUILD_COMMIT=\"${FINTERM_BUILD_COMMIT}\";FINTERM_BUILD_TIME=${FINTERM_BUILD_TIME}")
endif()
message(STATUS "finterm application version: ${FINTERM_APP_VERSION}")
target_compile_definitions(finterm PRIVATE
    FINTERM_VERSION_STRING="${FINTERM_APP_VERSION}")

# ── Optional feature compile definitions (applied after add_executable) ──────
if(FINTERM_HAS_PRIVATE_HEADERS)
    target_compile_definitions(finterm PRIVATE FINTERM_HAS_QXLSX)
endif()

if(HAS_WEBSOCKETS)
    target_link_libraries(finterm PRIVATE Qt6::WebSockets)
    target_compile_definitions(finterm PRIVATE HAS_QT_WEBSOCKETS)
endif()

if(HAS_MULTIMEDIA)
    target_link_libraries(finterm PRIVATE Qt6::MultimediaWidgets)
    target_compile_definitions(finterm PRIVATE HAS_QT_MULTIMEDIA)
endif()

if(HAS_TTS)
    target_link_libraries(finterm PRIVATE Qt6::TextToSpeech)
    target_compile_definitions(finterm PRIVATE HAS_QT_TTS)
endif()

if(Qt6WebEngineWidgets_FOUND)
    # When found via find_package, link the imported targets.
    # The probe fallback already linked raw .so paths, so guard with TARGET check.
    if(TARGET Qt6::WebEngineWidgets)
        target_link_libraries(finterm PRIVATE Qt6::WebEngineWidgets Qt6::WebEngineCore)
    endif()
    target_compile_definitions(finterm PRIVATE HAS_QT_WEBENGINE)
endif()

# ── Copy qgeoview DLL next to the exe so it's found at runtime ────────────────
if(WIN32)
    add_custom_command(TARGET finterm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:qgeoview>"
            "$<TARGET_FILE_DIR:finterm>"
        COMMENT "Copying qgeoview.dll..."
    )
endif()

# ── Windows: deploy Qt DLLs ──────────────────────────────────────────────────
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt
        HINTS
            "$ENV{QT_ROOT_DIR}/bin"
            "C:/Qt/6.8.3/msvc2022_64/bin"
            "C:/Qt/6.8.2/msvc2022_64/bin"
    )
    if(WINDEPLOYQT_EXE)
        # Only deploy Qt DLLs when explicitly requested via -DDEPLOY_QT=ON
        # Avoids 10-30s overhead on every incremental build
        if(DEPLOY_QT)
            add_custom_command(TARGET finterm POST_BUILD
                COMMAND "${WINDEPLOYQT_EXE}"
                ARGS "--no-translations" "$<TARGET_FILE:finterm>"
                COMMENT "Running windeployqt..."
            )
        endif()
    endif()

    # ── Fix: Qt Maintenance Tool partial-update leaves plugins mismatched ──────
    # The local Qt 6.8.3 install has Qt6Core/Qt6Sql updated (March 2026) but
    # sqldrivers/qsqlite.dll still at the original March 2025 build, causing a
    # crash on startup (0xc0000005 in qsqlite.dll).
    # Qt_temp contains a clean, fully-consistent Qt 6.8.3 install from aqtinstall.
    # Always overwrite the sql plugins with the consistent set after every build.
    set(QT_CONSISTENT_PLUGINS "C:/Qt_temp/6.8.3/msvc2022_64/plugins")
    if(EXISTS "${QT_CONSISTENT_PLUGINS}/sqldrivers/qsqlite.dll")
        add_custom_command(TARGET finterm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:finterm>/sqldrivers"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_CONSISTENT_PLUGINS}/sqldrivers/qsqlite.dll"
                "$<TARGET_FILE_DIR:finterm>/sqldrivers/qsqlite.dll"
            COMMENT "Overwriting sqldrivers with consistent Qt_temp plugins..."
        )
    endif()
endif()

# ── Copy Python scripts to build output ──────────────────────────────────────
# POST_BUILD drags the whole scripts/ tree over. Afterwards we prune junk
# that must never ship — backup folders (agents.deleted.*), test caches,
# local SQLite databases — because on macOS anything unexpected in the
# .app bundle makes codesign reject it ("bundle format unrecognized").
add_custom_command(TARGET finterm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:finterm>/scripts"
    COMMAND ${CMAKE_COMMAND} -DSCRIPTS_DIR=$<TARGET_FILE_DIR:finterm>/scripts
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/prune_scripts_junk.cmake"
    COMMENT "Copying Python scripts to build output + pruning junk..."
)

# ── Copy requirements files to build output ──────────────────────────────────
# PythonSetupManager::find_requirements_file() searches exe_dir/resources/ first.
# Without this POST_BUILD step the files are only available via cmake --install,
# causing setup to silently fail in dev/debug builds.
add_custom_command(TARGET finterm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:finterm>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy1.txt"
        "$<TARGET_FILE_DIR:finterm>/resources/requirements-numpy1.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy2.txt"
        "$<TARGET_FILE_DIR:finterm>/resources/requirements-numpy2.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/component_catalog.json"
        "$<TARGET_FILE_DIR:finterm>/resources/component_catalog.json"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:finterm>/resources/ai"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/ai/capabilities.json"
        "$<TARGET_FILE_DIR:finterm>/resources/ai/capabilities.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/knowledge"
        "$<TARGET_FILE_DIR:finterm>/resources/knowledge"
    COMMENT "Copying requirements files to build output..."
)

# Bundle yt-dlp for dashboard YouTube playback on all platforms.
# Override path with: -DFINTERM_YTDLP_EXE=/abs/path/to/yt-dlp[.exe]
set(FINTERM_YTDLP_EXE "" CACHE FILEPATH "Path to yt-dlp executable to bundle with finterm")
# Default OFF so builds on CI / macOS / Linux succeed without yt-dlp pre-installed.
# Set ON only for official release builds where yt-dlp must be present.
option(FINTERM_REQUIRE_BUNDLED_YTDLP "Fail configure if yt-dlp is missing" OFF)

set(FINTERM_YTDLP_TO_BUNDLE "")
if(FINTERM_YTDLP_EXE)
    set(FINTERM_YTDLP_TO_BUNDLE "${FINTERM_YTDLP_EXE}")
else()
    find_program(FINTERM_YTDLP_AUTO_EXE NAMES yt-dlp yt-dlp.exe)
    if(FINTERM_YTDLP_AUTO_EXE)
        set(FINTERM_YTDLP_TO_BUNDLE "${FINTERM_YTDLP_AUTO_EXE}")
    endif()
endif()

if(FINTERM_YTDLP_TO_BUNDLE AND EXISTS "${FINTERM_YTDLP_TO_BUNDLE}")
    if(WIN32)
        set(FINTERM_YTDLP_OUTPUT_NAME "yt-dlp.exe")
    else()
        set(FINTERM_YTDLP_OUTPUT_NAME "yt-dlp")
    endif()

    add_custom_command(TARGET finterm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FINTERM_YTDLP_TO_BUNDLE}"
            "$<TARGET_FILE_DIR:finterm>/${FINTERM_YTDLP_OUTPUT_NAME}"
        COMMENT "Copying yt-dlp for dashboard video playback..."
    )

    if(UNIX)
        add_custom_command(TARGET finterm POST_BUILD
            COMMAND chmod +x "$<TARGET_FILE_DIR:finterm>/${FINTERM_YTDLP_OUTPUT_NAME}"
            COMMENT "Setting executable permission on bundled yt-dlp..."
        )
    endif()
else()
    set(FINTERM_YTDLP_MISSING_MSG
        "yt-dlp not found. Set FINTERM_YTDLP_EXE or install yt-dlp on build machine. "
        "Bundling is required so end users need no setup."
    )
    if(FINTERM_REQUIRE_BUNDLED_YTDLP)
        message(FATAL_ERROR "${FINTERM_YTDLP_MISSING_MSG}")
    else()
        message(WARNING "${FINTERM_YTDLP_MISSING_MSG}")
    endif()
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
# Used by: cmake --install build --prefix /usr
# Required for AppImage, .deb, .rpm, and system-wide installation on Linux.

if(WIN32)
    # Windows: install exe + all DLLs beside it (windeployqt already copies them)
    install(TARGETS finterm RUNTIME DESTINATION "." COMPONENT core)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/"
            DESTINATION "scripts" COMPONENT scripts
            REGEX "(\\.deleted\\.|\\.pytest_cache|\\.benchmarks|__pycache__|\\.pyc$|\\.db$|\\.sqlite$|\\.sqlite3$|\\.bak$|\\.orig$)" EXCLUDE)
    # Requirements files — needed by PythonSetupManager at first run to install
    # Python packages. Must be deployed beside the exe so find_requirements_file()
    # can locate them at C:/Program Files/finterm/resources/*.txt.
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy1.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy2.txt"
            DESTINATION "resources" COMPONENT core)

elseif(APPLE)
    # macOS: install the .app bundle
    install(TARGETS finterm BUNDLE DESTINATION "." COMPONENT core)
    # Requirements files — deployed beside the .app so PythonSetupManager finds them
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy1.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements-numpy2.txt"
            DESTINATION "finterm.app/Contents/Resources/resources" COMPONENT core)

else()
    # Linux: FHS-compliant layout
    install(TARGETS finterm
            RUNTIME DESTINATION "bin" COMPONENT core)

    # Python analytics scripts
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/"
            DESTINATION "share/finterm/scripts"
            USE_SOURCE_PERMISSIONS COMPONENT scripts
            REGEX "(\\.deleted\\.|\\.pytest_cache|\\.benchmarks|__pycache__|\\.pyc$|\\.db$|\\.sqlite$|\\.sqlite3$|\\.bak$|\\.orig$)" EXCLUDE)

    # App icon (PNG required for hicolor theme; convert .ico → .png in CI)
    set(_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/resources/finterm.png")
    if(EXISTS "${_ICON_PNG}")
        install(FILES "${_ICON_PNG}"
                DESTINATION "share/icons/hicolor/256x256/apps"
                RENAME "finterm.png" COMPONENT core)
    else()
        message(STATUS "Linux icon not found at ${_ICON_PNG} — "
                       "convert resources/finterm.ico to finterm.png for proper desktop integration")
    endif()

    # .desktop file
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/finterm.desktop"
            DESTINATION "share/applications" COMPONENT core)

    # AppStream metadata
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/finterm.appdata.xml"
            DESTINATION "share/metainfo" COMPONENT core)

    # yt-dlp (if bundled)
    if(FINTERM_YTDLP_TO_BUNDLE AND EXISTS "${FINTERM_YTDLP_TO_BUNDLE}")
        install(PROGRAMS "${FINTERM_YTDLP_TO_BUNDLE}"
                DESTINATION "bin"
                RENAME "yt-dlp" COMPONENT core)
    endif()
endif()

# ── Installer (QtIFW via CPackIFW) ───────────────────────────────────────────
# Generate cross-platform installer with uninstaller:
#   cmake -B build -DFINTERM_BUILD_INSTALLER=ON -DDEPLOY_QT=ON ...
#   cmake --build build --config Release
#   cd build && cpack -G IFW -C Release
#
# Requires: Qt Installer Framework (binarycreator on PATH or in Qt install)
# Output:   finterm-<ver>-<platform>-<arch>-setup.exe/.run/.app

option(FINTERM_BUILD_INSTALLER "Generate QtIFW installer via CPack" OFF)

if(FINTERM_BUILD_INSTALLER)

    # ── Windows: capture windeployqt output for the installer ────────────────
    if(WIN32 AND DEPLOY_QT)
        install(DIRECTORY "$<TARGET_FILE_DIR:finterm>/"
                DESTINATION "."
                COMPONENT core
                FILES_MATCHING
                PATTERN "*.dll")
        # Python analytics scripts (4000+ files) — required at runtime by
        # PythonRunner / ExchangeService. Without this the Crypto tab and all
        # Python-backed screens crash on an installed build.
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/scripts/"
                DESTINATION "scripts" COMPONENT scripts)
        # Full resources/ tree — icons, images, stylesheets, data files, etc.
        # Also catches requirements-*.txt. Without this, asset paths resolved
        # via AppPaths::resources_dir() return an empty dir and screens that
        # load PNGs / JSON config crash on missing-file errors.
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/"
                DESTINATION "resources" COMPONENT core
                PATTERN ".git*" EXCLUDE
                PATTERN "*.pyc" EXCLUDE)
        # qt.conf — pins plugin/library paths relative to the exe. Without
        # this Qt uses hardcoded build-machine paths that don't exist on the
        # user's machine, producing "could not load the Qt platform plugin".
        install(FILES "$<TARGET_FILE_DIR:finterm>/qt.conf"
                DESTINATION "." COMPONENT core OPTIONAL)
        # Qt plugin directories. OPTIONAL keeps the install non-fatal when
        # a plugin dir isn't produced by windeployqt on a given host (e.g.
        # texttospeech only appears when Qt TTS is present).
        foreach(_plugin_dir
                platforms sqldrivers imageformats iconengines styles tls
                multimedia texttospeech networkinformation platforminputcontexts
                printsupport generic)
            install(DIRECTORY "$<TARGET_FILE_DIR:finterm>/${_plugin_dir}"
                    DESTINATION "." COMPONENT core OPTIONAL)
        endforeach()
    endif()

    # ── CPack general metadata ───────────────────────────────────────────────
    set(CPACK_PACKAGE_NAME "finterm")
    set(CPACK_PACKAGE_VENDOR "finterm contributors")
    set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "finterm — local-first financial-research terminal")
    set(CPACK_PACKAGE_HOMEPAGE_URL "")
    set(CPACK_RESOURCE_FILE_LICENSE
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/installer/packages/io.github.yumima.finterm.core/meta/license.txt")
    set(CPACK_PACKAGE_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/finterm.ico")

    # ── Generator: IFW only ──────────────────────────────────────────────────
    set(CPACK_GENERATOR "IFW")

    # ── QtIFW tool discovery ─────────────────────────────────────────────────
    find_program(BINARYCREATOR_EXE binarycreator
        HINTS
            "$ENV{QT_IFW_DIR}/bin"
            "$ENV{QT_ROOT_DIR}/../../Tools/QtInstallerFramework/4.8/bin"
            "C:/Qt/Tools/QtInstallerFramework/4.8/bin"
            "C:/Qt/Tools/QtInstallerFramework/4.7/bin"
            "/opt/Qt/Tools/QtInstallerFramework/4.8/bin"
            "$ENV{HOME}/Qt/Tools/QtInstallerFramework/4.8/bin"
    )
    if(BINARYCREATOR_EXE)
        get_filename_component(_IFW_BIN_DIR "${BINARYCREATOR_EXE}" DIRECTORY)
        set(CPACK_IFW_ROOT "${_IFW_BIN_DIR}/..")
        message(STATUS "QtIFW found: ${BINARYCREATOR_EXE}")
    else()
        message(WARNING "binarycreator not found — set QT_IFW_DIR or install Qt Installer Framework")
    endif()

    # ── CPackIFW installer configuration ─────────────────────────────────────
    set(CPACK_IFW_PACKAGE_TITLE "finterm")
    set(CPACK_IFW_PACKAGE_NAME "io.github.yumima.finterm")
    set(CPACK_IFW_PACKAGE_PUBLISHER "finterm contributors")
    set(CPACK_IFW_PACKAGE_START_MENU_DIRECTORY "finterm")
    set(CPACK_IFW_PACKAGE_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/finterm.ico")
    # Kept as-is: Qt IFW finds an existing installation by this name, so
    # renaming it would strand earlier installs (orphaned uninstaller). It is
    # an install-time identifier, like io.github.yumima.finterm.
    set(CPACK_IFW_PACKAGE_MAINTENANCE_TOOL_NAME "FintermMaintenanceTool")
    set(CPACK_IFW_PACKAGE_WIZARD_STYLE "Modern")
    set(CPACK_IFW_PACKAGE_ALLOW_SPACE_IN_PATH ON)
    set(CPACK_IFW_PACKAGE_ALLOW_NON_ASCII_CHARACTERS ON)

    # Target directory defaults per platform
    if(WIN32)
        # Per user (%LOCALAPPDATA%\Programs\finterm, like VS Code's / Chrome's
        # user installers): no administrator rights to install, and the
        # in-place updater can replace it (Program Files would need UAC for
        # every update — see services/updater/UpdateInstaller). An earlier
        # Program Files install must be uninstalled first (installscript.qs
        # says so) — two installs would share one data folder.
        # Spelled out: IFW's @ApplicationsDirUser@ differs from Program Files
        # only on macOS.
        set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/AppData/Local/Programs/finterm")
    elseif(APPLE)
        set(CPACK_IFW_TARGET_DIRECTORY "/Applications/finterm")
    else()
        set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/finterm")
    endif()

    # Output file naming — arch-aware so the auto-updater manifest can point
    # at predictable, stable URLs. Scheme (matched by release.yml's upload
    # globs and .github/scripts/generate_updates_manifest.py):
    #   finterm-<version>-<platform>-<arch>-setup
    # Platform: windows | linux | macos
    # Arch:     x64 | arm64
    # The CPack IFW generator appends .exe on Windows, .app on macOS, .run on Linux.
    if(WIN32)
        # CMAKE_SYSTEM_PROCESSOR on Windows is "AMD64" for x64 and "ARM64" for arm64.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(_FINTERM_PKG_ARCH "arm64")
        else()
            set(_FINTERM_PKG_ARCH "x64")
        endif()
        set(CPACK_PACKAGE_FILE_NAME
            "finterm-${CMAKE_PROJECT_VERSION}-windows-${_FINTERM_PKG_ARCH}-setup")
    elseif(APPLE)
        # On macOS CMAKE_OSX_ARCHITECTURES is the authoritative source
        # (it drives the actual binary slice). Fall back to host processor.
        if(CMAKE_OSX_ARCHITECTURES)
            list(LENGTH CMAKE_OSX_ARCHITECTURES _osx_arch_count)
            if(_osx_arch_count GREATER 1)
                set(_FINTERM_PKG_ARCH "universal")
            elseif("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64")
                set(_FINTERM_PKG_ARCH "arm64")
            else()
                set(_FINTERM_PKG_ARCH "x64")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(_FINTERM_PKG_ARCH "arm64")
        else()
            set(_FINTERM_PKG_ARCH "x64")
        endif()
        set(CPACK_PACKAGE_FILE_NAME
            "finterm-${CMAKE_PROJECT_VERSION}-macos-${_FINTERM_PKG_ARCH}-setup")
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_FINTERM_PKG_ARCH "arm64")
        else()
            set(_FINTERM_PKG_ARCH "x64")
        endif()
        set(CPACK_PACKAGE_FILE_NAME
            "finterm-${CMAKE_PROJECT_VERSION}-linux-${_FINTERM_PKG_ARCH}-setup")
    endif()
    message(STATUS "  Installer filename: ${CPACK_PACKAGE_FILE_NAME}")

    # ── Include CPack + IFW module ───────────────────────────────────────────
    include(CPack)
    include(CPackIFW)

    # ── Define install components ────────────────────────────────────────────
    cpack_add_component(core
        DISPLAY_NAME "finterm"
        DESCRIPTION  "Core application executable and Qt runtime libraries"
        REQUIRED)

    cpack_add_component(scripts
        DISPLAY_NAME "Python Analytics Scripts"
        DESCRIPTION  "1300+ CFA-level Python analytics scripts (equity, derivatives, portfolio, quant)"
        DEPENDS core)

    # ── CPackIFW component configuration ─────────────────────────────────────
    cpack_ifw_configure_component(core
        NAME "io.github.yumima.finterm.core"
        VERSION "${CMAKE_PROJECT_VERSION}"
        SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/packaging/installer/packages/io.github.yumima.finterm.core/meta/installscript.qs"
        LICENSES "AGPL-3.0" "${CMAKE_CURRENT_SOURCE_DIR}/packaging/installer/packages/io.github.yumima.finterm.core/meta/license.txt"
        SORTING_PRIORITY 100
        DEFAULT TRUE
        FORCED_INSTALLATION)

    cpack_ifw_configure_component(scripts
        NAME "io.github.yumima.finterm.scripts"
        VERSION "${CMAKE_PROJECT_VERSION}"
        SORTING_PRIORITY 50
        DEFAULT TRUE
        DEPENDS "io.github.yumima.finterm.core")

endif() # FINTERM_BUILD_INSTALLER

# ── Build configuration summary ──────────────────────────────────────────────
message(STATUS "")
message(STATUS "============================================================")
message(STATUS "  finterm ${CMAKE_PROJECT_VERSION} — build summary")
message(STATUS "============================================================")
message(STATUS "  Qt version        : ${Qt6Core_VERSION}")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform          : ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "------------------------------------------------------------")
message(STATUS "  Excel export      : ${FINTERM_HAS_PRIVATE_HEADERS}  (Qt6::GuiPrivate / QXlsx)")
if(DEFINED HAS_WEBSOCKETS)
message(STATUS "  WebSockets        : ${HAS_WEBSOCKETS}")
endif()
if(DEFINED HAS_MULTIMEDIA)
message(STATUS "  Multimedia        : ${HAS_MULTIMEDIA}")
endif()
if(DEFINED HAS_TTS)
message(STATUS "  TextToSpeech      : ${HAS_TTS}")
endif()
message(STATUS "  Tests             : ${FINTERM_BUILD_TESTS}")
message(STATUS "============================================================")
message(STATUS "")

# ── Test suite (opt-in; see app/tests/CMakeLists.txt) ─────────────────
if(FINTERM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
