// src/screens/economics/panels/EconPanelBase.cpp
#include "screens/economics/panels/EconPanelBase.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QLocale>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

// ── Stylesheet (token-based) ──────────────────────────────────────────────────

QString EconPanelBase::panel_style() const {
    using namespace ui::colors;
    int r = QColor(color_).red();
    int g = QColor(color_).green();
    int b = QColor(color_).blue();
    return QString("#econToolbar { background:%5; border-bottom:1px solid %6; }"
                   "#econFetchBtn { background:%1; color:%7; border:none;"
                   "  font-size:12px; font-weight:700; padding:4px 14px; }"
                   "#econFetchBtn:hover { background:rgba(%2,%3,%4,0.75); }"
                   "#econFetchBtn:disabled { background:%6; color:%8; }"
                   "#econCsvBtn { background:transparent; color:%9; border:1px solid %6;"
                   "  font-size:12px; font-weight:700; padding:4px 10px; }"
                   "#econCsvBtn:hover { color:%10; background:%11; }"
                   "QComboBox, QSpinBox, QDateEdit, QLineEdit {"
                   "  background:%7; color:%10; border:1px solid %6;"
                   "  font-size:12px; padding:2px 6px; }"
                   "QComboBox::drop-down, QDateEdit::drop-down { border:none; }"
                   "QComboBox QAbstractItemView { background:%5; color:%10;"
                   "  border:1px solid %6; }"
                   "QTableWidget { background:%7; color:%10; border:none;"
                   "  gridline-color:%6; font-size:12px; alternate-background-color:%12; }"
                   "QTableWidget::item { padding:5px 8px; border-bottom:1px solid %6; }"
                   "QTableWidget::item:selected { background:rgba(%2,%3,%4,0.1); color:%1; }"
                   "QHeaderView::section { background:%5; color:%9; border:none;"
                   "  border-bottom:2px solid %6; border-right:1px solid %6;"
                   "  padding:5px 8px; font-size:12px; font-weight:700; letter-spacing:0.5px; }"
                   "#econCardsRow  { background:%7; border-bottom:1px solid %6; }"
                   "#econTitleBar  { background:%12; border-bottom:1px solid %6; }"
                   "#econStatCard { background:%12; border:1px solid %6; }"
                   "#econStatCard:hover { border-color:%13; }"
                   "#econStatLabel { color:%9; font-size:12px; font-weight:700;"
                   "  letter-spacing:1px; background:transparent; }"
                   "#econStatVal  { color:%1;  font-size:15px; font-weight:700; background:transparent; }"
                   "#econStatPos  { color:%16; font-size:15px; font-weight:700; background:transparent; }"
                   "#econStatNeg  { color:%15; font-size:15px; font-weight:700; background:transparent; }"
                   "#econStatSub  { color:%14; font-size:12px;  background:transparent; }"
                   "#econEmptyPage { background:%7; }"
                   "#econEmptyMsg   { color:%9;  font-size:13px; background:transparent; }"
                   "#econLoadingMsg { color:%1;  font-size:13px; background:transparent; }"
                   "#econErrMsg     { color:%15; font-size:12px; background:transparent; }"
                   "#econTitleLbl { color:%10; font-size:12px; font-weight:700;"
                   "  background:transparent; }"
                   "#econRowCount { color:%14; font-size:12px; background:transparent; }"
                   "QScrollBar:vertical { background:%7; width:5px; }"
                   "QScrollBar::handle:vertical { background:%6; min-height:20px; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
                   "QScrollBar:horizontal { background:%7; height:5px; }"
                   "QScrollBar::handle:horizontal { background:%6; min-width:20px; }"
                   "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }")
        .arg(color_) // %1: source accent
        .arg(r)
        .arg(g)
        .arg(b)                // %2,%3,%4: accent RGB
        .arg(BG_RAISED())      // %5
        .arg(BORDER_DIM())     // %6
        .arg(BG_BASE())        // %7
        .arg(TEXT_DIM())       // %8
        .arg(TEXT_SECONDARY()) // %9
        .arg(TEXT_PRIMARY())   // %10
        .arg(BG_HOVER())       // %11
        .arg(BG_SURFACE())     // %12
        .arg(BORDER_BRIGHT())  // %13
        .arg(TEXT_TERTIARY())  // %14
        .arg(NEGATIVE())       // %15
        .arg(POSITIVE());      // %16
}

// ── Reusable token-based style helpers ────────────────────────────────────────

QString EconPanelBase::sidebar_style() {
    using namespace ui::colors;
    return QString("background:%1; border-right:1px solid %2;").arg(BG_SURFACE(), BORDER_DIM());
}

QString EconPanelBase::section_hdr_style() {
    using namespace ui::colors;
    return QString("background:%1; border-bottom:1px solid %2;").arg(BG_RAISED(), BORDER_DIM());
}

QString EconPanelBase::section_lbl_style() {
    using namespace ui::colors;
    return QString("color:%1; font-size:12px; font-weight:700;"
                   " letter-spacing:1px; background:transparent; padding:4px 10px;")
        .arg(TEXT_TERTIARY());
}

QString EconPanelBase::search_input_style() {
    using namespace ui::colors;
    return QString("background:%1; color:%2; border:none;"
                   " border-bottom:1px solid %3; padding:4px 10px; font-size:12px;")
        .arg(BG_BASE(), TEXT_PRIMARY(), BORDER_DIM());
}

QString EconPanelBase::ctrl_label_style() {
    using namespace ui::colors;
    return QString("color:%1; font-size:12px; font-weight:700; background:transparent;").arg(TEXT_TERTIARY());
}

QString EconPanelBase::list_style() const {
    using namespace ui::colors;
    QColor c(color_);
    QString rgba = QString("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
    return QString("QListWidget { background:transparent; border:none; outline:none; }"
                   "QListWidget::item { color:%1; padding:5px 10px;"
                   "  border-bottom:1px solid %2; font-size:12px; }"
                   "QListWidget::item:hover { color:%3; background:%4; }"
                   "QListWidget::item:selected { color:%5; background:rgba(%6,0.1);"
                   "  border-left:2px solid %5; font-weight:700; }")
        .arg(TEXT_SECONDARY(), BG_RAISED(), TEXT_PRIMARY(), BG_HOVER())
        .arg(color_, rgba);
}

QString EconPanelBase::notice_style() {
    using namespace ui::colors;
    return QString("color:%1; font-size:12px; background:transparent;").arg(WARNING());
}

// ── Constructor ───────────────────────────────────────────────────────────────

EconPanelBase::EconPanelBase(const QString& source_id, const QString& color, QWidget* parent)
    : QWidget(parent), source_id_(source_id), color_(color) {
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, &EconPanelBase::refresh_panel_theme);
}

// ── Build ─────────────────────────────────────────────────────────────────────

void EconPanelBase::build_base_ui(QWidget* container) {
    container_ = container;
    container->setStyleSheet(panel_style());

    auto* root = new QVBoxLayout(container);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("econToolbar");
    toolbar->setFixedHeight(40);
    auto* thl = new QHBoxLayout(toolbar);
    thl->setContentsMargins(10, 0, 10, 0);
    thl->setSpacing(6);
    build_controls(thl);
    thl->addStretch(1);

    fetch_btn_ = new QPushButton("FETCH");
    fetch_btn_->setObjectName("econFetchBtn");
    fetch_btn_->setCursor(Qt::PointingHandCursor);
    connect(fetch_btn_, &QPushButton::clicked, this, &EconPanelBase::on_fetch);

    export_btn_ = new QPushButton("CSV");
    export_btn_->setObjectName("econCsvBtn");
    export_btn_->setCursor(Qt::PointingHandCursor);
    connect(export_btn_, &QPushButton::clicked, this, &EconPanelBase::export_csv);

    thl->addWidget(fetch_btn_);
    thl->addWidget(export_btn_);
    root->addWidget(toolbar);

    // Stat cards row
    cards_row_ = new QWidget(this);
    cards_row_->setObjectName("econCardsRow");
    auto* crhl = new QHBoxLayout(cards_row_);
    crhl->setContentsMargins(10, 6, 10, 6);
    crhl->setSpacing(6);

    auto make_card = [&](const QString& label, QLabel*& out) {
        auto* card = new QWidget(this);
        card->setObjectName("econStatCard");
        card->setMinimumWidth(90);
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(10, 6, 10, 6);
        vl->setSpacing(1);
        auto* lbl = new QLabel(label);
        lbl->setObjectName("econStatLabel");
        out = new QLabel("—");
        out->setObjectName("econStatVal");
        vl->addWidget(lbl);
        vl->addWidget(out);
        crhl->addWidget(card);
    };
    make_card("LATEST", stat_latest_);
    make_card("CHANGE", stat_change_);
    make_card("MIN", stat_min_);
    make_card("MAX", stat_max_);
    make_card("AVG", stat_avg_);
    make_card("POINTS", stat_count_);
    crhl->addStretch(1);
    root->addWidget(cards_row_);

    // Title bar
    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("econTitleBar");
    auto* tbhl = new QHBoxLayout(title_bar_);
    tbhl->setContentsMargins(12, 4, 12, 4);
    title_lbl_ = new QLabel;
    title_lbl_->setObjectName("econTitleLbl");
    row_count_ = new QLabel;
    row_count_->setObjectName("econRowCount");
    tbhl->addWidget(title_lbl_);
    tbhl->addStretch(1);
    tbhl->addWidget(row_count_);
    root->addWidget(title_bar_);

    // Content stack
    stack_ = new QStackedWidget;

    auto* empty_pg = new QWidget(this);
    empty_pg->setObjectName("econEmptyPage");
    auto* evl = new QVBoxLayout(empty_pg);
    evl->setAlignment(Qt::AlignCenter);
    empty_lbl_ = new QLabel("Select parameters and click FETCH");
    empty_lbl_->setObjectName("econEmptyMsg");
    empty_lbl_->setAlignment(Qt::AlignCenter);
    empty_lbl_->setWordWrap(true);
    evl->addWidget(empty_lbl_);
    stack_->addWidget(empty_pg); // index 0

    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(true);
    stack_->addWidget(table_); // index 1

    root->addWidget(stack_, 1);

    // Apply token-based inline styles for non-QSS-covered widgets
    refresh_panel_theme();
}

// ── Theme refresh ─────────────────────────────────────────────────────────────

void EconPanelBase::refresh_panel_theme() {
    if (container_)
        container_->setStyleSheet(panel_style());
}

// ── State ─────────────────────────────────────────────────────────────────────

void EconPanelBase::show_loading(const QString& msg) {
    if (!empty_lbl_)
        return;
    empty_lbl_->setObjectName("econLoadingMsg");
    empty_lbl_->style()->unpolish(empty_lbl_);
    empty_lbl_->style()->polish(empty_lbl_);
    empty_lbl_->setText(msg);
    stack_->setCurrentIndex(0);
    if (fetch_btn_)
        fetch_btn_->setEnabled(false);
}

void EconPanelBase::show_error(const QString& msg) {
    if (!empty_lbl_)
        return;
    empty_lbl_->setObjectName("econErrMsg");
    empty_lbl_->style()->unpolish(empty_lbl_);
    empty_lbl_->style()->polish(empty_lbl_);
    empty_lbl_->setText("Error: " + msg);
    stack_->setCurrentIndex(0);
    if (fetch_btn_)
        fetch_btn_->setEnabled(true);
}

void EconPanelBase::show_empty(const QString& msg) {
    if (!empty_lbl_)
        return;
    empty_lbl_->setObjectName("econEmptyMsg");
    empty_lbl_->style()->unpolish(empty_lbl_);
    empty_lbl_->style()->polish(empty_lbl_);
    empty_lbl_->setText(msg);
    stack_->setCurrentIndex(0);
    if (fetch_btn_)
        fetch_btn_->setEnabled(true);
}

void EconPanelBase::show_table() {
    stack_->setCurrentIndex(1);
    if (fetch_btn_)
        fetch_btn_->setEnabled(true);
}

// ── Display ───────────────────────────────────────────────────────────────────

void EconPanelBase::display(const QJsonArray& rows, const QString& title, const QString& value_key,
                            const QString& date_key) {
    if (rows.isEmpty()) {
        show_empty("No data returned for this selection");
        return;
    }

    // Collect all keys, prioritise date then value
    QStringList cols;
    for (const auto& v : rows) {
        for (const auto& k : v.toObject().keys())
            if (!cols.contains(k))
                cols << k;
    }
    static const QStringList kDateKeys = {"date",   "period",      "year",        "time",    "Date",
                                          "Period", "TIME_PERIOD", "record_date", "ref_date"};
    // Declared keys win over the name heuristics for column placement.
    if (!value_key.isEmpty() && cols.indexOf(value_key) > 0)
        cols.move(cols.indexOf(value_key), 0);
    if (!date_key.isEmpty() && cols.indexOf(date_key) > 0)
        cols.move(cols.indexOf(date_key), 0);
    for (const auto& dk : kDateKeys) {
        if (!date_key.isEmpty())
            break;
        int idx = cols.indexOf(dk);
        if (idx > 0) {
            cols.move(idx, 0);
            break;
        }
    }
    static const QStringList kValKeys = {"value", "Value", "val", "OBS_VALUE", "obs_value", "amount", "rate"};
    for (const auto& vk : kValKeys) {
        if (!value_key.isEmpty())
            break;
        int idx = cols.indexOf(vk);
        if (idx > 1) {
            cols.move(idx, 1);
            break;
        }
    }

    table_->setColumnCount(cols.size());
    table_->setHorizontalHeaderLabels(cols);
    table_->setRowCount(rows.size());

    for (int r = 0; r < rows.size(); ++r) {
        const auto obj = rows[r].toObject();
        for (int c = 0; c < cols.size(); ++c) {
            const auto jv = obj[cols[c]];
            QString text;
            if (jv.isDouble())
                text = QString::number(jv.toDouble(), 'g', 8);
            else if (jv.isNull())
                text = QStringLiteral("—"); // explicit null from the source = unavailable
            else
                text = jv.toString();
            auto* item = new QTableWidgetItem(text);
            if (c == 0)
                item->setForeground(QColor(color_));
            table_->setItem(r, c, item);
        }
    }

    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (cols.size() > 1)
        table_->horizontalHeader()->setSectionResizeMode(cols.size() - 1, QHeaderView::Stretch);

    if (title_lbl_)
        title_lbl_->setText(title);
    if (row_count_)
        row_count_->setText(QString::number(rows.size()) + " records");

    update_stats(rows, value_key, date_key);
    show_table();
}

void EconPanelBase::update_stats(const QJsonArray& rows, const QString& value_key, const QString& date_key) {
    const QString dash = ui::formatting::placeholder();
    for (auto* l : {stat_latest_, stat_change_, stat_min_, stat_max_, stat_avg_})
        if (l)
            l->setText(dash);
    if (stat_change_) {
        stat_change_->setObjectName("econStatVal");
        stat_change_->style()->unpolish(stat_change_);
        stat_change_->style()->polish(stat_change_);
    }
    if (stat_count_)
        stat_count_->setText(QString::number(rows.size()));

    // No declared value column → the series value is unknown; never guess one.
    if (value_key.isEmpty())
        return;

    auto to_number = [](const QJsonValue& jv, double& out) {
        if (jv.isDouble()) {
            out = jv.toDouble();
            return std::isfinite(out);
        }
        if (jv.isString()) {
            bool ok = false;
            out = jv.toString().trimmed().toDouble(&ok);
            return ok && std::isfinite(out);
        }
        return false;
    };
    // Sort key for the date column: ISO-style strings ("2024-03-01", "2024-03",
    // "2024-Q1", "2024") order correctly lexically; numeric years are rendered
    // to text so mixed int/string years still compare.
    auto to_date_key = [](const QJsonValue& jv) -> QString {
        if (jv.isDouble())
            return QString::number(jv.toDouble(), 'f', 0);
        const QString s = jv.toString().trimmed();
        // Day-first formats used by BoE ("02 Jan 2025"), RBA ("02-Jan-2025")
        // and BCB-style "dd/MM/yyyy" do not sort lexically — normalise to ISO.
        if (!s.isEmpty() && !s.at(0).isDigit())
            return s;
        if (s.size() >= 10 && s.at(2) != QLatin1Char('-') && s.at(4) == QLatin1Char('-'))
            return s; // already ISO-like
        static const QLocale c_locale = QLocale::c();
        for (const char* f : {"dd MMM yyyy", "dd-MMM-yyyy", "dd/MM/yyyy", "d MMM yyyy", "d-MMM-yyyy"}) {
            const QDate d = c_locale.toDate(s, QLatin1String(f));
            if (d.isValid())
                return d.toString(Qt::ISODate);
        }
        return s;
    };

    struct Obs {
        QString date;
        double value;
    };
    QVector<double> vals;
    QVector<Obs> series;
    for (const auto& v : rows) {
        const auto obj = v.toObject();
        double d = 0.0;
        if (!to_number(obj.value(value_key), d))
            continue;
        vals << d;
        if (!date_key.isEmpty()) {
            const QString dk = to_date_key(obj.value(date_key));
            if (!dk.isEmpty())
                series.push_back({dk, d});
        }
    }

    if (vals.isEmpty())
        return;

    double mn = *std::min_element(vals.begin(), vals.end());
    double mx = *std::max_element(vals.begin(), vals.end());
    double sum = 0.0;
    for (double x : vals)
        sum += x;
    double avg = sum / vals.size();

    auto fmt = [](double v) { return QString::number(v, 'g', 6); };
    if (stat_min_)
        stat_min_->setText(fmt(mn));
    if (stat_max_)
        stat_max_->setText(fmt(mx));
    if (stat_avg_)
        stat_avg_->setText(fmt(avg));

    // LATEST/CHANGE need a time order. Sort oldest→newest by the declared date
    // key (sources disagree on order). Repeated dates mean the table mixes
    // several series, so there is no single "latest" — leave "—".
    if (series.isEmpty())
        return;
    std::stable_sort(series.begin(), series.end(), [](const Obs& a, const Obs& b) { return a.date < b.date; });
    for (int i = 1; i < series.size(); ++i)
        if (series[i].date == series[i - 1].date)
            return;

    const double latest = series.last().value;
    if (stat_latest_)
        stat_latest_->setText(fmt(latest));

    // No prior observation (or a zero base) means the change is undefined —
    // show "—", not a fabricated 0.00%.
    const bool has_change = series.size() > 1 && series[series.size() - 2].value != 0.0;
    if (stat_change_ && has_change) {
        const double prev = series[series.size() - 2].value;
        const double change = ((latest - prev) / qAbs(prev)) * 100.0;
        stat_change_->setText((change >= 0 ? "+" : "") + QString::number(change, 'f', 2) + "%");
        stat_change_->setObjectName(change >= 0 ? "econStatPos" : "econStatNeg");
        stat_change_->style()->unpolish(stat_change_);
        stat_change_->style()->polish(stat_change_);
    }
}

// ── CSV ───────────────────────────────────────────────────────────────────────

void EconPanelBase::export_csv() {
    if (!table_ || table_->rowCount() == 0)
        return;

    QString path = QFileDialog::getSaveFileName(this, "Export CSV", source_id_ + "_data.csv", "CSV Files (*.csv)");
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);

    QStringList hdrs;
    for (int c = 0; c < table_->columnCount(); ++c) {
        auto* h = table_->horizontalHeaderItem(c);
        hdrs << (h ? h->text() : "");
    }
    out << hdrs.join(",") << "\n";

    for (int r = 0; r < table_->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < table_->columnCount(); ++c) {
            auto* item = table_->item(r, c);
            QString val = item ? item->text() : "";
            if (val.contains(',') || val.contains('"'))
                val = "\"" + val.replace("\"", "\"\"") + "\"";
            row << val;
        }
        out << row.join(",") << "\n";
    }
}

// ── Shared utilities ──────────────────────────────────────────────────────────

void EconPanelBase::filter_list(QListWidget* list, const QString& text) {
    if (!list)
        return;
    for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

} // namespace fincept::screens
