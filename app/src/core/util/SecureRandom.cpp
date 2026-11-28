// src/core/util/SecureRandom.cpp — kept out of unity batches (<windows.h>).
#include "core/util/SecureRandom.h"

#include <cstddef>

#if defined(Q_OS_WIN)
#    include <windows.h>
#    include <bcrypt.h>
#    if defined(_MSC_VER)
#        pragma comment(lib, "bcrypt")
#    endif
#else
#    include <unistd.h>
#    if defined(Q_OS_MACOS)
#        include <sys/random.h>
#    endif
#endif

namespace finterm::util {

QByteArray secure_random_bytes(qsizetype n) {
    QByteArray out(n, '\0');
    char* p = out.data();
    for (qsizetype left = n; left > 0;) {
        const qsizetype chunk = qMin<qsizetype>(left, 256); // getentropy's limit
#if defined(Q_OS_WIN)
        const bool ok = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(p), ULONG(chunk),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
        const bool ok = ::getentropy(p, std::size_t(chunk)) == 0;
#endif
        if (!ok)
            qFatal("the system's random source failed");
        p += chunk;
        left -= chunk;
    }
    return out;
}

} // namespace finterm::util
