#include "screens/portfolio/BrokerCsvDialog.h"

#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace finterm::screens {

namespace csv = portfolio::csv;

namespace {

const csv::Field kFields[] = {csv::Field::Ignore,   csv::Field::Date,  csv::Field::Action,
                              csv::Field::Symbol,   csv::Field::Quantity, csv::Field::Price,
                              csv::Field::Amount,   csv::Field::CostBasisTotal, csv::Field::AverageCost};

QString samples(const csv::Table& t, int col) {
    QStringList out;
    for (const auto& row : t.rows) {
        if (col < row.size() && !row[col].isEmpty())
            out << row[col];
        if (out.size() == 3)
            break;
    }
    return out.join(QStringLiteral(" · "));
}

} // namespace

BrokerCsvDialog::BrokerCsvDialog(const QString& file_path, bool new_portfolio, QWidget* parent)
    : QDialog(parent), new_portfolio_(new_portfolio) {
    QFile f(file_path);
    if (!f.open(QIODevice::ReadOnly)) {
        error_ = QStringLiteral("Cannot open %1").arg(file_path);
        return;
    }
    table_ = csv::read_table(f.readAll(), &error_);
    if (!table_)
        return;
    build_ui(file_path, new_portfolio);
    reparse();
}

void BrokerCsvDialog::build_ui(const QString& file_path, bool new_portfolio) {
    setWindowTitle(QStringLiteral("Import broker CSV"));
    resize(760, 640);
    setStyleSheet(QString("QDialog { background:%1; color:%2; }"
                          "QLabel { color:%3; font-size:12px; }"
                          "QLineEdit, QDateEdit, QComboBox, QPlainTextEdit { background:%4; color:%2; border:1px solid %5;"
                          "  padding:4px 8px; font-size:12px; }"
                          "QRadioButton { color:%2; font-size:12px; }"
                          "QTableWidget { background:%4; color:%2; gridline-color:%5; font-size:12px; }"
                          "QHeaderView::section { background:%1; color:%3; border:none; padding:4px; }")
                      .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::TEXT_SECONDARY(),
                           ui::colors::BG_BASE(), ui::colors::BORDER_MED()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("IMPORT %1").arg(QFileInfo(file_path).fileName().toUpper()));
    title->setStyleSheet(QString("color:%1; font-size:13px; font-weight:700;").arg(ui::colors::CYAN()));
    layout->addWidget(title);

    if (new_portfolio) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("Portfolio name")));
        name_edit_ = new QLineEdit(QFileInfo(file_path).completeBaseName());
        row->addWidget(name_edit_, 1);
        layout->addLayout(row);
    }

    auto* kind_row = new QHBoxLayout;
    positions_radio_ = new QRadioButton(QStringLiteral("Positions (one row per holding)"));
    activity_radio_ = new QRadioButton(QStringLiteral("Activity (trades, dividends)"));
    kind_row->addWidget(positions_radio_);
    kind_row->addWidget(activity_radio_);
    kind_row->addStretch();
    kind_row->addWidget(new QLabel(QStringLiteral("Holdings as of")));
    as_of_edit_ = new QDateEdit(QDate::currentDate());
    as_of_edit_->setCalendarPopup(true);
    as_of_edit_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    as_of_edit_->setToolTip(QStringLiteral("A positions file has no trade dates: each holding is recorded as one "
                                           "purchase on this date, at its average cost. Performance before it is "
                                           "not known."));
    kind_row->addWidget(as_of_edit_);
    layout->addLayout(kind_row);

    const auto mapping = csv::guess_mapping(table_->headers);
    (csv::guess_kind(mapping) == csv::Kind::Activity ? activity_radio_ : positions_radio_)->setChecked(true);

    auto* hint = new QLabel(QStringLiteral("Columns were matched automatically — check them against the sample "
                                           "values and correct any that are wrong."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    mapping_table_ = new QTableWidget(table_->headers.size(), 3);
    mapping_table_->setHorizontalHeaderLabels({QStringLiteral("COLUMN"), QStringLiteral("SAMPLE VALUES"),
                                               QStringLiteral("IMPORT AS")});
    mapping_table_->verticalHeader()->setVisible(false);
    mapping_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mapping_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mapping_table_->setSelectionMode(QAbstractItemView::NoSelection);
    for (int col = 0; col < table_->headers.size(); ++col) {
        mapping_table_->setItem(col, 0, new QTableWidgetItem(table_->headers[col]));
        mapping_table_->setItem(col, 1, new QTableWidgetItem(samples(*table_, col)));
        auto* box = new QComboBox;
        for (const auto f : kFields)
            box->addItem(csv::field_name(f), static_cast<int>(f));
        box->setCurrentIndex(box->findData(static_cast<int>(mapping.value(col, csv::Field::Ignore))));
        connect(box, &QComboBox::currentIndexChanged, this, [this, box]() {
            // One column per field: choosing a field another column holds
            // moves it here, so the mapping on screen is the one imported.
            const int f = box->currentData().toInt();
            if (f != static_cast<int>(csv::Field::Ignore))
                for (auto* other : std::as_const(field_boxes_))
                    if (other != box && other->currentData().toInt() == f) {
                        const QSignalBlocker block(other);
                        other->setCurrentIndex(other->findData(static_cast<int>(csv::Field::Ignore)));
                    }
            reparse();
        });
        mapping_table_->setCellWidget(col, 2, box);
        field_boxes_ << box;
    }
    layout->addWidget(mapping_table_, 3);

    summary_label_ = new QLabel;
    summary_label_->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    layout->addWidget(summary_label_);
    report_ = new QPlainTextEdit;
    report_->setReadOnly(true);
    layout->addWidget(report_, 2);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("CANCEL"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    import_btn_ = new QPushButton(QStringLiteral("IMPORT"));
    import_btn_->setStyleSheet(QString("QPushButton { background:%1; color:%2; border:none; padding:6px 16px;"
                                       " font-weight:700; } QPushButton:disabled { background:%3; }")
                                   .arg(ui::colors::CYAN(), ui::colors::BG_BASE(), ui::colors::BORDER_MED()));
    connect(import_btn_, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(import_btn_);
    layout->addLayout(buttons);

    connect(positions_radio_, &QRadioButton::toggled, this, &BrokerCsvDialog::reparse);
    connect(as_of_edit_, &QDateEdit::dateChanged, this, &BrokerCsvDialog::reparse);
}

void BrokerCsvDialog::reparse() {
    if (!table_ || field_boxes_.isEmpty())
        return;
    QHash<int, csv::Field> mapping;
    for (int col = 0; col < field_boxes_.size(); ++col)
        mapping.insert(col, static_cast<csv::Field>(field_boxes_[col]->currentData().toInt()));
    const auto kind = positions_radio_->isChecked() ? csv::Kind::Positions : csv::Kind::Activity;
    as_of_edit_->setEnabled(kind == csv::Kind::Positions);
    parsed_ = csv::to_transactions(*table_, mapping, kind, as_of_edit_->date());

    summary_label_->setText(QStringLiteral("%1 transaction(s) to import · %2 row(s) skipped")
                                .arg(parsed_.transactions.size())
                                .arg(parsed_.skipped.size()));
    QStringList lines = parsed_.warnings;
    if (!parsed_.skipped.isEmpty()) {
        lines << QStringLiteral("Skipped:");
        lines << parsed_.skipped;
    }
    // A positions file books every holding as a fresh purchase. Merged into a
    // portfolio that already holds them, it would double every position.
    const bool positions_into_existing = kind == csv::Kind::Positions && !new_portfolio_;
    if (positions_into_existing)
        lines.prepend(QStringLiteral("A positions file can only start a NEW portfolio: merged into an existing one "
                                     "it would add every holding a second time. Go back and choose \"Create new "
                                     "portfolio\", or import an activity export to add trades."));
    report_->setPlainText(lines.join('\n'));
    import_btn_->setEnabled(!parsed_.transactions.isEmpty() && !positions_into_existing);
}

QString BrokerCsvDialog::portfolio_name() const {
    const QString n = name_edit_ ? name_edit_->text().trimmed() : QString();
    return n.isEmpty() ? QStringLiteral("Imported") : n;
}

QVector<portfolio::ImportRecord> BrokerCsvDialog::records() const {
    QVector<portfolio::ImportRecord> out;
    out.reserve(parsed_.transactions.size());
    for (const auto& t : parsed_.transactions)
        out.append({t.date, t.symbol, t.type, t.quantity, t.price, t.note});
    return out;
}

} // namespace finterm::screens
