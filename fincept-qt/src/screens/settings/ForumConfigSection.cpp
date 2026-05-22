#include "screens/settings/ForumConfigSection.h"

#include "services/forum/ForumService.h"
#include "storage/repositories/SettingsRepository.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::screens {

ForumConfigSection::ForumConfigSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    load_current();
}

void ForumConfigSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* hdr = new QLabel(QStringLiteral(
        "<b>Forum</b> — point at any compatible forum backend.  Leave "
        "empty to keep the Forum surface disabled (no outbound traffic). "
        " A finterm-hosted forum will be the default URL once it ships."));
    hdr->setWordWrap(true);
    root->addWidget(hdr);

    auto* form = new QFormLayout;
    url_edit_ = new QLineEdit;
    url_edit_->setPlaceholderText(QStringLiteral("https://forum.example.com  (empty = disabled)"));
    form->addRow(QStringLiteral("Base URL"), url_edit_);
    root->addLayout(form);

    auto* btn_row = new QHBoxLayout;
    save_btn_ = new QPushButton(QStringLiteral("Save"));
    clear_btn_ = new QPushButton(QStringLiteral("Clear"));
    connect(save_btn_, &QPushButton::clicked, this, &ForumConfigSection::on_save);
    connect(clear_btn_, &QPushButton::clicked, this, &ForumConfigSection::on_clear);
    btn_row->addWidget(save_btn_);
    btn_row->addWidget(clear_btn_);
    btn_row->addStretch();
    root->addLayout(btn_row);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    root->addWidget(status_lbl_);
    root->addStretch();
}

void ForumConfigSection::load_current() {
    auto r = SettingsRepository::instance().get(
        QString::fromUtf8(services::ForumService::kBaseUrlSettingsKey));
    if (r.is_ok())
        url_edit_->setText(r.value());
}

void ForumConfigSection::on_save() {
    const QString val = url_edit_->text().trimmed();
    if (!val.isEmpty()) {
        // Light sanity check — anything we'd refuse to actually request
        // against rejects here rather than after a confusing 0-byte
        // response.  We don't ping the URL; user might be configuring
        // it offline.
        const QUrl parsed(val);
        if (!parsed.isValid() || parsed.scheme().isEmpty()
            || (parsed.scheme() != QStringLiteral("http")
                && parsed.scheme() != QStringLiteral("https"))) {
            show_status(QStringLiteral("URL must start with http:// or https://"), true);
            return;
        }
    }
    auto r = SettingsRepository::instance().set(
        QString::fromUtf8(services::ForumService::kBaseUrlSettingsKey), val, "forum");
    if (r.is_err()) {
        show_status(QStringLiteral("Failed to save: %1")
                        .arg(QString::fromStdString(r.error())), true);
        return;
    }
    show_status(val.isEmpty()
                    ? QStringLiteral("Cleared — Forum surface disabled.")
                    : QStringLiteral("Saved.  Forum screen will pick up the new URL on next refresh."));
}

void ForumConfigSection::on_clear() {
    url_edit_->clear();
    on_save();
}

void ForumConfigSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
