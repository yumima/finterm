#include "screens/settings/NetworkSection.h"

#include "core/errors/DataFailure.h"
#include "core/network/ProxySettings.h"
#include "python/PythonWorker.h"
#include "screens/settings/SettingsStyles.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens {

namespace {
// The host the market data comes from: any HTTP answer from it proves the
// route (proxy, VPN, DNS, TLS) works.
const QUrl kTestUrl(QStringLiteral("https://query1.finance.yahoo.com/v8/finance/chart/SPY?range=1d&interval=1d"));
constexpr int kTestTimeoutMs = 15000;

proxy::Mode mode_of(int id) {
    return id == 1 ? proxy::Mode::None : id == 2 ? proxy::Mode::Manual : proxy::Mode::System;
}
} // namespace

NetworkSection::NetworkSection(QWidget* parent) : QWidget(parent) {
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("NETWORK"), this);
    title->setStyleSheet(settings_style::sub_title_ss());
    vl->addWidget(title);
    auto* info = new QLabel(QStringLiteral("How finterm reaches Yahoo Finance, SEC and the other sources. Where they "
                                           "are blocked, a VPN is needed; if your VPN client works as a local proxy "
                                           "(for example 127.0.0.1:7890), choose it here."),
                            this);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(info);

    mode_ = new QButtonGroup(this);
    const proxy::Config cfg = proxy::load();
    const struct {
        int id;
        const char* label;
    } choices[] = {{0, "Use the system's proxy setting (default)"}, {1, "No proxy — connect directly"},
                   {2, "Use this proxy:"}};
    QRadioButton* manual = nullptr;
    for (const auto& c : choices) {
        auto* r = new QRadioButton(QString::fromUtf8(c.label), this);
        r->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
        mode_->addButton(r, c.id);
        if (c.id == 2) {
            manual = r;
            continue;
        }
        vl->addWidget(r);
    }
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(manual);
    url_ = new QLineEdit(cfg.url, this);
    url_->setPlaceholderText(QStringLiteral("http://127.0.0.1:7890  or  socks5://127.0.0.1:7891"));
    url_->setStyleSheet(settings_style::input_ss());
    url_->setMinimumWidth(280);
    row->addWidget(url_, 1);
    vl->addLayout(row);
    mode_->button(cfg.mode == proxy::Mode::None ? 1 : cfg.mode == proxy::Mode::Manual ? 2 : 0)->setChecked(true);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 4, 0, 0);
    apply_ = new QPushButton(QStringLiteral("Apply"), this);
    apply_->setStyleSheet(settings_style::btn_primary_ss());
    test_ = new QPushButton(QStringLiteral("Test connection"), this);
    test_->setStyleSheet(settings_style::btn_secondary_ss());
    test_->setToolTip(QStringLiteral("Ask Yahoo Finance for one quote through the choice above (not yet applied "
                                     "choices too)."));
    buttons->addWidget(apply_);
    buttons->addWidget(test_);
    buttons->addStretch();
    vl->addLayout(buttons);

    in_use_ = new QLabel(this);
    in_use_->setWordWrap(true);
    in_use_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(in_use_);
    result_ = new QLabel(this);
    result_->setWordWrap(true);
    result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_->hide();
    vl->addWidget(result_);

    connect(mode_, &QButtonGroup::idClicked, this, [this](int) { update_state(); });
    connect(url_, &QLineEdit::textChanged, this, [this]() { update_state(); });
    connect(apply_, &QPushButton::clicked, this, &NetworkSection::on_apply);
    connect(test_, &QPushButton::clicked, this, &NetworkSection::on_test);
    in_use_->setText(QStringLiteral("In use: ") + proxy::describe(cfg));
    update_state();
}

void NetworkSection::update_state() {
    const bool manual = mode_->checkedId() == 2;
    url_->setEnabled(manual);
    const QString why = manual ? proxy::url_problem(url_->text()) : QString();
    apply_->setEnabled(why.isEmpty());
    test_->setEnabled(why.isEmpty());
    apply_->setToolTip(why);
}

void NetworkSection::on_apply() {
    proxy::Config c{mode_of(mode_->checkedId()), url_->text().trimmed()};
    proxy::save(c);
    in_use_->setText(QStringLiteral("In use: ") + proxy::apply(c));
    // The data service took its proxy at launch: give it the new one.
    python::PythonWorker::instance().restart();
    result_->setText(QStringLiteral("Applied. New data requests use it; scripts already running finish as they "
                                    "started."));
    result_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::POSITIVE()));
    result_->show();
}

void NetworkSection::on_test() {
    proxy::Config c{mode_of(mode_->checkedId()), url_->text().trimmed()};
    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    nam_->setProxyFactory(proxy::make_factory(c)); // takes ownership
    QNetworkRequest req(kTestUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                 "Chrome/124.0 Safari/537.36"));
    req.setTransferTimeout(kTestTimeoutMs);
    test_->setEnabled(false);
    result_->setText(QStringLiteral("Asking Yahoo Finance through %1…").arg(proxy::describe(c)));
    result_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    result_->show();
    auto* timer = new QElapsedTimer;
    timer->start();
    QNetworkReply* reply = nam_->get(req);
    QPointer<NetworkSection> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply, timer]() {
        reply->deleteLater();
        const qint64 ms = timer->elapsed();
        delete timer;
        if (!self)
            return;
        self->update_state();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // The proxy itself answered — asking for credentials (407) or
        // refusing — not Yahoo: the route does not work.
        const auto err = reply->error();
        const bool proxy_refused = status == 407 || (err >= QNetworkReply::ProxyConnectionRefusedError &&
                                                     err <= QNetworkReply::UnknownProxyError);
        // Any other HTTP answer means the route works — this tests the
        // network path (proxy, VPN, DNS, TLS), not Yahoo's mood: a bare
        // request without its cookies is often answered 429 while the data
        // service, which has them, does fine.
        if (status > 0 && !proxy_refused) {
            QString text = QStringLiteral("✓ Yahoo Finance answered in %1 s — the connection works this way.")
                               .arg(ms / 1000.0, 0, 'f', 1);
            if (status == 429)
                text += QStringLiteral(" (It also said it is limiting requests from this address right now — data "
                                       "may load slowly for a few minutes.)");
            self->result_->setText(text);
            self->result_->setStyleSheet(
                QStringLiteral("color:%1;background:transparent;").arg(ui::colors::POSITIVE()));
            return;
        }
        QString raw = reply->errorString();
        if (status == 407 || err == QNetworkReply::ProxyAuthenticationRequiredError)
            raw = QStringLiteral("Proxy authentication required (HTTP 407): ") + raw;
        if (err == QNetworkReply::OperationCanceledError || err == QNetworkReply::TimeoutError)
            raw = QStringLiteral("timed out after %1 s").arg(ms / 1000);
        const auto f = data_failure::classify(raw, QStringLiteral("Yahoo Finance"));
        self->result_->setText(QStringLiteral("✗ %1\n%2\n(%3)").arg(f.reason, f.action, f.raw));
        self->result_->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(ui::colors::WARNING()));
    });
}

} // namespace finterm::screens
