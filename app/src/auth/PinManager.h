#pragma once
#include "core/result/Result.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

#include <array>
#include <functional>

namespace finterm::auth {

/// The lock-screen gate: which PIN unlocks, and the lockout after wrong ones.
///
/// The PIN is the signed-in user's account PIN (AuthService, installed here
/// by AuthManager with set_account()). Builds before 4.2.2 also asked for a
/// separate 6-digit "device PIN"; one that was set keeps working (either
/// PIN unlocks) until the user changes their PIN, which retires it — a user
/// who remembers only one of the two is never locked out.
///
/// Wrong PINs, counted per user across the lock screen and the sign-in screen
/// (one user's mistakes never hold up another, or a new user): two free, then
/// waits of 30 s, 1, 5, 15 and 60 minutes (and 60 from then on).
/// There is no permanent lock: forgetting the PIN is handled by the recovery
/// options (recovery key, another user, reset), not by trying harder.
class PinManager : public QObject {
    Q_OBJECT
  public:
    static PinManager& instance();

    /// The signed-in account's PIN check and setter. Both empty at sign-out.
    void set_account(const QString& username, std::function<bool(const QString&)> check,
                     std::function<QString(const QString&)> set);
    /// The account the gate is for ("" when signed out).
    QString account() const { return account_; }

    /// True when there is a PIN to unlock with (an account, or an older
    /// build's device PIN).
    bool has_pin() const;
    /// An older build's separate device PIN is still set.
    bool has_device_pin() const { return has_device_pin_; }

    /// Change the PIN, requiring the current one (a wrong one counts like a
    /// failed unlock). Retires the device PIN. Returns "" or why not.
    QString change_pin(const QString& old_pin, const QString& new_pin);

    /// `user`'s PIN was set after they proved who they are another way (a
    /// recovery key): their lockout ends, and the device PIN, if it is
    /// theirs, retires.
    void pin_was_reset(const QString& user);

    /// Verify an unlock attempt against the account PIN or the device PIN.
    /// Counts failures and applies the waits; false while a wait runs.
    bool verify_pin(const QString& pin);

    /// For a PIN checked elsewhere (the sign-in screen): count the outcome
    /// against `user`'s lockout.
    void record_attempt(const QString& user, bool ok);

    /// A deleted user's lockout goes with them: a user created again under
    /// the same name starts clean.
    void forget_user(const QString& user);

    /// Forget the device PIN and the lockout (sign-out of a deleted user,
    /// a fresh start).
    void clear_pin();

    /// `user`'s lockout ("" = no account: an older build's device PIN).
    bool is_locked_out(const QString& user);
    int lockout_remaining_seconds(const QString& user);
    int failed_attempts(const QString& user);

    /// Wrong PINs that cost nothing but the message.
    static constexpr int kFreeAttempts = 2;

  signals:
    void lockout_changed(bool locked, int remaining_seconds);

  private:
    PinManager();

    // PBKDF2-SHA256 parameters.
    // 600_000 iterations per OWASP Password Storage Cheat Sheet (2023) for
    // PBKDF2-SHA256. Verify cost stays well under 500ms on target hardware.
    static constexpr int kIterations = 600000;
    static constexpr int kSaltLength = 32;
    static constexpr int kHashLength = 32;

    // Waits (seconds) after the free attempts; the last repeats.
    static constexpr std::array<int, 5> kLockoutTiers = {30, 60, 300, 900, 3600};

    QByteArray derive_key(const QString& pin, const QByteArray& salt) const;

    struct Lock {
        int failed = 0;
        QDateTime until;
    };
    /// Per user, read from the keyring on first use.
    QHash<QString, Lock> locks_;
    Lock& lock_for(const QString& user);
    void save_lock(const QString& user);

    QString account_;
    std::function<bool(const QString&)> account_check_;
    std::function<QString(const QString&)> account_set_;

    // An older build's device PIN, from SecureStorage
    bool has_device_pin_ = false;
    QByteArray stored_hash_;
    QByteArray stored_salt_;

    void load_state();
    void retire_device_pin();
};

} // namespace finterm::auth
