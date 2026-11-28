#include "auth/PinPolicy.h"

#include "core/util/SecureRandom.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QStringList>

namespace finterm::auth::pin_policy {

namespace {

constexpr const char* kScheme = "pbkdf2-sha256";
constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
// Crockford base32: no I, L, O or U.
constexpr const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr int kKeyChars = 20;

bool same_bytes(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

QByteArray derive(const QString& secret, const QByteArray& salt, int iterations) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, secret.toUtf8(), salt, iterations,
                                              kHashBytes);
}

} // namespace

QString problem(const QString& pin) {
    if (pin.size() < 4 || pin.size() > 6)
        return QStringLiteral("A PIN is 4 to 6 digits.");
    for (const QChar c : pin)
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return QStringLiteral("A PIN is digits only.");
    bool same = true, up = true, down = true;
    for (qsizetype i = 1; i < pin.size(); ++i) {
        same = same && pin[i] == pin[0];
        up = up && pin[i].unicode() == pin[i - 1].unicode() + 1;
        down = down && pin[i].unicode() == pin[i - 1].unicode() - 1;
    }
    if (same)
        return QStringLiteral("That PIN is one digit repeated — pick one that is harder to guess.");
    if (up || down)
        return QStringLiteral("That PIN is a run of digits — pick one that is harder to guess.");
    return {};
}

QString hash(const QString& secret, int iterations) {
    const QByteArray salt = util::secure_random_bytes(kSaltBytes);
    return QStringLiteral("%1$%2$%3$%4")
        .arg(QLatin1String(kScheme))
        .arg(iterations)
        .arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(derive(secret, salt, iterations).toHex()));
}

bool is_legacy(const QString& stored) {
    return !stored.startsWith(QLatin1String(kScheme) + QLatin1Char('$'));
}

bool verify(const QString& secret, const QString& stored) {
    if (!is_legacy(stored)) {
        const QStringList parts = stored.split(QLatin1Char('$'));
        if (parts.size() != 4)
            return false;
        bool ok = false;
        const int iterations = parts[1].toInt(&ok);
        if (!ok || iterations <= 0)
            return false;
        const QByteArray salt = QByteArray::fromHex(parts[2].toLatin1());
        return same_bytes(derive(secret, salt, iterations), QByteArray::fromHex(parts[3].toLatin1()));
    }
    // salt_hex:sha256(salt || secret) — what accounts were created with
    // before 4.2.2.
    const qsizetype colon = stored.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    const QByteArray salt = QByteArray::fromHex(stored.left(colon).toLatin1());
    const QByteArray actual = QCryptographicHash::hash(salt + secret.toUtf8(), QCryptographicHash::Sha256);
    return same_bytes(actual, QByteArray::fromHex(stored.mid(colon + 1).toLatin1()));
}

QString new_recovery_key() {
    QString key;
    const QByteArray bytes = util::secure_random_bytes(kKeyChars);
    for (int i = 0; i < kKeyChars; ++i) {
        if (i > 0 && i % 5 == 0)
            key += QLatin1Char('-');
        key += QLatin1Char(kAlphabet[quint8(bytes[i]) & 31]); // 256 = 8 × 32: unbiased
    }
    return key;
}

QString normalize_recovery_key(const QString& typed) {
    QString out;
    for (QChar c : typed) {
        if (c == QLatin1Char('-') || c.isSpace())
            continue;
        c = c.toUpper();
        if (c == QLatin1Char('O'))
            c = QLatin1Char('0');
        else if (c == QLatin1Char('I') || c == QLatin1Char('L'))
            c = QLatin1Char('1');
        if (c.unicode() > 127 || !QByteArray(kAlphabet).contains(char(c.unicode())))
            return {};
        out += c;
    }
    return out.size() == kKeyChars ? out : QString();
}

} // namespace finterm::auth::pin_policy
