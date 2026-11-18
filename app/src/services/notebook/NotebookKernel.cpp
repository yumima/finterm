#include "services/notebook/NotebookKernel.h"

#include <QPointer>
#include <QTimer>

namespace finterm::services::notebook {

void run_cell(QObject* ctx, const QString& code, std::function<void(const python::PythonResult&)> done) {
    QPointer<QObject> guard(ctx);
    python::PythonRunner::instance().run_code(code, [guard, done = std::move(done)](python::PythonResult result) {
        // Queued: a cell that could not start (no temp file) is answered
        // inside run_code.
        if (guard)
            QTimer::singleShot(0, guard.data(), [done, result]() { done(result); });
    });
}

} // namespace finterm::services::notebook
