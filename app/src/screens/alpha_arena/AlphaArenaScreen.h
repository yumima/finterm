#pragma once

#include "screens/IStatefulScreen.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <functional>

namespace finterm::screens {

/// A competitor the user can choose: an LLM profile, a provider's active model,
/// or the grid bot (rules, no model).
struct ArenaModelEntry {
    QString key;          // what finds its connection again: the profile's id, "provider:<name>"
    QString display_name; // shown in the list, and its name in a competition
    QString provider;
    QString model_id;
    QString api_key;
    QString base_url;
    int max_tokens = 0;
};

/// Alpha Arena: AI models trade the same market with paper money, against each
/// other and a grid bot. The engine is scripts/alpha_arena; every action answers
/// with where the competition stands, and this screen shows that.
class AlphaArenaScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit AlphaArenaScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "alpha_arena"; }
    int state_version() const override { return 1; }

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void on_create_competition();
    void on_run_cycle();
    void on_toggle_auto_run();
    void on_reset();
    void on_right_tab_changed(int tab);
    void on_competition_type_changed(int index);
    void on_past_competitions_toggle();

  private:
    using Answer = std::function<void(const QJsonObject&)>;

    void setup_ui();
    QWidget* create_header();
    QWidget* create_create_panel();
    QWidget* create_controls_bar();
    QWidget* create_main_content();
    QWidget* create_leaderboard_panel();
    QWidget* create_right_panel();
    QWidget* create_past_competitions_panel();
    QWidget* create_status_bar();

    /// Run an engine action; `done` gets its answer, an error goes to the status
    /// bar (`failed`, when given, as well).
    void run_action(const QString& action, const QJsonObject& params, Answer done,
                    std::function<void(const QString& error)> failed = {});
    /// Every LLM profile and provider's active model in Settings now.
    static QVector<ArenaModelEntry> settings_entries();
    /// How each model is asked: never saved with a competition.
    static QJsonArray connections();
    void open_competition(const QString& id);
    void show_competition(const QJsonObject& state);
    void show_leaderboard(const QJsonArray& rows);
    void show_decisions(const QJsonArray& rows);
    void show_metrics(const QJsonArray& rows);
    void show_news(const QJsonObject& news);
    void show_grid(const QJsonObject& grid);
    /// What the open tab shows that is not part of a competition: headlines, exchanges' quotes.
    void load_tab();
    void load_news();
    void load_quotes();
    void load_markets();
    void load_past_competitions();
    QString current_symbol() const;
    void set_busy(bool busy);
    void set_auto_running(bool on);
    void set_badge(const QString& text, const QString& color);
    void say(const QString& text, bool problem = false);
    void populate_model_list();

    // The open competition
    QString competition_id_;
    QString shown_id_; // the one whose state is on the screen
    QString to_open_; // the last session's, opened when the screen is first shown
    QString competition_symbol_;
    QString competition_exchange_;
    int interval_s_ = 150;
    bool is_auto_running_ = false;
    bool busy_ = false; // creating one, or running a cycle

    QVector<ArenaModelEntry> model_entries_; // model_list_'s rows
    QTimer* auto_timer_ = nullptr;

    QLabel* status_badge_ = nullptr;
    QLabel* cycle_label_ = nullptr;
    QLabel* price_label_ = nullptr;

    QWidget* create_panel_ = nullptr;
    QLineEdit* comp_name_ = nullptr;
    QComboBox* comp_type_ = nullptr;
    QWidget* crypto_fields_ = nullptr;
    QWidget* market_fields_ = nullptr;
    QComboBox* comp_symbol_ = nullptr;
    QComboBox* comp_exchange_ = nullptr;
    QDoubleSpinBox* grid_range_ = nullptr;
    QSpinBox* grid_levels_ = nullptr;
    QListWidget* market_list_ = nullptr;
    QComboBox* comp_mode_ = nullptr;
    QDoubleSpinBox* comp_capital_ = nullptr;
    QSpinBox* comp_interval_ = nullptr;
    QListWidget* model_list_ = nullptr;
    QPushButton* create_btn_ = nullptr;

    QPushButton* run_btn_ = nullptr;
    QPushButton* auto_btn_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QLabel* interval_label_ = nullptr;

    QTableWidget* leaderboard_table_ = nullptr;
    QLabel* leaderboard_cycle_ = nullptr;

    QStackedWidget* right_stack_ = nullptr;
    QList<QPushButton*> right_tab_btns_;
    QListWidget* decisions_list_ = nullptr;
    QTableWidget* metrics_table_ = nullptr;
    QLabel* news_summary_ = nullptr;
    QListWidget* news_list_ = nullptr;
    QLabel* grid_summary_ = nullptr;
    QTableWidget* grid_table_ = nullptr;
    QLabel* quotes_summary_ = nullptr;
    QTableWidget* quotes_table_ = nullptr;

    QWidget* past_panel_ = nullptr;
    QListWidget* past_list_ = nullptr;
    bool past_visible_ = false;

    QLabel* status_comp_ = nullptr;
    QLabel* status_models_ = nullptr;
    QLabel* status_info_ = nullptr;
};

} // namespace finterm::screens
