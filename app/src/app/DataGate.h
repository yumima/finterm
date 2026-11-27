#pragma once
// DataGate — at startup, after the profile is chosen and before anything opens
// a user database: refuse data written by a newer finterm (offering the
// backup taken when it was upgraded past this version), and bring older data
// up to date with a backup first (core/setup/DataUpgrade.h).

namespace finterm::data_gate {

/// At startup (beside Start fresh): an import asked for in the previous run,
/// and what happened, said to the user. Before anything opens a database.
void import_if_scheduled();

/// False when the app must not go on (the user chose to quit at the "newer
/// data" question). Needs the QApplication. `interactive` false (a self-test,
/// which no one watches): no questions — newer data stops it, problems are
/// logged. `sole`: no other finterm process has this data folder open; when
/// one has, nothing is upgraded or put back (each user's login migrates
/// their database, as before).
bool run(bool interactive, bool sole);

/// `finterm --export FILE` / `finterm --import FILE`: one JSON line on stdout.
/// An import needs the data to itself, and upgrades what it put in place.
/// The exit code, or -1 when neither was asked for.
int transfer_command_line();

/// `finterm --upgrade-data [--check-only]`: what the data is at and, unless
/// --check-only, upgrade it — one JSON line on stdout. The exit code. Refused
/// while another finterm has the data folder open. Registers the migrations.
int run_command_line();

} // namespace finterm::data_gate
