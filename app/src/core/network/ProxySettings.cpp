#include "core/network/ProxySettings.h"

#include "core/config/AppIdentity.h"
#include "core/logging/Logger.h"

#include <QHostAddress>
#include <QMutex>
#include <QNetworkProxyFactory>
#include <QSettings>
#include <QUrl>

namespace finterm::proxy {

namespace {

constexpr const char* kModeKey = "network/proxy_mode";
constexpr const char* kUrlKey = "network/proxy_url";
const QUrl kProbeUrl(QStringLiteral("https://query1.finance.yahoo.com/"));

// The proxy variables children read, both spellings (libcurl reads only the
// lower-case http_proxy; most tools read either).
const char* const kVars[] = {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy",
                             "ALL_PROXY",   "all_proxy",   "NO_PROXY",   "no_proxy"};

/// The environment as finterm was started with, before any apply() — so
/// switching back to System restores the user's own variables.
struct OriginalEnv {
    QHash<QString, QByteArray> values;
    bool captured = false;
};
OriginalEnv& original_env() {
    static OriginalEnv e;
    if (!e.captured) {
        for (const char* v : kVars)
            if (qEnvironmentVariableIsSet(v))
                e.values.insert(QString::fromLatin1(v), qgetenv(v));
        e.captured = true;
    }
    return e;
}

bool is_loopback(const QString& host) {
    if (host.isEmpty() || host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress a(host);
    return !a.isNull() && a.isLoopback();
}

QNetworkProxy proxy_from_url(const QString& url) {
    const QUrl u(url.trimmed());
    const QString scheme = u.scheme().toLower();
    const auto type = scheme.startsWith(QLatin1String("socks")) ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy;
    QNetworkProxy p(type, u.host(), quint16(u.port(scheme.startsWith(QLatin1String("socks")) ? 1080 : 8080)));
    if (!u.userName().isEmpty()) {
        p.setUser(u.userName());
        p.setPassword(u.password());
    }
    return p;
}

QString url_of(const QNetworkProxy& p) {
    if (p.type() != QNetworkProxy::HttpProxy && p.type() != QNetworkProxy::Socks5Proxy)
        return {};
    QUrl u;
    // socks5h: the proxy resolves names — what a VPN's proxy is there for.
    u.setScheme(p.type() == QNetworkProxy::Socks5Proxy ? QStringLiteral("socks5h") : QStringLiteral("http"));
    u.setHost(p.hostName());
    u.setPort(p.port());
    if (!p.user().isEmpty()) {
        u.setUserName(p.user());
        u.setPassword(p.password());
    }
    return u.toString();
}

/// The system's proxy for `query`. Windows and macOS: the OS settings (and a
/// PAC script). Elsewhere Qt reads the proxy environment variables — which
/// apply() itself rewrites for the children — so there they are read as
/// finterm was started with, never as a Manual choice left them.
QList<QNetworkProxy> system_proxy_for(const QNetworkProxyQuery& query) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return QNetworkProxyFactory::systemProxyForQuery(query);
#else
    const auto& env = original_env().values;
    auto get = [&env](const char* upper, const char* lower) {
        const QByteArray v = env.value(QString::fromLatin1(lower), env.value(QString::fromLatin1(upper)));
        return QString::fromUtf8(v).trimmed();
    };
    const QString host = query.url().host().isEmpty() ? query.peerHostName() : query.url().host();
    for (QString rule : get("NO_PROXY", "no_proxy").split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        rule = rule.trimmed();
        if (rule == QLatin1String("*"))
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        if (rule.startsWith(QLatin1Char('.')))
            rule.remove(0, 1);
        if (!rule.isEmpty() && (host == rule || host.endsWith(QLatin1Char('.') + rule)))
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
    }
    QString url = query.url().scheme() == QLatin1String("http") ? get("HTTP_PROXY", "http_proxy")
                                                                 : get("HTTPS_PROXY", "https_proxy");
    if (url.isEmpty())
        url = get("ALL_PROXY", "all_proxy");
    if (url.isEmpty())
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    if (!url.contains(QLatin1String("://")))
        url.prepend(QStringLiteral("http://"));
    return {proxy_from_url(url)};
#endif
}

/// Loopback direct; everything else per the configuration.
class Factory : public QNetworkProxyFactory {
  public:
    explicit Factory(Config config) : config_(std::move(config)) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        if (is_loopback(query.peerHostName()) || is_loopback(query.url().host()))
            return {QNetworkProxy(QNetworkProxy::NoProxy)};
        switch (config_.mode) {
            case Mode::None:
                return {QNetworkProxy(QNetworkProxy::NoProxy)};
            case Mode::Manual:
                return {proxy_from_url(config_.url)};
            case Mode::System:
                break;
        }
        return system_proxy_for(query);
    }

  private:
    const Config config_;
};

/// For display and logs: never a password.
QString shown(const QString& url) {
    QUrl u(url.trimmed());
    if (!u.password().isEmpty())
        u.setPassword(QStringLiteral("***"));
    return u.toString();
}

void set_var(const char* name, const QString& value) {
    if (value.isEmpty())
        qunsetenv(name);
    else
        qputenv(name, value.toUtf8());
}

/// Loopback is always direct for children too, whatever else NO_PROXY held.
QString with_loopback(const QString& no_proxy) {
    QStringList parts = no_proxy.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& h : {QStringLiteral("localhost"), QStringLiteral("127.0.0.1"), QStringLiteral("::1")})
        if (!parts.contains(h))
            parts << h;
    return parts.join(QLatin1Char(','));
}

} // namespace

QProcessEnvironment with_user_proxy_env(QProcessEnvironment env) {
    const OriginalEnv& orig = original_env();
    for (const char* v : kVars) {
        const QString name = QString::fromLatin1(v);
        if (orig.values.contains(name))
            env.insert(name, QString::fromUtf8(orig.values.value(name)));
        else
            env.remove(name);
    }
    return env;
}

QNetworkProxyFactory* make_factory(const Config& config) {
    return new Factory(config);
}

Config load() {
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    Config c;
    const QString mode = s.value(QString::fromLatin1(kModeKey), QStringLiteral("system")).toString();
    c.mode = mode == QLatin1String("none") ? Mode::None : mode == QLatin1String("manual") ? Mode::Manual : Mode::System;
    c.url = s.value(QString::fromLatin1(kUrlKey)).toString();
    if (c.mode == Mode::Manual && !url_problem(c.url).isEmpty())
        c.mode = Mode::System;
    return c;
}

void save(const Config& c) {
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    s.setValue(QString::fromLatin1(kModeKey), c.mode == Mode::None     ? QStringLiteral("none")
                                              : c.mode == Mode::Manual ? QStringLiteral("manual")
                                                                       : QStringLiteral("system"));
    s.setValue(QString::fromLatin1(kUrlKey), c.url.trimmed());
}

QString url_problem(const QString& url) {
    const QUrl u(url.trimmed(), QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (url.trimmed().isEmpty())
        return QStringLiteral("Enter the proxy, like http://127.0.0.1:7890.");
    if (!u.isValid() || u.host().isEmpty())
        return QStringLiteral("That is not an address like http://127.0.0.1:7890.");
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("socks5") &&
        scheme != QLatin1String("socks5h"))
        return QStringLiteral("Start it with http://, https:// or socks5://.");
    if (u.port() <= 0)
        return QStringLiteral("Add the port, like :7890.");
    return {};
}

QString describe(const Config& c) {
    switch (c.mode) {
        case Mode::None:
            return QStringLiteral("direct (no proxy)");
        case Mode::Manual:
            return QStringLiteral("proxy %1").arg(shown(c.url));
        case Mode::System:
            break;
    }
    const auto list = system_proxy_for(QNetworkProxyQuery(kProbeUrl));
    const QString url = list.isEmpty() ? QString() : url_of(list.first());
    return url.isEmpty() ? QStringLiteral("the system's setting: direct (no proxy found)")
                         : QStringLiteral("the system's setting: %1").arg(shown(url));
}

QString apply(const Config& c) {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const OriginalEnv& orig = original_env();
    QNetworkProxyFactory::setApplicationProxyFactory(new Factory(c)); // takes ownership

    // Children: start from what finterm was given, then the choice on top.
    for (const char* v : kVars)
        set_var(v, QString::fromUtf8(orig.values.value(QString::fromLatin1(v))));
    QString child_proxy;
    if (c.mode == Mode::Manual) {
        child_proxy = c.url.trimmed();
        if (child_proxy.startsWith(QLatin1String("socks5://"), Qt::CaseInsensitive))
            child_proxy.replace(0, 6, QStringLiteral("socks5h")); // names resolved by the proxy
    } else if (c.mode == Mode::System && !orig.values.contains(QStringLiteral("HTTPS_PROXY")) &&
               !orig.values.contains(QStringLiteral("https_proxy"))) {
        // The system's own setting (Windows/macOS settings, a PAC script)
        // reaches Qt but not Python's curl: hand it over.
        const auto list = system_proxy_for(QNetworkProxyQuery(kProbeUrl));
        if (!list.isEmpty())
            child_proxy = url_of(list.first());
    }
    if (c.mode == Mode::None) {
        for (const char* v : {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"})
            qunsetenv(v);
        set_var("NO_PROXY", QStringLiteral("*"));
        set_var("no_proxy", QStringLiteral("*"));
    } else {
        if (!child_proxy.isEmpty())
            for (const char* v : {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy"})
                set_var(v, child_proxy);
        const QString no_proxy = with_loopback(qEnvironmentVariable("NO_PROXY", qEnvironmentVariable("no_proxy")));
        set_var("NO_PROXY", no_proxy);
        set_var("no_proxy", no_proxy);
    }
    const QString what = describe(c);
    LOG_INFO("Network", QStringLiteral("Internet access: %1").arg(what));
    return what;
}

} // namespace finterm::proxy
