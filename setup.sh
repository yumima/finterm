#!/usr/bin/env bash
# setup.sh — DEPRECATED shim. Setup is now a subcommand of finterm.sh so there
# is a single entry point for everything. This forwards to `finterm.sh setup`
# (e.g. `./setup.sh --ci` → `finterm.sh setup --ci`) and will be removed later.
#
# Use directly:  ./finterm.sh setup [--ci]
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
echo "setup.sh is deprecated — forwarding to 'finterm.sh setup'. Prefer: ./finterm.sh setup" >&2
exec "$HERE/finterm.sh" setup "$@"
