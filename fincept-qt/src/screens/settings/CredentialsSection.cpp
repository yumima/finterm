#include "screens/settings/CredentialsSection.h"

#include "core/keys/CredentialCatalogue.h"
#include "core/logging/Logger.h"
#include "screens/settings/SettingsStyles.h"
#include "storage/secure/SecureStorage.h"
#include "ui/theme/ThemeManager.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr const char* kTag = "Credentials";

void clear_layout(QVBoxLayout* l) {
    // Detach the cards without deleting them; place_cards() re-adds them.
    while (l->count() > 1) { // keep the section title at index 0
        QLayoutItem* it = l->takeAt(1);
        delete it;
    }
}

/// state: "ok" | "err" | "" — styled by the section stylesheet.
void set_status(QLabel* l, const QString& text, const char* state) {
    l->setText(text);
    l->setProperty("state", QString::fromLatin1(state));
    l->style()->unpolish(l);
    l->style()->polish(l);
}

} // namespace

CredentialsSection::CredentialsSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    apply_theme();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { apply_theme(); });
}

void CredentialsSection::apply_theme() {
    // One sheet keyed on object names, so a theme switch restyles every card.
    setStyleSheet(QString("QScrollArea{border:none;background:transparent;}"
                          "QFrame#credCard{background:%1;border:1px solid %2;border-radius:4px;}"
                          "QLabel#credName{color:%3;font-weight:600;background:transparent;border:none;}"
                          "QLabel#credEnv{color:%4;background:transparent;border:none;}"
                          "QLabel#credStatus{color:%5;background:transparent;border:none;}"
                          "QLabel#credStatus[state=\"ok\"]{color:%6;}"
                          "QLabel#credStatus[state=\"err\"]{color:%7;}")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::TEXT_TERTIARY(), ui::colors::TEXT_SECONDARY(), ui::colors::POSITIVE(),
                           ui::colors::NEGATIVE()) +
                  QString("QLabel#credGroup{%1}QLabel#credTitle{%2}QLabel#credInfo{%3}")
                      .arg(settings_style::sub_title_ss(), settings_style::section_title_ss(), settings_style::label_ss()) +
                  settings_style::input_ss() + settings_style::check_ss());
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        if (it->reveal)
            it->reveal->setStyleSheet(settings_style::btn_secondary_ss());
        it->save->setStyleSheet(settings_style::btn_primary_ss());
        it->remove->setStyleSheet(settings_style::btn_danger_ss());
    }
}

void CredentialsSection::build_ui() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll);

    auto* page = new QWidget(this);
    page->setObjectName("credPage"); // scoped: a bare rule would also blank every card
    page->setStyleSheet("QWidget#credPage{background:transparent;}");
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(24, 24, 24, 24);
    vl->setSpacing(0);

    auto* title = new QLabel(tr("API KEYS"));
    title->setObjectName("credTitle");
    vl->addWidget(title);
    vl->addSpacing(8);

    info_ = new QLabel;
    info_->setObjectName("credInfo");
    info_->setTextFormat(Qt::RichText);
    info_->setWordWrap(true);
    vl->addWidget(info_);
    vl->addSpacing(14);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Filter — e.g. \"fred\", \"crypto\", \"EIA_API_KEY\""));
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, [this]() { apply_filter(); });
    bar->addWidget(filter_, 1);
    only_configured_ = new QCheckBox(tr("Only my keys"));
    connect(only_configured_, &QCheckBox::toggled, this, [this]() { apply_filter(); });
    bar->addWidget(only_configured_);
    vl->addLayout(bar);
    vl->addSpacing(8);
    summary_ = new QLabel;
    summary_->setObjectName("credStatus");
    vl->addWidget(summary_);
    vl->addSpacing(14);

    auto make_group = [&](const QString& name) {
        auto* box = new QWidget(this);
        auto* bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 14);
        bl->setSpacing(8);
        auto* h = new QLabel(name.toUpper());
        h->setObjectName("credGroup");
        bl->addWidget(h);
        vl->addWidget(box);
        return std::make_pair(box, bl);
    };
    std::tie(mine_box_, mine_layout_) = make_group(tr("Your keys"));
    for (const QString& g : keys::groups()) {
        auto [box, bl] = make_group(g);
        group_boxes_.insert(g, box);
        group_layouts_.insert(g, bl);
    }

    for (const auto& def : keys::catalogue()) {
        const QString key = QString::fromLatin1(def.env_name);
        Card c;
        c.group = QString::fromLatin1(def.group);
        c.label = QString::fromUtf8(def.label);
        c.secret = def.secret;
        c.frame = build_card(key, c);
        cards_.insert(key, c);
        order_.append(key);
    }
    place_cards(); // every card gets a home now; reload() re-sorts by state

    vl->addStretch();
    scroll->setWidget(page);
}

QWidget* CredentialsSection::build_card(const QString& key, Card& c) {
    auto* card = new QFrame(this);
    card->setObjectName("credCard");
    auto* cvl = new QVBoxLayout(card);
    cvl->setContentsMargins(12, 8, 12, 10);
    cvl->setSpacing(6);

    auto* head = new QHBoxLayout;
    auto* name = new QLabel(c.label);
    name->setObjectName("credName");
    head->addWidget(name);
    // The variable name tells apart cards with the same provider label
    // (an account's key vs its secret) and is what docs and errors cite.
    auto* env = new QLabel(key);
    env->setObjectName("credEnv");
    env->setTextInteractionFlags(Qt::TextSelectableByMouse);
    head->addWidget(env);
    head->addStretch();
    c.status = new QLabel;
    c.status->setObjectName("credStatus");
    head->addWidget(c.status);
    cvl->addLayout(head);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    c.field = new QLineEdit;
    if (c.secret)
        c.field->setEchoMode(QLineEdit::Password);
    row->addWidget(c.field, 1);
    if (c.secret) {
        c.reveal = new QPushButton(tr("Show"));
        connect(c.reveal, &QPushButton::clicked, this, [this, key]() { toggle_reveal(key); });
        row->addWidget(c.reveal);
    }
    c.save = new QPushButton(tr("Save"));
    connect(c.save, &QPushButton::clicked, this, [this, key]() { save_key(key); });
    connect(c.field, &QLineEdit::returnPressed, this, [this, key]() { save_key(key); });
    row->addWidget(c.save);
    c.remove = new QPushButton(tr("Remove"));
    connect(c.remove, &QPushButton::clicked, this, [this, key]() { remove_key(key); });
    row->addWidget(c.remove);
    cvl->addLayout(row);
    return card;
}

void CredentialsSection::reload() {
    const QString backend = SecureStorage::backend_name();
    const bool obfuscated = backend.contains(QLatin1String("NOT SECURE"));
    info_->setText(
        tr("Keys and identity settings for the data services finterm can use, kept in <b>%1</b> on this "
           "computer — never synced, never committed. finterm passes them to its own data scripts, never "
           "to other programs.%2 Keys for AI providers live under <b>AI Config</b>, the speech key under "
           "<b>Voice</b>, alert channels under <b>Notifications</b>; <b>All Credentials</b> lists everything "
           "stored.")
            .arg(backend.toHtmlEscaped(),
                 obfuscated ? tr(" <b>This build has no OS keyring support, so they are only obfuscated in a "
                                 "local file.</b>")
                            : QString()));

    auto& store = SecureStorage::instance();
    QSet<QString> configured;
    bool read_failed = false;
    if (SecureStorage::can_enumerate()) {
        // One keyring search instead of ~150 lookups.
        auto listed = store.list_keys();
        if (listed.is_ok()) {
            for (const QString& k : listed.value())
                if (cards_.contains(k))
                    configured.insert(k);
        } else {
            read_failed = true;
        }
    } else {
        for (const QString& k : order_) {
            auto r = store.retrieve(k);
            if (r.is_ok() && !r.value().isEmpty())
                configured.insert(k);
            else if (r.is_err() && !SecureStorage::is_not_found(r))
                read_failed = true;
        }
    }
    if (read_failed) {
        // Never present "could not read" as "nothing configured". Cards keep
        // the last state that WAS read; before any successful read they are
        // locked, since Show/Remove/Save would act on a state nobody knows.
        read_ok_ = false;
        set_status(summary_, tr("Could not read the keyring — is it unlocked? Your keys are unchanged."), "err");
        if (!loaded_)
            for (auto it = cards_.begin(); it != cards_.end(); ++it)
                it->frame->setEnabled(false);
        return;
    }
    read_ok_ = true;
    loaded_ = true;
    for (auto it = cards_.begin(); it != cards_.end(); ++it)
        it->frame->setEnabled(true);
    configured_ = configured;
    for (const QString& k : order_)
        set_card_state(k, configured_.contains(k));
    place_cards();
}

void CredentialsSection::set_card_state(const QString& key, bool configured) {
    Card& c = cards_[key];
    c.remove->setVisible(configured);
    if (c.reveal) {
        c.reveal->setVisible(configured);
        c.reveal->setText(tr("Show"));
    }
    // A value the user typed but has not saved survives a reload (the page
    // reloads every time it is opened); a fetched or stale value does not.
    const bool typed = c.field->isModified() && !c.field->text().isEmpty() &&
                       !c.field->property("revealed").toBool();
    if (!typed) {
        c.field->clear();
        if (c.secret)
            c.field->setEchoMode(QLineEdit::Password);
    }
    c.field->setProperty("revealed", false);
    if (configured) {
        set_status(c.status, tr("Saved ✓"), "ok");
        if (c.secret) {
            c.field->setPlaceholderText(tr("•••••••• (saved — Show to view, or type a new value)"));
        } else {
            // Identifiers and preferences are not secret: show them.
            auto r = SecureStorage::instance().retrieve(key);
            if (r.is_ok()) {
                if (!typed) {
                    c.field->setText(r.value());
                    c.field->setModified(false);
                }
            } else {
                set_status(c.status, tr("Saved, but could not read — is the keyring unlocked?"), "err");
            }
        }
    } else {
        set_status(c.status, tr("Not set"), "");
        c.field->setPlaceholderText(tr("Not configured"));
    }
}

void CredentialsSection::place_cards() {
    clear_layout(mine_layout_);
    for (auto* l : group_layouts_)
        clear_layout(l);
    for (const QString& k : order_) {
        const Card& c = cards_[k];
        QVBoxLayout* target = configured_.contains(k)
                                  ? mine_layout_
                                  : group_layouts_.value(c.group, group_layouts_.value(keys::groups().last()));
        target->addWidget(c.frame);
    }
    if (read_ok_)
        set_status(summary_, tr("%1 of %2 configured").arg(configured_.size()).arg(order_.size()), "");
    apply_filter();
}

void CredentialsSection::apply_filter() {
    const QString needle = filter_->text().trimmed();
    const bool only_mine = only_configured_->isChecked();
    QHash<QWidget*, int> visible_in_box;
    for (const QString& k : order_) {
        const Card& c = cards_[k];
        const bool mine = configured_.contains(k);
        bool show = !only_mine || mine;
        if (show && !needle.isEmpty())
            show = k.contains(needle, Qt::CaseInsensitive) || c.label.contains(needle, Qt::CaseInsensitive) ||
                   c.group.contains(needle, Qt::CaseInsensitive);
        c.frame->setVisible(show);
        if (show)
            ++visible_in_box[mine ? mine_box_ : group_boxes_.value(c.group)];
    }
    // Hide a group whose every card is filtered out, title included.
    mine_box_->setVisible(visible_in_box.value(mine_box_) > 0);
    for (auto* box : group_boxes_)
        box->setVisible(visible_in_box.value(box) > 0);
}

void CredentialsSection::save_key(const QString& key) {
    Card& c = cards_[key];
    const QString val = c.field->text().trimmed();
    if (val.isEmpty())
        return; // Remove is the explicit way to clear a key
    auto r = SecureStorage::instance().store(key, val);
    if (r.is_err()) {
        LOG_ERROR(kTag, "Failed to store " + key);
        set_status(c.status, tr("Save failed — is the keyring unlocked?"), "err");
        return;
    }
    LOG_INFO(kTag, "Stored key: " + key);
    c.field->setModified(false);
    configured_.insert(key);
    set_card_state(key, true);
    place_cards();
}

void CredentialsSection::remove_key(const QString& key) {
    const Card& c = cards_[key];
    if (QMessageBox::question(this, tr("Remove key"),
                              tr("Remove %1 (%2) from this computer's keyring?").arg(c.label, key),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if (SecureStorage::instance().remove(key).is_err()) {
        QMessageBox::warning(this, tr("Remove key"), tr("The keyring refused — is it unlocked?"));
        return;
    }
    LOG_INFO(kTag, "Cleared key: " + key);
    configured_.remove(key);
    set_card_state(key, false);
    place_cards();
}

void CredentialsSection::toggle_reveal(const QString& key) {
    Card& c = cards_[key];
    if (c.field->echoMode() == QLineEdit::Normal) {
        c.field->setEchoMode(QLineEdit::Password);
        c.reveal->setText(tr("Show"));
        // Only the stored value we fetched is cleared again; anything the
        // user typed stays for them to Save.
        if (c.field->property("revealed").toBool() && !c.field->isModified())
            c.field->clear();
        c.field->setProperty("revealed", false);
        return;
    }
    if (c.field->text().isEmpty()) {
        auto r = SecureStorage::instance().retrieve(key);
        if (r.is_err()) {
            set_status(c.status, tr("Could not read — is the keyring unlocked?"), "err");
            return;
        }
        c.field->setText(r.value());
        c.field->setModified(false);
        c.field->setProperty("revealed", true);
    }
    // A value already typed (not yet saved) is just unmasked, never replaced.
    c.field->setEchoMode(QLineEdit::Normal);
    c.reveal->setText(tr("Hide"));
}

} // namespace fincept::screens
