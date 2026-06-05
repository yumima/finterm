#include "ai_chat/HearthService.h"

#include <QByteArray>
#include <QtGlobal> // qgetenv

namespace fincept {

namespace {
QString normalize(QString base) {
    while (base.endsWith('/'))
        base.chop(1);
    return base;
}
} // namespace

HearthService& HearthService::instance() {
    static HearthService s;
    return s;
}

HearthService::Resolution HearthService::resolve() const {
    const QByteArray env = qgetenv("FINCEPT_ENGINE_BASE_URL");
    const QString base = env.isEmpty() ? QString::fromLatin1(kDefaultBase)
                                       : normalize(QString::fromUtf8(env));
    // Treat hearth's default port as hearth even before any probe, so the
    // common zero-config case addresses role aliases (primary_chat) and "just
    // works" the moment the engine is up at :11435.
    return {base, base == QLatin1String(kDefaultBase)};
}

} // namespace fincept
