#pragma once
// DataTransferDialog — Settings › Storage & Data: export this profile's personal
// data to one file, or import one (core/setup/DataTransfer.h). An import is
// done at the next start, before anything opens a database: finterm restarts.

class QString;
class QWidget;

namespace finterm::screens::data_transfer_dialog {

/// Ask where, then write the export. Returns once it is done (or cancelled).
void export_data(QWidget* parent);

/// Ask which file, check it, confirm, then restart finterm to import it.
void import_data(QWidget* parent);

/// Check an exported file off the GUI thread, behind a busy note: "" when it
/// can be imported, else why.
QString check_file(QWidget* parent, const QString& file);

} // namespace finterm::screens::data_transfer_dialog
