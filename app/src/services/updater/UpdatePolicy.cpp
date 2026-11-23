#include "services/updater/UpdatePolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace finterm::services::update_policy {

bool parse_version(const QString& s, int& major, int& minor, int& patch) {
    static const QRegularExpression re(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    bool ok_major = false, ok_minor = false, ok_patch = false;
    major = m.captured(1).toInt(&ok_major);
    minor = m.captured(2).toInt(&ok_minor);
    patch = m.captured(3).toInt(&ok_patch);
    return ok_major && ok_minor && ok_patch;
}

bool is_newer(const QString& local, const QString& remote) {
    int lmaj = 0, lmin = 0, lpat = 0;
    int rmaj = 0, rmin = 0, rpat = 0;
    if (!parse_version(local, lmaj, lmin, lpat))
        return false; // dev build or malformed — never "update available"
    if (!parse_version(remote, rmaj, rmin, rpat))
        return false; // malformed manifest — reject
    if (rmaj != lmaj)
        return rmaj > lmaj;
    if (rmin != lmin)
        return rmin > lmin;
    return rpat > lpat;
}

ManifestResult parse_manifest(const QByteArray& body, const QString& platform_key, Offer& out) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return ManifestResult::Malformed;
    const QJsonObject updates = doc.object().value(QStringLiteral("updates")).toObject();
    if (platform_key.isEmpty() || !updates.contains(platform_key))
        return ManifestResult::NoEntry;
    const QJsonObject e = updates.value(platform_key).toObject();
    Offer o;
    o.version = e.value(QStringLiteral("latest-version")).toString().trimmed();
    o.download_url = e.value(QStringLiteral("download-url")).toString().trimmed();
    o.sha256 = e.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    o.open_url = e.value(QStringLiteral("open-url")).toString().trimmed();
    o.changelog = e.value(QStringLiteral("changelog")).toString();
    if (o.version.isEmpty() || o.download_url.isEmpty())
        return ManifestResult::Incomplete;
    out = o;
    return ManifestResult::Ok;
}

bool check_due(const QDateTime& last, const QDateTime& now) {
    if (!last.isValid())
        return true;
    const qint64 elapsed = last.secsTo(now);
    return elapsed < 0 || elapsed >= kCheckIntervalSecs;
}

} // namespace finterm::services::update_policy
