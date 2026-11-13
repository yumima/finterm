#pragma once
// BrokerCsvDialog.h — confirm how a broker's CSV export maps onto holdings.
//
// The file's columns are matched to fields automatically (BrokerCsv); this
// dialog shows that guess next to sample values so the user can correct it,
// says whether the file reads as positions or activity, and previews what
// will be imported and what will be skipped — before anything is written.

#include "screens/portfolio/PortfolioTypes.h"
#include "services/portfolio/BrokerCsv.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace fincept::screens {

class BrokerCsvDialog : public QDialog {
    Q_OBJECT
  public:
    /// nullptr-safe: check ok() after construction; error() says why not.
    BrokerCsvDialog(const QString& file_path, bool new_portfolio, QWidget* parent = nullptr);

    bool ok() const { return table_.has_value(); }
    QString error() const { return error_; }

    QString portfolio_name() const;
    QVector<portfolio::ImportRecord> records() const;

  private:
    void build_ui(const QString& file_path, bool new_portfolio);
    void reparse();

    bool new_portfolio_ = true;
    std::optional<portfolio::csv::Table> table_;
    QString error_;
    portfolio::csv::ParseResult parsed_;

    QLineEdit* name_edit_ = nullptr;
    QRadioButton* positions_radio_ = nullptr;
    QRadioButton* activity_radio_ = nullptr;
    QDateEdit* as_of_edit_ = nullptr;
    QTableWidget* mapping_table_ = nullptr;
    QVector<QComboBox*> field_boxes_;
    QLabel* summary_label_ = nullptr;
    QPlainTextEdit* report_ = nullptr;
    QPushButton* import_btn_ = nullptr;
};

} // namespace fincept::screens
