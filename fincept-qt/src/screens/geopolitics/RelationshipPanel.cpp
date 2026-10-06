// src/screens/geopolitics/RelationshipPanel.cpp
#include "screens/geopolitics/RelationshipPanel.h"

#include "ui/theme/Theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>

namespace fincept::screens {

using namespace fincept::services::geo;

// Static reference taxonomy only (which crises/organizations relate to which
// conflicts). Severity ratings and dataset counts used to be hardcoded here
// with no source; they are intentionally left empty (severity "", count 0)
// and are not rendered. The live HDX panel shows real dataset counts.
static QVector<RelationshipNode> build_nodes() {
    QVector<RelationshipNode> nodes;

    // Primary conflicts
    nodes.append({"ukraine", "Ukraine", "conflict", "", 0, {"displacement", "humanitarian", "food_security"}});
    nodes.append({"gaza", "Gaza", "conflict", "", 0, {"displacement", "humanitarian", "health"}});
    nodes.append({"sudan", "Sudan", "conflict", "", 0, {"displacement", "food_security", "humanitarian"}});
    nodes.append({"yemen", "Yemen", "conflict", "", 0, {"food_security", "humanitarian", "health"}});
    nodes.append({"syria", "Syria", "conflict", "", 0, {"displacement", "humanitarian"}});
    nodes.append({"myanmar", "Myanmar", "conflict", "", 0, {"displacement", "humanitarian"}});

    // Crisis types
    nodes.append({"displacement", "Displacement", "crisis", "", 0, {"unhcr"}});
    nodes.append({"food_security", "Food Security", "crisis", "", 0, {"wfp", "fao"}});
    nodes.append({"humanitarian", "Humanitarian", "crisis", "", 0, {"unhcr", "wfp", "who"}});
    nodes.append({"health", "Health Crisis", "crisis", "", 0, {"who"}});

    // Organizations
    nodes.append({"unhcr", "UNHCR", "organization", "", 0, {"displacement", "humanitarian"}});
    nodes.append({"wfp", "WFP", "organization", "", 0, {"food_security", "humanitarian"}});
    nodes.append({"who", "WHO", "organization", "", 0, {"health", "humanitarian"}});
    nodes.append({"fao", "FAO", "organization", "", 0, {"food_security"}});

    return nodes;
}

static QColor severity_color(const QString& sev) {
    if (sev == "critical")
        return QColor(ui::colors::NEGATIVE());
    if (sev == "high")
        return QColor(ui::colors::WARNING());
    if (sev == "medium")
        return QColor(ui::colors::WARNING()).lighter(120);
    if (sev == "low")
        return QColor(ui::colors::POSITIVE());
    return QColor(ui::colors::INFO()); // organization
}

RelationshipPanel::RelationshipPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void RelationshipPanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(48);
    header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    auto* hhl = new QHBoxLayout(header);
    hhl->setContentsMargins(16, 0, 16, 0);
    hhl->setSpacing(12);

    auto* title = new QLabel("GEOPOLITICAL RELATIONSHIP NETWORK", header);
    title->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3; letter-spacing:1px;")
                             .arg(ui::colors::INFO())
                             .arg(ui::fonts::TINY)
                             .arg(ui::fonts::DATA_FAMILY()));
    hhl->addWidget(title);
    hhl->addStretch();

    auto nodes = build_nodes();
    int conflicts = 0, orgs = 0;
    for (const auto& n : nodes) {
        if (n.type == "conflict")
            conflicts++;
        if (n.type == "organization")
            orgs++;
    }

    auto* stats = new QLabel(
        QString("REFERENCE MAP  |  NODES: %1  |  CONFLICTS: %2  |  ORGANIZATIONS: %3")
            .arg(nodes.size())
            .arg(conflicts)
            .arg(orgs),
        header);
    stats->setToolTip("Static reference taxonomy — no live severity or dataset counts. "
                      "Use the HDX tab for live humanitarian datasets.");
    stats->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3; padding:2px 8px;"
                                 "background:rgba(255,255,255,0.04); border:1px solid %4;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::TINY)
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(ui::colors::BORDER_DIM()));
    hhl->addWidget(stats);
    root->addWidget(header);

    // Network view
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QString("QScrollArea { border:none; background:%1; }"
                                  "QScrollBar:vertical { background:%1; width:6px; }"
                                  "QScrollBar::handle:vertical { background:%2; border-radius:3px; }"
                                  "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
                              .arg(ui::colors::BG_BASE(), ui::colors::BORDER_MED()));

    auto* content = new QWidget(scroll);
    content->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* cvl = new QVBoxLayout(content);
    cvl->setContentsMargins(16, 16, 16, 16);
    cvl->setSpacing(16);

    // Helper: add a titled section with a 3-column card grid
    auto add_section = [&](const QString& title, const QString& color, const QString& type) {
        auto* sec_lbl = new QLabel(title, content);
        sec_lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3;"
                                       "letter-spacing:2px; padding-bottom:4px; border-bottom:1px solid rgba(%4,0.3);")
                                   .arg(color)
                                   .arg(ui::fonts::SMALL)
                                   .arg(ui::fonts::DATA_FAMILY)
                                   .arg(color.mid(1)));
        cvl->addWidget(sec_lbl);

        auto* grid_w = new QWidget(content);
        auto* gl = new QGridLayout(grid_w);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setSpacing(10);
        // All 3 columns equal width
        gl->setColumnStretch(0, 1);
        gl->setColumnStretch(1, 1);
        gl->setColumnStretch(2, 1);

        int col = 0, row = 0;
        for (const auto& n : nodes) {
            if (n.type != type)
                continue;
            gl->addWidget(build_node_card(n, grid_w), row, col);
            if (++col >= 3) {
                col = 0;
                row++;
            }
        }
        // Fill remaining cells so columns stay even
        while (col > 0 && col < 3) {
            auto* spacer = new QWidget(grid_w);
            spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            gl->addWidget(spacer, row, col++);
        }

        cvl->addWidget(grid_w);
    };

    add_section("ACTIVE CONFLICTS", ui::colors::NEGATIVE(), "conflict");
    add_section("CRISIS TYPES", ui::colors::WARNING(), "crisis");
    add_section("ORGANIZATIONS", ui::colors::INFO(), "organization");

    cvl->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

QWidget* RelationshipPanel::build_node_card(const RelationshipNode& node, QWidget* parent) {
    // Colour by node type — there is no sourced severity to colour by.
    auto color = node.type == "conflict" ? QColor(ui::colors::NEGATIVE())
                 : node.type == "crisis" ? QColor(ui::colors::WARNING())
                                         : severity_color(node.severity.isEmpty() ? "organization" : node.severity);
    const QString col_hex = color.name();
    const QString col_rgb = QString("%1,%2,%3").arg(color.red()).arg(color.green()).arg(color.blue());

    auto* card = new QWidget(parent);
    card->setObjectName("nodeCard");
    card->setMinimumHeight(110);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setStyleSheet(QString("#nodeCard { background:%1; border:1px solid rgba(%2,0.25);"
                                "border-left:3px solid %3; }")
                            .arg(ui::colors::BG_RAISED())
                            .arg(col_rgb)
                            .arg(col_hex));

    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(12, 10, 12, 10);
    vl->setSpacing(4);

    // Name row
    auto* name_row = new QWidget(card);
    auto* name_hl = new QHBoxLayout(name_row);
    name_hl->setContentsMargins(0, 0, 0, 0);
    name_hl->setSpacing(6);

    auto* name = new QLabel(node.label.toUpper(), name_row);
    name->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3;")
                            .arg(col_hex)
                            .arg(ui::fonts::SMALL)
                            .arg(ui::fonts::DATA_FAMILY));
    name_hl->addWidget(name, 1);

    const QString badge_text = node.type.toUpper();
    auto* badge = new QLabel(badge_text, name_row);
    badge->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3;"
                                 "padding:2px 6px; background:rgba(%4,0.12); border:1px solid rgba(%4,0.3);"
                                 "border-radius:8px; letter-spacing:1px;")
                             .arg(col_hex)
                             .arg(ui::fonts::TINY)
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(col_rgb));
    name_hl->addWidget(badge);
    vl->addWidget(name_row);

    // Type label
    auto* type_lbl = new QLabel(node.type.toUpper(), card);
    type_lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                                .arg(ui::colors::TEXT_SECONDARY())
                                .arg(ui::fonts::TINY)
                                .arg(ui::fonts::DATA_FAMILY));
    vl->addWidget(type_lbl);

    // Dataset counts are not shown: they were hardcoded, not fetched. See the
    // HDX DATA tab for live dataset counts.

    // Connection pills (max 3)
    if (!node.connections.isEmpty()) {
        auto* pills_row = new QWidget(card);
        auto* pills_hl = new QHBoxLayout(pills_row);
        pills_hl->setContentsMargins(0, 2, 0, 0);
        pills_hl->setSpacing(4);

        const int max_pills = 3;
        int shown = qMin(node.connections.size(), max_pills);
        for (int i = 0; i < shown; ++i) {
            auto* pill = new QLabel(node.connections[i], pills_row);
            pill->setStyleSheet(QString("color:%1; font-size:%2px; padding:1px 6px;"
                                        "background:rgba(255,255,255,0.05); border:1px solid %3;"
                                        "border-radius:8px; font-family:%4;")
                                    .arg(ui::colors::TEXT_SECONDARY())
                                    .arg(ui::fonts::TINY)
                                    .arg(ui::colors::BORDER_DIM())
                                    .arg(ui::fonts::DATA_FAMILY));
            pills_hl->addWidget(pill);
        }

        if (node.connections.size() > max_pills) {
            auto* more = new QLabel(QString("+%1").arg(node.connections.size() - max_pills), pills_row);
            more->setStyleSheet(QString("color:%1; font-size:%2px; padding:1px 6px;"
                                        "background:rgba(255,255,255,0.05); border:1px solid %3;"
                                        "border-radius:8px; font-family:%4;")
                                    .arg(ui::colors::TEXT_SECONDARY())
                                    .arg(ui::fonts::TINY)
                                    .arg(ui::colors::BORDER_DIM())
                                    .arg(ui::fonts::DATA_FAMILY));
            pills_hl->addWidget(more);
        }

        pills_hl->addStretch();
        vl->addWidget(pills_row);
    }

    return card;
}

} // namespace fincept::screens
