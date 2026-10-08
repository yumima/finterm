// src/services/app_context/AppContextService.cpp
#include "services/app_context/AppContextService.h"

#include "services/portfolio/PortfolioService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QMutexLocker>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QWidget>

namespace fincept::services {

namespace {
// Dock ids (DockScreenRouter objectName, minus any "#dupN" suffix).
constexpr const char* kAppCtxSymbolSource = "equity_research";
constexpr const char* kAppCtxPortfolioSource = "portfolio";

bool app_ctx_is_chat_surface(const QString& base) {
    return base == QLatin1String("ai_chat") || base == QLatin1String("agent_config") ||
           base == QLatin1String("fingpt");  // embeds AiChatScreen as its Chat sub-tab
}

QString app_ctx_base_id(const QObject* o) {
    return o->objectName().section(QLatin1Char('#'), 0, 0);
}
} // namespace

// Needs only a virtual eventFilter override, which does not require Q_OBJECT.
class AppContextService::Watcher : public QObject {
  public:
    explicit Watcher(AppContextService* svc) : QObject(nullptr), svc_(svc) {}

  protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        // Cheapest test first: this sees every event in the application.
        if (e->type() != QEvent::Show || e->spontaneous())
            return false;
        if (!o->isWidgetType() || !o->inherits("ads::CDockWidget"))
            return false;
        svc_->on_dock_shown(static_cast<QWidget*>(o));
        return false;
    }

  private:
    AppContextService* svc_;
};

AppContextService& AppContextService::instance() {
    static AppContextService s;
    return s;
}

AppContextService::AppContextService() = default;
AppContextService::~AppContextService() = default;

void AppContextService::set_current_symbol(const QString& sym) {
    ensure_watch();
    QMutexLocker lock(&mutex_);
    snap_.symbol = sym.trimmed().toUpper();
    snap_.symbol_as_of = QDateTime::currentDateTime();
    parked_symbol_.clear();
}

void AppContextService::set_active_portfolio(const QString& id, const QString& name) {
    ensure_watch();
    QMutexLocker lock(&mutex_);
    const QString trimmed = id.trimmed();
    if (trimmed != snap_.portfolio_id)
        snap_.portfolio_name.clear(); // never pair a new id with the old name
    snap_.portfolio_id = trimmed;
    if (!name.isEmpty())
        snap_.portfolio_name = name;
    snap_.portfolio_as_of = QDateTime::currentDateTime();
    parked_portfolio_id_.clear();
    parked_portfolio_name_.clear();
}

void AppContextService::clear_symbol() {
    QMutexLocker lock(&mutex_);
    snap_.symbol.clear();
    snap_.symbol_as_of = {};
    parked_symbol_.clear();
}

void AppContextService::clear_portfolio() {
    QMutexLocker lock(&mutex_);
    clear_portfolio_locked();
    parked_portfolio_id_.clear();
    parked_portfolio_name_.clear();
}

void AppContextService::clear_portfolio_if(const QString& id) {
    QMutexLocker lock(&mutex_);
    if (!id.isEmpty() && snap_.portfolio_id == id)
        clear_portfolio_locked();
    if (!id.isEmpty() && parked_portfolio_id_ == id) {
        parked_portfolio_id_.clear();
        parked_portfolio_name_.clear();
    }
}

AppContextService::Snapshot AppContextService::snapshot() const {
    QMutexLocker lock(&mutex_);
    return snap_;
}

void AppContextService::clear_portfolio_locked() {
    snap_.portfolio_id.clear();
    snap_.portfolio_name.clear();
    snap_.portfolio_as_of = {};
}

// GUI thread only. Installed lazily by the first producer call (producers
// are screens, so this runs on the GUI thread with a QCoreApplication).
void AppContextService::ensure_watch() {
    auto* app = QCoreApplication::instance();
    if (watcher_ || !app || QThread::currentThread() != app->thread())
        return;
    watcher_ = new Watcher(this);
    watcher_->moveToThread(app->thread());
    app->installEventFilter(watcher_);
    QObject::connect(&PortfolioService::instance(), &PortfolioService::portfolio_deleted, watcher_,
                     [this](const QString& id) { clear_portfolio_if(id); });
    // Panels that already exist (and may be on screen) before the watch did.
    for (QWidget* w : QApplication::allWidgets())
        if (w->inherits("ads::CDockWidget"))
            docks_.insert(w->objectName(), QPointer<QWidget>(w));
}

void AppContextService::on_dock_shown(QWidget* dock) {
    const QString base = app_ctx_base_id(dock);
    docks_.insert(dock->objectName(), QPointer<QWidget>(dock));
    last_shown_base_ = base;
    // Re-evaluate once the show/hide pair of a tab switch has settled.
    QTimer::singleShot(0, watcher_, [this]() { reevaluate(); });
}

bool AppContextService::source_visible(const QString& source) {
    for (auto it = docks_.begin(); it != docks_.end();) {
        if (it.value().isNull()) {
            it = docks_.erase(it);
            continue;
        }
        if (it.key().section(QLatin1Char('#'), 0, 0) == source && it.value()->isVisible())
            return true;
        ++it;
    }
    return false;
}

void AppContextService::reevaluate() {
    const QString last = last_shown_base_;
    const bool sym_src = source_visible(QLatin1String(kAppCtxSymbolSource));
    const bool pf_src = source_visible(QLatin1String(kAppCtxPortfolioSource));
    QMutexLocker lock(&mutex_);
    const QDateTime now = QDateTime::currentDateTime();
    // Returning to the source screen: bring back what it was showing.
    if (last == QLatin1String(kAppCtxSymbolSource) && snap_.symbol.isEmpty() && !parked_symbol_.isEmpty()) {
        snap_.symbol = parked_symbol_;
        snap_.symbol_as_of = now;
        parked_symbol_.clear();
    }
    if (last == QLatin1String(kAppCtxPortfolioSource) && snap_.portfolio_id.isEmpty() &&
        !parked_portfolio_id_.isEmpty()) {
        snap_.portfolio_id = parked_portfolio_id_;
        snap_.portfolio_name = parked_portfolio_name_;
        snap_.portfolio_as_of = now;
        parked_portfolio_id_.clear();
        parked_portfolio_name_.clear();
    }
    // Moved to some other (non-chat) screen with the source no longer
    // visible: the focus is no longer "current view". Park it.
    if (app_ctx_is_chat_surface(last))
        return;
    if (!sym_src && !snap_.symbol.isEmpty() && last != QLatin1String(kAppCtxSymbolSource)) {
        parked_symbol_ = snap_.symbol;
        snap_.symbol.clear();
        snap_.symbol_as_of = {};
    }
    if (!pf_src && !snap_.portfolio_id.isEmpty() && last != QLatin1String(kAppCtxPortfolioSource)) {
        parked_portfolio_id_ = snap_.portfolio_id;
        parked_portfolio_name_ = snap_.portfolio_name;
        clear_portfolio_locked();
    }
}

} // namespace fincept::services
