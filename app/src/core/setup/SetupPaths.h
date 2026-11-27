#pragma once
// SetupPaths — the active profile's places, for the data upgrade
// (DataUpgrade) and for an export or import (DataTransfer): one definition,
// used by the startup gate, the updater's rollback and Settings.

#include "core/setup/DataTransfer.h"
#include "core/setup/DataUpgrade.h"

namespace finterm::setup_paths {

/// This build, this profile. The build id is the updater's ("4.2.3+<commit>").
data_upgrade::Plan current_plan();

data_transfer::Where current_where();

} // namespace finterm::setup_paths
