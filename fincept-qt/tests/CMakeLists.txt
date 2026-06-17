# Fincept Terminal — test suite (opt-in)
#
# Enable by configuring with -DFINCEPT_BUILD_TESTS=ON. Disabled by default so
# normal dev / release builds remain unchanged. Each test is a self-contained
# Qt Test executable that links the minimum subset of sources it needs
# (no linking against the full FinceptTerminal target — keeps tests fast).

# Inherits ${_fincept_qt_match} from the parent CMakeLists (exact pin under
# -DFINCEPT_QT_STRICT=ON, else a major.minor floor that accepts system Qt).
find_package(Qt6 ${_fincept_qt_match} REQUIRED COMPONENTS Gui Test)

enable_testing()

# ── DataHub tests ────────────────────────────────────────────────────────────
# Links against the datahub sources and the Logger (a direct dependency).

add_executable(test_datahub
    datahub/test_datahub.cpp
    ../src/datahub/DataHub.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_datahub PRIVATE
    ../src
)

target_link_libraries(test_datahub PRIVATE
    Qt6::Core
    Qt6::Gui   # DataHub.cpp includes <QGuiApplication>
    Qt6::Test
)

# Tests shouldn't need the app's unity build settings.
set_target_properties(test_datahub PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME datahub COMMAND test_datahub)

# ── EventBus tests ───────────────────────────────────────────────────────────
# Reentrancy + thread-safety regression suite. Locks in the invariants that
# (1) handlers can subscribe()/unsubscribe()/publish() during dispatch
# without crashing or skipping events, and (2) cross-thread publish is safe
# (MCP tools publish from a worker thread).

add_executable(test_event_bus
    core/test_event_bus.cpp
    ../src/core/events/EventBus.cpp
)

target_include_directories(test_event_bus PRIVATE
    ../src
)

target_link_libraries(test_event_bus PRIVATE
    Qt6::Core
    Qt6::Test
)

set_target_properties(test_event_bus PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME event_bus COMMAND test_event_bus)

# ── LiveHlsProxy::trim_playlist tests ────────────────────────────────────────
# Locks in the playlist-trim correctness. The whole live-HLS fix
# (commit 59a0e513) depends on the trim producing a valid playlist with
# the right MEDIA-SEQUENCE, per-segment tags carried through, etc.
# Compiles only the LiveHlsProxy.cpp + its Logger dependency — the
# trim function is static and pure, so we don't need to instantiate
# any QNetwork/QTcpServer machinery to exercise it.

add_executable(test_live_hls_proxy
    services/video/test_live_hls_proxy.cpp
    ../src/services/video/LiveHlsProxy.cpp
    ../src/core/logging/Logger.cpp
)

target_include_directories(test_live_hls_proxy PRIVATE
    ../src
)

target_link_libraries(test_live_hls_proxy PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

set_target_properties(test_live_hls_proxy PROPERTIES
    UNITY_BUILD OFF
    AUTOMOC ON
)

add_test(NAME live_hls_proxy COMMAND test_live_hls_proxy)

# ── prune_scripts_junk smoke test ────────────────────────────────────────────
# Pure cmake -P script that builds a fixture tree, runs the real prune script,
# and asserts legitimate dirs survive while junk is gone. Guards against the
# GLOB_RECURSE/LIST_DIRECTORIES regression that previously wiped technicals/.
add_test(
    NAME prune_scripts_junk
    COMMAND ${CMAKE_COMMAND}
            -DPRUNE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/prune_scripts_junk.cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_prune_scripts_junk.cmake
)
