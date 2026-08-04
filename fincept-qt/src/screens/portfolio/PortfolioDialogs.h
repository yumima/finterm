// src/screens/portfolio/PortfolioDialogs.h
#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QString>
#include <QTimer>

namespace fincept::screens {

/// Dialog for creating a new portfolio.
class CreatePortfolioDialog : public QDialog {
    Q_OBJECT
  public:
    explicit CreatePortfolioDialog(QWidget* parent = nullptr);
    /// Rename/edit an existing portfolio: same form, prefilled, different
    /// wording. The repository has supported update_portfolio() all along —
    /// nothing ever called it, so a portfolio's name was fixed at creation.
    static CreatePortfolioDialog* for_edit(const portfolio::Portfolio& existing, QWidget* parent = nullptr);

    QString name() const;
    QString owner() const;
    QString currency() const;

  private:
    QLineEdit* name_edit_ = nullptr;
    QLineEdit* owner_edit_ = nullptr;
    QComboBox* currency_cb_ = nullptr;
};

/// Confirmation dialog for deleting a portfolio.
class ConfirmDeleteDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ConfirmDeleteDialog(const QString& portfolio_name, QWidget* parent = nullptr);
};

/// Dialog for adding an asset (BUY).
/// The symbol field has inline search: type a name or ticker and a dropdown
/// appears with matching results fetched from /market/search.
class AddAssetDialog : public QDialog {
    Q_OBJECT
  public:
    explicit AddAssetDialog(QWidget* parent = nullptr, const QString& prefill_symbol = {});

    QString symbol() const;
    double quantity() const;
    double price() const;

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    void schedule_search(const QString& query);
    void fire_search(const QString& query);
    void show_results(const QJsonArray& results);
    void select_result(const QString& symbol);
    void position_dropdown();

    QLineEdit* symbol_edit_ = nullptr;
    QLineEdit* quantity_edit_ = nullptr;
    QLineEdit* price_edit_ = nullptr;

    // Ticker search dropdown (parented to dialog, floats over form)
    QFrame* search_frame_ = nullptr;
    QListWidget* search_list_ = nullptr;
    QTimer* search_debounce_ = nullptr;
    QString pending_query_;
    bool selecting_ = false; // guard against recursive text-changed
};

/// Dialog for selling an asset. Takes the whole holdings list so it can open
/// even when no row is focused — the symbol combo picks which position to
/// sell, and @p preselect_symbol pre-selects the focused row when there is
/// one. (The order panel's SELL used to require a prior row selection and
/// silently did nothing without one, while BUY always opened its dialog.)
class SellAssetDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SellAssetDialog(const QVector<portfolio::HoldingWithQuote>& holdings,
                             const QString& preselect_symbol = {}, QWidget* parent = nullptr);

    QString symbol() const;
    double quantity() const;
    double price() const;

  private:
    double held_qty() const;

    QComboBox* symbol_cb_ = nullptr;
    QLabel* held_label_ = nullptr;
    QLineEdit* quantity_edit_ = nullptr;
    QLineEdit* price_edit_ = nullptr;
    QVector<double> held_qtys_; // parallel to symbol_cb_ items
};

/// Dialog for editing an existing transaction.
class EditTransactionDialog : public QDialog {
    Q_OBJECT
  public:
    /// @p other_net_qty is the net quantity of every OTHER transaction for this
    /// symbol (buys minus sells). The position is derived from the whole
    /// ledger, not from this one lot, so the dialog shows what the edit
    /// actually leaves you holding — without it, shrinking a buy that has a
    /// larger sell recorded against it silently produced a negative position.
    explicit EditTransactionDialog(const portfolio::Transaction& txn, double other_net_qty = 0,
                                   QWidget* parent = nullptr);

    double quantity() const;
    double price() const;
    QString date() const;
    QString notes() const;

  private:
    void update_resulting_position();

    QLineEdit* quantity_edit_ = nullptr;
    QLineEdit* price_edit_ = nullptr;
    QDateEdit* date_edit_ = nullptr;
    QLineEdit* notes_edit_ = nullptr;
    QLabel* result_label_ = nullptr;
    double other_net_qty_ = 0;
    int sign_ = 1; // +1 for a BUY lot, -1 for a SELL lot
};

/// Dialog for mapping symbols to sectors.
class SectorMappingDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SectorMappingDialog(const QVector<portfolio::HoldingWithQuote>& holdings, QWidget* parent = nullptr);

    QHash<QString, QString> sector_map() const;

  private:
    QHash<QString, QComboBox*> combos_;
};

/// Dialog for recording a dividend payment for a holding.
class AddDividendDialog : public QDialog {
    Q_OBJECT
  public:
    explicit AddDividendDialog(const QStringList& symbols, QWidget* parent = nullptr);

    QString symbol() const;
    double amount_per_share() const;
    QString date() const;
    QString notes() const;

  private:
    QComboBox* symbol_cb_ = nullptr;
    QLineEdit* amount_edit_ = nullptr;
    QDateEdit* date_edit_ = nullptr;
    QLineEdit* notes_edit_ = nullptr;
};

/// Dialog for importing a portfolio from JSON file.
class ImportPortfolioDialog : public QDialog {
    Q_OBJECT
  public:
    explicit ImportPortfolioDialog(const QVector<portfolio::Portfolio>& portfolios, QWidget* parent = nullptr);

    QString file_path() const;
    portfolio::ImportMode mode() const;
    QString merge_target_id() const;

  private:
    void browse_file();

    QLineEdit* file_edit_ = nullptr;
    QRadioButton* new_radio_ = nullptr;
    QRadioButton* merge_radio_ = nullptr;
    QComboBox* target_cb_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace fincept::screens
