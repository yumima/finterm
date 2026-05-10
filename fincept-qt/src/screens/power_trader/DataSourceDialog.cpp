// src/screens/power_trader/DataSourceDialog.cpp
#include "screens/power_trader/DataSourceDialog.h"

#include "storage/secure/SecureStorage.h"
#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::power_trader {

// SecureStorage key — matches the entry in PythonRunner::kManagedCredentialKeys,
// so PythonRunner auto-injects it as CONGRESS_GOV_API_KEY env var into every
// Python subprocess. That makes the key available to senate_disclosures_data.py
// (Power Trader) and congress_gov_data.py (GovData "US Congress" provider)
// without any per-call plumbing.
static constexpr const char* kSecureKey = "CONGRESS_GOV_API_KEY";
static constexpr const char* kSignupUrl = "https://api.congress.gov/sign-up/";

// ── Static helpers ────────────────────────────────────────────────────────────

QString DataSourceDialog::stored_key() {
    auto r = SecureStorage::instance().retrieve(QString::fromLatin1(kSecureKey));
    return r.is_ok() ? r.value() : QString();
}

void DataSourceDialog::save_key(const QString& key) {
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        SecureStorage::instance().remove(QString::fromLatin1(kSecureKey));
    } else {
        SecureStorage::instance().store(QString::fromLatin1(kSecureKey), trimmed);
    }
}

bool DataSourceDialog::has_key() {
    return !stored_key().isEmpty();
}

// ── Constructor ───────────────────────────────────────────────────────────────

DataSourceDialog::DataSourceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Connect Congress.gov API");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(560);
    build_ui();
}

void DataSourceDialog::build_ui() {
    setStyleSheet(QString("QDialog{background:%1;color:%2;}"
                          "QLabel{background:transparent;}"
                          "QLineEdit{background:%3;color:%2;border:1px solid %4;"
                          "  border-radius:3px;padding:6px 10px;font-size:13px;}"
                          "QLineEdit:focus{border-color:%5;}"
                          "QPushButton{border-radius:3px;padding:7px 16px;"
                          "  font-size:13px;font-weight:600;}"
                          "QPushButton:hover{opacity:0.9;}")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::BG_BASE(),    ui::colors::BORDER_MED(),
                           ui::colors::AMBER()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* title = new QLabel("Connect Congress.gov API");
    title->setStyleSheet(
        QString("color:%1;font-size:16px;font-weight:700;letter-spacing:0.5px;")
            .arg(ui::colors::AMBER()));
    root->addWidget(title);

    auto* subtitle = new QLabel(
        "Unlocks the live member roster, real committee assignments, photos, "
        "voting records, sponsored legislation and richer signal scoring in "
        "Power Trader. The key is free and yours alone.");
    subtitle->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    // ── What you get card ────────────────────────────────────────────────────
    auto* card = new QWidget;
    card->setStyleSheet(
        QString("QWidget{background:rgba(245,158,11,0.07);"
                "border:1px solid rgba(245,158,11,0.25);border-radius:4px;}"));
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(14, 12, 14, 12);
    cl->setSpacing(4);
    auto* card_hdr = new QLabel("With a key — Power Trader uses");
    card_hdr->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;").arg(ui::colors::AMBER()));
    cl->addWidget(card_hdr);
    auto* card_body = new QLabel(
        "• Congress.gov  · 119th Congress live roster + member photos + bioguide IDs\n"
        "• Senate eFD    · efdsearch.senate.gov full PTR scrape (per-transaction)\n"
        "• House FDS     · disclosures-clerk.house.gov annual filing index\n"
        "• unitedstates.io · real committee assignments (canonical structured source)");
    card_body->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    card_body->setWordWrap(false);
    cl->addWidget(card_body);
    root->addWidget(card);

    // ── Signup link ──────────────────────────────────────────────────────────
    auto* signup_btn = new QPushButton("Get a free Congress.gov key  →  api.congress.gov/sign-up");
    signup_btn->setCursor(Qt::PointingHandCursor);
    signup_btn->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "text-align:left;padding:8px 12px;}"
                "QPushButton:hover{border-color:%1;}")
            .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM()));
    connect(signup_btn, &QPushButton::clicked, this, &DataSourceDialog::on_open_signup);
    root->addWidget(signup_btn);

    // ── Key input ────────────────────────────────────────────────────────────
    auto* input_row = new QHBoxLayout;
    key_input_ = new QLineEdit;
    key_input_->setPlaceholderText("Paste your Congress.gov API key here…");
    key_input_->setEchoMode(QLineEdit::Password);
    const QString existing = stored_key();
    if (!existing.isEmpty())
        key_input_->setText(existing);
    input_row->addWidget(key_input_, 1);

    auto* toggle_btn = new QPushButton("Show");
    toggle_btn->setFixedWidth(56);
    toggle_btn->setStyleSheet(
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY(),
                 ui::colors::BORDER_MED()));
    toggle_btn->setCursor(Qt::PointingHandCursor);
    connect(toggle_btn, &QPushButton::clicked, this, [this, toggle_btn]() {
        const bool showing = key_input_->echoMode() == QLineEdit::Normal;
        key_input_->setEchoMode(showing ? QLineEdit::Password : QLineEdit::Normal);
        toggle_btn->setText(showing ? "Show" : "Hide");
    });
    input_row->addWidget(toggle_btn);
    root->addLayout(input_row);

    status_lbl_ = new QLabel;
    status_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    status_lbl_->setWordWrap(true);
    status_lbl_->setVisible(false);
    root->addWidget(status_lbl_);

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto* btn_row = new QHBoxLayout;
    btn_row->setSpacing(8);

    skip_btn_ = new QPushButton("Skip — use fallback data");
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

// ── Slots ─────────────────────────────────────────────────────────────────────

void DataSourceDialog::on_save() {
    const QString key = key_input_->text().trimmed();
    if (key.isEmpty()) {
        status_lbl_->setText("Please paste a key, or click 'Skip — use fallback data'.");
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;").arg(ui::colors::NEGATIVE()));
        status_lbl_->setVisible(true);
        return;
    }
    if (key.length() < 20) {
        status_lbl_->setText("That key looks too short — Congress.gov keys are ~40 chars.");
        status_lbl_->setStyleSheet(
            QString("color:%1;font-size:12px;").arg(ui::colors::WARNING()));
        status_lbl_->setVisible(true);
        return;
    }
    save_key(key);
    accept();
}

void DataSourceDialog::on_open_signup() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(kSignupUrl)));
}

} // namespace fincept::power_trader
