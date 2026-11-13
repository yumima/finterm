# Prune files/directories that accidentally live under scripts/ but must
# never ship inside the built bundle. Runs under `cmake -P` — portable
# across Windows / macOS / Linux.
#
# Rationale:
#   - *.deleted.* / *.bak / *.orig  — leftover backups from refactors
#   - .pytest_cache / .benchmarks   — pytest caches committed by mistake
#   - __pycache__                   — Python bytecode caches
#   - *.db / *.sqlite*              — local SQLite DBs (e.g. memories_alice.db)
#
# On macOS anything unexpected inside an .app bundle trips codesign with
# "bundle format unrecognized, invalid, or unsuitable". On Linux/Windows
# these just bloat the installer; removing them shrinks it meaningfully.
#
# IMPORTANT — do NOT mix `GLOB_RECURSE` with `LIST_DIRECTORIES true`. CMake's
# globbing then returns every intermediate directory on the search path, not
# just the pattern matches. (Diagnosed when `technicals/` and 11 other
# legitimate subdirectories were being wiped on every build, breaking
# `compute_technicals.py` because its `from technicals.X import Y` failed.)

if(NOT DEFINED SCRIPTS_DIR)
    message(FATAL_ERROR "prune_scripts_junk.cmake: SCRIPTS_DIR must be set")
endif()

if(NOT IS_DIRECTORY "${SCRIPTS_DIR}")
    return()
endif()

# Recursive walk that only matches blacklisted basenames — no glob patterns.
# Returns sets in the parent scope.
set(_junk_dirs "")
set(_junk_files "")

function(_walk dir)
    file(GLOB _children RELATIVE "${dir}" "${dir}/*")
    foreach(_name IN LISTS _children)
        set(_path "${dir}/${_name}")
        if(IS_DIRECTORY "${_path}")
            set(_blacklisted FALSE)
            if(_name STREQUAL "__pycache__"
                OR _name STREQUAL ".pytest_cache"
                OR _name STREQUAL ".benchmarks")
                set(_blacklisted TRUE)
            elseif(_name MATCHES "\\.deleted$" OR _name MATCHES "\\.deleted\\.")
                set(_blacklisted TRUE)
            endif()
            if(_blacklisted)
                list(APPEND _junk_dirs "${_path}")
                set(_junk_dirs "${_junk_dirs}" PARENT_SCOPE)
            else()
                _walk("${_path}")
                set(_junk_dirs "${_junk_dirs}" PARENT_SCOPE)
                set(_junk_files "${_junk_files}" PARENT_SCOPE)
            endif()
        else()
            if(_name MATCHES "\\.(db|sqlite|sqlite3|bak|orig|pyc)$")
                list(APPEND _junk_files "${_path}")
                set(_junk_files "${_junk_files}" PARENT_SCOPE)
            endif()
        endif()
    endforeach()
endfunction()

_walk("${SCRIPTS_DIR}")

set(_removed 0)
foreach(_path IN LISTS _junk_dirs)
    if(IS_DIRECTORY "${_path}")
        file(REMOVE_RECURSE "${_path}")
        math(EXPR _removed "${_removed} + 1")
    endif()
endforeach()
foreach(_path IN LISTS _junk_files)
    if(EXISTS "${_path}")
        file(REMOVE "${_path}")
        math(EXPR _removed "${_removed} + 1")
    endif()
endforeach()

if(_removed GREATER 0)
    message(STATUS "prune_scripts_junk: removed ${_removed} junk path(s) under ${SCRIPTS_DIR}")
endif()
