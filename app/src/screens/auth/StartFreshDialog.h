#pragma once
// StartFreshDialog — confirm a fresh start (core/config/StartFresh), then
// restart finterm to carry it out. Reached from "Forgot PIN?" and from
// Settings › Storage.

class QWidget;

namespace finterm::screens::StartFreshDialog {

/// Ask; on confirmation schedule the fresh start, start finterm again and
/// quit this instance. Returns false when the user cancelled or it failed.
bool ask(QWidget* parent);

} // namespace finterm::screens::StartFreshDialog
