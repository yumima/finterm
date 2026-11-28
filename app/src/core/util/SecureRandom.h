// src/core/util/SecureRandom.h
#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace finterm::util {

/// `n` bytes from the operating system's own random source (getentropy,
/// BCryptGenRandom) — for secrets: salts, recovery keys, OAuth state.
/// Not QRandomGenerator::system(): a CI runner gave 22 distinct recovery keys
/// in 50 from it (it takes the CPU's RDRAND first where there is one — a
/// faulty one is the likely cause, unconfirmed). No secret is better than a
/// weak one, so a source that fails stops the program.
QByteArray secure_random_bytes(qsizetype n);

/// A token for a secret — a session, an API key, a loopback handshake:
/// `bytes` of secure_random_bytes as lower-case hex.
inline QString secure_token(qsizetype bytes = 32) {
    return QString::fromLatin1(secure_random_bytes(bytes).toHex());
}

} // namespace finterm::util
