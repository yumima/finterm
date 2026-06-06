// src/services/app_context/AppContextService.h
#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QString>

namespace fincept::services {

/// Lightweight, thread-safe snapshot of "what the user is currently looking at"
/// — the security in focus and the active portfolio. Producers (the equity and
/// portfolio screens) push updates as the user navigates; the AI chat *pulls* a
/// snapshot when building a request so answers are grounded in the current view
/// instead of starting blind.
///
/// Deliberately header-only and **non-QObject** (no signals): consumers pull on
/// demand, they don't subscribe. That keeps it dependency-free and avoids any
/// MOC / build-graph wiring.
class AppContextService {
  public:
    struct Snapshot {
        QString symbol;          ///< e.g. "AAPL" — the security in focus, if any
        QString portfolio_id;    ///< active portfolio id, if any
        QString portfolio_name;  ///< its display name, if known
    };

    static AppContextService& instance() {
        static AppContextService s;
        return s;
    }

    void set_current_symbol(const QString& sym) {
        QMutexLocker lock(&mutex_);
        snap_.symbol = sym.trimmed().toUpper();
    }

    void set_active_portfolio(const QString& id, const QString& name = QString()) {
        QMutexLocker lock(&mutex_);
        snap_.portfolio_id = id.trimmed();
        if (!name.isEmpty())
            snap_.portfolio_name = name;
    }

    Snapshot snapshot() const {
        QMutexLocker lock(&mutex_);
        return snap_;
    }

  private:
    AppContextService() = default;
    mutable QMutex mutex_;
    Snapshot snap_;
};

} // namespace fincept::services
