#include "mcp/UntrustedContent.h"

namespace fincept::mcp {

QString UntrustedContent::wrap(const QString& content) {
    if (content.isEmpty())
        return content;
    // Pre-existing `<untrusted>` markers in the content would let an
    // attacker close our wrapper early.  Neutralise them by encoding
    // angle brackets — content readers (the model) see literal
    // "<untrusted>" without it functioning as a tag boundary.
    QString safe = content;
    safe.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    safe.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    return QString::fromLatin1(kOpenTag) + safe + QString::fromLatin1(kCloseTag);
}

} // namespace fincept::mcp
