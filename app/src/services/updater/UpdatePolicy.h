#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

/// The update service's decisions, kept apart from its network and dialogs so
/// they can be tested: which version is newer, what the manifest offers this
/// platform, and when the next automatic check is due.
namespace finterm::services::update_policy {

/// How often an automatic check runs. The time of the last successful check is
/// stored, so this is measured across restarts: relaunching often does not
/// mean checking often.
inline constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;

/// What the manifest offers the running platform.
struct Offer {
    QString version;
    QString download_url;
    QString sha256; ///< lower-case hex; empty when the manifest carries none
    QString open_url;
    QString changelog;
};

enum class ManifestResult {
    Ok,         ///< `out` is filled
    Malformed,  ///< not a JSON object
    NoEntry,    ///< no entry for this platform — nothing on offer here
    Incomplete, ///< the entry lacks latest-version or download-url
};

/// Strict "major.minor.patch". Anything else (dev builds, suffixes) is false.
bool parse_version(const QString& s, int& major, int& minor, int& patch);

/// True iff `remote` is strictly newer than `local`. A version that doesn't
/// parse on either side is never newer.
bool is_newer(const QString& local, const QString& remote);

/// Read the entry for `platform_key` from an updates.json body.
ManifestResult parse_manifest(const QByteArray& body, const QString& platform_key, Offer& out);

/// Whether an automatic check is due at `now`, given the last successful one.
/// Never checked (invalid `last`) is due. A `last` in the future means the
/// clock was set back; that is due too, or the check could stop for as long as
/// the clock was wrong.
bool check_due(const QDateTime& last, const QDateTime& now);

} // namespace finterm::services::update_policy
