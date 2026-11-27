#pragma once
// SetupAssistant — what the downloaded package offers when it is started from
// anywhere but its install location (core/setup/Install.h): install, update,
// open the installed one, or run this copy once; optionally start fresh. And
// the command-line forms (--install, --uninstall) for scripts and CI.

namespace finterm::setup_assistant {

/// --install / --uninstall: done here, one JSON line on stdout. The exit code
/// to return, or -1 when neither was asked for.
int handle_command_line();

/// Offer to install when this package isn't running from its install
/// location. True to go on running this process; false when it handed over to
/// the installed copy (or the user quit) and this one should exit.
/// An install running from its place has its launcher brought up to date.
/// `another_running`: finterm already runs (this is a secondary instance).
bool offer(bool another_running);

} // namespace finterm::setup_assistant
