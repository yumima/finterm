#include "screens/ownership/FirmBookPanel.h"

#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

QString action_colour(const QString& action) {
    if (action == QLatin1String("new") || action == QLatin1String("added"))
        return ui::colors::GREEN();
    if (action == QLatin1String("trimmed") || action == QLatin1String("exited"))
        return ui::colors::RED();
    return ui::colors::TEXT_SECONDARY();
}

QTableWidgetItem* cell(const QString& text, const QString& colour = {}) {
    auto* it = new QTableWidgetItem(text);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

QTableWidget* make_table(const QStringList& headers, int min_height) {
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setMinimumHeight(min_height);
    return t;
}

} // namespace

FirmBookPanel::FirmBookPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    firm_ = new QComboBox;
    firm_->setMinimumWidth(260);
    bar->addWidget(firm_);

    load_btn_ = new QPushButton(QStringLiteral("LOAD"));
    load_btn_->setToolTip(QStringLiteral("Reads this manager's latest two 13F filings "
                                         "from EDGAR."));
    connect(load_btn_, &QPushButton::clicked, this, [this]() {
        const QString cik = firm_->currentData().toString();
        if (!cik.isEmpty()) {
            services::OwnershipService::instance().load_book(cik);
            render();
        }
    });
    bar->addWidget(load_btn_);

    edit_btn_ = new QPushButton(QStringLiteral("EDIT"));
    edit_btn_->setToolTip(QStringLiteral("Choose which firms are tracked."));
    connect(edit_btn_, &QPushButton::clicked, this, [this]() {
        auto& svc = services::OwnershipService::instance();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Tracked 13F managers"));
        auto* v = new QVBoxLayout(&dlg);

        auto* help = new QLabel(QStringLiteral(
            "One firm per line: <b>Name | CIK | style</b>. CIK may be left blank — it is "
            "resolved from EDGAR on first use and written back here.<br><br>"
            "The <i>style</i> is not decoration. A position weight in a concentrated long-only "
            "book is a conviction statement; the same weight in a hedged multi-strategy book is "
            "one leg of a position, because 13F shows only the long equity side. The screen "
            "labels rows by this, so getting it wrong makes the numbers read as more than they "
            "are.<br><br>Saved to:<br><code>%1</code>")
                .arg(services::OwnershipService::managers_file_path()));
        help->setWordWrap(true);
        help->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(help);

        auto* edit = new QPlainTextEdit;
        edit->setMinimumSize(560, 320);
        QStringList lines;
        for (const auto& m : svc.managers())
            lines << QStringLiteral("%1 | %2 | %3").arg(m.name, m.cik, m.style);
        edit->setPlainText(lines.join(QLatin1Char('\n')));
        edit->setPlaceholderText(
            QStringLiteral("Berkshire Hathaway Inc | 0001067983 | concentrated long\n"
                           "Baupost Group LLC |  | concentrated long"));
        v->addWidget(edit, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults);
        v->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
            QVector<Manager> list;
            for (const QString& line : edit->toPlainText().split(QLatin1Char('\n'))) {
                const QString t = line.trimmed();
                if (t.isEmpty())
                    continue;
                const QStringList parts = t.split(QLatin1Char('|'));
                Manager m;
                m.name = parts.value(0).trimmed();
                m.cik = parts.value(1).trimmed();
                m.style = parts.value(2).trimmed();
                if (!m.name.isEmpty())
                    list.push_back(m);
            }
            svc.set_managers(list);
            dlg.accept();
        });
        connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
                &dlg, [&]() {
                    // An empty list deletes the override, so the curated
                    // defaults shipped with the script apply again.
                    svc.set_managers({});
                    dlg.accept();
                });

        if (dlg.exec() == QDialog::Accepted)
            reload_managers();
    });
    bar->addWidget(edit_btn_);

    seed_btn_ = new QPushButton(QStringLiteral("LOAD FIRMS"));
    seed_btn_->setToolTip(QStringLiteral("Fetch the curated manager list and resolve each "
                                         "firm's CIK from EDGAR. Runs once."));
    connect(seed_btn_, &QPushButton::clicked, this, [this]() {
        seed_btn_->setEnabled(false);
        seed_btn_->setText(QStringLiteral("RESOLVING…"));
        services::OwnershipService::instance().seed_default_managers();
    });
    bar->addWidget(seed_btn_);

    status_ = new QLabel;
    bar->addWidget(status_, 1);
    root->addLayout(bar);

    caveat_ = new QLabel(QStringLiteral(
        "13F covers long US equities only — no shorts, no bonds, no cash, no leverage — so these "
        "weights are shares of the manager's disclosed equity book, not of their fund. Filed 45 "
        "days after quarter end."));
    caveat_->setWordWrap(true);
    caveat_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(caveat_);

    auto* pos_lbl = new QLabel(QStringLiteral("POSITIONS"));
    pos_lbl->setStyleSheet(QString("color:%1;font-weight:700;letter-spacing:1px;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(pos_lbl);
    positions_ = make_table({QStringLiteral("Issuer"), QStringLiteral("Class"),
                             QStringLiteral("% of book"), QStringLiteral("Shares"),
                             QStringLiteral("Value")}, 200);
    connect(positions_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* it = positions_->item(row, 0))
            emit navigate_to_symbol(it->text());
    });
    root->addWidget(positions_, 2);

    auto* mv_lbl = new QLabel(QStringLiteral("MOVES — LATEST QUARTER"));
    mv_lbl->setStyleSheet(QString("color:%1;font-weight:700;letter-spacing:1px;")
                              .arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(mv_lbl);
    moves_ = make_table({QStringLiteral("Action"), QStringLiteral("Issuer"),
                         QStringLiteral("Shares now"), QStringLiteral("Change"),
                         QStringLiteral("% change")}, 150);
    root->addWidget(moves_, 1);

    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::book_updated, this, [this](const QString& cik) {
                if (cik == firm_->currentData().toString())
                    render();
            });
    connect(firm_, &QComboBox::currentIndexChanged, this, [this](int) { render(); });
    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::managers_changed, this, [this]() {
                seed_btn_->setEnabled(true);
                seed_btn_->setText(QStringLiteral("LOAD FIRMS"));
                reload_managers();
                render();
            });

    reload_managers();
    render();
}

void FirmBookPanel::reload_managers() {
    const QString keep = firm_->currentData().toString();
    firm_->clear();
    const auto managers = services::OwnershipService::instance().managers();
    if (managers.isEmpty()) {
        // The user list is only written once a smart-money fetch has resolved
        // the shipped defaults. Say so rather than showing an empty dropdown
        // that looks broken.
        firm_->addItem(QStringLiteral("No firms yet — press LOAD DEFAULT FIRMS"),
                       QString());
        firm_->setEnabled(false);
        seed_btn_->setVisible(true);
        return;
    }
    firm_->setEnabled(true);
    seed_btn_->setVisible(false);
    for (const auto& m : managers) {
        const QString label = m.style.isEmpty()
                                  ? m.name
                                  : QStringLiteral("%1  —  %2").arg(m.name, m.style);
        firm_->addItem(label, m.cik);
    }
    const int idx = firm_->findData(keep);
    if (idx >= 0)
        firm_->setCurrentIndex(idx);
}

void FirmBookPanel::render() {
    auto& svc = services::OwnershipService::instance();
    const QString cik = firm_->currentData().toString();
    load_btn_->setEnabled(!cik.isEmpty());

    if (cik.isEmpty()) {
        status_->setText(QStringLiteral("No firm selected."));
        positions_->setRowCount(0);
        moves_->setRowCount(0);
        return;
    }

    const auto b = svc.book(cik);
    if (svc.is_book_loading(cik)) {
        status_->setText(QStringLiteral("Reading filings from EDGAR…"));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    } else if (!b.error.isEmpty()) {
        status_->setText(b.error);
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::RED()));
    } else if (b.positions.isEmpty()) {
        status_->setText(QStringLiteral("Not loaded."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    } else {
        status_->setText(QStringLiteral("%1 positions · %2 book · quarter ending %3 · filed %4")
                             .arg(b.position_count)
                             .arg(fmt::format_money(b.total_value),
                                  b.period.toString(QStringLiteral("d MMM yyyy")),
                                  b.filed_date.toString(QStringLiteral("d MMM yyyy"))));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));
        // How the filing's values were read. SEC moved 13F from thousands to
        // whole dollars in 2023 and a misread is a silent 1000x error, so the
        // interpretation is shown rather than assumed.
        if (!b.value_basis.isEmpty())
            status_->setToolTip(QStringLiteral("Values read as: ") + b.value_basis);
    }

    positions_->setRowCount(b.positions.size());
    for (int i = 0; i < b.positions.size(); ++i) {
        const auto& p = b.positions[i];
        positions_->setItem(i, 0, cell(p.issuer));
        positions_->setItem(i, 1, cell(p.security_class));
        positions_->setItem(i, 2, cell(p.weight ? fmt::format_percent(*p.weight * 100.0)
                                                : fmt::placeholder(),
                                       ui::colors::AMBER()));
        positions_->setItem(i, 3, cell(p.shares ? fmt::format_compact(*p.shares)
                                                : fmt::placeholder()));
        positions_->setItem(i, 4, cell(p.value ? fmt::format_money(*p.value)
                                               : fmt::placeholder()));
    }

    moves_->setRowCount(b.moves.size());
    for (int i = 0; i < b.moves.size(); ++i) {
        const auto& m = b.moves[i];
        moves_->setItem(i, 0, cell(m.action, action_colour(m.action)));
        moves_->setItem(i, 1, cell(m.issuer));
        moves_->setItem(i, 2, cell(m.shares ? fmt::format_compact(*m.shares)
                                            : fmt::placeholder()));
        QString delta = fmt::placeholder();
        if (m.shares_delta) {
            delta = QStringLiteral("%1%2")
                        .arg(*m.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                             fmt::format_compact(std::abs(*m.shares_delta)));
        }
        moves_->setItem(i, 3, cell(delta, action_colour(m.action)));
        moves_->setItem(i, 4, cell(m.pct_change ? fmt::format_percent(*m.pct_change * 100.0, 1, true)
                                                : fmt::placeholder()));
    }
}

} // namespace fincept::screens
