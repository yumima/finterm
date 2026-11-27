#pragma once
// ProxySettings — how finterm reaches the internet: the system's proxy (the
// default), no proxy, or one the user names (a VPN client's local proxy such
// as http://127.0.0.1:7890). Applied in one place to both halves of the app:
// Qt's network stack (news, calendars, SEC) through a proxy factory, and the
// Python data service and scripts through the proxy environment variables
// they inherit (yfinance's curl and requests read them). Loopback always
// goes direct — the data daemon's socket, a local AI server.

#include <QString>

class QNetworkProxyFactory;

namespace finterm::proxy {

enum class Mode { System, None, Manual };

struct Config {
    Mode mode = Mode::System;
    QString url; ///< Manual: http://host:port, https://…, socks5://host:port
};

/// The stored choice (QSettings network/proxy_mode, network/proxy_url).
Config load();
void save(const Config& config);

/// "" when `url` can be a proxy, else why not.
QString url_problem(const QString& url);

/// Apply `config` to this process: Qt's proxy factory and the environment
/// every child process inherits. Call at startup (before any request), and
/// after the user changes it. Returns what is in use, for display.
QString apply(const Config& config);

/// A proxy factory for `config` (loopback direct) — for a network manager
/// that tries a configuration before it is saved. The caller owns it.
QNetworkProxyFactory* make_factory(const Config& config);

/// What `config` resolves to right now, in words ("system: http://… ",
/// "direct", …).
QString describe(const Config& config);

} // namespace finterm::proxy
