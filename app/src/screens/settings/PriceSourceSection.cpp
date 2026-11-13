#include "screens/settings/PriceSourceSection.h"

#include "core/config/AppIdentity.h"
#include "screens/settings/SettingsStyles.h"
#include "storage/secure/SecureStorage.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {

constexpr const char* kSettingKey = "market_data/price_source";

struct Vendor {
    const char* id;
    const char* name;
    QStringList keys; // keyring entries it needs (Settings › API Keys)
    const char* what; // what it answers, and on its free tier
};

const QList<Vendor>& vendors() {
    static const QList<Vendor> v = {
        {"yahoo", "Yahoo Finance (default)", {}, "Everything, no key. Unofficial; can rate-limit or refuse."},
        {"tiingo", "Tiingo", {"TIINGO_API_KEY"},
         "Daily bars (adjusted) and live IEX quotes. Free tier covers both, within its daily limits."},
        {"polygon", "Polygon.io / Massive", {"POLYGON_API_KEY"},
         "Split-adjusted daily bars, used where finterm wants prices without dividend adjustment (the "
         "trailing stop); snapshot quotes on paid plans. Free tier: bars only, 5 calls a minute."},
        {"alpaca", "Alpaca market data (free IEX feed)", {"ALPACA_API_KEY_ID", "ALPACA_API_SECRET_KEY"},
         "Live quotes, price only (one venue's volume would mislead). Bars stay on Yahoo. Free with an "
         "Alpaca account."},
        {"alpaca_sip", "Alpaca market data (paid consolidated feed)", {"ALPACA_API_KEY_ID", "ALPACA_API_SECRET_KEY"},
         "Daily bars (adjusted) and quotes from the full tape. Needs Alpaca's paid market-data plan."},
        {"finnhub", "Finnhub", {"FINNHUB_API_KEY"}, "Live quotes only (bars stay on Yahoo). Free tier covers quotes."},
        {"fmp", "Financial Modeling Prep", {"FMP_API_KEY"},
         "Daily bars (adjusted) and quotes. Free tier: 250 calls a day."},
    };
    return v;
}

bool keys_set(const Vendor& v) {
    for (const QString& k : v.keys) {
        const auto r = SecureStorage::instance().retrieve(k);
        if (r.is_err() || r.value().trimmed().isEmpty())
            return false;
    }
    return true;
}

} // namespace

PriceSourceSection::PriceSourceSection(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("PRICE SOURCE"));
    title->setStyleSheet(settings_style::section_title_ss());
    vl->addWidget(title);

    auto* intro = new QLabel(QStringLiteral(
        "Who answers daily, weekly and monthly prices and stock quotes. Intraday bars, indices, futures, "
        "FX, crypto and fundamentals stay on Yahoo, and Yahoo answers whenever the chosen source can't "
        "(no key, a limit reached, a symbol it doesn't carry, a day's bar not posted yet, quotes outside "
        "regular hours). Takes effect on the next start."));
    intro->setWordWrap(true);
    intro->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(intro);

    combo_ = new QComboBox;
    combo_->setMaximumWidth(360);
    vl->addWidget(combo_);

    detail_ = new QLabel;
    detail_->setWordWrap(true);
    detail_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(detail_);

    connect(combo_, &QComboBox::currentIndexChanged, this, [this]() {
        QSettings(AppIdentity::kOrg, AppIdentity::kApp).setValue(kSettingKey, combo_->currentData().toString());
        update_detail();
    });
}

void PriceSourceSection::reload() {
    const QString current = QSettings(AppIdentity::kOrg, AppIdentity::kApp)
                                .value(kSettingKey, QStringLiteral("yahoo"))
                                .toString();
    const QSignalBlocker block(combo_);
    combo_->clear();
    missing_keys_.clear();
    for (const Vendor& v : vendors()) {
        QString label = QString::fromLatin1(v.name);
        if (!v.keys.isEmpty() && !keys_set(v)) {
            missing_keys_.insert(QString::fromLatin1(v.id));
            label += QStringLiteral("  — needs a key");
        }
        combo_->addItem(label, QString::fromLatin1(v.id));
    }
    const int idx = combo_->findData(current);
    combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    update_detail();
}

void PriceSourceSection::update_detail() {
    const QString id = combo_->currentData().toString();
    for (const Vendor& v : vendors()) {
        if (id != QLatin1String(v.id))
            continue;
        QString text = QString::fromLatin1(v.what);
        if (missing_keys_.contains(QString::fromLatin1(v.id)))
            text += QStringLiteral("\n\nAdd %1 under Settings › API Keys — until then prices come from Yahoo.")
                        .arg(v.keys.join(QStringLiteral(" and ")));
        detail_->setText(text);
        return;
    }
}

void PriceSourceSection::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    reload(); // a key added under API Keys shows here without a restart
}

} // namespace finterm::screens
