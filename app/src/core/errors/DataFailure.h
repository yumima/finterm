#pragma once
// DataFailure — why some data did not load, in words a user can act on.
//
// Every fetch path ends in an error *string*: a Python exception's text from
// the data daemon ("YFRateLimitError('Too Many Requests…')", "curl: (6)
// Could not resolve host"), a QNetworkReply::errorString(), or one of our own
// messages ("Yahoo refused the quote download"). classify() reads that string
// — the one thing every path carries — and says what happened, where, and
// what the user can do about it. The raw text stays available as the detail.
//
// One classifier for every surface (widget error pages, the status-bar data
// indicator, tab messages), so the same failure reads the same everywhere.

#include <QString>

namespace finterm::data_failure {

enum class Kind {
    RateLimited,  ///< HTTP 429 / "too many requests": asking too often
    Refused,      ///< HTTP 401/403, invalid crumb: the source said no
    NoAnswer,     ///< refused or never answered — the source didn't say which
    Unresolved,   ///< DNS: the source's address could not be found
    Unreachable,  ///< connection refused / reset / network unreachable
    Timeout,      ///< asked, no answer in time
    Tls,          ///< the secure connection failed (certificates, interception)
    Proxy,        ///< the proxy failed or refused
    ServerError,  ///< HTTP 5xx: the source's own problem
    NotFound,     ///< no such symbol / page (HTTP 404, "possibly delisted")
    NoData,       ///< answered, with nothing for this request
    NeedsKey,     ///< the source needs an API key that is missing or wrong
    LocalService, ///< finterm's own data service (Python) failed
    Unknown,
};

struct Failure {
    Kind kind = Kind::Unknown;
    QString source;  ///< "Yahoo Finance", "SEC EDGAR", … ("" when unknown)
    int http_status = 0;
    QString reason;  ///< what happened, one sentence
    QString action;  ///< what to do about it, one or two sentences
    QString raw;     ///< the original text (for "Details")
    /// Worth retrying on its own (network, rate limit, the source's outage)
    /// as opposed to something the user has to change (symbol, key).
    bool transient = false;
};

/// Classify `error`. `source_hint` names the source when the text doesn't
/// ("Yahoo Finance" for market:* topics).
Failure classify(const QString& error, const QString& source_hint = {});

/// "Couldn't load <what> — <reason>" for a one-line status label.
QString one_line(const QString& what, const Failure& f);

/// A short label for the kind ("rate-limited", "no connection", …).
QString kind_label(Kind kind);

} // namespace finterm::data_failure
