#pragma once
// PinPolicy — the rules and the hashing every PIN and recovery key goes
// through, in one place: registration, the lock screen, Settings › Change PIN
// and the recovery-key reset all apply the same ones.

#include <QString>

namespace finterm::auth::pin_policy {

/// Why `pin` can't be a new PIN, or "" when it can: 4 to 6 digits, not one
/// digit repeated, not a run up or down (1234, 987654).
QString problem(const QString& pin);

/// A stored form of `secret`: "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>".
/// The iterations are stored with it, so the default can change.
QString hash(const QString& secret, int iterations = 100000);

/// Whether `secret` matches `stored`, in the current form or the older
/// "<salt hex>:<sha256 hex>" one. Constant time in the comparison.
bool verify(const QString& secret, const QString& stored);

/// Whether `stored` is in the older, unstretched form (kept as is until the
/// user sets a new PIN: older builds can read only that form).
bool is_legacy(const QString& stored);

/// A new recovery key: 20 characters of Crockford base32 (100 bits) in four
/// groups, "7KQ2M-9XD4T-…". Shown once; only its hash is kept.
QString new_recovery_key();

/// The key as typed, in canonical form: upper case, separators and spaces
/// dropped, the letters Crockford reads as digits (O→0, I/L→1) mapped.
/// "" when it can't be a key (wrong length or a character outside the set).
QString normalize_recovery_key(const QString& typed);

/// Iterations for a recovery key: it already carries 100 bits.
constexpr int kRecoveryIterations = 20000;

// Why 100,000 for a PIN, not OWASP's 600,000: a check runs on the GUI thread
// at every unlock, sign-in and PIN change, and 600,000 measured ~550 ms on a
// fast desktop (a frozen window on a slow laptop). A 4–6 digit PIN has at
// most a million values, so stretching only slows an offline attack on a
// stolen auth.db; the lockout is what guards the lock screen.

} // namespace finterm::auth::pin_policy
