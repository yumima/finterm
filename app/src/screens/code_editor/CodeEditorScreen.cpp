// src/screens/code_editor/CodeEditorScreen.cpp
#include "screens/code_editor/CodeEditorScreen.h"

#include "core/keys/KeyConfigManager.h"
#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "services/file_manager/FileManagerService.h"
#include "ui/theme/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>

namespace finterm::screens {

using namespace finterm::ui;

// ═════════════════════════════════════════════════════════════════════════════
// CodeTextEdit — editor with keyboard shortcuts
// ═════════════════════════════════════════════════════════════════════════════

CodeTextEdit::CodeTextEdit(QWidget* parent) : QTextEdit(parent) {}

void CodeTextEdit::keyPressEvent(QKeyEvent* event) {
    auto& km = KeyConfigManager::instance();
    const QKeySequence pressed(event->keyCombination());

    if (pressed == km.key(KeyAction::RunCell)) {
        emit run_shortcut();
        return;
    }
    if (pressed == km.key(KeyAction::RunAndNext)) {
        emit run_and_next();
        return;
    }
    // Tab inserts 4 spaces
    if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ShiftModifier)) {
        insertPlainText("    ");
        return;
    }
    QTextEdit::keyPressEvent(event);
}

// ═════════════════════════════════════════════════════════════════════════════
// CellWidget
// ═════════════════════════════════════════════════════════════════════════════

CellWidget::CellWidget(const NotebookCell& cell, int index, QWidget* parent)
    : QWidget(parent),
      cell_id_(cell.id),
      cell_type_(cell.cell_type),
      execution_count_(cell.execution_count),
      index_(index) {
    build_ui();
    set_cell_data(cell);
}

void CellWidget::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 2, 0, 2);
    root->setSpacing(0);

    // ── Main cell row: [gutter] [editor] ──
    auto* cell_row = new QWidget(this);
    auto* row_layout = new QHBoxLayout(cell_row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(0);

    // ── Gutter ──
    gutter_ = new QWidget(cell_row);
    gutter_->setFixedWidth(56);
    gutter_->setStyleSheet(QString("background:%1;").arg(colors::BG_RAISED()));
    auto* gutter_layout = new QVBoxLayout(gutter_);
    gutter_layout->setContentsMargins(0, 4, 6, 2);
    gutter_layout->setSpacing(2);
    gutter_layout->setAlignment(Qt::AlignTop | Qt::AlignRight);

    gutter_number_ = new QLabel("[ ]", gutter_);
    gutter_number_->setAlignment(Qt::AlignRight | Qt::AlignTop);
    gutter_number_->setStyleSheet(
        QString("color:%1; font-family:%2; font-size:%3px; font-weight:700; background:transparent;")
            .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY)
            .arg(fonts::TINY));
    gutter_layout->addWidget(gutter_number_);

    gutter_type_ = new QLabel("PY", gutter_);
    gutter_type_->setAlignment(Qt::AlignRight);
    gutter_type_->setStyleSheet(QString("color:%1; font-family:%2; font-size:12px; font-weight:600;"
                                        " letter-spacing:0.5px; background:transparent;")
                                    .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY));
    gutter_layout->addWidget(gutter_type_);
    gutter_layout->addStretch();

    row_layout->addWidget(gutter_);

    // ── Editor container ──
    editor_container_ = new QWidget(cell_row);
    auto* editor_vbox = new QVBoxLayout(editor_container_);
    editor_vbox->setContentsMargins(0, 0, 0, 0);
    editor_vbox->setSpacing(0);

    // Toolbar — always occupies 24px to prevent layout shift; buttons hide/show
    toolbar_ = new QWidget(editor_container_);
    toolbar_->setFixedHeight(24);
    toolbar_->setStyleSheet(QString("background:%1;").arg(colors::BG_RAISED()));
    auto* tb_layout = new QHBoxLayout(toolbar_);
    tb_layout->setContentsMargins(8, 0, 8, 0);
    tb_layout->setSpacing(2);

    auto make_tool_btn = [&](const QString& text, const QString& fg_color) -> QPushButton* {
        auto* btn = new QPushButton(text, toolbar_);
        btn->setFixedHeight(20);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setVisible(false); // hidden until hover
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:none;"
                                   " font-family:%2; font-size:%3px; font-weight:600; padding:0 8px;"
                                   " letter-spacing:0.5px; }"
                                   "QPushButton:hover { background:%4; color:%5; }")
                               .arg(fg_color, fonts::DATA_FAMILY)
                               .arg(fonts::TINY)
                               .arg(colors::BG_HOVER(), colors::TEXT_PRIMARY()));
        return btn;
    };

    auto* run_btn = make_tool_btn("RUN", colors::POSITIVE);
    connect(run_btn, &QPushButton::clicked, this, [this]() { emit run_requested(cell_id_); });
    tb_layout->addWidget(run_btn);

    auto* type_btn = make_tool_btn("TYPE", colors::CYAN);
    connect(type_btn, &QPushButton::clicked, this, [this]() { emit toggle_type_requested(cell_id_); });
    tb_layout->addWidget(type_btn);

    auto* up_btn = make_tool_btn("UP", colors::TEXT_SECONDARY);
    connect(up_btn, &QPushButton::clicked, this, [this]() { emit move_up_requested(cell_id_); });
    tb_layout->addWidget(up_btn);

    auto* dn_btn = make_tool_btn("DN", colors::TEXT_SECONDARY);
    connect(dn_btn, &QPushButton::clicked, this, [this]() { emit move_down_requested(cell_id_); });
    tb_layout->addWidget(dn_btn);

    tb_layout->addStretch();

    auto* del_btn = make_tool_btn("DEL", colors::NEGATIVE);
    connect(del_btn, &QPushButton::clicked, this, [this]() { emit delete_requested(cell_id_); });
    tb_layout->addWidget(del_btn);

    editor_vbox->addWidget(toolbar_);

    // ── Code editor ──
    editor_ = new CodeTextEdit(editor_container_);
    editor_->setAcceptRichText(false);
    editor_->setTabStopDistance(28);
    editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor_->setLineWrapMode(QTextEdit::NoWrap);
    editor_->setStyleSheet(QString("QTextEdit { background:%1; color:%2; border:none;"
                                   " font-family:%3; font-size:%4px; padding:2px 6px;"
                                   " selection-background-color:%5; }"
                                   "QScrollBar:vertical { background:%1; width:5px; }"
                                   "QScrollBar::handle:vertical { background:%6; min-height:20px; }"
                                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
                                   "QScrollBar:horizontal { background:%1; height:5px; }"
                                   "QScrollBar::handle:horizontal { background:%6; min-width:20px; }"
                                   "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }")
                               .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), fonts::DATA_FAMILY)
                               .arg(fonts::DATA)
                               .arg(colors::AMBER_DIM(), colors::BORDER_BRIGHT()));
    editor_->document()->setDocumentMargin(2);

    // Keyboard shortcuts
    connect(editor_, &CodeTextEdit::run_shortcut, this, [this]() { emit run_requested(cell_id_); });
    connect(editor_, &CodeTextEdit::run_and_next, this, [this]() { emit run_and_advance(cell_id_); });

    // Responsive height on every content change
    connect(editor_->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this](const QSizeF&) { adjust_editor_height(); });

    editor_vbox->addWidget(editor_);

    // ── Markdown preview (for MD cells, shown when not editing) ──
    md_preview_ = new QTextBrowser(editor_container_);
    md_preview_->setOpenExternalLinks(true);
    md_preview_->setReadOnly(true);
    md_preview_->setVisible(false);
    md_preview_->setStyleSheet(QString("QTextBrowser { background:%1; color:%2; border:none;"
                                       " font-family:%3; font-size:%4px; padding:10px 12px; }"
                                       "QScrollBar:vertical { background:%1; width:5px; }"
                                       "QScrollBar::handle:vertical { background:%5; min-height:20px; }"
                                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
                                   .arg(colors::BG_SURFACE(), colors::TEXT_PRIMARY(), fonts::DATA_FAMILY)
                                   .arg(fonts::DATA)
                                   .arg(colors::BORDER_BRIGHT()));

    // Click on preview to edit
    md_preview_->installEventFilter(this);

    editor_vbox->addWidget(md_preview_);

    row_layout->addWidget(editor_container_, 1);
    root->addWidget(cell_row);

    // ── Output area ──
    auto* output_row = new QWidget(this);
    auto* output_row_layout = new QHBoxLayout(output_row);
    output_row_layout->setContentsMargins(0, 0, 0, 0);
    output_row_layout->setSpacing(0);

    auto* output_gutter = new QWidget(output_row);
    output_gutter->setFixedWidth(56);
    output_row_layout->addWidget(output_gutter);

    output_area_ = new QWidget(output_row);
    auto* output_vbox = new QVBoxLayout(output_area_);
    output_vbox->setContentsMargins(0, 0, 0, 0);
    output_vbox->setSpacing(0);

    // Output header with collapse toggle
    output_toggle_ = new QPushButton("OUTPUT", output_area_);
    output_toggle_->setFixedHeight(22);
    output_toggle_->setCursor(Qt::PointingHandCursor);
    output_toggle_->setStyleSheet(
        QString("QPushButton { background:%1; color:%2; border:none; border-left:3px solid %3;"
                " font-family:%4; font-size:12px; font-weight:700; letter-spacing:0.5px;"
                " padding:0 10px; text-align:left; }"
                "QPushButton:hover { background:%5; }")
            .arg(colors::BG_RAISED(), colors::POSITIVE(), colors::BORDER_DIM(), fonts::DATA_FAMILY,
                 colors::BG_HOVER()));
    output_toggle_->setVisible(false);
    connect(output_toggle_, &QPushButton::clicked, this, [this]() {
        output_collapsed_ = !output_collapsed_;
        output_content_->setVisible(!output_collapsed_);
        output_toggle_->setText(output_collapsed_ ? "OUTPUT [collapsed]" : "OUTPUT");
    });
    output_vbox->addWidget(output_toggle_);

    // Scrollable output content
    output_content_ = new QWidget(output_area_);
    output_content_layout_ = new QVBoxLayout(output_content_);
    output_content_layout_->setContentsMargins(12, 6, 12, 8);
    output_content_layout_->setSpacing(4);
    output_content_->setStyleSheet(
        QString("background:%1; border-left:3px solid %2;").arg(colors::BG_BASE(), colors::BORDER_DIM()));
    output_content_->setVisible(false);
    output_vbox->addWidget(output_content_);

    output_area_->setVisible(false);
    output_row_layout->addWidget(output_area_, 1);
    root->addWidget(output_row);

    // ── Insert-below: always-visible dim "+" centered between cells ──
    insert_hint_ = new QWidget(this);
    insert_hint_->setFixedHeight(20);
    auto* insert_layout = new QHBoxLayout(insert_hint_);
    insert_layout->setContentsMargins(56, 0, 0, 0);
    insert_layout->setSpacing(0);

    auto* insert_line = new QWidget(insert_hint_);
    insert_line->setFixedHeight(1);
    insert_line->setStyleSheet(QString("background:%1;").arg(colors::BORDER_DIM()));
    insert_layout->addWidget(insert_line, 1);

    auto* insert_btn = new QPushButton("+", insert_hint_);
    insert_btn->setFixedSize(32, 18);
    insert_btn->setCursor(Qt::PointingHandCursor);
    insert_btn->setObjectName("insertBtn");
    insert_btn->setStyleSheet(QString("QPushButton { color:%1; font-family:%2; font-size:12px; font-weight:700;"
                                      " background:%3; border:1px solid %4; }"
                                      "QPushButton:hover { background:%5; color:%6; border-color:%5; }")
                                  .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY, colors::BG_SURFACE(),
                                       colors::BORDER_DIM(), colors::AMBER_DIM(), colors::AMBER()));
    connect(insert_btn, &QPushButton::clicked, this, [this]() { emit insert_below_requested(cell_id_); });
    insert_layout->addWidget(insert_btn);

    auto* insert_line2 = new QWidget(insert_hint_);
    insert_line2->setFixedHeight(1);
    insert_line2->setStyleSheet(QString("background:%1;").arg(colors::BORDER_DIM()));
    insert_layout->addWidget(insert_line2, 1);

    root->addWidget(insert_hint_);
    setMouseTracking(true);
}

void CellWidget::adjust_editor_height() {
    if (!editor_->isVisible())
        return;
    // Exact content height from the layout engine + CSS padding (2+2)
    int doc_h = static_cast<int>(editor_->document()->size().height());
    int target = doc_h + 6;
    int max_h = (cell_type_ == "code") ? 600 : 300;
    int h = qBound(28, target, max_h);
    editor_->setFixedHeight(h);
}

void CellWidget::set_cell_data(const NotebookCell& cell) {
    cell_id_ = cell.id;
    cell_type_ = cell.cell_type;
    execution_count_ = cell.execution_count;
    running_ = cell.running;

    editor_->blockSignals(true);
    editor_->setPlainText(cell.source);
    given_ = cell.source;
    given_shown_ = editor_->toPlainText();
    editor_->blockSignals(false);

    // Force document layout so height measurement is accurate
    editor_->document()->adjustSize();
    adjust_editor_height();
    update_gutter();

    // For markdown cells, show rendered preview by default
    if (cell_type_ == "markdown" && !cell.source.trimmed().isEmpty()) {
        md_editing_ = false;
        editor_->setVisible(false);
        md_preview_->setVisible(true);
        render_markdown();
    } else {
        md_editing_ = true;
        editor_->setVisible(true);
        md_preview_->setVisible(false);
    }

    if (!cell.outputs.isEmpty()) {
        set_outputs(cell.outputs, cell.execution_count);
    }
}

QString CellWidget::source() const {
    const QString shown = editor_->toPlainText();
    return shown == given_shown_ ? given_ : shown;
}

void CellWidget::set_running(bool running) {
    running_ = running;
    update_gutter();
}

void CellWidget::set_outputs(const QVector<CellOutput>& outputs, int exec_count) {
    execution_count_ = exec_count;
    update_gutter();

    // Clear old output content
    while (output_content_layout_->count() > 0) {
        auto* item = output_content_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (outputs.isEmpty()) {
        output_area_->setVisible(false);
        output_toggle_->setVisible(false);
        output_content_->setVisible(false);
        return;
    }

    output_area_->setVisible(true);
    output_toggle_->setVisible(true);
    output_toggle_->setText(QString("OUT [%1]").arg(exec_count));
    output_collapsed_ = false;
    output_content_->setVisible(true);

    for (const auto& out : outputs) {
        if (out.type == "stream" || out.type == "execute_result") {
            // Use QTextEdit in read-only mode for proper scrolling of long output
            auto* output_view = new QTextEdit(output_content_);
            output_view->setReadOnly(true);
            output_view->setPlainText(out.text);
            output_view->setLineWrapMode(QTextEdit::WidgetWidth);
            output_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            output_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

            QString fg = (out.name == "stderr") ? colors::WARNING : colors::TEXT_PRIMARY;
            output_view->setStyleSheet(
                QString("QTextEdit { background:transparent; color:%1; border:none;"
                        " font-family:%2; font-size:%3px; padding:2px 0; }"
                        "QScrollBar:vertical { background:transparent; width:4px; }"
                        "QScrollBar::handle:vertical { background:%4; }"
                        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
                    .arg(fg, fonts::DATA_FAMILY)
                    .arg(fonts::DATA)
                    .arg(colors::BORDER_BRIGHT()));

            // Size to content, max 300px then scroll
            int lines = out.text.count('\n') + 1;
            int line_h = QFontMetrics(output_view->font()).lineSpacing();
            int content_h = lines * line_h + 10;
            int clamped = qBound(30, content_h, 300);
            output_view->setFixedHeight(clamped);

            output_content_layout_->addWidget(output_view);

        } else if (out.type == "error") {
            auto* err_widget = new QWidget(output_content_);
            err_widget->setStyleSheet(QString("background:#1a0808; border-left:2px solid %1;").arg(colors::NEGATIVE()));
            auto* err_layout = new QVBoxLayout(err_widget);
            err_layout->setContentsMargins(10, 6, 10, 6);
            err_layout->setSpacing(4);

            auto* err_header = new QLabel(QString("%1: %2").arg(out.error_name, out.error_value), err_widget);
            err_header->setWordWrap(true);
            err_header->setStyleSheet(QString("color:%1; font-family:%2; font-size:%3px; font-weight:700;")
                                          .arg(colors::NEGATIVE(), fonts::DATA_FAMILY)
                                          .arg(fonts::DATA));
            err_layout->addWidget(err_header);

            if (!out.traceback.isEmpty()) {
                auto* tb_view = new QTextEdit(err_widget);
                tb_view->setReadOnly(true);
                tb_view->setPlainText(out.traceback.join("\n"));
                tb_view->setLineWrapMode(QTextEdit::WidgetWidth);
                tb_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                tb_view->setStyleSheet(
                    QString("QTextEdit { background:transparent; color:%1; border:none;"
                            " font-family:%2; font-size:%3px; }"
                            "QScrollBar:vertical { background:transparent; width:4px; }"
                            "QScrollBar::handle:vertical { background:%4; }"
                            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }")
                        .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY)
                        .arg(fonts::TINY)
                        .arg(colors::BORDER_BRIGHT()));

                int tb_lines = out.traceback.join("\n").count('\n') + 1;
                int tb_h = qBound(40, tb_lines * 16 + 10, 200);
                tb_view->setFixedHeight(tb_h);
                err_layout->addWidget(tb_view);
            }
            output_content_layout_->addWidget(err_widget);
        }
    }
}

void CellWidget::set_selected(bool selected) {
    selected_ = selected;

    // Selected: amber left accent, brighter surface. Unselected: dim border, standard surface.
    QString border = selected ? colors::AMBER : colors::BORDER_MED;
    QString bg = selected ? "#0e0e0e" : colors::BG_SURFACE();
    editor_container_->setStyleSheet(QString("background:%1; border-left:3px solid %2; border-top:1px solid %3;"
                                             " border-bottom:1px solid %3; border-right:1px solid %3;")
                                         .arg(bg, border, colors::BORDER_DIM()));

    // Gutter matches cell background
    QString gutter_bg = selected ? "#0e0e0e" : colors::BG_RAISED();
    gutter_->setStyleSheet(QString("background:%1;").arg(gutter_bg));

    QString num_color = selected ? colors::AMBER : colors::TEXT_TERTIARY;
    gutter_number_->setStyleSheet(
        QString("color:%1; font-family:%2; font-size:%3px; font-weight:700; background:transparent;")
            .arg(num_color, fonts::DATA_FAMILY)
            .arg(fonts::TINY));

    // For markdown: if selecting, switch to edit mode; if deselecting, render
    if (cell_type_ == "markdown") {
        if (selected && !md_editing_) {
            md_editing_ = true;
            md_preview_->setVisible(false);
            editor_->setVisible(true);
            editor_->setFocus();
            adjust_editor_height();
        } else if (!selected && md_editing_) {
            md_editing_ = false;
            editor_->setVisible(false);
            md_preview_->setVisible(true);
            render_markdown();
        }
    }
}

void CellWidget::update_gutter() {
    if (running_) {
        gutter_number_->setText("[*]");
        gutter_number_->setStyleSheet(QString("color:%1; font-family:%2; font-size:%3px; font-weight:700;")
                                          .arg(colors::WARNING(), fonts::DATA_FAMILY)
                                          .arg(fonts::TINY));
    } else if (execution_count_ > 0) {
        gutter_number_->setText(QString("[%1]").arg(execution_count_));
        gutter_number_->setStyleSheet(QString("color:%1; font-family:%2; font-size:%3px; font-weight:700;")
                                          .arg(selected_ ? colors::AMBER() : colors::POSITIVE(), fonts::DATA_FAMILY)
                                          .arg(fonts::TINY));
    } else {
        gutter_number_->setText(QString("[%1]").arg(index_ + 1));
        gutter_number_->setStyleSheet(
            QString("color:%1; font-family:%2; font-size:%3px; font-weight:700;")
                .arg(selected_ ? colors::AMBER() : colors::TEXT_SECONDARY(), fonts::DATA_FAMILY)
                .arg(fonts::TINY));
    }

    if (cell_type_ == "markdown") {
        gutter_type_->setText("MD");
        gutter_type_->setStyleSheet(
            QString("color:%1; font-family:%2; font-size:12px; font-weight:600; letter-spacing:0.5px;")
                .arg(colors::INFO(), fonts::DATA_FAMILY));
    } else {
        gutter_type_->setText("PY");
        gutter_type_->setStyleSheet(
            QString("color:%1; font-family:%2; font-size:12px; font-weight:600; letter-spacing:0.5px;")
                .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY));
    }
}

void CellWidget::render_markdown() {
    QString src = editor_->toPlainText();
    if (src.trimmed().isEmpty()) {
        md_preview_->setHtml(QString("<p style='color:%1; font-style:italic;'>Empty markdown cell — click to edit</p>")
                                 .arg(colors::TEXT_SECONDARY()));
        md_preview_->setMinimumHeight(48);
        md_preview_->setMaximumHeight(48);
        return;
    }

    // Convert basic markdown to HTML for rendering
    // Handles: headers, bold, italic, code blocks, inline code, lists, links, horizontal rules
    QString html;
    html +=
        QString("<style>"
                "body { color:%1; font-family:%2; font-size:%3px; }"
                "h1 { color:%4; font-size:20px; font-weight:700; margin:8px 0 4px; border-bottom:1px solid %5; "
                "padding-bottom:4px; }"
                "h2 { color:%4; font-size:17px; font-weight:700; margin:8px 0 4px; }"
                "h3 { color:%4; font-size:15px; font-weight:700; margin:6px 0 3px; }"
                "h4 { color:%6; font-size:14px; font-weight:600; margin:4px 0 2px; }"
                "code { background:%7; color:%4; padding:2px 4px; font-family:%2; }"
                "pre { background:%7; color:%1; padding:8px; font-family:%2; font-size:13px;"
                "      border-left:2px solid %5; margin:6px 0; }"
                "a { color:%8; }"
                "strong { color:%1; font-weight:700; }"
                "em { color:%6; font-style:italic; }"
                "ul, ol { margin:4px 0; padding-left:20px; }"
                "li { margin:2px 0; }"
                "hr { border:none; border-top:1px solid %5; margin:8px 0; }"
                "blockquote { border-left:3px solid %4; padding-left:10px; color:%6; margin:6px 0; }"
                "</style>")
            .arg(colors::TEXT_PRIMARY(), fonts::DATA_FAMILY)
            .arg(fonts::DATA)
            .arg(colors::AMBER(), colors::BORDER_MED(), colors::TEXT_SECONDARY(), colors::BG_RAISED(), colors::CYAN());

    bool in_code_block = false;
    QString code_block;
    QStringList lines = src.split('\n');

    for (const auto& line : lines) {
        // Fenced code blocks
        if (line.trimmed().startsWith("```")) {
            if (in_code_block) {
                html += "<pre>" + code_block.toHtmlEscaped() + "</pre>";
                code_block.clear();
                in_code_block = false;
            } else {
                in_code_block = true;
            }
            continue;
        }
        if (in_code_block) {
            if (!code_block.isEmpty())
                code_block += "\n";
            code_block += line;
            continue;
        }

        QString trimmed = line.trimmed();

        // Horizontal rule
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            html += "<hr>";
            continue;
        }
        // Headers
        if (trimmed.startsWith("#### ")) {
            html += "<h4>" + trimmed.mid(5).toHtmlEscaped() + "</h4>";
            continue;
        }
        if (trimmed.startsWith("### ")) {
            html += "<h3>" + trimmed.mid(4).toHtmlEscaped() + "</h3>";
            continue;
        }
        if (trimmed.startsWith("## ")) {
            html += "<h2>" + trimmed.mid(3).toHtmlEscaped() + "</h2>";
            continue;
        }
        if (trimmed.startsWith("# ")) {
            html += "<h1>" + trimmed.mid(2).toHtmlEscaped() + "</h1>";
            continue;
        }
        // Blockquote
        if (trimmed.startsWith("> ")) {
            html += "<blockquote>" + trimmed.mid(2).toHtmlEscaped() + "</blockquote>";
            continue;
        }
        // Unordered list
        if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
            html += "<ul><li>" + trimmed.mid(2).toHtmlEscaped() + "</li></ul>";
            continue;
        }
        // Ordered list (simple)
        if (trimmed.length() > 2 && trimmed[0].isDigit() && trimmed[1] == '.') {
            html += "<ol><li>" + trimmed.mid(3).toHtmlEscaped() + "</li></ol>";
            continue;
        }
        // Empty line = paragraph break
        if (trimmed.isEmpty()) {
            html += "<br>";
            continue;
        }

        // Inline formatting: **bold**, *italic*, `code`, [link](url)
        QString escaped = line.toHtmlEscaped();
        // Inline code first (to avoid nested processing)
        escaped.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");
        // Bold
        escaped.replace(QRegularExpression("\\*\\*([^*]+)\\*\\*"), "<strong>\\1</strong>");
        // Italic
        escaped.replace(QRegularExpression("\\*([^*]+)\\*"), "<em>\\1</em>");
        // Links
        escaped.replace(QRegularExpression("\\[([^\\]]+)\\]\\(([^)]+)\\)"), "<a href='\\2'>\\1</a>");

        html += "<p style='margin:2px 0;'>" + escaped + "</p>";
    }

    // Close unclosed code block
    if (in_code_block && !code_block.isEmpty()) {
        html += "<pre>" + code_block.toHtmlEscaped() + "</pre>";
    }

    md_preview_->setHtml(html);

    // Size preview to content
    QTimer::singleShot(0, this, [this]() {
        int h = static_cast<int>(md_preview_->document()->size().height()) + 20;
        int clamped = qBound(48, h, 600);
        md_preview_->setMinimumHeight(clamped);
        md_preview_->setMaximumHeight(clamped);
    });
}

void CellWidget::enterEvent(QEnterEvent* /*event*/) {
    // Show toolbar buttons
    for (int i = 0; i < toolbar_->layout()->count(); ++i) {
        auto* w = toolbar_->layout()->itemAt(i)->widget();
        if (w)
            w->setVisible(true);
    }
    toolbar_->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(colors::BG_RAISED(), colors::BORDER_DIM()));
}

void CellWidget::leaveEvent(QEvent* /*event*/) {
    // Hide toolbar buttons
    for (int i = 0; i < toolbar_->layout()->count(); ++i) {
        auto* w = toolbar_->layout()->itemAt(i)->widget();
        if (w)
            w->setVisible(false);
    }
    toolbar_->setStyleSheet(QString("background:%1;").arg(colors::BG_RAISED()));
}

void CellWidget::mousePressEvent(QMouseEvent* event) {
    emit cell_clicked(cell_id_);
    QWidget::mousePressEvent(event);
}

// ═════════════════════════════════════════════════════════════════════════════
// CellNavigator (sidebar)
// ═════════════════════════════════════════════════════════════════════════════

CellNavigator::CellNavigator(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setFixedHeight(32);
    header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(colors::BG_RAISED(), colors::BORDER_DIM()));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(10, 0, 10, 0);

    auto* title = new QLabel("CELLS", header);
    title->setStyleSheet(QString("color:%1; font-family:%2; font-size:%3px; font-weight:700; letter-spacing:1px;")
                             .arg(colors::AMBER(), fonts::DATA_FAMILY)
                             .arg(fonts::TINY));
    header_layout->addWidget(title);
    layout->addWidget(header);

    list_ = new QListWidget(this);
    list_->setStyleSheet(QString("QListWidget { background:%1; border:none; outline:none;"
                                 " font-family:%2; font-size:%3px; }"
                                 "QListWidget::item { color:%4; padding:6px 10px;"
                                 " border-bottom:1px solid %5; }"
                                 "QListWidget::item:selected { background:%6; color:%7;"
                                 " border-left:2px solid %7; }"
                                 "QListWidget::item:hover { background:%8; }")
                             .arg(colors::BG_SURFACE(), fonts::DATA_FAMILY)
                             .arg(fonts::TINY)
                             .arg(colors::TEXT_SECONDARY(), colors::BORDER_DIM(), colors::BG_HOVER(), colors::AMBER(),
                                  colors::BG_HOVER()));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setWordWrap(false);
    list_->setUniformItemSizes(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < list_->count()) {
            emit cell_selected(list_->item(row)->data(Qt::UserRole).toString());
        }
    });

    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        if (!item)
            return;

        QMenu menu(this);
        auto* rename_action = menu.addAction("Rename Cell");
        QAction* chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
        if (chosen == rename_action) {
            emit rename_requested(item->data(Qt::UserRole).toString());
        }
    });

    auto* rename_shortcut = KeyConfigManager::instance().action(KeyAction::RenameCell);
    rename_shortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    list_->addAction(rename_shortcut);
    connect(rename_shortcut, &QAction::triggered, this, [this]() {
        auto* item = list_->currentItem();
        if (!item)
            return;
        emit rename_requested(item->data(Qt::UserRole).toString());
    });

    layout->addWidget(list_, 1);
}

void CellNavigator::rebuild(const QVector<NotebookCell>& cells, const QString& selected_id) {
    list_->blockSignals(true);
    list_->clear();

    int selected_row = -1;
    for (int i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        QString type_tag = (cell.cell_type == "code") ? "PY" : "MD";
        QString exec_tag;
        if (cell.execution_count > 0)
            exec_tag = QString(" [%1]").arg(cell.execution_count);

        QString preview = cell.title.trimmed();
        if (preview.isEmpty())
            preview = cell.source.split('\n').first().trimmed();
        if (preview.isEmpty())
            preview = "(empty)";

        const QString label = QString("%1  %2  %3%4").arg(i + 1, 2).arg(type_tag, -2).arg(preview, exec_tag);
        auto* item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, cell.id);
        item->setToolTip(label);

        if (cell.id == selected_id)
            selected_row = i;
    }

    if (selected_row >= 0)
        list_->setCurrentRow(selected_row);
    list_->blockSignals(false);
}

// ═════════════════════════════════════════════════════════════════════════════
// CodeEditorScreen
// ═════════════════════════════════════════════════════════════════════════════

CodeEditorScreen::CodeEditorScreen(QWidget* parent) : QWidget(parent) {
    using services::notebook::NotebookKernel;
    kernel_ = new NotebookKernel(this);
    connect(kernel_, &NotebookKernel::state_changed, this, &CodeEditorScreen::on_kernel_state);
    connect(kernel_, &NotebookKernel::output, this, &CodeEditorScreen::on_kernel_output);
    connect(kernel_, &NotebookKernel::finished, this, &CodeEditorScreen::on_kernel_finished);
    output_timer_ = new QTimer(this);
    output_timer_->setSingleShot(true);
    connect(output_timer_, &QTimer::timeout, this, &CodeEditorScreen::show_outputs);

    build_ui();
    reset_notebook();
}

void CodeEditorScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    LOG_INFO("CodeEditor", "Screen shown");
}

void CodeEditorScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    LOG_INFO("CodeEditor", "Screen hidden");
}

void CodeEditorScreen::build_ui() {
    setStyleSheet(QString("QWidget { background:%1; color:%2; }").arg(colors::BG_BASE(), colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(build_toolbar());

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setStyleSheet(QString("QSplitter { background:%1; } QSplitter::handle { background:%2; }")
                                 .arg(colors::BG_BASE(), colors::BORDER_DIM()));

    navigator_ = new CellNavigator(splitter_);
    navigator_->setMinimumWidth(180);
    navigator_->setMaximumWidth(300);
    connect(navigator_, &CellNavigator::cell_selected, this, &CodeEditorScreen::on_select_cell);
    connect(navigator_, &CellNavigator::rename_requested, this, &CodeEditorScreen::on_rename_cell);
    splitter_->addWidget(navigator_);

    scroll_area_ = new QScrollArea(splitter_);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setStyleSheet(
        QString("QScrollArea { border:none; background:%1; }"
                "QScrollBar:vertical { background:%2; width:6px; margin:0; }"
                "QScrollBar::handle:vertical { background:%3; min-height:40px; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
                "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")
            .arg(colors::BG_BASE(), colors::BG_SURFACE(), colors::BORDER_BRIGHT()));

    cells_container_ = new QWidget(scroll_area_);
    cells_layout_ = new QVBoxLayout(cells_container_);
    cells_layout_->setContentsMargins(8, 4, 12, 40);
    cells_layout_->setSpacing(0);
    cells_layout_->addStretch();

    scroll_area_->setWidget(cells_container_);
    splitter_->addWidget(scroll_area_);

    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({200, 800});

    root->addWidget(splitter_, 1);
    root->addWidget(build_status_bar());
}

QWidget* CodeEditorScreen::build_toolbar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(36);
    bar->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(colors::BG_RAISED(), colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(10, 0, 10, 0);
    hl->setSpacing(0);

    auto* title = new QLabel("PYTHON NOTEBOOK", bar);
    title->setStyleSheet(QString("color:%1; font-family:%2; font-size:%3px; font-weight:700;"
                                 " letter-spacing:1px; padding-right:16px;")
                             .arg(colors::AMBER(), fonts::DATA_FAMILY)
                             .arg(fonts::SMALL));
    hl->addWidget(title);

    auto add_sep = [&]() {
        auto* sep = new QWidget(bar);
        sep->setFixedSize(1, 18);
        sep->setStyleSheet(QString("background:%1;").arg(colors::BORDER_MED()));
        hl->addWidget(sep);
    };

    auto make_btn = [&](const QString& text, const QString& fg = QString()) -> QPushButton* {
        auto* btn = new QPushButton(text, bar);
        btn->setCursor(Qt::PointingHandCursor);
        QString color = fg.isEmpty() ? colors::TEXT_SECONDARY : fg;
        btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1; border:none;"
                                   " font-family:%2; font-size:%3px; font-weight:600; padding:0 10px;"
                                   " letter-spacing:0.5px; }"
                                   "QPushButton:hover { color:%4; background:%5; }")
                               .arg(color, fonts::DATA_FAMILY)
                               .arg(fonts::TINY)
                               .arg(colors::TEXT_PRIMARY(), colors::BG_HOVER()));
        return btn;
    };

    auto* new_btn = make_btn("NEW");
    connect(new_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_new_notebook);
    hl->addWidget(new_btn);

    auto* open_btn = make_btn("OPEN");
    connect(open_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_open_notebook);
    hl->addWidget(open_btn);

    auto* save_btn = make_btn("SAVE");
    connect(save_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_save_notebook);
    hl->addWidget(save_btn);

    add_sep();

    auto* add_btn = make_btn("+ CELL", colors::CYAN);
    connect(add_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_add_cell);
    hl->addWidget(add_btn);

    auto* clear_btn = make_btn("CLEAR OUT");
    connect(clear_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_clear_outputs);
    hl->addWidget(clear_btn);

    add_sep();

    auto* run_all_btn = new QPushButton("RUN ALL", bar);
    run_all_btn->setCursor(Qt::PointingHandCursor);
    run_all_btn->setStyleSheet(QString("QPushButton { background:%1; color:%2; border:none;"
                                       " font-family:%3; font-size:%4px; font-weight:700; padding:4px 14px;"
                                       " letter-spacing:0.5px; }"
                                       "QPushButton:hover { background:%5; }")
                                   .arg(colors::AMBER(), colors::BG_BASE(), fonts::DATA_FAMILY)
                                   .arg(fonts::TINY)
                                   .arg(colors::ORANGE()));
    connect(run_all_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_run_all);
    hl->addWidget(run_all_btn);

    interrupt_btn_ = make_btn("INTERRUPT");
    interrupt_btn_->setToolTip("Stop the running cell; those waiting are dropped");
    connect(interrupt_btn_, &QPushButton::clicked, kernel_, &services::notebook::NotebookKernel::interrupt);
    hl->addWidget(interrupt_btn_);

    restart_btn_ = make_btn("RESTART");
    restart_btn_->setToolTip("End the notebook's Python: its variables go, whatever runs is stopped");
    connect(restart_btn_, &QPushButton::clicked, kernel_, &services::notebook::NotebookKernel::restart);
    hl->addWidget(restart_btn_);

    hl->addStretch();

    auto* sidebar_btn = make_btn("SIDEBAR");
    connect(sidebar_btn, &QPushButton::clicked, this, &CodeEditorScreen::on_toggle_sidebar);
    hl->addWidget(sidebar_btn);

    add_sep();

    kernel_label_ = new QLabel(bar);
    hl->addWidget(kernel_label_);

    python_label_ = new QLabel(bar);
    python_label_->setStyleSheet(QString("color:%1; font-family:%2; font-size:12px; padding-left:8px;")
                                     .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY));
    hl->addWidget(python_label_);
    on_kernel_state();

    return bar;
}

QWidget* CodeEditorScreen::build_status_bar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(24);
    bar->setStyleSheet(
        QString("background:%1; border-top:1px solid %2;").arg(colors::BG_RAISED(), colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(10, 0, 10, 0);

    status_label_ = new QLabel("READY", bar);
    status_label_->setStyleSheet(
        QString("color:%1; font-family:%2; font-size:12px; font-weight:600; letter-spacing:0.5px;")
            .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY));
    hl->addWidget(status_label_);
    hl->addStretch();

    auto* shortcuts = new QLabel("Ctrl+Enter: RUN  |  Shift+Enter: RUN & NEXT  |  Tab: 4 SPACES  |  Ctrl+S: SAVE", bar);
    shortcuts->setStyleSheet(QString("color:%1; font-family:%2; font-size:12px; letter-spacing:0.3px;")
                                 .arg(colors::TEXT_SECONDARY(), fonts::DATA_FAMILY));
    hl->addWidget(shortcuts);

    return bar;
}

// ─────────────────────────────────────────────────────────────────────────────
// Notebook management
// ─────────────────────────────────────────────────────────────────────────────

void CodeEditorScreen::on_new_notebook() {
    reset_notebook();
    ScreenStateManager::instance().notify_changed(this);
}

void CodeEditorScreen::reset_notebook() {
    kernel_->restart(); // another notebook's variables are not this one's
    cells_.clear();
    execution_counter_ = 0;
    notebook_path_.clear();
    notebook_raw_ = {};

    NotebookCell cell;
    cell.id = new_cell_id();
    cell.cell_type = "code";
    cell.source = "# finterm Python Notebook\n"
                  "# Write Python code and press Ctrl+Enter to run\n"
                  "\n"
                  "print(\"Hello from finterm!\")";
    cells_.append(cell);

    selected_cell_id_ = cell.id;
    rebuild_cells();
    update_status();
    update_navigator();
}

void CodeEditorScreen::on_add_cell() {
    QString after_id = selected_cell_id_;
    if (after_id.isEmpty() && !cells_.isEmpty())
        after_id = cells_.last().id;
    add_cell_after(after_id, "code");
}

void CodeEditorScreen::on_insert_below(const QString& cell_id) {
    add_cell_after(cell_id, "code");
}

void CodeEditorScreen::add_cell_after(const QString& after_id, const QString& type) {
    NotebookCell cell;
    cell.id = new_cell_id();
    cell.cell_type = type;

    int idx = find_cell_index(after_id);
    if (idx >= 0)
        cells_.insert(idx + 1, cell);
    else
        cells_.append(cell);

    selected_cell_id_ = cell.id;
    rebuild_cells();
    if (auto* cw = find_cell_widget(cell.id))
        cw->focus_editor(); // what is typed next is the cell's, not the button's
    update_status();
    update_navigator();
}

void CodeEditorScreen::on_delete_cell(const QString& cell_id) {
    if (cells_.size() <= 1)
        return;

    kernel_->cancel(cell_id);
    sync_cells();
    int idx = find_cell_index(cell_id);
    if (idx >= 0) {
        cells_.removeAt(idx);
        if (idx < cells_.size())
            selected_cell_id_ = cells_[idx].id;
        else if (!cells_.isEmpty())
            selected_cell_id_ = cells_.last().id;

        rebuild_cells();
        update_status();
        update_navigator();
    }
}

void CodeEditorScreen::on_move_cell_up(const QString& cell_id) {
    int idx = find_cell_index(cell_id);
    if (idx <= 0)
        return;

    cells_.swapItemsAt(idx, idx - 1);
    rebuild_cells();
    update_navigator();
}

void CodeEditorScreen::on_move_cell_down(const QString& cell_id) {
    int idx = find_cell_index(cell_id);
    if (idx < 0 || idx >= cells_.size() - 1)
        return;

    cells_.swapItemsAt(idx, idx + 1);
    rebuild_cells();
    update_navigator();
}

void CodeEditorScreen::on_toggle_cell_type(const QString& cell_id) {
    int idx = find_cell_index(cell_id);
    if (idx < 0)
        return;

    kernel_->cancel(cell_id);
    cells_[idx].cell_type = (cells_[idx].cell_type == "code") ? "markdown" : "code";
    cells_[idx].outputs.clear();
    cells_[idx].execution_count = 0;
    cells_[idx].running = false;

    rebuild_cells();
    update_navigator();
}

void CodeEditorScreen::on_select_cell(const QString& cell_id) {
    if (cell_id == selected_cell_id_)
        return;

    auto* old_cw = find_cell_widget(selected_cell_id_);
    if (old_cw)
        old_cw->set_selected(false);

    selected_cell_id_ = cell_id;

    auto* new_cw = find_cell_widget(cell_id);
    if (new_cw) {
        new_cw->set_selected(true);
        scroll_area_->ensureWidgetVisible(new_cw, 50, 50);
    }
    update_navigator();
}

void CodeEditorScreen::on_rename_cell(const QString& cell_id) {
    int idx = find_cell_index(cell_id);
    if (idx < 0)
        return;

    QString current_name = cells_[idx].title.trimmed();
    if (current_name.isEmpty()) {
        current_name = cells_[idx].source.split('\n').first().trimmed();
        if (current_name.isEmpty())
            current_name = QString("Cell %1").arg(idx + 1);
    }

    bool ok = false;
    const QString name =
        QInputDialog::getText(this, "Rename Cell", "Cell name:", QLineEdit::Normal, current_name, &ok).trimmed();
    if (!ok)
        return;

    // Empty name means "use auto preview from source"
    cells_[idx].title = name;
    update_navigator();
}

void CodeEditorScreen::on_clear_outputs() {
    for (auto& cell : cells_) {
        cell.outputs.clear();
        cell.execution_count = 0;
    }
    rebuild_cells();
    update_status();
    update_navigator();
}

void CodeEditorScreen::on_toggle_sidebar() {
    sidebar_visible_ = !sidebar_visible_;
    navigator_->setVisible(sidebar_visible_);
}

void CodeEditorScreen::advance_to_next(const QString& current_id) {
    int idx = find_cell_index(current_id);
    if (idx < 0)
        return;

    if (idx + 1 < cells_.size()) {
        on_select_cell(cells_[idx + 1].id);
    } else {
        // Add a new cell at the end and select it
        add_cell_after(current_id, "code");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell execution
// ─────────────────────────────────────────────────────────────────────────────

void CodeEditorScreen::on_run_cell(const QString& cell_id) {
    const int idx = find_cell_index(cell_id);
    if (idx < 0 || kernel_->holds(cell_id))
        return;
    sync_cells();
    NotebookCell& cell = cells_[idx];
    if (cell.cell_type != "code" || cell.source.trimmed().isEmpty())
        return;

    cell.running = true;
    cell.outputs.clear();
    cell.execution_count = ++execution_counter_;
    dirty_outputs_.remove(cell_id);
    if (auto* cw = find_cell_widget(cell_id)) {
        cw->set_outputs({}, cell.execution_count);
        cw->set_running(true);
    }
    kernel_->run(cell_id, cell.execution_count, cell.source);
}

void CodeEditorScreen::on_kernel_state() {
    using State = services::notebook::NotebookKernel::State;
    const State state = kernel_->state();
    const bool busy = state == State::Busy || state == State::Starting;
    kernel_label_->setText(state == State::Off        ? "KERNEL: OFF"
                           : state == State::Starting ? "KERNEL: STARTING"
                           : state == State::Busy     ? "KERNEL: BUSY"
                                                      : "KERNEL: IDLE");
    kernel_label_->setToolTip(state == State::Off ? "Starts with the first cell you run" : QString());
    kernel_label_->setStyleSheet(
        QString("color:%1; font-family:%2; font-size:12px; font-weight:600; letter-spacing:0.5px; padding:0 8px;")
            .arg(busy                  ? colors::WARNING()
                 : state == State::Off ? colors::TEXT_SECONDARY()
                                       : colors::POSITIVE(),
                 fonts::DATA_FAMILY));
    python_label_->setText(kernel_->python_version().isEmpty() ? QString() : "Python " + kernel_->python_version());
    interrupt_btn_->setEnabled(busy);
    restart_btn_->setEnabled(state != State::Off);
}

void CodeEditorScreen::on_kernel_output(const QString& cell_id, const services::notebook::KernelOutput& out) {
    const int idx = find_cell_index(cell_id);
    if (idx < 0 || cells_[idx].cell_type != "code")
        return; // gone, or text since
    auto& outputs = cells_[idx].outputs;
    // One block of stdout and one of stderr: which came first is not known, and
    // a cell that prints to both in turn would make thousands.
    CellOutput* block = nullptr;
    for (int i = outputs.size() - 1; out.type == "stream" && i >= 0 && outputs[i].type == "stream" && !block; --i)
        if (outputs[i].name == out.name && outputs[i].raw.isEmpty())
            block = &outputs[i];
    if (block) {
        block->text += out.text;
    } else {
        CellOutput o;
        o.type = out.type;
        o.name = out.name;
        o.text = out.text;
        o.error_name = out.error_name;
        o.error_value = out.error_value;
        o.traceback = out.traceback;
        outputs.append(o);
    }
    dirty_outputs_.insert(cell_id);
    if (!output_timer_->isActive()) // long output is dearer to show: less often
        output_timer_->start(outputs.last().text.size() > 100'000 ? 1000 : 120);
}

void CodeEditorScreen::show_outputs() {
    const auto ids = std::exchange(dirty_outputs_, {});
    for (const QString& id : ids) {
        const int idx = find_cell_index(id);
        auto* cw = find_cell_widget(id);
        if (idx >= 0 && cw)
            cw->set_outputs(cells_[idx].outputs, cells_[idx].execution_count);
    }
}

void CodeEditorScreen::on_kernel_finished(const QString& cell_id, bool ran, bool /*ok*/) {
    const int idx = find_cell_index(cell_id);
    if (idx < 0)
        return;
    cells_[idx].running = false;
    if (!ran)
        cells_[idx].execution_count = 0;
    if (auto* cw = find_cell_widget(cell_id)) {
        cw->set_running(false);
        cw->set_outputs(cells_[idx].outputs, cells_[idx].execution_count);
    }
    dirty_outputs_.remove(cell_id);
    update_status();
    update_navigator();
}

void CodeEditorScreen::on_run_and_advance(const QString& cell_id) {
    on_run_cell(cell_id);
    advance_to_next(cell_id);
}

void CodeEditorScreen::on_run_all() {
    QStringList ids;
    for (const auto& cell : std::as_const(cells_))
        ids << cell.id;
    for (const QString& id : std::as_const(ids)) // in order; an error drops the rest
        on_run_cell(id);
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

namespace {

QString joined(const QJsonValue& v) { // nbformat: a string, or a list of its lines
    if (!v.isArray())
        return v.toString();
    QString out;
    for (const auto& line : v.toArray())
        out += line.toString();
    return out;
}

QJsonArray lines_of(const QString& text) {
    QJsonArray out;
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const bool last = i + 1 == lines.size();
        if (!last || !lines[i].isEmpty())
            out.append(last ? lines[i] : lines[i] + '\n');
    }
    return out;
}

CellOutput output_from_json(const QJsonObject& o) {
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
    CellOutput out;
    out.raw = o;
    const QString type = o["output_type"].toString();
    if (type == "stream") {
        out.type = "stream";
        out.name = o["name"].toString();
        out.text = joined(o["text"]);
    } else if (type == "error") {
        out.type = "error";
        out.error_name = o["ename"].toString();
        out.error_value = o["evalue"].toString();
        for (const auto& line : o["traceback"].toArray())
            out.traceback << line.toString().remove(ansi);
    } else { // execute_result, display_data
        out.type = "execute_result";
        const QJsonObject data = o["data"].toObject();
        out.text = joined(data["text/plain"]);
        if (out.text.isEmpty())
            out.text = "[" + QStringList(data.keys()).join(", ") + "]";
    }
    return out;
}

QJsonObject output_to_json(const CellOutput& out, int execution_count) {
    if (!out.raw.isEmpty())
        return out.raw;
    if (out.type == "error")
        return {{"output_type", "error"},
                {"ename", out.error_name},
                {"evalue", out.error_value},
                {"traceback", QJsonArray::fromStringList(out.traceback)}};
    if (out.type == "stream")
        return {{"output_type", "stream"}, {"name", out.name}, {"text", lines_of(out.text)}};
    return {{"output_type", "execute_result"},
            {"execution_count", execution_count},
            {"data", QJsonObject{{"text/plain", lines_of(out.text)}}},
            {"metadata", QJsonObject{}}};
}

} // namespace

void CodeEditorScreen::on_open_notebook() {
    const QString path =
        QFileDialog::getOpenFileName(this, "Open Notebook", {}, "Jupyter Notebooks (*.ipynb);;All Files (*)");
    if (path.isEmpty())
        return;
    QString why;
    if (!load_notebook(path, &why)) {
        QMessageBox::warning(this, "Open Notebook", why);
        return;
    }
    LOG_INFO("CodeEditor", "Opened notebook: " + path);
    ScreenStateManager::instance().notify_changed(this);
}

bool CodeEditorScreen::load_notebook(const QString& path, QString* why) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *why = QString("%1 could not be opened: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError err{};
    const QJsonObject root = QJsonDocument::fromJson(bytes, &err).object();
    if (err.error != QJsonParseError::NoError) {
        *why = QString("%1 could not be read: %2, at byte %3.").arg(path, err.errorString()).arg(err.offset);
        return false;
    }
    if (!root["cells"].isArray()) {
        *why = QString("%1 is not a Jupyter notebook (format 4).").arg(path);
        return false;
    }

    kernel_->restart();
    cells_.clear();
    execution_counter_ = 0;
    notebook_raw_ = root;
    notebook_raw_.remove("cells");
    file_hash_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);

    for (const auto& jc : root["cells"].toArray()) {
        const QJsonObject co = jc.toObject();
        NotebookCell cell;
        cell.id = new_cell_id();
        cell.raw = co;
        cell.cell_type = co["cell_type"].toString("code");
        cell.source = joined(co["source"]);
        const QJsonObject metadata = co["metadata"].toObject();
        cell.title = metadata.value("finterm_title").toString(metadata.value("fincept_title").toString()).trimmed(); // older notebooks: fincept_title
        cell.execution_count = co["execution_count"].toInt(0);
        execution_counter_ = qMax(execution_counter_, cell.execution_count);
        for (const auto& out : co["outputs"].toArray())
            cell.outputs.append(output_from_json(out.toObject()));
        cells_.append(cell);
    }

    if (cells_.isEmpty()) {
        NotebookCell cell;
        cell.id = new_cell_id();
        cell.cell_type = "code";
        cells_.append(cell);
    }

    notebook_path_ = path;
    selected_cell_id_ = cells_.first().id;
    rebuild_cells();
    update_status();
    update_navigator();
    return true;
}

QJsonObject CodeEditorScreen::notebook_json() const {
    QJsonArray cells;
    for (const auto& cell : cells_) {
        QJsonObject co = cell.raw;
        co["cell_type"] = cell.cell_type;
        if (co["id"].toString().isEmpty())
            co["id"] = cell.id;
        co["source"] = lines_of(cell.source);
        QJsonObject metadata = co["metadata"].toObject();
        metadata.remove("fincept_title");
        metadata.remove("finterm_title");
        if (!cell.title.trimmed().isEmpty())
            metadata["finterm_title"] = cell.title.trimmed();
        co["metadata"] = metadata;
        if (cell.cell_type == "code") {
            co["execution_count"] = cell.execution_count > 0 ? QJsonValue(cell.execution_count) : QJsonValue();
            QJsonArray outputs;
            for (const auto& out : cell.outputs)
                outputs.append(output_to_json(out, cell.execution_count));
            co["outputs"] = outputs;
        } else { // format 4: only a code cell has these
            co.remove("execution_count");
            co.remove("outputs");
        }
        cells.append(co);
    }

    QJsonObject root = notebook_raw_;
    root["cells"] = cells;
    root["nbformat"] = 4;
    root["nbformat_minor"] = qMax(5, root["nbformat_minor"].toInt()); // 5: cells have ids
    QJsonObject metadata = root["metadata"].toObject();
    if (!metadata.contains("kernelspec"))
        metadata["kernelspec"] = QJsonObject{{"display_name", "Python 3"}, {"language", "python"}, {"name", "python3"}};
    root["metadata"] = metadata;
    return root;
}

void CodeEditorScreen::on_save_notebook() {
    sync_cells();
    QString path = notebook_path_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save Notebook", "notebook.ipynb", "Jupyter Notebooks (*.ipynb)");
        if (path.isEmpty())
            return;
    }

    // Another tab or program may have saved it since.
    if (QFile on_disk(path); path == notebook_path_ && on_disk.open(QIODevice::ReadOnly)) {
        if (QCryptographicHash::hash(on_disk.readAll(), QCryptographicHash::Sha256) != file_hash_ &&
            QMessageBox::question(this, "Save Notebook",
                                  QString("%1 was changed since it was opened here. Save over it?").arg(path),
                                  QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save)
            return;
    }

    QSaveFile file(path); // the old file stays until the new one is whole
    const QByteArray json = QJsonDocument(notebook_json()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        QMessageBox::warning(this, "Save Notebook", QString("%1 could not be saved: %2").arg(path, file.errorString()));
        return;
    }
    notebook_path_ = path;
    file_hash_ = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
    LOG_INFO("CodeEditor", "Saved notebook: " + path);
    services::FileManagerService::instance().import_file(path, "code_editor");
    update_status();
    ScreenStateManager::instance().notify_changed(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void CodeEditorScreen::sync_cells() {
    for (auto& cell : cells_)
        if (const auto* cw = find_cell_widget(cell.id))
            cell.source = cw->source();
}

void CodeEditorScreen::rebuild_cells() {
    sync_cells();
    while (cells_layout_->count() > 1) {
        auto* item = cells_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < cells_.size(); ++i) {
        auto* cw = new CellWidget(cells_[i], i, cells_container_);
        cw->set_selected(cells_[i].id == selected_cell_id_);

        connect(cw, &CellWidget::run_requested, this, &CodeEditorScreen::on_run_cell);
        connect(cw, &CellWidget::run_and_advance, this, &CodeEditorScreen::on_run_and_advance);
        connect(cw, &CellWidget::delete_requested, this, &CodeEditorScreen::on_delete_cell);
        connect(cw, &CellWidget::move_up_requested, this, &CodeEditorScreen::on_move_cell_up);
        connect(cw, &CellWidget::move_down_requested, this, &CodeEditorScreen::on_move_cell_down);
        connect(cw, &CellWidget::toggle_type_requested, this, &CodeEditorScreen::on_toggle_cell_type);
        connect(cw, &CellWidget::cell_clicked, this, &CodeEditorScreen::on_select_cell);
        connect(cw, &CellWidget::insert_below_requested, this, &CodeEditorScreen::on_insert_below);

        cells_layout_->insertWidget(i, cw);
    }
}

int CodeEditorScreen::find_cell_index(const QString& cell_id) const {
    for (int i = 0; i < cells_.size(); ++i)
        if (cells_[i].id == cell_id)
            return i;
    return -1;
}

CellWidget* CodeEditorScreen::find_cell_widget(const QString& cell_id) const {
    for (int i = 0; i < cells_layout_->count(); ++i) {
        auto* item = cells_layout_->itemAt(i);
        if (!item || !item->widget())
            continue;
        auto* cw = qobject_cast<CellWidget*>(item->widget());
        if (cw && cw->cell_id() == cell_id)
            return cw;
    }
    return nullptr;
}

QString CodeEditorScreen::new_cell_id() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void CodeEditorScreen::update_status() {
    int code_count = 0, md_count = 0, executed = 0;
    for (const auto& c : cells_) {
        if (c.cell_type == "code")
            ++code_count;
        else
            ++md_count;
        if (c.execution_count > 0)
            ++executed;
    }

    QString info = QString("CELLS: %1 CODE  %2 MD  |  EXECUTED: %3").arg(code_count).arg(md_count).arg(executed);
    if (!notebook_path_.isEmpty())
        info += "  |  " + QFileInfo(notebook_path_).fileName();
    status_label_->setText(info);
}

void CodeEditorScreen::update_navigator() {
    if (navigator_)
        navigator_->rebuild(cells_, selected_cell_id_);
}

// ── IStatefulScreen ───────────────────────────────────────────────────────────

QVariantMap CodeEditorScreen::save_state() const {
    return {{"notebook_path", notebook_path_}};
}

void CodeEditorScreen::restore_state(const QVariantMap& state) {
    const QString path = state.value("notebook_path").toString();
    if (path.isEmpty() || notebook_path_ == path || !QFile::exists(path))
        return;
    // The cells come with the path: with the path alone, Save would write the
    // new notebook's one cell over the file.
    QString why;
    if (!load_notebook(path, &why))
        LOG_WARN("CodeEditor", why);
}

} // namespace finterm::screens
