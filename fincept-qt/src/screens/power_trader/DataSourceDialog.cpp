// src/screens/power_trader/DataSourceDialog.cpp
#include "screens/power_trader/DataSourceDialog.h"

#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::power_trader {

static constexpr const char* kSettingsKey = "finnhub_api_key";
static constexpr const char* kFinnhubSignup = "https://finnhub.io/register";

// ── Static helpers ─────────────────────────────────────────────────────────────

QString DataSourceDialog::stored_key() {
    QSettings s("Fincept", "Terminal");
    return s.value(kSettingsKey, QString()).toString();
}

void DataSourceDialog::save_key(const QString& key) {
    QSettings s("Fincept", "Terminal");
    s.setValue(kSettingsKey, key.trimmed());
}

bool DataSourceDialog::has_key() {
    return !stored_key().isEmpty();
}

// ── Constructor ────────────────────────────────────────────────────────────────

DataSourceDialog::DataSourceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Connect Power Trader Data");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(520);
    build_ui();
}

void DataSourceDialog::build_ui() {
    setStyleSheet(QString("QDialog{background:%1;color:%2;}"
                          "QLabel{background:transparent;}"
                          "QLineEdit{background:%3;color:%2;border:1px solid %4;"
                          "  border-radius:3px;padding:6px 10px;font-size:12px;}"
                          "QLineEdit:focus{border-color:%5;}"
                          "QPushButton{border-radius:3px;padding:7px 16px;"
                          "  font-size:12px;font-weight:600;}"
                          "QPushButton:hover{opacity:0.9;}")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::BG_BASE(), ui::colors::BORDER_MED(),
                           ui::colors::AMBER()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* title = new QLabel("Connect Live Congressional Trade Data");
    title->setStyleSheet(
        QString("color:%1;font-size:15px;font-weight:700;letter-spacing:0.5px;")
            .arg(ui::colors::AMBER()));
    root->addWidget(title);

    // ── Free sources note ─────────────────────────────────────────────────────
    auto* note_card = new QWidget;
    note_card->setStyleSheet(
        QString("QWidget{background:rgba(22,163,74,0.08);border:1px solid rgba(22,163,74,0.3);"
                "border-radius:4px;}"));
    auto* nl = new QVBoxLayout(note_card);
    nl->setContentsMargins(12, 10, 12, 10);
    nl->setSpacing(4);
    auto* note_hdr = new QLabel("✓  Free sources active (no key required)");
    note_hdr->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;").arg(ui::colors::POSITIVE()));
    nl->addWidget(note_hdr);
    auto* note_body = new QLabel(
        "Senate eFTS (efts.senate.gov)  ·  House FDS (disclosures-clerk.house.gov)\n"
        "These are tried first on every refresh. They work when the government sites are reachable.");
    note_body->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    note_body->setWordWrap(true);
    nl->addWidget(note_body);
    root->addWidget(note_card);

    // ── Finnhub section ───────────────────────────────────────────────────────
    auto* divider = new QLabel("Finnhub fallback  ·  used when government sites are unreachable");
    divider->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;padding-top:4px;")
            .arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(divider);

    auto* desc = new QLabel(
        "Finnhub queries the top 30 most-traded congressional tickers in real time. "
        "Free tier: 60 requests/min, no credit card required.");
    desc->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    desc->setWordWrap(true);
    root->addWidget(desc);

    // Link button
    auto* link_btn = new QPushButton("Get free Finnhub API key  →  finnhub.io/register");
    link_btn->setCursor(Qt::PointingHandCursor);
    link_btn->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "text-decoration:none;text-align:left;}"
                "QPushButton:hover{border-color:%1;}")
            .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM()));
    connect(link_btn, &QPushButton::clicked, this, &DataSourceDialog::on_open_link);
    root->addWidget(link_btn);

    // Key input
    auto* input_row = new QHBoxLayout;
    key_input_ = new QLineEdit;
    key_input_->setPlaceholderText("Paste your Finnhub API key here…");
    key_input_->setEchoMode(QLineEdit::Password);
    // Pre-fill if already stored
    const QString existing = stored_key();
    if (!existing.isEmpty())
        key_input_->setText(existing);
    input_row->addWidget(key_input_, 1);

    auto* toggle_btn = new QPushButton("Show");
    toggle_btn->setFixedWidth(56);
    toggle_btn->setStyleSheet(
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED()));
    toggle_btn->setCursor(Qt::PointingHandCursor);
    connect(toggle_btn, &QPushButton::clicked, this, [this, toggle_btn]() {
        const bool showing = key_input_->echoMode() == QLineEdit::Normal;
        key_input_->setEchoMode(showing ? QLineEdit::Password : QLineEdit::Normal);
        toggle_btn->setText(showing ? "Show" : "Hide");
    });
    input_row->addWidget(toggle_btn);
    root->addLayout(input_row);

    // Status label
    status_lbl_ = new QLabel;
    status_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    status_lbl_->setWordWrap(true);
    status_lbl_->setVisible(false);
    root->addWidget(status_lbl_);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(8);

    skip_btn_ = new QPushButton("Try without key");
    skip_btn_->setCursor(Qt::PointingHandCursor);
    skip_btn_->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    connect(skip_btn_, &QPushButton::clicked, this, &QDialog::reject);
    btn_row->addWidget(skip_btn_);
    btn_row->addStretch();

    save_btn_ = new QPushButton("Save key & load data");
    save_btn_->setCursor(Qt::PointingHandCursor);
    save_btn_->setStyleSheet(
        QString("QPushButton{background:%1;color:%2;border:none;}"
                "QPushButton:hover{background:%3;}")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE(), ui::colors::AMBER_DIM()));
    save_btn_->setDefault(true);
    connect(save_btn_, &QPushButton::clicked, this, &DataSourceDialog::on_save);
    btn_row->addWidget(save_btn_);

    root->addLayout(btn_row);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void DataSourceDialog::on_save() {
    const QString key = key_input_->text().trimmed();
    if (key.isEmpty()) {
        status_lbl_->setText("Please enter a key, or click 'Try without key'.");
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;").arg(ui::colors::NEGATIVE()));
        status_lbl_->setVisible(true);
        return;
    }
    if (key.length() < 10) {
        status_lbl_->setText("Key looks too short — please check and paste again.");
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;").arg(ui::colors::WARNING()));
        status_lbl_->setVisible(true);
        return;
    }
    save_key(key);
    accept();
}

void DataSourceDialog::on_open_link() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kFinnhubSignup)));
}

} // namespace fincept::power_trader
