#include "screens/fingpt/FinGptScreen.h"

#include "ai_chat/AiChatScreen.h"
#include "core/logging/Logger.h"
#include "screens/fingpt/FinGptForecasterTab.h"
#include "screens/fingpt/FinGptSentimentTab.h"
#include "services/app_context/AppContextService.h"
#include "storage/repositories/PortfolioRepository.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace fincept::screens::fingpt {

using namespace fincept::ui;

namespace {

struct TabDef {
    const char* label;
    const char* description;
};

constexpr TabDef kTabDefs[FinGptScreen::TabCount] = {
    {"Chat", "AI chat (FinGPT Analyst persona)"},
    {"Forecaster", "Weekly news-driven forecast, FinGPT-Forecaster pipeline"},
    {"Sentiment", "Headline & text sentiment, FinGPT instruction format"},
};

} // namespace

FinGptScreen::FinGptScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("fingptScreen");
    setStyleSheet(QStringLiteral(
        "#fingptScreen { background:%1; }"
        "#fingptTabBar { background:%1; border-bottom:1px solid %2; }"
        "#fingptTabBtn { background:transparent; color:%3; border:none; padding:8px 18px; "
        "               font-size:10px; font-weight:700; letter-spacing:0.5px; }"
        "#fingptTabBtn:hover { background:%4; color:%5; }"
        "#fingptTabBtn[active=\"true\"] { color:%6; border-bottom:2px solid %6; }"
        "#fingptPlaceholder { color:%3; font-size:14px; background:transparent; }"
        "#fingptPlaceholderHint { color:%7; font-size:11px; background:transparent; }")
                      .arg(colors::BG_BASE(), colors::BORDER_DIM(), colors::TEXT_SECONDARY(),
                           colors::BG_HOVER(), colors::TEXT_PRIMARY(), colors::AMBER(),
                           colors::TEXT_DIM()));
    setup_ui();
    LOG_INFO("FinGptScreen", "Constructed");
}

void FinGptScreen::setup_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(build_tab_bar());

    stack_ = new QStackedWidget(this);
    // Placeholder per slot so stack indices map 1:1 to SubTab values; the
    // default tab is built immediately below.
    for (int i = 0; i < TabCount; ++i) {
        stack_->addWidget(build_placeholder(QString::fromLatin1(kTabDefs[i].label),
                                            QString::fromLatin1(kTabDefs[i].description)));
    }
    root->addWidget(stack_, 1);

    ensure_tab_built(TabChat);
    stack_->setCurrentIndex(TabChat);
    refresh_tab_button_styles();
}

QWidget* FinGptScreen::build_tab_bar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("fingptTabBar");
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 0, 8, 0);
    lay->setSpacing(2);

    tab_btns_.reserve(TabCount);
    for (int i = 0; i < TabCount; ++i) {
        auto* btn = new QPushButton(QString::fromLatin1(kTabDefs[i].label).toUpper(), bar);
        btn->setObjectName("fingptTabBtn");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("active", i == int(active_tab_));
        btn->setToolTip(QString::fromLatin1(kTabDefs[i].description));
        connect(btn, &QPushButton::clicked, this, [this, i]() { on_tab_clicked(i); });
        lay->addWidget(btn);
        tab_btns_.append(btn);
    }
    lay->addStretch(1);
    auto* credit = new QLabel(QStringLiteral("FinGPT task suite · AI4Finance (MIT) · local models"), bar);
    credit->setObjectName("fingptPlaceholderHint");
    lay->addWidget(credit);
    return bar;
}

QWidget* FinGptScreen::build_placeholder(const QString& tab_name, const QString& detail) {
    auto* wrap = new QWidget;
    auto* lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addStretch(1);
    auto* title = new QLabel(tab_name, wrap);
    title->setObjectName("fingptPlaceholder");
    title->setAlignment(Qt::AlignCenter);
    auto* hint = new QLabel(detail, wrap);
    hint->setObjectName("fingptPlaceholderHint");
    hint->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);
    lay->addWidget(hint);
    lay->addStretch(2);
    return wrap;
}

void FinGptScreen::ensure_tab_built(SubTab which) {
    const auto swap_in = [this](int slot, QWidget* widget) {
        QWidget* old = stack_->widget(slot);
        stack_->removeWidget(old);
        stack_->insertWidget(slot, widget);
        if (old)
            old->deleteLater();
    };
    if (which == TabChat && !chat_tab_) {
        // The terminal's full chat screen, opening on the FinGPT Analyst
        // persona. Sessions are shared with every other chat surface
        // (same ChatRepository) — this is a view, not a fork.
        auto* chat = new fincept::screens::AiChatScreen(this, QStringLiteral("fingpt"));
        connect(chat, &fincept::screens::AiChatScreen::request_new_pane, this,
                &FinGptScreen::request_new_chat_pane);
        if (!pending_chat_state_.isEmpty())
            chat->restore_state(std::exchange(pending_chat_state_, {}));
        swap_in(TabChat, chat);
        chat_tab_ = chat;
    }
    if (which == TabForecaster && !forecaster_tab_) {
        auto* t = new FinGptForecasterTab(this);
        if (!pending_symbol_.isEmpty())
            t->set_symbol(pending_symbol_);
        if (!pending_forecaster_state_.isEmpty())
            t->restore_state(std::exchange(pending_forecaster_state_, {}));
        // After link traffic and restore have had their say: a still-blank tab
        // seeds itself and assembles the evidence pane (no LLM call).
        t->seed(seed_symbol());
        swap_in(TabForecaster, t);
        forecaster_tab_ = t;
    }
    if (which == TabSentiment && !sentiment_tab_) {
        auto* t = new FinGptSentimentTab(this);
        if (!pending_symbol_.isEmpty())
            t->set_symbol(pending_symbol_);
        if (!pending_sentiment_state_.isEmpty())
            t->restore_state(std::exchange(pending_sentiment_state_, {}));
        t->seed(seed_symbol());
        swap_in(TabSentiment, t);
        sentiment_tab_ = t;
    }
}

void FinGptScreen::on_tab_clicked(int index) {
    if (index < 0 || index >= TabCount)
        return;
    active_tab_ = SubTab(index);
    ensure_tab_built(active_tab_);
    stack_->setCurrentIndex(index);
    refresh_tab_button_styles();
}

void FinGptScreen::refresh_tab_button_styles() {
    for (int i = 0; i < tab_btns_.size(); ++i) {
        QPushButton* btn = tab_btns_.at(i);
        btn->setProperty("active", i == int(active_tab_));
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

QVariantMap FinGptScreen::save_state() const {
    QVariantMap m;
    m["active_tab"] = int(active_tab_);
    // A tab that was never built this session still carries the previous
    // session's restored-but-pending state — write it back out, or one
    // save/restore cycle without opening the tab would erase it.
    if (chat_tab_)
        m["chat"] = chat_tab_->save_state();
    else if (!pending_chat_state_.isEmpty())
        m["chat"] = pending_chat_state_;
    if (forecaster_tab_)
        m["forecaster"] = forecaster_tab_->save_state();
    else if (!pending_forecaster_state_.isEmpty())
        m["forecaster"] = pending_forecaster_state_;
    if (sentiment_tab_)
        m["sentiment"] = sentiment_tab_->save_state();
    else if (!pending_sentiment_state_.isEmpty())
        m["sentiment"] = pending_sentiment_state_;
    return m;
}

void FinGptScreen::restore_state(const QVariantMap& state) {
    // Sub-states are handed off BEFORE any tab is built: ensure_tab_built
    // applies the pending map and only seeds what is still blank afterwards.
    // The old order built-and-seeded the active tab first, so the seed's
    // evidence run raced the saved state — a restored NVDA answer could end
    // up over freshly fetched AAPL evidence, and save_state() would then
    // persist that mixed state to disk.
    //
    // Tabs not built here keep their maps pending and restore on first build;
    // eagerly constructing every sub-tab with a saved key would rebuild all
    // three widget trees on every layout restore.
    const auto hand_off = [this](const char* key, const QVariantMap& state, QWidget* built,
                                 QVariantMap& pending, auto&& apply) {
        if (!state.contains(QLatin1String(key)))
            return;
        const QVariantMap sub = state.value(QLatin1String(key)).toMap();
        if (built)
            apply(sub);
        else
            pending = sub;
    };
    hand_off("chat", state, chat_tab_, pending_chat_state_,
             [this](const QVariantMap& m) { chat_tab_->restore_state(m); });
    hand_off("forecaster", state, forecaster_tab_, pending_forecaster_state_,
             [this](const QVariantMap& m) { forecaster_tab_->restore_state(m); });
    hand_off("sentiment", state, sentiment_tab_, pending_sentiment_state_,
             [this](const QVariantMap& m) { sentiment_tab_->restore_state(m); });

    if (state.contains("active_tab")) {
        const int idx = state.value("active_tab").toInt();
        if (idx >= 0 && idx < TabCount) {
            active_tab_ = SubTab(idx);
            ensure_tab_built(active_tab_);
            if (stack_)
                stack_->setCurrentIndex(idx);
            refresh_tab_button_styles();
        }
    }
}

void FinGptScreen::on_group_symbol_changed(const fincept::SymbolRef& ref) {
    // Follow the linked group's ticker into the task tabs (field only — a run
    // always starts from the user's own click, never from link traffic).
    // Tabs not built yet just remember it (pending_symbol_): link traffic
    // must not construct widget trees the user may never open.
    if (ref.symbol.isEmpty())
        return;
    pending_symbol_ = ref.symbol.toUpper();
    if (forecaster_tab_)
        forecaster_tab_->set_symbol(pending_symbol_);
    if (sentiment_tab_)
        sentiment_tab_->set_symbol(pending_symbol_);
}

QString FinGptScreen::seed_symbol() {
    // A blank pane with an empty field read as "the tab shows nothing" — seed
    // it with what the user most likely means. Link traffic first: a group-
    // linked pane must open on the group's ticker, whatever else is in focus.
    if (!pending_symbol_.isEmpty())
        return pending_symbol_;
    const auto snap = fincept::services::AppContextService::instance().snapshot();
    if (!snap.symbol.isEmpty())
        return snap.symbol.toUpper();

    if (portfolio_seed_cache_.isEmpty()) {
        // Largest holding of the active portfolio, else of the first one.
        // Cost basis in the instrument's own currency — a familiarity guess,
        // not a valuation (no FX, no live prices; PortfolioService owns the
        // real figure). |qty × avg price| so a short position still counts as
        // a holding the user knows.
        auto& repo = fincept::PortfolioRepository::instance();
        QString pid = snap.portfolio_id;
        if (pid.isEmpty()) {
            const auto ports = repo.list_portfolios();
            if (ports.is_ok() && !ports.value().isEmpty())
                pid = ports.value().first().id;
        }
        QString best, first_sym;
        double best_val = 0.0;
        if (!pid.isEmpty()) {
            const auto assets = repo.get_assets(pid);
            if (assets.is_ok()) {
                for (const auto& a : assets.value()) {
                    if (a.symbol.isEmpty())
                        continue;
                    if (first_sym.isEmpty())
                        first_sym = a.symbol;
                    const double v = std::abs(a.quantity * a.avg_buy_price);
                    if (v > best_val) {
                        best_val = v;
                        best = a.symbol;
                    }
                }
            }
        }
        // Zero-cost imports leave every |v| at 0 — any real holding still
        // beats the generic fallback.
        if (best.isEmpty())
            best = first_sym;
        portfolio_seed_cache_ = best.isEmpty() ? QStringLiteral("AAPL") : best.toUpper();
    }
    return portfolio_seed_cache_;
}

fincept::SymbolRef FinGptScreen::current_symbol() const {
    // Whichever task tab has a ticker — the forecaster first, then sentiment,
    // then the remembered link traffic. A symbol typed only into Sentiment is
    // not second-class for "load group from me".
    QString sym;
    if (forecaster_tab_ && !forecaster_tab_->symbol().isEmpty())
        sym = forecaster_tab_->symbol();
    else if (sentiment_tab_ && !sentiment_tab_->symbol().isEmpty())
        sym = sentiment_tab_->symbol();
    else
        sym = pending_symbol_;
    if (sym.isEmpty())
        return {};
    fincept::SymbolRef ref;
    ref.symbol = sym;
    ref.asset_class = QStringLiteral("equity");
    return ref;
}

} // namespace fincept::screens::fingpt
