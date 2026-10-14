#include "screens/settings/StoredCredentialsSection.h"

#include "auth/AuthManager.h"
#include "core/keys/CredentialCatalogue.h"
#include "core/logging/Logger.h"
#include "screens/settings/SettingsStyles.h"
#include "storage/secure/SecureStorage.h"
#include "ui/theme/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

struct Category {
    QString title;
    QString edited_in; ///< where the user manages these
    /// Only entries that ARE the whole record (a data-provider API key) are
    /// removable here. Alert-channel secrets are tracked by the notification
    /// store's index, so they are cleared in Settings › Notifications. Broker, exchange, prediction-market, MCP,
    /// AI-provider and voice entries belong to screens that keep related
    /// records; removing just the secret would leave those half-configured.
    bool removable;
};

/// Which group a keyring entry belongs to. Order of the checks matters.
Category categorize(const QString& key, const QString& me_prefix, const QSet<QString>& catalogue) {
    if (catalogue.contains(key))
        return {QStringLiteral("Data-provider API keys"), QStringLiteral("Settings › API Keys"), true};
    if (!me_prefix.isEmpty() && key.startsWith(me_prefix + QStringLiteral("notif_")))
        return {QStringLiteral("Alert channels (yours)"), QStringLiteral("Settings › Notifications"), false};
    if (key.startsWith(QLatin1String("users/")))
        return {QStringLiteral("Other local users' alert channels"), QStringLiteral("that user's Settings"), false};
    if (key.startsWith(QLatin1String("account.")) || key.startsWith(QLatin1String("broker.")))
        return {QStringLiteral("Broker accounts"), QStringLiteral("Equity Trading › Accounts"), false};
    if (key.startsWith(QLatin1String("crypto:")))
        return {QStringLiteral("Crypto exchanges"), QStringLiteral("Crypto Trading › API keys"), false};
    if (key.startsWith(QLatin1String("prediction/")))
        return {QStringLiteral("Prediction markets"), QStringLiteral("Polymarket › Account"), false};
    if (key.startsWith(QLatin1String("mcp.")))
        return {QStringLiteral("MCP server sign-ins"), QStringLiteral("Settings › AI Config › Tools"), false};
    if (key.startsWith(QLatin1String("llm.")))
        return {QStringLiteral("AI provider keys"), QStringLiteral("Settings › AI Config › Models"), false};
    if (key.startsWith(QLatin1String("voice.")))
        return {QStringLiteral("Voice"), QStringLiteral("Settings › Voice"), false};
    if (key == QLatin1String("api_key") || key.startsWith(QLatin1String("pin_")))
        return {QStringLiteral("Sign-in & device lock"), QStringLiteral("managed by sign-in and Settings › Security"),
                false};
    return {QStringLiteral("Other"), QString(), false};
}

} // namespace

StoredCredentialsSection::StoredCredentialsSection(QWidget* parent) : QWidget(parent) {
    // Rows are built with the current theme's colours; rebuild on a switch.
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens&) {
        if (isVisible())
            reload();
    });
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");
    outer->addWidget(scroll);

    auto* page = new QWidget(this);
    page->setStyleSheet("background:transparent;");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(24, 24, 24, 24);
    vl->setSpacing(0);

    title_ = new QLabel(tr("ALL CREDENTIALS"));
    vl->addWidget(title_);
    vl->addSpacing(8);
    info_ = new QLabel(tr("Everything finterm keeps in your OS keyring for this profile, in one place. "
                               "Values are never shown here — each group names the screen that edits it. "
                               "Nothing listed here leaves this computer."));
    info_->setWordWrap(true);
    vl->addWidget(info_);
    vl->addSpacing(10);
    summary_ = new QLabel;
    vl->addWidget(summary_);
    vl->addSpacing(14);

    list_ = new QWidget(this);
    list_layout_ = new QVBoxLayout(list_);
    list_layout_->setContentsMargins(0, 0, 0, 0);
    list_layout_->setSpacing(14);
    vl->addWidget(list_);
    vl->addStretch();
    scroll->setWidget(page);
}

void StoredCredentialsSection::reload() {
    title_->setStyleSheet(settings_style::section_title_ss());
    info_->setStyleSheet(settings_style::label_ss());
    summary_->setStyleSheet(settings_style::label_ss());
    while (QLayoutItem* it = list_layout_->takeAt(0)) {
        delete it->widget();
        delete it;
    }

    if (!SecureStorage::can_enumerate()) {
        summary_->setText(tr("This system's keyring (%1) cannot list its entries, so credentials are "
                             "managed on each feature's own screen.")
                              .arg(SecureStorage::backend_name()));
        return;
    }

    QSet<QString> catalogue;
    for (const auto& def : keys::catalogue())
        catalogue.insert(QString::fromLatin1(def.env_name));
    const QString me = auth::AuthManager::instance().session().user_info.username.toLower();
    const QString me_prefix = me.isEmpty() ? QString{} : QStringLiteral("users/") + me + QLatin1Char('/');

    QMap<QString, std::pair<Category, QStringList>> groups; // ordered by title
    int total = 0;
    auto listed = SecureStorage::instance().list_keys();
    if (listed.is_err()) {
        // Not the same as "nothing stored" — say so.
        summary_->setText(tr("Could not read the keyring — is it unlocked? Nothing has been changed."));
        return;
    }
    for (const QString& key : listed.value()) {
        if (key.endsWith(QLatin1String("notif_index")))
            continue; // bookkeeping, not a credential
        const Category cat = categorize(key, me_prefix, catalogue);
        auto& g = groups[cat.title];
        g.first = cat;
        g.second.append(key);
        ++total;
    }
    summary_->setText(tr("%1 credentials stored in %2 groups.").arg(total).arg(groups.size()));

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Category& cat = it.value().first;
        auto* box = new QFrame;
        box->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:4px;}")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* bl = new QVBoxLayout(box);
        bl->setContentsMargins(12, 10, 12, 10);
        bl->setSpacing(6);
        auto* head = new QLabel(QStringLiteral("%1  ·  %2").arg(cat.title.toUpper()).arg(it.value().second.size()));
        head->setStyleSheet(settings_style::sub_title_ss() + "border:none;");
        bl->addWidget(head);
        if (!cat.edited_in.isEmpty()) {
            auto* where = new QLabel(tr("Edited in %1").arg(cat.edited_in));
            where->setStyleSheet(settings_style::label_ss() + "border:none;");
            bl->addWidget(where);
        }
        for (const QString& key : it.value().second) {
            auto* row = new QHBoxLayout;
            // Show the user-facing part of a per-user key only.
            const QString shown = (!me_prefix.isEmpty() && key.startsWith(me_prefix)) ? key.mid(me_prefix.size()) : key;
            auto* name = new QLabel(shown);
            name->setTextInteractionFlags(Qt::TextSelectableByMouse);
            name->setStyleSheet(QString("color:%1;background:transparent;border:none;").arg(ui::colors::TEXT_PRIMARY()));
            row->addWidget(name, 1);
            if (cat.removable) {
                auto* rm = new QPushButton(tr("Remove"));
                rm->setStyleSheet(settings_style::btn_danger_ss());
                connect(rm, &QPushButton::clicked, this, [this, key]() {
                    if (QMessageBox::question(this, tr("Remove credential"),
                                              tr("Remove \"%1\" from this computer's keyring?").arg(key),
                                              QMessageBox::Yes | QMessageBox::Cancel,
                                              QMessageBox::Cancel) != QMessageBox::Yes)
                        return;
                    if (SecureStorage::instance().remove(key).is_err()) {
                        QMessageBox::warning(this, tr("Remove credential"), tr("The keyring refused — is it unlocked?"));
                        return;
                    }
                    LOG_INFO("Credentials", "Removed stored credential: " + key);
                    // Queued: reload() deletes this very button.
                    QMetaObject::invokeMethod(this, [this]() { reload(); }, Qt::QueuedConnection);
                });
                row->addWidget(rm);
            }
            bl->addLayout(row);
        }
        list_layout_->addWidget(box);
    }
}

} // namespace fincept::screens
