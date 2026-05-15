#include "screens/dashboard/widgets/NewsWidget.h"

#include "core/logging/Logger.h"
#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

namespace fincept::screens::widgets {

namespace {
constexpr int kRefreshTimeoutMs = 25'000; // PythonWorker network timeout is similar; we want slightly larger
} // namespace

NewsWidget::NewsWidget(QWidget* parent) : BaseWidget("MARKET NEWS", parent, ui::colors::CYAN) {
    // Status line above the scrollable feed — small, single-line, always
    // present so the user has visible proof the refresh actually completed
    // (matters when yfinance returns the same headlines after a long idle).
    status_label_ = new QLabel(QStringLiteral("Loading…"));
    status_label_->setFixedHeight(14);
    content_layout()->addWidget(status_label_);

    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);

    auto* container = new QWidget(this);
    news_layout_ = new QVBoxLayout(container);
    news_layout_->setContentsMargins(4, 4, 4, 4);
    news_layout_->setSpacing(0);
    news_layout_->addStretch();

    scroll_area_->setWidget(container);
    content_layout()->addWidget(scroll_area_);

    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this]() {
        // Daemon never returned — treat as failure so the spinner doesn't
        // hang. The original request's callback may still fire later; the
        // token check (below) silently discards it.
        ++refresh_token_;
        set_loading(false);
        finalize_refresh(QStringLiteral("timeout"));
        LOG_WARN("NewsWidget", "fetch_news watchdog fired — daemon did not respond");
    });

    connect(this, &BaseWidget::refresh_requested, this, &NewsWidget::refresh_data);

    apply_styles();
    set_loading(true);
    refresh_data();
}

void NewsWidget::apply_styles() {
    scroll_area_->setStyleSheet(QString("QScrollArea { border: none; background: transparent; }"
                                        "QScrollBar:vertical { width: 6px; background: transparent; }"
                                        "QScrollBar::handle:vertical { background: %1; border-radius: 3px; }"
                                        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
                                    .arg(ui::colors::BORDER_MED()));
    if (status_label_)
        status_label_->setStyleSheet(
            QString("color:%1;background:transparent;padding:0 6px;").arg(ui::colors::TEXT_SECONDARY()));
}

void NewsWidget::on_theme_changed() {
    apply_styles();
    if (!last_articles_.isEmpty())
        populate(last_articles_);
}

void NewsWidget::refresh_data() {
    set_loading(true);
    status_label_->setText(QStringLiteral("Refreshing…"));

    // Token guards against the watchdog firing and a late real callback
    // racing — the late callback will see a mismatched token and bail.
    const qint64 token = ++refresh_token_;
    watchdog_->start(kRefreshTimeoutMs);

    services::MarketDataService::instance().fetch_news("SPY", 10, [this, token](bool ok, QJsonArray articles) {
        if (token != refresh_token_)
            return; // superseded by watchdog or a newer click
        watchdog_->stop();
        set_loading(false);

        if (!ok) {
            finalize_refresh(QStringLiteral("error"));
            if (news_layout_->count() <= 1)
                set_error("News fetch failed. Check Python/yfinance.");
            LOG_WARN("NewsWidget", "fetch_news failed (ok=false)");
            return;
        }
        if (articles.isEmpty()) {
            // yfinance occasionally returns an empty list (rate limit, weekend,
            // session reset). Keep prior articles, just acknowledge the click.
            finalize_refresh(QStringLiteral("no new articles"));
            LOG_INFO("NewsWidget", "fetch_news returned empty array — keeping prior articles");
            return;
        }
        populate(articles);
        finalize_refresh(QString::number(articles.size()) + " items");
    });
}

void NewsWidget::finalize_refresh(const QString& outcome) {
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    status_label_->setText(QStringLiteral("Last updated %1 · %2").arg(time, outcome));
}

void NewsWidget::populate(const QJsonArray& articles) {
    last_articles_ = articles;

    // Clear old items (keep the stretch)
    while (news_layout_->count() > 1) {
        auto* item = news_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (const auto& val : articles) {
        auto obj = val.toObject();
        QString title = obj["title"].toString();
        QString publisher = obj["publisher"].toString();
        QString pub_date = obj["published_date"].toString();

        if (title.isEmpty())
            continue;

        // Extract time portion from date
        QString time_str;
        if (pub_date.length() >= 16) {
            time_str = pub_date.mid(11, 5); // "HH:MM"
        }

        auto* row = new QWidget(this);
        row->setStyleSheet(QString("border-bottom: 1px solid %1;").arg(ui::colors::BORDER_DIM()));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(4, 4, 4, 4);
        rl->setSpacing(8);

        if (!time_str.isEmpty()) {
            auto* time_lbl = new QLabel(time_str);
            time_lbl->setFixedWidth(36);
            time_lbl->setStyleSheet(
                QString("color: %1; background: transparent;").arg(ui::colors::CYAN()));
            rl->addWidget(time_lbl);
        }

        auto* headline = new QLabel(title);
        headline->setWordWrap(true);
        headline->setStyleSheet(
            QString("color: %1; background: transparent;").arg(ui::colors::TEXT_PRIMARY()));
        rl->addWidget(headline, 1);

        if (!publisher.isEmpty()) {
            auto* src = new QLabel(publisher);
            src->setStyleSheet(
                QString("color: %1; background: transparent;").arg(ui::colors::TEXT_SECONDARY()));
            rl->addWidget(src);
        }

        // Insert before the stretch
        news_layout_->insertWidget(news_layout_->count() - 1, row);
    }
}

} // namespace fincept::screens::widgets
