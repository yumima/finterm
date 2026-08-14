#pragma once
// AiRolesSection.h — bind each AI role to one of the configured providers.
//
// Division of labour between the two tabs:
//   Models — WHICH providers finterm may use, and the model each one runs.
//            That tab is the allow-list.
//   Roles  — WHERE each part of the product sends its requests, chosen from
//            that allow-list. Nothing else.
//
// So the dropdown here lists the configured provider rows, not a provider's
// whole catalogue. Offering every model Gemini advertises made the Models tab
// meaningless and the choice unreadable; wanting a model that isn't configured
// is a job for the Models tab, not this one.
//
// Providers are independent — hearth on loopback and a cloud API are both
// reachable at once — so news can sit on a local model while chat is on Gemini.
//
// Bindings are stored as LlmProfileRepository assignments keyed by role id, so
// an unbound role falls through to the chat model. Binding is always an
// override, never a prerequisite.

#include <QComboBox>
#include <QLabel>
#include <QMap>
#include <QPair>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

class AiRolesSection : public QWidget {
    Q_OBJECT
  public:
    explicit AiRolesSection(QWidget* parent = nullptr);

    /// Re-read the configured providers and the current bindings.
    /// Cheap and purely local — no provider is contacted, so this cannot spend
    /// a metered provider's quota.
    void reload();

    /// Called when the Roles tab becomes visible.
    void on_shown() { reload(); }

  signals:
    void bindings_changed();

  private:
    void build_ui();
    void apply_binding(const QString& role_id, const QString& provider, const QString& model_id);
    void populate_combo(QComboBox* combo, const QString& provider, const QString& model);

    /// Find an existing profile for (provider, model), or create one.
    QString profile_for_model(const QString& provider, const QString& model_id);

    QMap<QString, QComboBox*> role_combos_;      // role id → its dropdown
    QVector<QPair<QString, QString>> configured_; // (provider, model) rows
    QString default_provider_;
    QLabel* status_ = nullptr;
    QLabel* provider_note_ = nullptr;
    bool loading_ = false;  // suppresses currentIndexChanged during repopulation
};

} // namespace fincept::screens
