# Smoke test for cmake/prune_scripts_junk.cmake
#
# Builds a fixture tree containing both legitimate subdirectories (which must
# survive) and known junk paths (which must be removed). Runs the real prune
# script. Fails with `message(FATAL_ERROR ...)` on any mismatch.
#
# Run with:  cmake -DPRUNE_SCRIPT=<path> -P test_prune_scripts_junk.cmake
#
# This guards against regressions like the GLOB_RECURSE/LIST_DIRECTORIES bug
# that was silently wiping `technicals/`, `agents/`, `Analytics/`, and 9
# other legitimate subdirectories on every build.

if(NOT DEFINED PRUNE_SCRIPT)
    # Default to the in-tree script when run from CTest.
    get_filename_component(_self_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
    set(PRUNE_SCRIPT "${_self_dir}/../../cmake/prune_scripts_junk.cmake")
endif()
if(NOT EXISTS "${PRUNE_SCRIPT}")
    message(FATAL_ERROR "PRUNE_SCRIPT not found: ${PRUNE_SCRIPT}")
endif()

# ── Fixture tree ─────────────────────────────────────────────────────────────
# Use a temp dir so concurrent runs don't collide.
set(_fixture_root "${CMAKE_CURRENT_BINARY_DIR}/test_prune_fixture_$ENV{USER}_$<RANDOM>")
# `<` and `>` aren't allowed in `<RANDOM>` interpolation under -P mode; just use a fixed name.
set(_fixture_root "${CMAKE_CURRENT_BINARY_DIR}/test_prune_fixture")
file(REMOVE_RECURSE "${_fixture_root}")
set(_scripts "${_fixture_root}/scripts")
file(MAKE_DIRECTORY "${_scripts}")

# Legitimate subdirectories that MUST survive.
set(_keep_dirs
    "${_scripts}/technicals"
    "${_scripts}/technicals/momentum_indicators"      # nested package
    "${_scripts}/agents"
    "${_scripts}/Analytics"
    "${_scripts}/Analytics/backtesting"
    "${_scripts}/exchange"
)
foreach(_d IN LISTS _keep_dirs)
    file(MAKE_DIRECTORY "${_d}")
    file(WRITE "${_d}/__init__.py" "# keep me\n")
endforeach()

# Junk that MUST be removed.
set(_junk_dirs_to_create
    "${_scripts}/__pycache__"
    "${_scripts}/technicals/__pycache__"
    "${_scripts}/.pytest_cache"
    "${_scripts}/agents/.benchmarks"
    "${_scripts}/old_module.deleted"
    "${_scripts}/agents.deleted.20240101"
)
foreach(_d IN LISTS _junk_dirs_to_create)
    file(MAKE_DIRECTORY "${_d}")
    file(WRITE "${_d}/marker.txt" "junk\n")
endforeach()

# Junk files at top level
file(WRITE "${_scripts}/test.db" "stub\n")
file(WRITE "${_scripts}/cache.sqlite3" "stub\n")
file(WRITE "${_scripts}/old.bak" "stub\n")
file(WRITE "${_scripts}/agents/keep.py" "x = 1\n")        # legit, must survive
file(WRITE "${_scripts}/keep.py" "y = 2\n")               # legit, must survive

# ── Run the real prune script ────────────────────────────────────────────────
execute_process(
    COMMAND ${CMAKE_COMMAND} -DSCRIPTS_DIR=${_scripts} -P ${PRUNE_SCRIPT}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "prune script failed (rc=${_rc})\n${_out}")
endif()

# ── Assertions ───────────────────────────────────────────────────────────────
set(_failures "")

# Legitimate paths must still exist.
foreach(_d IN LISTS _keep_dirs)
    if(NOT IS_DIRECTORY "${_d}")
        list(APPEND _failures "REGRESSION: legitimate dir was removed → ${_d}")
    endif()
endforeach()
foreach(_f "${_scripts}/agents/keep.py" "${_scripts}/keep.py")
    if(NOT EXISTS "${_f}")
        list(APPEND _failures "REGRESSION: legitimate file was removed → ${_f}")
    endif()
endforeach()

# Junk must be gone.
foreach(_d IN LISTS _junk_dirs_to_create)
    if(EXISTS "${_d}")
        list(APPEND _failures "FAIL: junk dir survived → ${_d}")
    endif()
endforeach()
foreach(_f "${_scripts}/test.db" "${_scripts}/cache.sqlite3" "${_scripts}/old.bak")
    if(EXISTS "${_f}")
        list(APPEND _failures "FAIL: junk file survived → ${_f}")
    endif()
endforeach()

if(_failures)
    foreach(_msg IN LISTS _failures)
        message(STATUS "  ${_msg}")
    endforeach()
    file(REMOVE_RECURSE "${_fixture_root}")
    list(LENGTH _failures _n)
    message(FATAL_ERROR "test_prune_scripts_junk: ${_n} assertion(s) failed")
endif()

file(REMOVE_RECURSE "${_fixture_root}")
message(STATUS "test_prune_scripts_junk: PASS")
