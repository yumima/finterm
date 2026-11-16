#include "ai_chat/AiChatBubble.h"

#include "ai_chat/ChatPanelSize.h"
#include "ai_chat/ChatPersonas.h"
#include "ai_chat/VoiceOrb.h"
#include "core/logging/Logger.h"
#include "services/stt/SpeechService.h"
#include "services/tts/SpeechText.h"
#include "services/tts/TtsService.h"
#include "storage/repositories/ChatRepository.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/ReadAloudToggle.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBrowser>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <memory>

namespace finterm {

namespace col = finterm::ui::colors;

namespace {
// ── Layout constants ──────────────────────────────────────────────────────────
using ai_chat::panel_size::BASE_PANEL_H;
using ai_chat::panel_size::BASE_PANEL_W;
using ai_chat::panel_size::MIN_PANEL_H;
using ai_chat::panel_size::MIN_PANEL_W;
using ai_chat::panel_size::clamp_panel_size;
using ai_chat::panel_size::default_panel_size;
constexpr int BTN_W = 30; // launcher: sized to sit inside the 26px status bar
constexpr int BTN_H = 22;
constexpr int MARGIN = 8;
constexpr int GRIP = 14;
// The conversation column: full width on the docked panel, centred at a
// reading width when the panel is expanded (Claude/Gemini).
constexpr int kReadingWidth = 860;
constexpr double kUserShare = 0.78; // a user bubble takes at most this share of the column

/// The chat reads in the News headline font: the theme's data family at the
/// headline size (NewsFeedDelegate: DATA_FAMILY, TINY pt).
QString chat_font_css() {
    return QStringLiteral("font-family:'%1'; font-size:%2pt;")
        .arg(QString(ui::fonts::DATA_FAMILY))
        .arg(ui::fonts::TINY);
}

QFont chat_font() {
    return QFont(QString(ui::fonts::DATA_FAMILY), ui::fonts::TINY);
}

QString md_css(const QString& text_color) {
    // No line-height: Qt applies a unitless one in layout but not in the
    // document's measured size, so the measured height came out short and
    // replies were cut off at the bottom.
    return QString("body { color: %1; }"
                   "p { margin-top: 6px; margin-bottom: 6px; }"
                   "ul, ol { margin-top: 4px; margin-bottom: 4px; padding-left: 18px; }"
                   "li { margin-top: 3px; margin-bottom: 3px; }"
                   "h1, h2, h3, h4 { margin-top: 12px; margin-bottom: 4px; color: %2; font-weight: 700; }"
                   "hr { margin-top: 8px; margin-bottom: 8px; border: none; border-top: 1px solid %3; }"
                   "a { color: %2; text-decoration: underline; }"
                   "code { background: %4; color: %2; padding: 1px 3px; }"
                   "pre { background: %4; border: 1px solid %3; padding: 8px 10px; "
                   "      margin-top: 6px; margin-bottom: 6px; }"
                   "blockquote { border-left: 2px solid %2; padding-left: 8px; color: %5; }"
                   "table { border-collapse: collapse; margin-top: 6px; margin-bottom: 6px; }"
                   "th { background: %4; border: 1px solid %3; padding: 3px 8px; font-weight: 600; color: %2; }"
                   "td { border: 1px solid %3; padding: 3px 8px; }"
                   "strong { color: %1; font-weight: 700; }")
        .arg(text_color, col::AMBER(), col::BORDER_MED(), col::BG_BASE(), col::TEXT_SECONDARY());
}

/// Read-only rich text that is exactly as tall as its content at whatever
/// width the layout gives it — so a reply fills the column and re-wraps on
/// every resize (the old fixed-width columns took half the panel: the row's
/// stretch claimed the rest).
class AutoText : public QTextBrowser {
  public:
    explicit AutoText(const QString& text_color, QWidget* parent = nullptr) : QTextBrowser(parent) {
        setFrameShape(QFrame::NoFrame);
        setOpenExternalLinks(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFont(chat_font());
        document()->setDefaultFont(chat_font());
        document()->setDocumentMargin(0);
        document()->setDefaultStyleSheet(md_css(text_color));
        // padding/margin zeroed explicitly: the app stylesheet pads text
        // widgets, which shrank the viewport inside the fixed height and cut
        // the last line off every reply.
        setStyleSheet(QStringLiteral("QTextBrowser{background:transparent;color:%1;border:none;padding:0px;"
                                     "margin:0px;%2}")
                          .arg(text_color, chat_font_css()));
        QPalette p = palette();
        p.setColor(QPalette::Link, QColor(col::AMBER()));
        p.setColor(QPalette::Highlight, QColor(col::AMBER_DIM()));
        p.setColor(QPalette::HighlightedText, QColor(col::TEXT_PRIMARY()));
        p.setColor(QPalette::Base, Qt::transparent);
        setPalette(p);
        connect(document(), &QTextDocument::contentsChanged, this, [this]() { refit(); });
    }
    /// Plain text that hugs: the width it wants on one line (capped by the
    /// layout), so a user bubble fits a short message instead of spanning the
    /// column. Measured once here — sizeHint() runs on every relayout.
    void set_hug_text(const QString& text) {
        setPlainText(text);
        QTextDocument d;
        d.setDefaultFont(font());
        d.setDocumentMargin(0);
        d.setPlainText(text);
        hug_w_ = int(std::ceil(d.idealWidth())) + 4;
        updateGeometry();
    }
    QSize sizeHint() const override {
        return hug_w_ > 0 ? QSize(hug_w_, height()) : QTextBrowser::sizeHint();
    }
    void refit() {
        // The widget's own content width, not viewport(): during a resize the
        // scroll area lays its viewport out AFTER this runs, so the viewport
        // still has the old width and the height came out one size behind
        // (wrapped lines cut off).
        // QTextEdit wraps at its VIEWPORT width; measure there (the narrower of
        // the two while a resize is still settling).
        const int vw = viewport()->width();
        const int w = vw > 0 ? std::min(vw, contentsRect().width()) : contentsRect().width();
        if (w <= 0)
            return;
        document()->setTextWidth(w);
        const int h = std::max(20, int(std::ceil(document()->documentLayout()->documentSize().height())) + chrome());
        if (h != height())
            setFixedHeight(h);
        // Once more after the layout settles (a scroll bar appearing narrows
        // the column after this pass).
        if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this]() {
                pending_ = false;
                const int w2 = viewport()->width() > 0 ? viewport()->width() : contentsRect().width();
                if (w2 <= 0)
                    return;
                document()->setTextWidth(w2);
                const int h2 = std::max(
                    20, int(std::ceil(document()->documentLayout()->documentSize().height())) + chrome());
                if (h2 != height())
                    setFixedHeight(h2);
            });
        }
    }

  protected:
    /// Whatever the widget adds around its viewport (frame, any padding a
    /// stylesheet still applies) — the height must include it.
    int chrome() const {
        const int vh = viewport()->height();
        return (vh > 0 && height() > vh ? height() - vh : 0) + 2;
    }
    void resizeEvent(QResizeEvent* e) override {
        QTextBrowser::resizeEvent(e);
        refit();
    }

  private:
    int hug_w_ = 0;
    bool pending_ = false;
};

QPushButton* flat_button(const QString& text, const QString& tip) {
    auto* b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    b->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:1px solid transparent;"
                             "padding:2px 8px;font-size:12px;}"
                             "QPushButton:hover{color:%2;border-color:%3;}")
                         .arg(col::TEXT_SECONDARY(), col::TEXT_PRIMARY(), col::BORDER_MED()));
    return b;
}
} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────
AiChatBubble::AiChatBubble(QWidget* panel_host, QWidget* parent) : QWidget(parent), panel_host_(panel_host) {
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(BTN_W, BTN_H);
    setToolTip(QStringLiteral("AI assistant"));

    build_bubble_button();
    build_chat_panel();
    chat_panel_->hide();

    // ── Speech in ────────────────────────────────────────────────────────────
    auto& stt = services::SpeechService::instance();
    connect(&stt, &services::SpeechService::transcription_ready, this, &AiChatBubble::on_transcription);
    connect(&stt, &services::SpeechService::listening_changed, this, &AiChatBubble::on_stt_listening_changed);
    connect(&stt, &services::SpeechService::error_occurred, this, &AiChatBubble::on_stt_error);

    // ── Speech out: the engine's voice, the reading this panel started ───────
    auto& tts = services::TtsService::instance();
    connect(&tts, &services::TtsService::session_state, this, [this](int session, bool speaking) {
        if (session == tts_session_ && !speaking && is_speaking_)
            speech_finished();
    });
    connect(&tts, &services::TtsService::session_error, this, [this](int session, const QString& msg) {
        if (session != tts_session_)
            return;
        voice_error_ = QStringLiteral("Voice: ") + msg; // shown by update_status on either page
        if (is_speaking_)
            speech_finished();
        else
            update_status();
    });

    if (panel_host_) {
        panel_host_->installEventFilter(this);
        chat_panel_->setParent(panel_host_);
        chat_panel_->hide();
    }

    // Restore the user's panel size and expanded state.
    {
        const auto rw = SettingsRepository::instance().get("ui.chat_panel.w");
        const auto rh = SettingsRepository::instance().get("ui.chat_panel.h");
        if (rw.is_ok() && rh.is_ok()) {
            bool ok_w = false, ok_h = false;
            const int w = rw.value().toInt(&ok_w);
            const int h = rh.value().toInt(&ok_h);
            if (ok_w && ok_h && w > 0 && h > 0)
                user_panel_size_ = QSize(w, h);
        }
        const auto ex = SettingsRepository::instance().get("ui.chat_panel.expanded");
        expanded_ = ex.is_ok() && ex.value() == QLatin1String("1");
        update_expand_button();
    }

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
}

// ── Theme refresh ─────────────────────────────────────────────────────────────
void AiChatBubble::refresh_theme() {
    if (bubble_btn_)
        bubble_btn_->setStyleSheet(QString("background:%1;border:1px solid %2;border-radius:0px;")
                                       .arg(col::BG_SURFACE(), col::BORDER_BRIGHT()));
    if (chat_panel_)
        chat_panel_->setStyleSheet(QString("QWidget#chatPanel{background:%1;border:1px solid %2;border-radius:0px;}")
                                       .arg(col::BG_SURFACE(), col::BORDER_MED()));
    if (msg_container_)
        msg_container_->setStyleSheet(QString("background:%1;").arg(col::BG_SURFACE()));
    if (scroll_area_)
        scroll_area_->setStyleSheet(
            QString("QScrollArea{background:%1;border:none;}"
                    "QScrollBar:vertical{background:transparent;width:6px;margin:2px;}"
                    "QScrollBar::handle:vertical{background:%2;min-height:20px;}"
                    "QScrollBar::handle:vertical:hover{background:%3;}"
                    "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}")
                .arg(col::BG_SURFACE(), col::BORDER_MED(), col::AMBER()));
    if (input_box_)
        input_box_->setStyleSheet(QString("QPlainTextEdit{background:%1;color:%2;border:1px solid %3;"
                                          "padding:6px 10px;%5}"
                                          "QPlainTextEdit:focus{border-color:%4;}")
                                      .arg(col::BG_RAISED(), col::TEXT_PRIMARY(), col::BORDER_MED(), col::AMBER(),
                                           chat_font_css()));
    if (send_btn_)
        send_btn_->setStyleSheet(
            QString("QPushButton{background:%1;color:%2;border:none;font-size:20px;font-weight:700;}"
                    "QPushButton:hover:enabled{background:%3;}"
                    "QPushButton:disabled{background:%4;color:%5;}")
                .arg(col::AMBER(), col::BG_BASE(), col::AMBER_DIM(), col::BG_RAISED(), col::TEXT_TERTIARY()));
    const QString tool_qss = QString("QPushButton{background:transparent;color:%1;border:1px solid %2;font-size:16px;}"
                                     "QPushButton:hover{border-color:%3;}"
                                     "QPushButton:checked{background:%4;border-color:%4;}")
                                 .arg(col::TEXT_PRIMARY(), col::BORDER_MED(), col::AMBER(), col::AMBER_DIM());
    for (auto* b : {mic_btn_, live_btn_})
        if (b)
            b->setStyleSheet(tool_qss);
}

// ── Reposition ────────────────────────────────────────────────────────────────
// Docked: bottom-right of the host at the user's size (or the default).
// Expanded: the whole host, less a margin — a full chat window.
void AiChatBubble::reposition() {
    if (!panel_host_ || !chat_panel_)
        return;
    const QRect pr = panel_host_->rect();
    if (expanded_) {
        chat_panel_->setGeometry(pr.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN));
    } else {
        const QSize sz = user_panel_size_.isValid() ? clamp_panel_size(user_panel_size_, pr.size())
                                                    : default_panel_size(pr.size());
        const int px = std::max(0, pr.width() - sz.width() - MARGIN);
        const int py = std::max(0, pr.height() - sz.height() - MARGIN);
        chat_panel_->setGeometry(px, py, sz.width(), sz.height());
    }
    if (resize_grip_) {
        resize_grip_->setVisible(!expanded_);
        resize_grip_->move(0, 0);
        resize_grip_->raise();
    }
    // Only while the launcher is on screen: reposition() also runs on host
    // resizes, and an open panel whose launcher is hidden (lock, workbench,
    // focus mode) must not come back without the button that closes it.
    if (is_open_ && isVisible()) {
        chat_panel_->show();
        chat_panel_->raise();
    }
    rewrap_messages();
}

void AiChatBubble::update_expand_button() {
    if (!expand_btn_)
        return;
    expand_btn_->setText(expanded_ ? QStringLiteral("⤡") : QStringLiteral("⤢"));
    expand_btn_->setToolTip(expanded_ ? QStringLiteral("Back to the side panel")
                                      : QStringLiteral("Expand to a full window"));
}

void AiChatBubble::set_expanded(bool on) {
    expanded_ = on;
    update_expand_button();
    SettingsRepository::instance().set("ui.chat_panel.expanded", on ? "1" : "0");
    reposition();
}

void AiChatBubble::rewrap_messages() {
    // The column is capped at a reading width; replies fill it, user bubbles
    // take at most kUserShare of it. AutoText re-measures its own height.
    if (!column_ || !scroll_area_)
        return;
    const int avail = scroll_area_->viewport()->width() - 28;
    column_->setMaximumWidth(std::max(200, std::min(avail, kReadingWidth)));
    const int cap = int(std::min(avail, kReadingWidth) * kUserShare);
    std::erase_if(user_bubbles_, [](const QPointer<QWidget>& w) { return w.isNull(); });
    for (auto& b : user_bubbles_)
        b->setMaximumWidth(std::max(160, cap));
}

void AiChatBubble::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // The panel is a sibling overlay in the host, so it does not follow the
    // launcher's visibility on its own.
    if (is_open_ && chat_panel_) {
        reposition();
        chat_panel_->show();
        chat_panel_->raise();
    }
}

void AiChatBubble::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    // Lock screen / workbench / setting off hide the launcher: the open panel
    // must go with it (it stays "open", and comes back on the next show).
    if (!e->spontaneous() && chat_panel_)
        chat_panel_->hide();
}

// ── Bubble button ─────────────────────────────────────────────────────────────
void AiChatBubble::build_bubble_button() {
    bubble_btn_ = new QWidget(this);
    bubble_btn_->setFixedSize(BTN_W, BTN_H);
    bubble_btn_->setCursor(Qt::PointingHandCursor);
    bubble_btn_->setStyleSheet(
        QString("background:%1;border:1px solid %2;border-radius:0px;").arg(col::BG_SURFACE(), col::BORDER_BRIGHT()));

    auto* lbl = new QLabel("⬡", bubble_btn_);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setGeometry(0, 0, BTN_W, BTN_H);
    lbl->setStyleSheet(QString("color:%1;font-size:16px;background:transparent;border:none;").arg(col::TEXT_PRIMARY()));

    unread_badge_ = new QLabel(bubble_btn_);
    unread_badge_->setFixedSize(14, 12);
    unread_badge_->setAlignment(Qt::AlignCenter);
    unread_badge_->move(BTN_W - 14, 0);
    unread_badge_->setStyleSheet(
        QString("background:%1;color:white;font-size:9px;font-weight:bold;").arg(col::NEGATIVE()));
    unread_badge_->hide();

    bubble_btn_->installEventFilter(this);
}

// ── Chat panel ────────────────────────────────────────────────────────────────
void AiChatBubble::build_chat_panel() {
    chat_panel_ = new QWidget(nullptr);
    chat_panel_->setObjectName("chatPanel");
    chat_panel_->setMinimumSize(MIN_PANEL_W, MIN_PANEL_H);
    chat_panel_->resize(2 * BASE_PANEL_W, 2 * BASE_PANEL_H);

    auto* vl = new QVBoxLayout(chat_panel_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(build_panel_header());

    stack_ = new QStackedWidget;

    // ── page 0: the conversation ──
    auto* convo = new QWidget;
    auto* cvl = new QVBoxLayout(convo);
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(0);
    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->viewport()->installEventFilter(this); // re-cap the column on resize

    msg_container_ = new QWidget;
    auto* centre = new QHBoxLayout(msg_container_);
    centre->setContentsMargins(14, 14, 14, 10);
    centre->setSpacing(0);
    column_ = new QWidget;
    column_->setStyleSheet("background:transparent;");
    msg_layout_ = new QVBoxLayout(column_);
    msg_layout_->setContentsMargins(0, 0, 0, 0);
    msg_layout_->setSpacing(18);
    msg_layout_->addStretch();
    centre->addStretch(1);
    centre->addWidget(column_, 100);
    centre->addStretch(1);
    scroll_area_->setWidget(msg_container_);
    cvl->addWidget(scroll_area_, 1);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    status_lbl_->setContentsMargins(16, 2, 16, 2);
    status_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;font-style:italic;background:transparent;").arg(col::TEXT_TERTIARY()));
    cvl->addWidget(status_lbl_);
    stack_->addWidget(convo);

    // ── page 1: live voice ──
    stack_->addWidget(build_live_page());
    vl->addWidget(stack_, 1);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;").arg(col::BORDER_DIM()));
    vl->addWidget(sep);
    vl->addWidget(build_input_row());

    // Resize grip, top-left: the docked panel is anchored bottom-right, so
    // that is the corner that moves. Hidden while expanded.
    resize_grip_ = new QWidget(chat_panel_);
    resize_grip_->setFixedSize(GRIP, GRIP);
    resize_grip_->setCursor(Qt::SizeFDiagCursor);
    resize_grip_->setToolTip(QStringLiteral("Drag to resize"));
    resize_grip_->setStyleSheet(QString("background:transparent;border-top:2px solid %1;border-left:2px solid %1;")
                                    .arg(col::BORDER_BRIGHT()));
    resize_grip_->installEventFilter(this);
    refresh_theme();
}

QWidget* AiChatBubble::build_panel_header() {
    auto* hdr = new QWidget;
    hdr->setFixedHeight(44);
    hdr->setStyleSheet(QString("background:%1;border-bottom:1px solid %2;").arg(col::BG_RAISED(), col::BORDER_MED()));

    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(14, 0, 10, 0);
    hl->setSpacing(8);

    auto* icon = new QLabel("⬡");
    icon->setStyleSheet(QString("color:%1;font-size:20px;background:transparent;border:none;").arg(col::AMBER()));
    hl->addWidget(icon);
    auto* title = new QLabel("finterm AI");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:700;background:transparent;border:none;")
                             .arg(col::TEXT_PRIMARY()));
    hl->addWidget(title, 1);

    const QString head_qss = QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                                     "font-size:15px;font-weight:700;}"
                                     "QPushButton:hover{background:%3;}")
                                 .arg(col::TEXT_PRIMARY(), col::BORDER_MED(), col::BG_HOVER());
    new_btn_ = new QPushButton("＋");
    new_btn_->setFixedSize(30, 26);
    new_btn_->setCursor(Qt::PointingHandCursor);
    new_btn_->setToolTip("New conversation");
    new_btn_->setStyleSheet(head_qss);
    connect(new_btn_, &QPushButton::clicked, this, &AiChatBubble::on_new_session);
    hl->addWidget(new_btn_);

    expand_btn_ = new QPushButton("⤢");
    expand_btn_->setFixedSize(30, 26);
    expand_btn_->setCursor(Qt::PointingHandCursor);
    expand_btn_->setToolTip("Expand to a full window");
    expand_btn_->setStyleSheet(head_qss);
    connect(expand_btn_, &QPushButton::clicked, this, [this]() { set_expanded(!expanded_); });
    hl->addWidget(expand_btn_);

    close_btn_ = new QPushButton("✕");
    close_btn_->setFixedSize(28, 26);
    close_btn_->setCursor(Qt::PointingHandCursor);
    close_btn_->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:none;font-size:16px;}"
                                      "QPushButton:hover{color:%2;}")
                                  .arg(col::TEXT_PRIMARY(), col::NEGATIVE()));
    connect(close_btn_, &QPushButton::clicked, this, &AiChatBubble::close_panel);
    hl->addWidget(close_btn_);
    return hdr;
}

QWidget* AiChatBubble::build_input_row() {
    // The composer is centred at the conversation's reading width, so on an
    // expanded panel it sits under the messages, not across the whole screen.
    auto* row = new QWidget;
    row->setStyleSheet(QString("background:%1;").arg(col::BG_SURFACE()));
    auto* outer = new QHBoxLayout(row);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(0);
    auto* inner = new QWidget;
    inner->setMaximumWidth(kReadingWidth + 28);
    auto* hl = new QHBoxLayout(inner);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    outer->addStretch(1);
    outer->addWidget(inner, 100);
    outer->addStretch(1);

    input_box_ = new QPlainTextEdit;
    input_box_->setPlaceholderText("Ask anything…");
    input_box_->setToolTip(QStringLiteral("Enter sends · Shift+Enter for a new line"));
    input_box_->setFont(chat_font());
    input_box_->installEventFilter(this);
    connect(input_box_, &QPlainTextEdit::textChanged, this, &AiChatBubble::fit_input);
    hl->addWidget(input_box_, 1);

    mic_btn_ = new QPushButton("🎤");
    mic_btn_->setFixedSize(38, 38);
    mic_btn_->setCheckable(true);
    mic_btn_->setCursor(Qt::PointingHandCursor);
    mic_btn_->setToolTip("Dictate — speak one message into the box");
    connect(mic_btn_, &QPushButton::clicked, this, &AiChatBubble::on_toggle_mic);
    hl->addWidget(mic_btn_, 0, Qt::AlignBottom);

    live_btn_ = new QPushButton("◉");
    live_btn_->setFixedSize(38, 38);
    live_btn_->setCheckable(true);
    live_btn_->setCursor(Qt::PointingHandCursor);
    live_btn_->setToolTip("Live voice conversation");
    connect(live_btn_, &QPushButton::clicked, this, [this]() { live_ ? exit_live() : enter_live(); });
    hl->addWidget(live_btn_, 0, Qt::AlignBottom);

    send_btn_ = new QPushButton("↑");
    send_btn_->setFixedSize(38, 38);
    send_btn_->setCursor(Qt::PointingHandCursor);
    send_btn_->setToolTip("Send  (Enter)");
    connect(send_btn_, &QPushButton::clicked, this, &AiChatBubble::on_send);
    hl->addWidget(send_btn_, 0, Qt::AlignBottom);
    fit_input();
    return row;
}

void AiChatBubble::fit_input() {
    // Grows with what is typed, one to six lines, like a chat composer.
    const QFontMetrics fm(input_box_->font());
    const int lines = std::clamp(int(input_box_->document()->size().height()), 1, 6);
    input_box_->setFixedHeight(lines * fm.lineSpacing() + 18);
}

QWidget* AiChatBubble::build_live_page() {
    auto* page = new QWidget;
    page->setStyleSheet(QString("background:%1;").arg(col::BG_SURFACE()));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 24, 24, 16);
    v->setSpacing(10);
    v->addStretch(1);

    orb_ = new ai_chat::VoiceOrb;
    orb_->on_click = [this]() {
        // The orb is the conversation's one control: interrupt a reply,
        // pause listening, or start again.
        if (is_speaking_) {
            stop_tts();
            start_listening();
        } else if (is_listening_) {
            stop_listening();
        } else if (!streaming_) {
            start_listening();
        }
    };
    v->addWidget(orb_, 3);

    live_state_lbl_ = new QLabel;
    live_state_lbl_->setAlignment(Qt::AlignCenter);
    live_state_lbl_->setStyleSheet(
        QString("color:%1;font-size:15px;font-weight:700;background:transparent;").arg(col::TEXT_PRIMARY()));
    v->addWidget(live_state_lbl_);

    live_you_lbl_ = new QLabel;
    live_you_lbl_->setAlignment(Qt::AlignCenter);
    live_you_lbl_->setWordWrap(true);
    live_you_lbl_->setStyleSheet(
        QString("color:%1;background:transparent;%2").arg(col::TEXT_SECONDARY(), chat_font_css()));
    v->addWidget(live_you_lbl_);

    live_reply_lbl_ = new QLabel;
    live_reply_lbl_->setAlignment(Qt::AlignCenter);
    live_reply_lbl_->setWordWrap(true);
    live_reply_lbl_->setStyleSheet(
        QString("color:%1;background:transparent;%2").arg(col::TEXT_PRIMARY(), chat_font_css()));
    v->addWidget(live_reply_lbl_);
    v->addStretch(1);

    auto* btns = new QHBoxLayout;
    btns->addStretch(1);
    live_mute_btn_ = new QPushButton("Mute mic");
    live_mute_btn_->setCheckable(true);
    live_mute_btn_->setCursor(Qt::PointingHandCursor);
    live_mute_btn_->setFixedHeight(34);
    live_mute_btn_->setStyleSheet(QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                                          "padding:0 16px;font-size:13px;}"
                                          "QPushButton:checked{background:%3;color:%4;border-color:%3;}")
                                      .arg(col::TEXT_PRIMARY(), col::BORDER_MED(), col::AMBER(), col::BG_BASE()));
    connect(live_mute_btn_, &QPushButton::toggled, this, [this](bool on) {
        live_muted_ = on;
        if (on)
            stop_listening();
        else if (!is_speaking_ && !streaming_)
            start_listening();
        update_status();
    });
    btns->addWidget(live_mute_btn_);
    live_end_btn_ = new QPushButton("End");
    live_end_btn_->setCursor(Qt::PointingHandCursor);
    live_end_btn_->setFixedHeight(34);
    live_end_btn_->setStyleSheet(QString("QPushButton{background:%1;color:white;border:none;padding:0 22px;"
                                         "font-size:13px;font-weight:700;}"
                                         "QPushButton:hover{background:%2;}")
                                     .arg(col::NEGATIVE(), col::WARNING()));
    connect(live_end_btn_, &QPushButton::clicked, this, &AiChatBubble::exit_live);
    btns->addWidget(live_end_btn_);
    btns->addStretch(1);
    v->addLayout(btns);
    return page;
}

// ── Open / Close ──────────────────────────────────────────────────────────────
void AiChatBubble::on_toggle_open() {
    if (is_open_)
        close_panel();
    else
        open_panel();
}

void AiChatBubble::open_panel() {
    if (is_open_)
        return;
    is_open_ = true;
    unread_count_ = 0;
    unread_badge_->hide();
    ensure_session();
    reposition();
    chat_panel_->show();
    chat_panel_->raise();
    raise();
    input_box_->setFocus();
}

void AiChatBubble::close_panel() {
    if (live_)
        exit_live(); // a hidden panel must not keep listening
    is_open_ = false;
    chat_panel_->hide();
}

// ── Session — always start fresh on first open ────────────────────────────────
void AiChatBubble::ensure_session() {
    if (!active_session_id_.isEmpty())
        return;
    on_new_session();
}

void AiChatBubble::on_new_session() {
    while (msg_layout_->count() > 1) {
        auto* item = msg_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    history_.clear();
    user_bubbles_.clear();
    streaming_bubble_.clear();
    active_session_id_.clear();

    const QString title = QString("Quick Chat %1").arg(QDateTime::currentDateTime().toString("hh:mm"));
    auto res = ChatRepository::instance().create_session(title, ai_chat::LlmService::instance().active_provider(),
                                                         ai_chat::LlmService::instance().active_model());
    if (res.is_ok()) {
        active_session_id_ = res.value().id;
        if (input_box_)
            input_box_->setEnabled(true);
        auto* hint = new QLabel("How can I help you?");
        hint->setObjectName("chatHint");
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet(QString("color:%1;font-size:16px;background:transparent;padding-top:40px;")
                                .arg(col::TEXT_TERTIARY()));
        msg_layout_->insertWidget(msg_layout_->count() - 1, hint);
    } else {
        auto* err = new QLabel("Session error — please restart.");
        err->setAlignment(Qt::AlignCenter);
        err->setStyleSheet(QString("color:%1;font-size:12px;background:transparent;").arg(col::NEGATIVE()));
        msg_layout_->insertWidget(msg_layout_->count() - 1, err);
        if (input_box_)
            input_box_->setEnabled(false);
        LOG_ERROR("AiChatBubble", "Failed to create chat session: " + QString::fromStdString(res.error()));
    }
    if (status_lbl_)
        status_lbl_->clear();
}

// ── Send ──────────────────────────────────────────────────────────────────────
void AiChatBubble::on_send() {
    const QString text = input_box_->toPlainText().trimmed();
    if (text.isEmpty() || streaming_)
        return;

    if (!ai_chat::LlmService::instance().is_configured()) {
        add_assistant_message(QStringLiteral("AI chat is not configured. Please go to **Settings → LLM Config** "
                                             "and add an API key or select the finterm provider."),
                              false);
        // Live mode shows its own page: say it there, and don't listen again
        // (every utterance would hit the same wall).
        voice_error_ = QStringLiteral("AI chat is not configured — Settings › LLM Config");
        update_status();
        return;
    }
    voice_error_.clear();

    input_box_->clear();
    ensure_session();

    // The welcome hint goes with the first message.
    if (msg_layout_->count() == 2) {
        if (auto* w = msg_layout_->itemAt(0)->widget(); w && w->objectName() == QLatin1String("chatHint")) {
            msg_layout_->takeAt(0);
            w->deleteLater();
        }
    }

    add_user_message(text);
    history_.push_back({"user", text});
    ChatRepository::instance().add_message(active_session_id_, "user", text);

    streaming_ = true;
    stream_text_.clear();
    set_ui_enabled(false);
    update_status();

    QPointer<AiChatBubble> self = this;
    auto first_chunk = std::make_shared<bool>(true);

    // The bubble is the terminal's GLOBAL chat surface, so it carries the
    // General persona's curated tools and the "Chat" role binding. Destructive
    // tools are gated behind a confirmation modal (McpProvider's auth gate →
    // ElicitPrompter), so tool access here cannot silently mutate state.
    ai_chat::PersonaScope persona;
    if (const auto* p = ai_chat::find_persona(QStringLiteral("general")))
        persona = {p->system_prompt, p->tool_globs, true};
    {
        const auto target = ai_chat::LlmService::instance().scope_for_role(QStringLiteral("ai_chat"));
        persona.provider = target.provider;
        persona.model = target.model;
        persona.api_key = target.api_key;
        persona.base_url = target.base_url;
    }
    ai_chat::LlmService::instance().chat_streaming(
        text, history_,
        [self, first_chunk](const QString& chunk, bool done) {
            QMetaObject::invokeMethod(
                qApp,
                [self, chunk, done, first_chunk]() {
                    if (!self)
                        return;
                    if (*first_chunk && !chunk.isEmpty()) {
                        *first_chunk = false;
                        self->add_assistant_message({}, true);
                    }
                    self->on_stream_chunk(chunk, done);
                },
                Qt::QueuedConnection);
        },
        /*use_tools=*/true, persona,
        // Per-call callback, not the global finished_streaming broadcast (it
        // fans out to every consumer). Already on the UI thread.
        [self](ai_chat::LlmResponse resp) {
            if (self)
                self->on_streaming_done(resp);
        });
}

void AiChatBubble::on_stream_chunk(const QString& chunk, bool done) {
    Q_UNUSED(done)
    if (chunk.isEmpty())
        return;
    stream_text_ += chunk;
    if (live_ && live_reply_lbl_)
        live_reply_lbl_->setText(stream_text_.right(260)); // the caption: the latest words
    QTextEdit* body = streaming_bubble_;
    if (!body)
        return;
    body->moveCursor(QTextCursor::End);
    body->insertPlainText(chunk);
    scroll_to_bottom();
}

void AiChatBubble::on_streaming_done(ai_chat::LlmResponse response) {
    streaming_ = false;
    set_ui_enabled(true);

    const QString content = response.success ? response.content : QString("Error: %1").arg(response.error);
    QWidget* row = nullptr;
    if (!streaming_bubble_ && !content.isEmpty()) {
        row = add_assistant_message(content, false);
    } else if (streaming_bubble_) {
        // The streamed plain text becomes rendered markdown. The model that
        // answered is named when a quota fallback switched it.
        const QString used_note = response.fell_back && !response.model_used.isEmpty()
                                      ? QStringLiteral("\n\n*Answered by %1 (%2) — fell back after a quota limit.*")
                                            .arg(response.model_used, response.provider_used)
                                      : QString();
        const QString shown = streaming_bubble_->toPlainText().isEmpty() ? content : streaming_bubble_->toPlainText();
        streaming_bubble_->setMarkdown(shown + used_note);
        row = streaming_bubble_->parentWidget();
        finish_assistant_message(row, shown);
        streaming_bubble_ = nullptr;
    }

    history_.push_back({"assistant", content});
    ChatRepository::instance().add_message(
        active_session_id_, "assistant", content,
        response.provider_used.isEmpty() ? ai_chat::LlmService::instance().active_provider() : response.provider_used,
        response.model_used.isEmpty() ? ai_chat::LlmService::instance().active_model() : response.model_used,
        response.total_tokens);

    if (!is_open_)
        update_unread(1);

    if (live_) {
        if (response.success) {
            if (live_reply_lbl_)
                live_reply_lbl_->setText(content.left(400));
            speak_text(content);
        } else {
            if (live_reply_lbl_)
                live_reply_lbl_->setText(content);
            if (!live_muted_)
                start_listening();
        }
    }
    update_status();
}

// ── Messages ──────────────────────────────────────────────────────────────────
void AiChatBubble::add_user_message(const QString& text) {
    auto* row = new QWidget;
    row->setStyleSheet("background:transparent;");
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addStretch(1);

    auto* bubble = new QWidget;
    bubble->setObjectName("userBubble");
    bubble->setStyleSheet(QString("QWidget#userBubble{background:%1;border:1px solid %2;}")
                              .arg(col::BG_RAISED(), col::BORDER_MED()));
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* bl = new QVBoxLayout(bubble);
    bl->setContentsMargins(12, 8, 12, 8);
    auto* body = new AutoText(col::TEXT_PRIMARY());
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    body->set_hug_text(text);
    bl->addWidget(body);
    rl->addWidget(bubble);
    user_bubbles_.emplace_back(bubble);

    msg_layout_->insertWidget(msg_layout_->count() - 1, row);
    rewrap_messages();
    scroll_to_bottom();
}

QWidget* AiChatBubble::add_assistant_message(const QString& markdown, bool streaming) {
    // Full width, no bubble: a small byline, the reply, then Copy / speaker.
    auto* row = new QWidget;
    row->setStyleSheet("background:transparent;");
    auto* v = new QVBoxLayout(row);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* by = new QLabel(QStringLiteral("⬡  finterm AI"));
    by->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(col::AMBER()));
    v->addWidget(by);

    auto* body = new AutoText(col::TEXT_PRIMARY());
    if (!markdown.isEmpty())
        body->setMarkdown(markdown);
    v->addWidget(body);
    if (streaming)
        streaming_bubble_ = body;

    auto* actions = new QWidget;
    actions->setObjectName("chatActions");
    actions->setStyleSheet("background:transparent;");
    auto* al = new QHBoxLayout(actions);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(4);
    v->addWidget(actions);
    actions->setVisible(false);

    msg_layout_->insertWidget(msg_layout_->count() - 1, row);
    scroll_to_bottom();
    if (!streaming)
        finish_assistant_message(row, markdown);
    return row;
}

void AiChatBubble::finish_assistant_message(QWidget* row, const QString& markdown) {
    if (!row)
        return;
    auto* actions = row->findChild<QWidget*>("chatActions");
    if (!actions || actions->layout()->count() > 0)
        return;
    auto* al = static_cast<QHBoxLayout*>(actions->layout());
    auto* copy = flat_button(QStringLiteral("Copy"), QStringLiteral("Copy the reply"));
    connect(copy, &QPushButton::clicked, this, [markdown, copy]() {
        QApplication::clipboard()->setText(markdown);
        copy->setText(QStringLiteral("Copied"));
        QTimer::singleShot(1500, copy, [copy]() { copy->setText(QStringLiteral("Copy")); });
    });
    al->addWidget(copy);
    // The reply read aloud in the engine's voice — News' TL;DR speaker.
    if (services::TtsService::instance().engine_or_local_available()) {
        auto* speak = new QPushButton;
        speak->setCursor(Qt::PointingHandCursor);
        speak->setFixedSize(28, 22);
        speak->setIconSize(QSize(16, 16));
        speak->setStyleSheet(QString("QPushButton{background:transparent;border:1px solid transparent;}"
                                     "QPushButton:hover{border-color:%1;}")
                                 .arg(col::BORDER_MED()));
        al->addWidget(speak);
        auto* toggle = new ui::ReadAloudToggle(
            speak,
            {QStringLiteral("Listen"), QStringLiteral("Pause"), QStringLiteral("Resume"), QStringLiteral("Stop"),
             QStringLiteral("Preparing"), /*icons=*/true},
            QStringLiteral("Read the reply aloud (right-click for the voice)"));
        const QString spoken = services::speech::from_markdown(markdown);
        connect(speak, &QPushButton::clicked, speak, [toggle, spoken]() {
            toggle->toggle([spoken]() { return services::TtsService::instance().speak_via_engine(spoken); });
        });
    }
    al->addStretch(1);
    actions->setVisible(true);
}

void AiChatBubble::scroll_to_bottom() {
    QTimer::singleShot(30, this, [this]() {
        if (scroll_area_ && scroll_area_->verticalScrollBar())
            scroll_area_->verticalScrollBar()->setValue(scroll_area_->verticalScrollBar()->maximum());
    });
}

void AiChatBubble::set_ui_enabled(bool enabled) {
    send_btn_->setEnabled(enabled);
    mic_btn_->setEnabled(enabled);
    input_box_->setEnabled(enabled);
    send_btn_->setText(enabled ? "↑" : "…");
}

void AiChatBubble::update_unread(int delta) {
    unread_count_ += delta;
    if (unread_count_ > 0) {
        unread_badge_->setText(unread_count_ > 9 ? "9+" : QString::number(unread_count_));
        unread_badge_->show();
    } else {
        unread_badge_->hide();
    }
}

// ── Speech in ─────────────────────────────────────────────────────────────────
void AiChatBubble::on_toggle_mic() {
    if (is_listening_)
        stop_listening();
    else
        start_listening();
}

void AiChatBubble::start_listening() {
    // Never while a reply is being written or spoken (the mic would hear it,
    // and on_send would drop what the user says), never with the panel shut.
    if (!is_open_ || is_listening_ || is_speaking_ || streaming_ || (live_ && live_muted_))
        return;
    voice_error_.clear();
    services::SpeechService::instance().start_listening();
}

void AiChatBubble::resume_live_listening() {
    // The delayed restarts: by the time they fire the user may have ended the
    // conversation or closed the panel.
    if (live_ && !live_muted_)
        start_listening();
}

void AiChatBubble::stop_listening() {
    services::SpeechService::instance().stop_listening();
}

void AiChatBubble::on_stt_listening_changed(bool active) {
    is_listening_ = active;
    mic_btn_->setChecked(active && !live_);
    update_status();
}

void AiChatBubble::on_transcription(const QString& text) {
    if (text.trimmed().isEmpty() || !is_open_)
        return;
    if (!live_) {
        // Dictation: into the box, for the user to edit and send.
        input_box_->setPlainText(text);
        input_box_->moveCursor(QTextCursor::End);
        stop_listening();
        mic_btn_->setChecked(false);
        update_status();
        return;
    }
    // Live: the mic stops while the reply is worked out and spoken — it would
    // otherwise hear the reply and answer itself.
    stop_listening();
    if (live_you_lbl_)
        live_you_lbl_->setText(QStringLiteral("You: ") + text);
    if (live_reply_lbl_)
        live_reply_lbl_->clear();
    input_box_->setPlainText(text);
    on_send();
}

void AiChatBubble::on_stt_error(const QString& message) {
    // Kept until the next listen or send: the listening_changed(false) that
    // follows an error must not paint over it with "Paused".
    voice_error_ = message;
    update_status();
}

// ── Live voice ────────────────────────────────────────────────────────────────
void AiChatBubble::enter_live() {
    if (!is_open_)
        open_panel();
    live_ = true;
    live_muted_ = false;
    live_mute_btn_->setChecked(false);
    live_btn_->setChecked(true);
    live_you_lbl_->clear();
    live_reply_lbl_->setText(QStringLiteral("Say something — I'm listening."));
    stack_->setCurrentIndex(1);
    ensure_session();
    QTimer::singleShot(200, this, &AiChatBubble::resume_live_listening);
    update_status();
}

void AiChatBubble::exit_live() {
    live_ = false;
    stop_listening();
    stop_tts();
    live_btn_->setChecked(false);
    stack_->setCurrentIndex(0);
    update_status();
    scroll_to_bottom();
}

// ── Speech out ────────────────────────────────────────────────────────────────
void AiChatBubble::speak_text(const QString& text) {
    const QString spoken = services::speech::from_markdown(text).trimmed();
    if (spoken.isEmpty()) {
        speech_finished();
        return;
    }
    is_speaking_ = true;
    update_status();
    // The engine's natural voice (News read-aloud: Kokoro / Piper), falling
    // back to the platform voice when no engine is configured.
    auto& tts = services::TtsService::instance();
    if (tts.engine_or_local_available() && tts.speak_via_engine(spoken)) {
        tts_session_ = tts.session();
        return;
    }
#ifdef HAS_QT_TTS
    if (!tts_engine_) {
        tts_engine_ = new QTextToSpeech(this);
        connect(tts_engine_, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
            if ((state == QTextToSpeech::Ready || state == QTextToSpeech::Error) && is_speaking_)
                speech_finished();
        });
    }
    tts_session_ = -1;
    tts_engine_->say(spoken.left(2000));
#else
    voice_error_ = QStringLiteral("No voice available — set one in Settings › Voice");
    speech_finished();
#endif
}

void AiChatBubble::stop_tts() {
    const bool was = is_speaking_;
    is_speaking_ = false;
    if (was && tts_session_ >= 0)
        services::TtsService::instance().stop();
#ifdef HAS_QT_TTS
    if (tts_engine_)
        tts_engine_->stop();
#endif
    tts_session_ = -1;
    update_status();
}

void AiChatBubble::speech_finished() {
    is_speaking_ = false;
    tts_session_ = -1;
    update_status();
    if (live_ && !live_muted_ && !streaming_)
        QTimer::singleShot(300, this, &AiChatBubble::resume_live_listening);
}

// ── Status ────────────────────────────────────────────────────────────────────
void AiChatBubble::update_status() {
    using S = ai_chat::VoiceOrb::State;
    QString text;
    S orb = S::Idle;
    if (is_speaking_) {
        text = QStringLiteral("Speaking — click the orb to interrupt");
        orb = S::Speaking;
    } else if (streaming_) {
        text = QStringLiteral("Thinking…");
        orb = S::Thinking;
    } else if (is_listening_) {
        text = live_ ? QStringLiteral("Listening…") : QStringLiteral("● Listening — speak now…");
        orb = S::Listening;
    } else if (!voice_error_.isEmpty()) {
        text = QStringLiteral("⚠ ") + voice_error_;
    } else if (live_) {
        text = live_muted_ ? QStringLiteral("Mic muted") : QStringLiteral("Paused — click the orb to talk");
    }
    if (orb_)
        orb_->set_state(orb);
    if (live_state_lbl_)
        live_state_lbl_->setText(text);
    if (status_lbl_ && !live_)
        status_lbl_->setText(text);
}

// ── Event filter ──────────────────────────────────────────────────────────────
bool AiChatBubble::eventFilter(QObject* obj, QEvent* e) {
    if (obj == bubble_btn_) {
        if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton)
            return true;
        if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
            on_toggle_open();
            return true;
        }
    }
    if (scroll_area_ && obj == scroll_area_->viewport() && e->type() == QEvent::Resize) {
        rewrap_messages();
        return false;
    }
    if (obj == resize_grip_) {
        // Bottom-right stays put; dragging the top-left corner up/left grows.
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                resizing_ = true;
                resize_press_pos_ = me->globalPosition().toPoint();
                resize_start_size_ = chat_panel_->size();
                return true;
            }
        } else if (e->type() == QEvent::MouseMove && resizing_) {
            auto* me = static_cast<QMouseEvent*>(e);
            const QPoint d = me->globalPosition().toPoint() - resize_press_pos_;
            user_panel_size_ = QSize(resize_start_size_.width() - d.x(), resize_start_size_.height() - d.y());
            if (panel_host_)
                user_panel_size_ = clamp_panel_size(user_panel_size_, panel_host_->size());
            reposition();
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && resizing_) {
            resizing_ = false;
            SettingsRepository::instance().set("ui.chat_panel.w", QString::number(user_panel_size_.width()));
            SettingsRepository::instance().set("ui.chat_panel.h", QString::number(user_panel_size_.height()));
            return true;
        }
    }
    if (obj == input_box_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
            on_send();
            return true;
        }
    }
    if (obj == panel_host_ && e->type() == QEvent::Resize) {
        reposition();
        return false;
    }
    return QWidget::eventFilter(obj, e);
}

} // namespace finterm
