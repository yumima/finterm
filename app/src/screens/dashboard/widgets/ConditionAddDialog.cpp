#include "screens/dashboard/widgets/ConditionAddDialog.h"

#include "services/equity/EquityResearchService.h"
#include "services/markets/MarketConditions.h"
#include "ui/SearchFailedText.h"
#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

namespace {
QString input_ss() {
    return QString("QLineEdit{background:%1;color:%2;border:1px solid %3;padding:4px 8px;}"
                   "QLineEdit:focus{border-color:%4;}")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED(), ui::colors::AMBER());
}
QString caption_ss() {
    return QString("color:%1;font-weight:700;letter-spacing:1px;background:transparent;font-size:12px;")
        .arg(ui::colors::TEXT_SECONDARY());
}
} // namespace

ConditionAddDialog::ConditionAddDialog(Kind kind, QWidget* parent) : QDialog(parent), kind_(kind) {
    setModal(true);
    setMinimumWidth(420);
    setStyleSheet(QString("QDialog{background:%1;color:%2;}"
                          "QListWidget{background:%3;border:1px solid %4;outline:none;color:%2;}"
                          "QListWidget::item{padding:4px 8px;}"
                          "QListWidget::item:hover,QListWidget::item:selected{background:%5;color:%6;}"
                          "QCheckBox{color:%2;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(),
                           ui::colors::BORDER_MED(), ui::colors::BG_HOVER(), ui::colors::AMBER()));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    auto* cap = new QLabel;
    cap->setStyleSheet(caption_ss());
    root->addWidget(cap);
    input_ = new QLineEdit;
    input_->setStyleSheet(input_ss());
    root->addWidget(input_);

    hint_ = new QLabel;
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QString("color:%1;font-size:12px;background:transparent;").arg(ui::colors::TEXT_TERTIARY()));

    if (kind_ == Kind::Symbol) {
        setWindowTitle(QStringLiteral("Add symbol"));
        cap->setText(QStringLiteral("SYMBOL  ·  type to search, click a match or press Enter"));
        input_->setPlaceholderText(QStringLiteral("SPY, Gold, ^MOVE, QO=F, EURUSD=X …"));
        results_ = new QListWidget;
        results_->setFixedHeight(180);
        root->addWidget(results_);
        hint_->setText(QStringLiteral("Any Yahoo Finance symbol: indices start with ^, futures end in =F, "
                                      "currency pairs in =X, coins in -USD."));
        root->addWidget(hint_);
        debounce_ = new QTimer(this);
        debounce_->setSingleShot(true);
        debounce_->setInterval(300);
        connect(debounce_, &QTimer::timeout, this, &ConditionAddDialog::fire_search);
        connect(input_, &QLineEdit::textChanged, this, [this](const QString& t) {
            pending_ = t.trimmed();
            if (pending_.isEmpty())
                results_->clear();
            else
                debounce_->start();
        });
        connect(results_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
            const QString sym = it->data(Qt::UserRole).toString();
            if (!sym.isEmpty())
                accept_symbol(sym, it->data(Qt::UserRole + 1).toString());
        });
        connect(input_, &QLineEdit::returnPressed, this, [this]() {
            auto* cur = results_->currentItem();
            if (cur && !cur->data(Qt::UserRole).toString().isEmpty())
                accept_symbol(cur->data(Qt::UserRole).toString(), cur->data(Qt::UserRole + 1).toString());
            else if (!input_->text().trimmed().isEmpty())
                accept_symbol(input_->text().trimmed().toUpper(), {});
        });
    } else {
        const bool fred = kind_ == Kind::Fred;
        setWindowTitle(fred ? QStringLiteral("Add FRED series") : QStringLiteral("Add Kalshi series"));
        cap->setText(fred ? QStringLiteral("FRED SERIES ID") : QStringLiteral("KALSHI SERIES TICKER"));
        input_->setPlaceholderText(fred ? QStringLiteral("UNRATE, DGS10, BAMLH0A0HYM2 …")
                                        : QStringLiteral("KXFEDDECISION, KXCPIYOY, KXRECSSNBER …"));
        if (fred) {
            yoy_ = new QCheckBox(QStringLiteral("Show as year-on-year % change (for index levels such as CPI)"));
            root->addWidget(yoy_);
            hint_->setText(QStringLiteral("The id from the series' page on fred.stlouisfed.org. Read without an "
                                          "API key through FRED's public download."));
        } else {
            hint_->setText(QStringLiteral("The series ticker from kalshi.com (the part of a market's ticker before "
                                          "the first dash). Its open events are listed, soonest first."));
        }
        root->addWidget(hint_);
        auto* lcap = new QLabel(QStringLiteral("LABEL  ·  optional"));
        lcap->setStyleSheet(caption_ss());
        root->addWidget(lcap);
        label_edit_ = new QLineEdit;
        label_edit_->setStyleSheet(input_ss());
        root->addWidget(label_edit_);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        root->addWidget(bb);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(bb, &QDialogButtonBox::accepted, this, [this, fred]() {
            const QString id = input_->text().trimmed().toUpper();
            if (fred) {
                if (!services::conditions::valid_fred_id(id)) {
                    hint_->setText(QStringLiteral("“%1” is not a FRED series id (capital letters, digits and _).")
                                       .arg(id.toHtmlEscaped()));
                    return;
                }
                symbol_ = QStringLiteral("FRED:") + id + (yoy_->isChecked() ? QStringLiteral("@YOY") : QString());
            } else {
                if (id.isEmpty() || id.contains(QLatin1Char(' '))) {
                    hint_->setText(QStringLiteral("Enter a series ticker such as KXFEDDECISION."));
                    return;
                }
                symbol_ = id;
            }
            label_ = label_edit_->text().trimmed();
            accept();
        });
    }
    if (kind_ == Kind::Symbol) {
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(bb);
    }
    input_->setFocus();
}

void ConditionAddDialog::fire_search() {
    const QString q = pending_;
    if (q.isEmpty())
        return;
    QPointer<ConditionAddDialog> self = this;
    services::equity::EquityResearchService::instance().search_symbols_reply(
        q, 15, QString(), [self, q](services::equity::SymbolSearchReply reply) {
            if (!self || self->pending_ != q)
                return;
            self->results_->clear();
            if (reply.failed) {
                auto* it = new QListWidgetItem(ui::search_failed_text(reply.transient));
                it->setFlags(Qt::NoItemFlags);
                self->results_->addItem(it);
                return;
            }
            for (const auto& r : reply.results) {
                auto* it = new QListWidgetItem(QStringLiteral("%1   %2   %3").arg(r.symbol, r.name, r.exchange));
                it->setData(Qt::UserRole, r.symbol);
                it->setData(Qt::UserRole + 1, r.name);
                self->results_->addItem(it);
            }
            if (reply.results.isEmpty()) {
                auto* it = new QListWidgetItem(QStringLiteral("No matches — press Enter to add “%1” as typed")
                                                   .arg(q.toUpper()));
                it->setFlags(Qt::NoItemFlags);
                self->results_->addItem(it);
            }
        });
}

void ConditionAddDialog::accept_symbol(const QString& sym, const QString& name) {
    symbol_ = sym.trimmed();
    label_ = name.trimmed();
    accept();
}

} // namespace finterm::screens::widgets
