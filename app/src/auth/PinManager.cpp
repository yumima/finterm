#include "auth/PinManager.h"

#include "auth/SecurityAuditLog.h"
#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QCryptographicHash>

#include <climits>

namespace finterm::auth {

namespace {

// Constant-time byte comparison. Returns true iff a and b are the same length
// AND every byte matches. The XOR-accumulate loop touches every byte even on
// mismatch so execution time does not reveal the first differing index.
bool constant_time_equals(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    const int n = a.size();
    for (int i = 0; i < n; ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

} // namespace

PinManager& PinManager::instance() {
    static PinManager s;
    return s;
}

PinManager::PinManager() {
    load_state();
}

// ── HMAC-SHA256 (manual, no QMessageAuthenticationCode dependency) ──────────
// RFC 2104: HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))

static QByteArray hmac_sha256(const QByteArray& key, const QByteArray& message) {
    static constexpr int kBlockSize = 64; // SHA-256 block size

    // Step 1: Normalize key length
    QByteArray k = key;
    if (k.size() > kBlockSize)
        k = QCryptographicHash::hash(k, QCryptographicHash::Sha256);
    while (k.size() < kBlockSize)
        k.append('\0');

    // Step 2: Create inner/outer padded keys.
    // 0x36 = ipad byte (RFC 2104 §2): XOR'd with key to form inner-hash input.
    // 0x5C = opad byte (RFC 2104 §2): XOR'd with key to form outer-hash input.
    QByteArray ipad(kBlockSize, '\0');
    QByteArray opad(kBlockSize, '\0');
    for (int i = 0; i < kBlockSize; ++i) {
        ipad[i] = static_cast<char>(k[i] ^ 0x36);
        opad[i] = static_cast<char>(k[i] ^ 0x5C);
    }

    // Step 3: Inner hash — H(ipad || message)
    QCryptographicHash inner(QCryptographicHash::Sha256);
    inner.addData(ipad);
    inner.addData(message);
    QByteArray inner_hash = inner.result();

    // Step 4: Outer hash — H(opad || inner_hash)
    QCryptographicHash outer(QCryptographicHash::Sha256);
    outer.addData(opad);
    outer.addData(inner_hash);
    return outer.result();
}

// ── PBKDF2-SHA256 key derivation ────────────────────────────────────────────
// RFC 2898 Section 5.2 — PBKDF2 with PRF = HMAC-SHA256.

QByteArray PinManager::derive_key(const QString& pin, const QByteArray& salt) const {
    const QByteArray password = pin.toUtf8();

    // Only need 1 block for 32-byte output
    // U_1 = PRF(Password, Salt || INT_32_BE(1))
    QByteArray salt_block = salt;
    salt_block.append('\0'); // block index 1 = 0x00000001
    salt_block.append('\0');
    salt_block.append('\0');
    salt_block.append('\x01');

    QByteArray u = hmac_sha256(password, salt_block);
    QByteArray t = u;

    for (int iter = 1; iter < kIterations; ++iter) {
        u = hmac_sha256(password, u);
        for (int j = 0; j < t.size(); ++j)
            t[j] = static_cast<char>(t[j] ^ u[j]);
    }

    return t.left(kHashLength);
}

// ── State persistence ───────────────────────────────────────────────────────

void PinManager::load_state() {
    auto& ss = SecureStorage::instance();

    auto hash_r = ss.retrieve("pin_hash");
    auto salt_r = ss.retrieve("pin_salt");

    if (hash_r.is_ok() && salt_r.is_ok() && !hash_r.value().isEmpty() && !salt_r.value().isEmpty()) {
        stored_hash_ = QByteArray::fromHex(hash_r.value().toUtf8());
        stored_salt_ = QByteArray::fromHex(salt_r.value().toUtf8());
        has_device_pin_ = true;
    } else {
        has_device_pin_ = false;
        stored_hash_.clear();
        stored_salt_.clear();
    }
}

// Keyring names of a user's lockout state. The account-less names (the
// builds before 4.2.2 kept one device-wide counter) are the "" user's.
static QString lock_key(const char* base, const QString& user) {
    return user.isEmpty() ? QString::fromLatin1(base) : QStringLiteral("%1:%2").arg(QLatin1String(base), user);
}

PinManager::Lock& PinManager::lock_for(const QString& user) {
    auto it = locks_.find(user);
    if (it != locks_.end())
        return it.value();
    Lock lock;
    auto& ss = SecureStorage::instance();
    const auto attempts_r = ss.retrieve(lock_key("pin_failed_attempts", user));
    if (attempts_r.is_ok() && !attempts_r.value().isEmpty())
        lock.failed = attempts_r.value().toInt();
    const auto until_r = ss.retrieve(lock_key("pin_lockout_until", user));
    if (until_r.is_ok() && !until_r.value().isEmpty()) {
        lock.until = QDateTime::fromString(until_r.value(), Qt::ISODate);
        // Clock-rollback defence: a wall clock moved back by more than a
        // minute since the deadline was written extends the deadline by as
        // much, so turning the clock back buys no free guesses.
        const auto stamp_r = ss.retrieve(lock_key("pin_lockout_stamp", user));
        const QDateTime now = QDateTime::currentDateTime();
        if (stamp_r.is_ok() && !stamp_r.value().isEmpty()) {
            const QDateTime stamp = QDateTime::fromString(stamp_r.value(), Qt::ISODate);
            if (stamp.isValid() && now.secsTo(stamp) > 60 && lock.until.isValid()) {
                LOG_WARN("Auth", QString("System clock rolled back %1s since the last lockout — extending it")
                                     .arg(now.secsTo(stamp)));
                lock.until = lock.until.addSecs(now.secsTo(stamp));
            }
        }
        if (lock.until.isValid() && lock.until <= now)
            lock.until = QDateTime();
    }
    return locks_.insert(user, lock).value();
}

void PinManager::save_lock(const QString& user) {
    const Lock& lock = lock_for(user);
    auto& ss = SecureStorage::instance();
    // Every write checked: a counter that silently fails to persist would let
    // a restart wipe the lockout. The in-memory one stays authoritative.
    auto check = [](const Result<void>& r, const QString& what) {
        if (r.is_err())
            LOG_ERROR("Auth", QString("Failed to persist %1: %2").arg(what, QString::fromStdString(r.error())));
    };
    if (lock.failed == 0 && !lock.until.isValid()) {
        for (const char* base : {"pin_failed_attempts", "pin_lockout_until", "pin_lockout_stamp"})
            check(ss.remove(lock_key(base, user)), lock_key(base, user));
        return;
    }
    check(ss.store(lock_key("pin_failed_attempts", user), QString::number(lock.failed)), "pin_failed_attempts");
    if (lock.until.isValid()) {
        check(ss.store(lock_key("pin_lockout_until", user), lock.until.toString(Qt::ISODate)), "pin_lockout_until");
        check(ss.store(lock_key("pin_lockout_stamp", user), QDateTime::currentDateTime().toString(Qt::ISODate)),
              "pin_lockout_stamp");
    } else {
        check(ss.remove(lock_key("pin_lockout_until", user)), "pin_lockout_until");
        check(ss.remove(lock_key("pin_lockout_stamp", user)), "pin_lockout_stamp");
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void PinManager::set_account(const QString& username, std::function<bool(const QString&)> check,
                             std::function<QString(const QString&)> set) {
    account_ = username;
    account_check_ = std::move(check);
    account_set_ = std::move(set);
}

bool PinManager::has_pin() const {
    return static_cast<bool>(account_check_) || has_device_pin_;
}

QString PinManager::change_pin(const QString& old_pin, const QString& new_pin) {
    if (!account_set_)
        return QStringLiteral("Sign in first.");
    if (old_pin == new_pin)
        return QStringLiteral("The new PIN is the same as the current one.");
    // Through the unlock path: a wrong current PIN counts and can start a
    // wait, so an unattended unlocked terminal can't have its PIN guessed
    // and swapped.
    if (is_locked_out(account_))
        return QStringLiteral("Too many wrong PINs — try again in %1 s.").arg(lockout_remaining_seconds(account_));
    if (!verify_pin(old_pin))
        return QStringLiteral("The current PIN is wrong.");
    const QString why = account_set_(new_pin);
    if (!why.isEmpty())
        return why;
    retire_device_pin();
    SecurityAuditLog::instance().record("pin_changed");
    return {};
}

void PinManager::pin_was_reset(const QString& user) {
    // The device PIN is its owner's: another user's reset leaves it alone.
    const auto owner = SecureStorage::instance().retrieve("pin_owner");
    if (owner.is_ok() && owner.value() == user)
        retire_device_pin();
    Lock& lock = lock_for(user);
    lock = Lock{};
    save_lock(user);
    emit lockout_changed(false, 0);
    SecurityAuditLog::instance().record("pin_reset_with_recovery_key");
}

void PinManager::retire_device_pin() {
    if (!has_device_pin_)
        return;
    auto& ss = SecureStorage::instance();
    ss.remove("pin_hash");
    ss.remove("pin_salt");
    stored_hash_.clear();
    stored_salt_.clear();
    has_device_pin_ = false;
    LOG_INFO("Auth", "Device PIN retired — the account PIN unlocks");
}

bool PinManager::verify_pin(const QString& pin) {
    if (!has_pin() || is_locked_out(account_))
        return false;
    // The device PIN compare is constant time; so is PinPolicy's.
    bool ok = account_check_ && account_check_(pin);
    if (!ok && has_device_pin_)
        ok = constant_time_equals(derive_key(pin, stored_salt_), stored_hash_);
    record_attempt(account_, ok);
    return ok;
}

void PinManager::record_attempt(const QString& user, bool ok) {
    Lock& lock = lock_for(user);
    if (ok) {
        if (lock.failed != 0 || lock.until.isValid()) {
            lock = Lock{};
            save_lock(user);
        }
        SecurityAuditLog::instance().record("pin_verify_ok");
        return;
    }
    ++lock.failed;
    LOG_WARN("Auth", QString("Wrong PIN (%1 in a row)").arg(lock.failed));
    SecurityAuditLog::instance().record("pin_verify_fail", QString("attempt=%1").arg(lock.failed));
    if (lock.failed > kFreeAttempts) {
        const int tier = qMin(lock.failed - kFreeAttempts - 1, static_cast<int>(kLockoutTiers.size()) - 1);
        const int secs = kLockoutTiers[static_cast<size_t>(tier)];
        lock.until = QDateTime::currentDateTime().addSecs(secs);
        SecurityAuditLog::instance().record("lockout_started", QString("seconds=%1").arg(secs));
        save_lock(user);
        emit lockout_changed(true, secs);
        return;
    }
    save_lock(user);
}

void PinManager::forget_user(const QString& user) {
    lock_for(user) = Lock{};
    save_lock(user); // removes the keyring entries
}

void PinManager::clear_pin() {
    auto& ss = SecureStorage::instance();
    ss.remove("pin_hash");
    ss.remove("pin_salt");
    stored_hash_.clear();
    stored_salt_.clear();
    has_device_pin_ = false;
    // The account-less lockout went with the device PIN.
    lock_for(QString()) = Lock{};
    save_lock(QString());

    LOG_INFO("Auth", "PIN cleared");
    SecurityAuditLog::instance().record("pin_cleared");
}

bool PinManager::is_locked_out(const QString& user) {
    const Lock& lock = lock_for(user);
    return lock.until.isValid() && QDateTime::currentDateTime() < lock.until;
}

int PinManager::lockout_remaining_seconds(const QString& user) {
    const Lock& lock = lock_for(user);
    if (!lock.until.isValid())
        return 0;
    const qint64 secs = QDateTime::currentDateTime().secsTo(lock.until);
    return static_cast<int>(qBound(qint64{0}, secs, qint64{INT_MAX}));
}

int PinManager::failed_attempts(const QString& user) {
    return lock_for(user).failed;
}

} // namespace finterm::auth
