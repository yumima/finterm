// AiRolesSection.cpp — bind each AI role to one of the configured providers.

#include "screens/settings/AiRolesSection.h"

#include "ai_chat/AiRoles.h"
#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "storage/repositories/LlmConfigRepository.h"
#include "storage/repositories/LlmProfileRepository.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QScrollArea>
#include <QUuid>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {
constexpr const char* TAG = "AiRolesSection";
constexpr const char* kUnbound = "";

// Combo userData packs provider+model. A bare model id is ambiguous the moment
// two providers expose the same name — hearth fronts Gemini, so
// "gemini-3.5-flash" is reachable through BOTH "ollama" and "gemini" here.
const QChar kSep = QChar(0x1f);
inline QString pack(const QString& provider, const QString& model) { return provider + kSep + model; }
inline QPair<QString, QString> unpack(const QString& data) {
    const int i = data.indexOf(kSep);
    return i < 0 ? qMakePair(QString(), QString()) : qMakePair(data.left(i), data.mid(i + 1));
}
} // namespace

AiRolesSection::AiRolesSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    reload();
}

void AiRolesSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* title = new QLabel("Roles");
    title->setStyleSheet(
        QString("color:%1;font-size:15px;font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    root->addWidget(title);

    auto* blurb = new QLabel(
        "Each role is a place in finterm that asks a model for something. Bind a role to send it "
        "to a different provider — a fast local one for news briefs and inline completion, a "
        "stronger cloud one for research. Providers run independently, so roles can point at "
        "different ones at the same time. Roles left on “Use default” follow the Chat role.");
    blurb->setWordWrap(true);
    blurb->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_DIM()));
    root->addWidget(blurb);

    provider_note_ = new QLabel();
    provider_note_->setWordWrap(true);
    provider_note_->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_DIM()));
    root->addWidget(provider_note_);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget();
    auto* grid = new QGridLayout(holder);
    grid->setContentsMargins(0, 6, 0, 6);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);

    int row = 0;
    for (const auto& role : ai_chat::ai_roles()) {
        auto* label = new QLabel(role.label);
        label->setStyleSheet(
            QString("color:%1;font-size:12px;font-weight:600;").arg(ui::colors::TEXT_PRIMARY()));
        grid->addWidget(label, row, 0, Qt::AlignTop | Qt::AlignLeft);

        auto* right = new QVBoxLayout();
        right->setSpacing(2);
        auto* combo = new QComboBox();
        combo->setMinimumWidth(300);
        combo->setToolTip(role.hint);
        const QString role_id = role.id;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, role_id](int) {
            if (loading_)
                return;
            const auto pm = unpack(combo->currentData().toString());
            apply_binding(role_id, pm.first, pm.second);
        });
        role_combos_.insert(role_id, combo);
        right->addWidget(combo);

        auto* hint = new QLabel(role.hint);
        hint->setWordWrap(true);
        hint->setStyleSheet(QString("color:%1;font-size:10px;").arg(ui::colors::TEXT_DIM()));
        right->addWidget(hint);

        grid->addLayout(right, row, 1);
        ++row;
    }
    grid->setRowStretch(row, 1);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    status_ = new QLabel();
    status_->setStyleSheet(QString("color:%1;font-size:11px;").arg(ui::colors::TEXT_DIM()));
    root->addWidget(status_);
}

void AiRolesSection::reload() {
    configured_.clear();
    default_provider_.clear();

    auto providers = LlmConfigRepository::instance().list_providers();
    if (providers.is_ok()) {
        for (const auto& c : providers.value()) {
            if (c.model.isEmpty())
                continue;  // a provider with no model chosen isn't bindable yet
            configured_.append({c.provider, c.model});
            if (c.is_active)
                default_provider_ = c.provider;
        }
    }

    provider_note_->setText(
        configured_.isEmpty()
            ? QString("No providers configured — add one on the Models tab first.")
            : QString("Choices come from the Models tab (%1 configured). To use a model that "
                      "isn't listed, add or edit it there.")
                  .arg(configured_.size()));

    auto& repo = LlmProfileRepository::instance();
    loading_ = true;
    for (auto it = role_combos_.constBegin(); it != role_combos_.constEnd(); ++it) {
        // get_assignment (not resolve_for_context): show what the user
        // explicitly bound, not what the fallback chain would produce —
        // otherwise every unbound role renders as if it were bound.
        const QString assigned = repo.get_assignment(it.key(), QString());
        QString model, provider;
        if (!assigned.isEmpty()) {
            auto p = repo.get_profile(assigned);
            if (p.is_ok()) {
                model = p.value().model_id;
                provider = p.value().provider;
            }
        }
        populate_combo(it.value(), provider, model);
    }
    loading_ = false;
}

void AiRolesSection::populate_combo(QComboBox* combo, const QString& provider,
                                    const QString& model) {
    combo->clear();

    // Name the default explicitly so "Use default" is never a guess.
    QString default_label = "Use default";
    for (const auto& c : configured_) {
        if (c.first == default_provider_) {
            default_label = "Use default  (" + c.first + "  ·  " + c.second + ")";
            break;
        }
    }
    combo->addItem(default_label, QString(kUnbound));

    bool have_selected = false;
    for (const auto& c : configured_) {
        combo->addItem(c.first + "  ·  " + c.second, pack(c.first, c.second));
        if (c.first == provider && c.second == model)
            have_selected = true;
    }

    // A binding whose provider/model is no longer configured must stay
    // selectable, or simply opening Settings would silently rewrite it.
    if (!model.isEmpty() && !have_selected)
        combo->addItem(provider + "  ·  " + model + "   (no longer configured)",
                       pack(provider, model));

    const int idx = model.isEmpty() ? 0 : combo->findData(pack(provider, model));
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString AiRolesSection::profile_for_model(const QString& provider, const QString& model_id) {
    auto& repo = LlmProfileRepository::instance();

    auto existing = repo.list_profiles();
    if (existing.is_ok()) {
        for (const auto& p : existing.value()) {
            if (p.model_id == model_id && p.provider.compare(provider, Qt::CaseInsensitive) == 0)
                return p.id;
        }
    }

    // Mint one, carrying THAT provider's credentials — not the default's.
    // Binding news to a hearth model while Gemini is the default must not
    // staple the Gemini key onto a loopback profile.
    QString key, base_url;
    auto providers = LlmConfigRepository::instance().list_providers();
    if (providers.is_ok()) {
        for (const auto& c : providers.value()) {
            if (c.provider.compare(provider, Qt::CaseInsensitive) == 0) {
                key = c.api_key;
                base_url = c.base_url;
                break;
            }
        }
    }

    LlmProfile p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = provider + " · " + model_id;
    p.provider = provider;
    p.model_id = model_id;
    p.api_key = key;
    p.base_url = base_url;
    p.runtime = ai_chat::runtime_for_provider(provider);
    if (auto r = repo.save_profile(p); r.is_err()) {
        LOG_WARN(TAG, "could not create profile for " + provider + "/" + model_id);
        return {};
    }
    return p.id;
}

void AiRolesSection::apply_binding(const QString& role_id, const QString& provider,
                                   const QString& model_id) {
    auto& repo = LlmProfileRepository::instance();

    // Write only on a real change. `loading_` already suppresses the signals
    // fired while repopulating, but that guard is one missed code path away
    // from silently rebinding every role at once — which is exactly the state
    // this panel's earlier builds left in the database. Comparing against what
    // is stored makes a spurious signal harmless instead of destructive.
    {
        const QString current_id = repo.get_assignment(role_id, QString());
        QString cur_provider, cur_model;
        if (!current_id.isEmpty()) {
            if (auto p = repo.get_profile(current_id); p.is_ok()) {
                cur_provider = p.value().provider;
                cur_model = p.value().model_id;
            }
        }
        if (cur_provider == provider && cur_model == model_id)
            return;  // nothing to do
    }
    if (model_id.isEmpty()) {
        repo.remove_assignment(role_id, QString());
        status_->setText(ai_chat::ai_role_label(role_id) + " → default.");
        emit bindings_changed();
        return;
    }
    const QString profile_id = profile_for_model(provider, model_id);
    if (profile_id.isEmpty()) {
        status_->setText("Could not save that binding — see logs.");
        return;
    }
    if (auto r = repo.assign_profile(role_id, QString(), profile_id); r.is_err()) {
        status_->setText("Could not save that binding — see logs.");
        return;
    }
    status_->setText(ai_chat::ai_role_label(role_id) + " → " + provider + " · " + model_id + ".");
    emit bindings_changed();
}

} // namespace fincept::screens
