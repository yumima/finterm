#pragma once
// NotebookKernel.h — runs the Code Editor's notebook cells: the user's own
// Python, one process per cell (PythonRunner::run_code).

#include "python/PythonRunner.h"

#include <QString>

#include <functional>

class QObject;

namespace finterm::services::notebook {

/// Run one cell's `code`. `done` gets the process's stdout, stderr and exit
/// code, on the GUI thread after this returns; dropped if `ctx` is gone.
void run_cell(QObject* ctx, const QString& code, std::function<void(const python::PythonResult&)> done);

} // namespace finterm::services::notebook
