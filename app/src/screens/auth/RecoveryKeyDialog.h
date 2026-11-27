#pragma once
// RecoveryKeyDialog — show a recovery key once, so the user can keep it, and
// offer one to a user who has none.

#include <QString>

class QWidget;

namespace finterm::screens::RecoveryKeyDialog {

/// Show `key` for `username`: copy, save to a file, and continue only once
/// the user says it is kept.
void show_key(QWidget* parent, const QString& username, const QString& key);

/// Once per user: if the signed-in user has no recovery key, offer to make
/// one. A "not now" is remembered (Settings › Security can still make one).
void offer_if_missing(QWidget* parent);

/// Make a new key for the signed-in user and show it. False on failure.
bool create_and_show(QWidget* parent);

} // namespace finterm::screens::RecoveryKeyDialog
