#pragma once
// The contact a client declares to SEC (EDGAR refuses an undeclared client
// with 403 "Undeclared Automated Tool"). Header-only so the rule is unit-tested
// (tests/services/test_sec_identity.cpp) over the SAME case table as the
// Python side's finterm_identity.valid_contact().

#include <QString>

namespace fincept::services::sec_identity {

/// local@domain.tld in printable ASCII, without the characters that would
/// break the "finterm (<email>)" comment syntax or the header itself (a CR/LF
/// splits it).
inline bool header_safe_email(const QString& e) {
    if (e.size() < 5 || e.size() > 254 || e.count(QLatin1Char('@')) != 1)
        return false;
    for (const QChar c : e)
        if (c.unicode() < 0x21 || c.unicode() > 0x7e || QStringLiteral("()<>,;:\"[]\\").contains(c))
            return false;
    const qsizetype at = e.indexOf(QLatin1Char('@'));
    const QString domain = e.mid(at + 1);
    return at > 0 && domain.contains(QLatin1Char('.')) && !domain.startsWith(QLatin1Char('.')) &&
           !domain.endsWith(QLatin1Char('.'));
}

} // namespace fincept::services::sec_identity
