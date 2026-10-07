// src/services/app_context/AppContextService.h
#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QString>

class QWidget;

namespace fincept::services {

/// Lightweight, thread-safe snapshot of "what the user is currently looking at"
/// — the security in focus and the active portfolio. Producers (the equity and
/// portfolio screens) push updates as the user navigates; the AI chat *pulls* a
/// snapshot when building a request so answers are grounded in the current view
/// instead of starting blind.
///
/// Every value carries the time it was set, so the prompt can say "as of HH:MM"
/// rather than presenting an old focus as current.
///
/// Staleness: a value is only "current" while its source screen is on screen.
/// The service watches dock-panel visibility (an application event filter that
/// looks only at Show events of `ads::CDockWidget`s) and clears a value when the
/// user moves to some *other* screen while no panel of the source screen is
/// visible. Moving to a chat panel does not clear it — going to the chat to ask
/// about the stock you were just looking at is the point of the feature — but
/// the "as of" time still tells the model how old it is. When the user returns
/// to the source screen the parked value is restored (re-stamped). A deleted
/// portfolio is dropped immediately.
///
/// Non-QObject (no MOC). The dock watcher and the portfolio_deleted wiring
/// live in AppContextService.cpp so this header stays light for its includers.
class AppContextService {
  public:
    struct Snapshot {
        QString symbol;            ///< e.g. "AAPL" — the security in focus, if any
        QDateTime symbol_as_of;    ///< when `symbol` was last set / re-confirmed
        QString portfolio_id;      ///< active portfolio id, if any
        QString portfolio_name;    ///< its display name, if known
        QDateTime portfolio_as_of; ///< when `portfolio_id` was last set / re-confirmed
    };

    static AppContextService& instance();

    void set_current_symbol(const QString& sym);
    void set_active_portfolio(const QString& id, const QString& name = QString());
    void clear_symbol();
    void clear_portfolio();
    /// Drop the active (or parked) portfolio if it is the one that was deleted.
    void clear_portfolio_if(const QString& id);
    Snapshot snapshot() const;

  private:
    class Watcher; // application event filter; defined in the .cpp

    AppContextService();
    ~AppContextService();
    AppContextService(const AppContextService&) = delete;
    AppContextService& operator=(const AppContextService&) = delete;

    void clear_portfolio_locked();
    // GUI thread only. Installed lazily by the first producer call.
    void ensure_watch();
    void on_dock_shown(QWidget* dock);
    bool source_visible(const QString& source);
    void reevaluate();

    mutable QMutex mutex_;
    Snapshot snap_;
    QString parked_symbol_;
    QString parked_portfolio_id_;
    QString parked_portfolio_name_;

    // GUI-thread state (touched only from the watcher / producers).
    Watcher* watcher_ = nullptr;
    QHash<QString, QPointer<QWidget>> docks_;
    QString last_shown_base_;
};

} // namespace fincept::services
