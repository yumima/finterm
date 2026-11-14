#include "screens/social/SocialScreen.h"

#include "screens/social/BriefsPanel.h"
#include "screens/social/SocialBackend.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "ui/components/TooltipText.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/notifications/ToastService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace finterm::screens {

namespace social_backend {
namespace {
constexpr const char* kScript = "social_app.py";
} // namespace


/// The interpreter for social_app.py. The backend reads the posting
/// credentials through libsecret's PyGObject binding (finterm_keyring.py),
/// which exists only as the distro's python3-gi, compiled for the distro's
/// CPython. The app's own UV venv is isolated and a different CPython, so
/// under PythonRunner every keyring read failed ("libsecret not available")
/// and no channel could ever read as connected. The backend needs nothing
/// else outside the standard library.
QString backend_python() {
#if defined(Q_OS_LINUX)
    static const QString system_python = QStandardPaths::findExecutable(
        QStringLiteral("python3"), {QStringLiteral("/usr/bin"), QStringLiteral("/bin")});
    if (!system_python.isEmpty())
        return system_python;
#endif
    return python::PythonRunner::instance().python_path();
}

/// One backend call, in a process of its own rather than PythonRunner's
/// queue: a confirmed post must not wait behind scrapers holding every
/// slot. The process is not owned by @p ctx — closing the screen or a
/// dialog must not kill a post mid-flight — and @p done is dropped once
/// @p ctx is gone.
///
/// The payload goes to the process's stdin, never its command line: argv is
/// readable by every local user (ps, /proc/<pid>/cmdline) and payloads carry
/// PINs, codes and passwords. social_app.py refuses those actions' payloads
/// from argv.
void call_backend(QObject* ctx, const QString& action, const QJsonObject& payload, int timeout_ms, Done done) {
    QPointer<QObject> guard(ctx);
    auto& runner = python::PythonRunner::instance();
    const QString python = backend_python();
    const QString script = QDir(runner.scripts_dir()).filePath(QString::fromLatin1(kScript));
    auto not_started = [guard, action, done](const QString& why) {
        LOG_WARN("Social", action + QStringLiteral(": ") + why);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, done, why]() {
                if (guard)
                    done({}, why, Outcome::NotStarted);
            },
            Qt::QueuedConnection);
    };
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        not_started(python.isEmpty() ? QStringLiteral("no Python interpreter found")
                                     : QStringLiteral("%1 is missing from the app's scripts").arg(kScript));
        return;
    }

    auto* proc = new QProcess(QCoreApplication::instance());
    proc->setProcessEnvironment(runner.build_python_env());
    proc->setWorkingDirectory(runner.scripts_dir());
    auto* timer = new QTimer(proc);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, proc, [proc, action, timeout_ms]() {
        LOG_WARN("Social", QStringLiteral("%1 timed out after %2s — killing it").arg(action).arg(timeout_ms / 1000));
        proc->kill();
    });
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, not_started](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return; // the rest end in finished()
        not_started(QStringLiteral("could not start Python (%1)").arg(proc->errorString()));
        proc->deleteLater();
    });
    QObject::connect(
        proc, &QProcess::finished, proc, [proc, timer, guard, action, done](int exit_code, QProcess::ExitStatus) {
            timer->stop();
            proc->deleteLater();
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            const QString errout = QString::fromUtf8(proc->readAllStandardError());
            if (!guard)
                return;
            const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(out).toUtf8());
            const QJsonObject o = doc.object();
            const bool answered = doc.isObject() && !o.isEmpty();
            QString err = o.value(QLatin1String("error")).toString();
            if (err.isEmpty() && !answered) {
                // The tail of stderr is the part that says what happened.
                const QString tail = errout.trimmed().section(QLatin1Char('\n'), -1).left(200);
                err = tail.isEmpty() ? QStringLiteral("the social backend gave no answer (exit %1)").arg(exit_code)
                                     : tail;
            }
            if (!err.isEmpty())
                LOG_WARN("Social", action + QStringLiteral(": ") + err);
            done(o, err, answered ? Outcome::Answered : Outcome::NoAnswer);
        });
    const QByteArray input = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QObject::connect(proc, &QProcess::started, proc, [proc, input]() {
        proc->write(input);
        proc->closeWriteChannel(); // EOF once written: the backend reads to the end
    });
    proc->start(python, {script, action, QStringLiteral("-")});
    if (timeout_ms > 0)
        timer->start(timeout_ms);
}

} // namespace social_backend

namespace {
namespace so = finterm::social;
namespace ou = finterm::screens::ownership_ui;
namespace fmt = finterm::ui::formatting;

// issue.json (15s) then the page itself (10s), plus interpreter start-up.
constexpr int kBriefTimeoutMs = 60'000;
// A post is fetch issue.json + fetch page + the network's API. Killing the
// process mid-post would leave the outcome unknown, so it gets room.
constexpr int kPublishTimeoutMs = 120'000;
constexpr int kConnectTimeoutMs = 60'000;
constexpr qint64 kStaleAfterMs = 10 * 60 * 1000;

using social_backend::Done;
using social_backend::Outcome;
using social_backend::call_backend;

QString esc(const QString& s) { return s.toHtmlEscaped(); }

QString link_html(const QString& url, const QString& text) {
    // Double-quoted: toHtmlEscaped() escapes " but not '.
    return QStringLiteral("<a style=\"color:%1;\" href=\"%2\">%3</a>").arg(ui::colors::CYAN(), esc(url), esc(text));
}

QString mark(bool ok) {
    return QStringLiteral("<span style='color:%1;font-weight:700;'>%2</span>")
        .arg(ok ? ui::colors::POSITIVE() : ui::colors::NEGATIVE(), ok ? QStringLiteral("✓") : QStringLiteral("✗"));
}

QPushButton* small_button(const QString& text, QWidget* parent = nullptr) {
    auto* b = new QPushButton(text, parent);
    b->setObjectName("socialBtn");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel* chip() {
    auto* l = new QLabel;
    l->setObjectName("socialChip");
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    return l;
}
} // namespace

SocialScreen::SocialScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("socialScreen");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName("socialSplitter");
    split->setChildrenCollapsible(false);
    split->addWidget(build_channels_pane());
    // BRIEFS (generate · review · correct · schedule · publish) and the
    // PUBLISHED issue (what is live, and what was posted) share the middle.
    auto* middle = new QWidget;
    auto* mv = new QVBoxLayout(middle);
    mv->setContentsMargins(8, 8, 8, 0);
    mv->setSpacing(4);
    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(6);
    auto* briefs_btn = small_button(QStringLiteral("BRIEFS"));
    auto* published_btn = small_button(QStringLiteral("PUBLISHED"));
    briefs_btn->setToolTip(QStringLiteral("Your briefs: this week's (last week + next week) and theme briefs — "
                                          "read, correct, schedule, publish"));
    published_btn->setToolTip(QStringLiteral("The issue that is live on the site, and everything posted"));
    for (auto* b : {briefs_btn, published_btn}) {
        b->setCheckable(true);
        tabs->addWidget(b);
    }
    tabs->addStretch(1);
    mv->addLayout(tabs);
    auto* stack = new QStackedWidget;
    briefs_ = new BriefsPanel;
    stack->addWidget(briefs_);
    stack->addWidget(build_issue_pane());
    mv->addWidget(stack, 1);
    briefs_btn->setChecked(true);
    connect(briefs_btn, &QPushButton::clicked, this, [stack, briefs_btn, published_btn]() {
        stack->setCurrentIndex(0);
        briefs_btn->setChecked(true);
        published_btn->setChecked(false);
    });
    connect(published_btn, &QPushButton::clicked, this, [stack, briefs_btn, published_btn]() {
        stack->setCurrentIndex(1);
        briefs_btn->setChecked(false);
        published_btn->setChecked(true);
    });
    connect(briefs_, &BriefsPanel::published, this, [this]() {
        load_brief();
        load_history();
    });
    split->addWidget(middle);
    split->addWidget(build_composer_pane());
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({240, 720, 480});
    root->addWidget(split, 1);

    apply_styles();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens&) {
        apply_styles();
        render_channels();
        render_brief();
        render_history();
        render_composer();
    });
    render_channels();
    render_brief();
    render_history();
    render_composer();
}

void SocialScreen::apply_styles() {
    setStyleSheet(ou::table_stylesheet() +
                  QStringLiteral("#socialScreen QSplitter::handle { background:%3; width:1px; }"
                                 "#socialScreen QLabel { background:transparent; font-size:12px; }"
                                 "#socialScreen QLabel#socialDim { color:%2; }"
                                 "#socialScreen QLabel#socialTitle { color:%4; font-weight:700; font-size:13px; }"
                                 "#socialScreen QLabel#socialError { color:%5; }"
                                 "#socialScreen QLabel#socialChip { border:1px solid %3; padding:1px 6px; }"
                                 "#socialScreen QWidget#socialRow { border-bottom:1px solid %3; }"
                                 "#socialScreen QPushButton#socialBtn { color:%2; background:transparent; "
                                 "  border:1px solid %3; padding:2px 8px; font-size:12px; }"
                                 "#socialScreen QPushButton#socialBtn:hover { color:%1; border-color:%2; }"
                                 "#socialScreen QPushButton#socialBtn:checked { color:%4; border-color:%4; }"
                                 "#socialScreen QPushButton#socialBtn:disabled { color:%6; border-color:%3; }"
                                 "#socialScreen QPushButton#socialPublish { color:%4; background:transparent; "
                                 "  border:1px solid %4; padding:4px 10px; font-weight:700; }"
                                 "#socialScreen QPushButton#socialPublish:hover { background:%7; }"
                                 "#socialScreen QPushButton#socialPublish:disabled { color:%6; border-color:%3; }"
                                 "#socialScreen QPlainTextEdit { background:%8; color:%1; border:1px solid %3; "
                                 "  font-size:13px; }"
                                 "#socialScreen QLineEdit#socialLine { background:%8; color:%1; border:1px solid %3; "
                                 "  padding:2px 4px; font-size:13px; }"
                                 "#socialScreen QScrollArea#socialScroll, #socialScreen QScrollArea#socialScroll > QWidget > QWidget "
                                 "  { background:transparent; }"
                                 "#socialScreen QListWidget { background:%8; border:1px solid %3; }"
                                 "#socialScreen QListWidget::item:selected { background:%7; color:%4; }")
                      .arg(ui::colors::TEXT_PRIMARY(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                           ui::colors::AMBER(), ui::colors::NEGATIVE(), ui::colors::TEXT_TERTIARY(),
                           ui::colors::BG_HOVER(), ui::colors::BG_SURFACE()));
}

// ── Panes ───────────────────────────────────────────────────────────────────

QWidget* SocialScreen::build_channels_pane() {
    auto* pane = new QWidget;
    auto* v = new QVBoxLayout(pane);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);
    v->addWidget(ou::section_title(QStringLiteral("CHANNELS")).widget);
    channels_box_ = new QVBoxLayout;
    channels_box_->setContentsMargins(0, 0, 0, 0);
    channels_box_->setSpacing(0);
    v->addLayout(channels_box_);
    v->addSpacing(10);
    v->addWidget(ou::section_title(QStringLiteral("SOURCES")).widget);
    auto* sources = new QListWidget;
    sources->addItem(QStringLiteral("Weekly brief"));
    sources->item(0)->setToolTip(QStringLiteral("The weekly insider-buying brief published at yumima.github.io/finterm"));
    sources->setCurrentRow(0);
    sources->setFixedHeight(sources->sizeHintForRow(0) + 2 * sources->frameWidth() + 4);
    v->addWidget(sources);
    v->addStretch(1);
    return pane;
}

QWidget* SocialScreen::build_issue_pane() {
    auto* pane = new QWidget;
    auto* v = new QVBoxLayout(pane);
    v->setContentsMargins(0, 4, 0, 8); // inside the middle pane's margins
    v->setSpacing(6);

    issue_title_ = new QLabel;
    issue_title_->setObjectName("socialTitle");
    v->addWidget(issue_title_);

    auto* chips = new QHBoxLayout;
    chips->setSpacing(6);
    chip_live_ = chip();
    chip_week_ = chip();
    chip_posted_ = chip();
    chips->addWidget(chip_live_);
    chips->addWidget(chip_week_);
    chips->addWidget(chip_posted_);
    chips->addStretch(1);
    open_page_btn_ = small_button(QStringLiteral("Open page"));
    connect(open_page_btn_, &QPushButton::clicked, this, [this]() {
        if (brief_.loaded)
            QDesktopServices::openUrl(QUrl(brief_.url));
    });
    chips->addWidget(open_page_btn_);
    refresh_btn_ = small_button(QStringLiteral("Refresh"));
    refresh_btn_->setToolTip(QStringLiteral("Reload channels, the published issue and the posting history"));
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() { refresh_all(); });
    chips->addWidget(refresh_btn_);
    v->addLayout(chips);

    busy_label_ = new QLabel;
    busy_label_->setObjectName("socialDim");
    v->addWidget(busy_label_);
    issue_error_ = new QLabel;
    issue_error_->setObjectName("socialError");
    issue_error_->setWordWrap(true);
    issue_error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    issue_error_->setTextFormat(Qt::PlainText); // backend errors are shown verbatim
    issue_error_->hide();
    v->addWidget(issue_error_);

    rows_table_ = ou::make_table({QStringLiteral("Symbol"), QStringLiteral("Company"), QStringLiteral("Insiders"),
                                  QStringLiteral("Value"), QStringLiteral("Trade dates"), QStringLiteral("Roles")});
    v->addWidget(rows_table_, 3);
    rows_note_ = ou::note_label();
    rows_note_->setTextFormat(Qt::PlainText);
    v->addWidget(rows_note_);

    v->addSpacing(6);
    v->addWidget(ou::section_title(QStringLiteral("HISTORY")).widget);
    history_table_ = ou::make_table({QStringLiteral("Issue"), QStringLiteral("Network"), QStringLiteral("Post")});
    // Underlined post cells open in the browser: show a hand over them.
    history_table_->setMouseTracking(true);
    connect(history_table_, &QTableWidget::cellEntered, this, [this](int row, int col) {
        const auto* it = history_table_->item(row, col);
        history_table_->viewport()->setCursor(it && !it->data(Qt::UserRole).toString().isEmpty()
                                                  ? Qt::PointingHandCursor
                                                  : Qt::ArrowCursor);
    });
    connect(history_table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (auto* it = history_table_->item(row, col)) {
            const QString link = it->data(Qt::UserRole).toString();
            if (!link.isEmpty())
                QDesktopServices::openUrl(QUrl(link));
        }
    });
    v->addWidget(history_table_, 1);
    history_note_ = ou::note_label();
    history_note_->setTextFormat(Qt::PlainText);
    v->addWidget(history_note_);
    return pane;
}

QWidget* SocialScreen::build_composer_pane() {
    auto* pane = new QWidget;
    auto* outer = new QVBoxLayout(pane);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(ou::section_title(QStringLiteral("COMPOSER")).widget);

    auto* nets = new QHBoxLayout;
    nets->setSpacing(4);
    network_group_ = new QButtonGroup(this);
    network_group_->setExclusive(true);
    for (const auto& n : so::all_networks()) {
        auto* b = small_button(so::network_label(n));
        b->setCheckable(true);
        b->setChecked(n == network_);
        network_group_->addButton(b);
        network_btns_.insert(n, b);
        connect(b, &QPushButton::clicked, this, [this, n]() { select_network(n); });
        nets->addWidget(b);
    }
    nets->addStretch(1);
    outer->addLayout(nets);

    composer_stack_ = new QStackedWidget;
    outer->addWidget(composer_stack_, 1);

    // Page 0: the automated networks.
    auto* auto_page = new QWidget;
    auto* v = new QVBoxLayout(auto_page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    posting_as_ = new QLabel;
    posting_as_->setObjectName("socialDim");
    v->addWidget(posting_as_);

    editor_ = new QPlainTextEdit;
    editor_->setPlaceholderText(QStringLiteral("The generated post appears here once the issue loads."));
    connect(editor_, &QPlainTextEdit::textChanged, this, [this]() {
        if (setting_editor_)
            return;
        drafts_[network_] = editor_->toPlainText();
        render_composer();
    });
    v->addWidget(editor_, 1);

    auto* under = new QHBoxLayout;
    counter_ = new QLabel;
    under->addWidget(counter_);
    under->addStretch(1);
    reset_btn_ = small_button(QStringLiteral("Reset to generated"));
    connect(reset_btn_, &QPushButton::clicked, this, [this]() {
        drafts_[network_] = generated_.value(network_);
        setting_editor_ = true;
        editor_->setPlainText(drafts_.value(network_));
        setting_editor_ = false;
        render_composer();
    });
    under->addWidget(reset_btn_);
    v->addLayout(under);

    v->addSpacing(6);
    v->addWidget(ou::section_title(QStringLiteral("PRE-FLIGHT")).widget);
    for (int i = 0; i < 6; ++i) {
        auto* l = new QLabel;
        l->setTextFormat(Qt::RichText);
        l->setWordWrap(true);
        check_labels_.append(l);
        v->addWidget(l);
    }

    v->addSpacing(6);
    publish_btn_ = new QPushButton;
    publish_btn_->setObjectName("socialPublish");
    publish_btn_->setCursor(Qt::PointingHandCursor);
    connect(publish_btn_, &QPushButton::clicked, this, [this]() { confirm_publish(); });
    v->addWidget(publish_btn_);
    composer_stack_->addWidget(auto_page);

    // Page 1: Reddit / Hacker News, by hand.
    composer_stack_->addWidget(build_manual_page());

    status_label_ = new QLabel;
    status_label_->setWordWrap(true);
    status_label_->setTextFormat(Qt::RichText);
    status_label_->setOpenExternalLinks(true);
    status_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    outer->addWidget(status_label_);
    return pane;
}

QWidget* SocialScreen::build_manual_page() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto* pick = new QHBoxLayout;
    auto* pl = new QLabel(QStringLiteral("Community"));
    pl->setObjectName("socialDim");
    pick->addWidget(pl);
    launch_combo_ = new QComboBox;
    launch_combo_->setObjectName("socialCombo");
    connect(launch_combo_, &QComboBox::activated, this,
            [this](int i) { show_launch_target(launch_combo_->itemData(i).toString()); });
    pick->addWidget(launch_combo_, 1);
    v->addLayout(pick);

    launch_info_ = new QLabel;
    launch_info_->setTextFormat(Qt::RichText);
    launch_info_->setWordWrap(true);
    launch_info_->setOpenExternalLinks(true);
    v->addWidget(launch_info_);

    // Edits are kept per target, so switching communities loses nothing.
    auto keep = [this]() {
        if (setting_editor_)
            return;
        if (const auto* t = current_launch_target()) {
            launch_drafts_[t->id] = {launch_title_->text(), launch_body_->toPlainText(), launch_url_->text(),
                                     launch_comment_->toPlainText()};
            render_launch_counts(); // per keystroke: only what the text changes
        }
    };
    auto* tl = new QHBoxLayout;
    auto* tlab = new QLabel(QStringLiteral("Title"));
    tlab->setObjectName("socialDim");
    tl->addWidget(tlab);
    tl->addStretch(1);
    launch_title_count_ = new QLabel;
    tl->addWidget(launch_title_count_);
    v->addLayout(tl);
    launch_title_ = new QLineEdit;
    launch_title_->setObjectName("socialLine");
    connect(launch_title_, &QLineEdit::textEdited, this, keep);
    v->addWidget(launch_title_);

    launch_url_label_ = new QLabel(QStringLiteral("URL"));
    launch_url_label_->setObjectName("socialDim");
    v->addWidget(launch_url_label_);
    launch_url_ = new QLineEdit;
    launch_url_->setObjectName("socialLine");
    connect(launch_url_, &QLineEdit::textEdited, this, keep);
    v->addWidget(launch_url_);

    launch_body_label_ = new QLabel(QStringLiteral("Body (Markdown)"));
    launch_body_label_->setObjectName("socialDim");
    v->addWidget(launch_body_label_);
    launch_body_ = new QPlainTextEdit;
    connect(launch_body_, &QPlainTextEdit::textChanged, this, keep);
    v->addWidget(launch_body_, 3);

    launch_comment_label_ = new QLabel(QStringLiteral("First comment — post it yourself right after"));
    launch_comment_label_->setObjectName("socialDim");
    v->addWidget(launch_comment_label_);
    launch_comment_ = new QPlainTextEdit;
    connect(launch_comment_, &QPlainTextEdit::textChanged, this, keep);
    v->addWidget(launch_comment_, 2);

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);
    copy_title_btn_ = small_button(QStringLiteral("Copy title"));
    connect(copy_title_btn_, &QPushButton::clicked, this,
            [this]() { copy_to_clipboard(launch_title_->text(), QStringLiteral("Title")); });
    row1->addWidget(copy_title_btn_);
    copy_body_btn_ = small_button(QStringLiteral("Copy body"));
    connect(copy_body_btn_, &QPushButton::clicked, this, [this]() {
        const auto* t = current_launch_target();
        if (t && t->is_link())
            copy_to_clipboard(launch_url_->text(), QStringLiteral("URL"));
        else
            copy_to_clipboard(launch_body_->toPlainText(), QStringLiteral("Body"));
    });
    row1->addWidget(copy_body_btn_);
    copy_comment_btn_ = small_button(QStringLiteral("Copy first comment"));
    connect(copy_comment_btn_, &QPushButton::clicked, this,
            [this]() { copy_to_clipboard(launch_comment_->toPlainText(), QStringLiteral("First comment")); });
    row1->addWidget(copy_comment_btn_);
    row1->addStretch(1);
    launch_reset_btn_ = small_button(QStringLiteral("Reset draft"));
    launch_reset_btn_->setToolTip(QStringLiteral("Back to the draft from launch_posts.py (this week's table refilled)"));
    connect(launch_reset_btn_, &QPushButton::clicked, this, [this]() {
        if (const auto* t = current_launch_target()) {
            launch_drafts_[t->id] = launch_templates_.value(t->id);
            show_launch_target(t->id);
        }
    });
    row1->addWidget(launch_reset_btn_);
    v->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->setSpacing(4);
    open_submit_btn_ = new QPushButton;
    open_submit_btn_->setObjectName("socialPublish");
    open_submit_btn_->setCursor(Qt::PointingHandCursor);
    connect(open_submit_btn_, &QPushButton::clicked, this, [this]() { open_submit_page(); });
    row2->addWidget(open_submit_btn_, 1);
    mark_posted_btn_ = small_button(QStringLiteral("Mark as posted…"));
    mark_posted_btn_->setToolTip(QStringLiteral("Record the post's link once it is up (one launch post per community)"));
    connect(mark_posted_btn_, &QPushButton::clicked, this, [this]() { open_mark_posted(); });
    row2->addWidget(mark_posted_btn_);
    v->addLayout(row2);

    v->addSpacing(4);
    v->addWidget(ou::section_title(QStringLiteral("RULES")).widget);
    launch_rules_ = new QLabel;
    launch_rules_->setTextFormat(Qt::RichText);
    launch_rules_->setWordWrap(true);
    launch_rules_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    launch_rules_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("socialScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(launch_rules_);
    v->addWidget(scroll, 2);
    return page;
}

// ── Loading ─────────────────────────────────────────────────────────────────

void SocialScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!last_refresh_.isValid() || last_refresh_.msecsTo(QDateTime::currentDateTime()) > kStaleAfterMs)
        refresh_all();
}

void SocialScreen::refresh_all() {
    last_refresh_ = QDateTime::currentDateTime();
    load_channels();
    load_brief(); // its answer loads the launch drafts, filled from its rows
    load_history();
}

void SocialScreen::load_channels() {
    const int seq = ++channels_seq_;
    channels_busy_ = true;
    render_busy();
    call_backend(this, QStringLiteral("channels"), {}, python::PythonRunner::kProcessTimeoutMs,
                 [this, seq](const QJsonObject& o, const QString& err, Outcome) {
                     if (seq != channels_seq_)
                         return;
                     channels_busy_ = false;
                     if (err.isEmpty()) {
                         channels_ = so::parse_channels(o);
                         channels_loaded_ = true;
                         channels_error_.clear();
                         // The composer must not stay on a network that is
                         // switched off: fall back to the first enabled one.
                         if (!so::network_enabled(channels_, network_)) {
                             for (const auto& n : so::all_networks())
                                 if (so::network_enabled(channels_, n)) {
                                     select_network(n);
                                     break;
                                 }
                         }
                     } else {
                         channels_error_ = err;
                     }
                     render_channels();
                     render_composer();
                     render_busy();
                 });
}

void SocialScreen::load_brief() {
    const int seq = ++brief_seq_;
    brief_busy_ = true;
    render_busy();
    render_composer(); // Publish waits for the fresh answer
    call_backend(this, QStringLiteral("brief"), {}, kBriefTimeoutMs,
                 [this, seq](const QJsonObject& o, const QString& err, Outcome) {
                     if (seq != brief_seq_)
                         return;
                     brief_busy_ = false;
                     so::Brief b = err.isEmpty() ? so::parse_brief(o) : so::Brief{};
                     if (err.isEmpty() && b.loaded) {
                         brief_ = b;
                         brief_error_.clear();
                         adopt_generated_texts();
                     } else {
                         brief_error_ = err.isEmpty() ? QStringLiteral("the backend's answer named no issue") : err;
                         // Kept on screen, but no longer good enough to publish against.
                         brief_.stale = brief_.loaded;
                     }
                     render_brief();
                     render_composer();
                     render_busy();
                     load_launch(); // after the brief: the drafts' table comes from its rows
                 });
}

void SocialScreen::load_history() {
    const int seq = ++history_seq_;
    history_busy_ = true;
    render_busy();
    call_backend(this, QStringLiteral("history"), {}, python::PythonRunner::kProcessTimeoutMs,
                 [this, seq](const QJsonObject& o, const QString& err, Outcome) {
                     if (seq != history_seq_)
                         return;
                     history_busy_ = false;
                     if (err.isEmpty()) {
                         history_ = so::parse_history(o);
                         history_loaded_ = true;
                         history_error_.clear();
                     } else {
                         history_error_ = err;
                     }
                     render_history();
                     render_busy();
                 });
}

void SocialScreen::load_launch() {
    const int seq = ++launch_seq_;
    launch_busy_ = true;
    render_busy();
    // The rows shown beside the composer, so the backend needn't fetch
    // issue.json again and the table always matches the issue on screen.
    QJsonObject payload;
    if (brief_.loaded)
        payload = {{QStringLiteral("issue"), brief_.issue}, {QStringLiteral("rows"), brief_.rows}};
    call_backend(this, QStringLiteral("launch"), payload, python::PythonRunner::kProcessTimeoutMs,
                 [this, seq](const QJsonObject& o, const QString& err, Outcome) {
                     if (seq != launch_seq_)
                         return;
                     launch_busy_ = false;
                     const so::LaunchSet l = err.isEmpty() ? so::parse_launch(o) : so::LaunchSet{};
                     if (l.loaded) {
                         launch_ = l;
                         launch_error_.clear();
                         adopt_launch_templates();
                         if (so::is_manual_network(network_))
                             show_launch_target(launch_target_.value(network_)); // a template may have changed
                     } else {
                         launch_error_ = err.isEmpty() ? QStringLiteral("the backend sent no launch drafts") : err;
                     }
                     render_channels();
                     render_composer();
                     render_busy();
                 });
}

void SocialScreen::adopt_launch_templates() {
    // Like the issue drafts: a draft still equal to its old template follows
    // the new one (e.g. this week's table arrived); an edited one is kept.
    for (const auto& t : std::as_const(launch_.targets)) {
        const LaunchDraft tpl{t.title, t.body, t.url, t.first_comment};
        if (!launch_drafts_.contains(t.id) || launch_drafts_.value(t.id) == launch_templates_.value(t.id))
            launch_drafts_[t.id] = tpl;
        launch_templates_[t.id] = tpl;
    }
}

void SocialScreen::adopt_generated_texts() {
    // A new issue replaces every draft; the same issue reloaded keeps the
    // reader's edits and refreshes only drafts still equal to the old text.
    const bool new_issue = brief_.issue != drafts_issue_;
    for (const auto& n : so::automated_networks()) {
        const QString gen = brief_.texts.value(n);
        if (new_issue || drafts_.value(n) == generated_.value(n))
            drafts_[n] = gen;
        generated_[n] = gen;
    }
    drafts_issue_ = brief_.issue;
    if (editor_->toPlainText() != drafts_.value(network_)) {
        setting_editor_ = true;
        editor_->setPlainText(drafts_.value(network_));
        setting_editor_ = false;
    }
}

// ── Rendering ───────────────────────────────────────────────────────────────

void SocialScreen::render_busy() {
    QStringList what;
    if (brief_busy_)
        what << QStringLiteral("loading the published issue");
    if (channels_busy_)
        what << QStringLiteral("reading channels");
    if (history_busy_)
        what << QStringLiteral("reading history");
    if (launch_busy_)
        what << QStringLiteral("reading launch drafts");
    if (publish_busy_)
        what << QStringLiteral("publishing");
    if (mark_busy_)
        what << QStringLiteral("recording");
    busy_label_->setText(what.isEmpty() ? QString() : what.join(QStringLiteral(" · ")) + QStringLiteral("…"));
    busy_label_->setVisible(!what.isEmpty());
}

void SocialScreen::render_channels() {
    while (channels_box_->count() > 0) {
        auto* it = channels_box_->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    if (!channels_loaded_) {
        auto* l = new QLabel(channels_error_.isEmpty() ? QStringLiteral("Reading channels…") : channels_error_);
        l->setTextFormat(Qt::PlainText);
        l->setObjectName(channels_error_.isEmpty() ? "socialDim" : "socialError");
        l->setWordWrap(true);
        channels_box_->addWidget(l);
        return;
    }
    for (const auto& c : std::as_const(channels_)) {
        auto* row = new QWidget;
        row->setObjectName("socialRow");
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto* rv = new QVBoxLayout(row);
        rv->setContentsMargins(2, 4, 2, 4);
        rv->setSpacing(1);
        auto* h = new QHBoxLayout;
        h->setSpacing(6);
        rv->addLayout(h);
        const bool live = c.enabled && c.configured && !c.manual && c.error.isEmpty();
        auto* dot = new QLabel(QStringLiteral("●"));
        dot->setStyleSheet(QStringLiteral("color:%1;").arg(!c.enabled           ? ui::colors::TEXT_TERTIARY()
                                                           : !c.error.isEmpty() ? ui::colors::AMBER()
                                                           : live               ? ui::colors::POSITIVE()
                                                                                : ui::colors::TEXT_TERTIARY()));
        h->addWidget(dot);
        auto* name = new QLabel(so::network_label(c.network));
        h->addWidget(name);
        h->addStretch(1);
        auto* sub = new QLabel; // second line: as whom / what
        sub->setObjectName("socialDim");
        sub->setTextFormat(Qt::PlainText);
        sub->setWordWrap(true);

        if (!c.enabled) {
            // Built and tested, switched off in social_post.ENABLED_NETWORKS.
            auto* m = new QLabel(QStringLiteral("coming soon"));
            m->setObjectName("socialDim");
            h->addWidget(m);
            row->setEnabled(false);
            row->setToolTip(QStringLiteral("%1 is not enabled yet").arg(so::network_label(c.network)));
        } else if (c.manual) {
            auto* m = new QLabel(QStringLiteral("manual"));
            m->setObjectName("socialDim");
            h->addWidget(m);
            int posted = 0;
            const auto targets = launch_.for_network(c.network);
            for (const auto* t : targets)
                posted += launch_.posted.contains(t->id) ? 1 : 0;
            if (!targets.isEmpty())
                sub->setText(QStringLiteral("%1 of %2 launch posts made").arg(posted).arg(targets.size()));
            row->setToolTip(QStringLiteral("Posted by hand: the composer copies the text and opens the submit "
                                           "page; nothing is submitted by finterm."));
        } else if (c.configured && c.error.isEmpty()) {
            sub->setText(c.account.isEmpty() ? QStringLiteral("connected") : so::account_label(c.account));
            auto* btn = small_button(QStringLiteral("disconnect"));
            btn->setToolTip(QStringLiteral("Remove the posting account's sign-in from this computer"));
            const QString net = c.network;
            connect(btn, &QPushButton::clicked, this, [this, net]() { confirm_disconnect(net); });
            h->addWidget(btn);
        } else {
            sub->setText(c.error.isEmpty() ? QStringLiteral("not connected") : QStringLiteral("keyring unavailable"));
            auto* btn = small_button(QStringLiteral("connect"));
            const QString net = c.network;
            btn->setToolTip(net == QLatin1String("x")         ? QStringLiteral("Sign in to X as the posting account (PIN)")
                            : net == QLatin1String("bluesky") ? QStringLiteral("Handle + an app password")
                                                              : QStringLiteral("Sign in on your Mastodon server"));
            connect(btn, &QPushButton::clicked, this, [this, net]() {
                if (net == QLatin1String("x"))
                    open_x_connect();
                else if (net == QLatin1String("bluesky"))
                    open_bsky_connect();
                else if (net == QLatin1String("mastodon"))
                    open_mastodon_connect();
            });
            h->addWidget(btn);
        }
        if (!sub->text().isEmpty())
            rv->addWidget(sub);
        else
            delete sub;
        if (!c.error.isEmpty())
            row->setToolTip(ui::tooltip_wrap(c.error));
        channels_box_->addWidget(row);
    }
    if (!channels_error_.isEmpty()) {
        auto* l = new QLabel(QStringLiteral("Refresh failed: ") + channels_error_);
        l->setTextFormat(Qt::PlainText);
        l->setObjectName("socialError");
        l->setWordWrap(true);
        channels_box_->addWidget(l);
    }
}

void SocialScreen::render_brief() {
    issue_title_->setText(so::brief_header(brief_));
    issue_error_->setVisible(!brief_error_.isEmpty());
    issue_error_->setText(brief_.loaded ? QStringLiteral("Refresh failed — showing the last copy: ") + brief_error_
                                        : brief_error_);
    open_page_btn_->setEnabled(brief_.loaded);
    open_page_btn_->setToolTip(brief_.url);

    const bool have = brief_.loaded;
    chip_live_->setVisible(have);
    chip_week_->setVisible(have);
    chip_posted_->setVisible(have);
    if (have) {
        chip_live_->setText(QStringLiteral("page live %1").arg(mark(brief_.page_live)));
        chip_live_->setToolTip(brief_.page_live ? QStringLiteral("The issue page shows this issue")
                                                : QStringLiteral("The issue page does not show this issue yet"));
        chip_week_->setText(QStringLiteral("current week %1").arg(mark(brief_.current_week)));
        const QString ref = brief_.posted.value(QStringLiteral("x"));
        if (ref.isEmpty()) {
            chip_posted_->setText(QStringLiteral("<span style='color:%1;'>X: not yet</span>")
                                      .arg(ui::colors::TEXT_SECONDARY()));
        } else {
            const QString link = so::ref_link(QStringLiteral("x"), ref);
            chip_posted_->setText(QStringLiteral("posted to X %1 %2")
                                      .arg(mark(true), link.isEmpty() ? esc(ref) : link_html(link, QStringLiteral("view"))));
        }
    }

    rows_table_->setUpdatesEnabled(false);
    rows_table_->setRowCount(int(brief_.rows.size()));
    for (int i = 0; i < brief_.rows.size(); ++i) {
        const QJsonObject r = brief_.rows[i].toObject();
        const QString sym = r.value(QLatin1String("symbol")).toString();
        rows_table_->setItem(i, 0, ou::cell(sym.isEmpty() ? fmt::placeholder() : sym, ui::colors::AMBER()));
        rows_table_->setItem(i, 1, ou::cell(r.value(QLatin1String("issuer")).toString()));
        const int insiders = r.value(QLatin1String("insiders")).toInt();
        const int trades = r.value(QLatin1String("trades")).toInt();
        auto* n = ou::num_cell(QString::number(insiders), insiders >= 2 ? ui::colors::GREEN() : QString());
        n->setToolTip(QStringLiteral("%1 distinct buyer%2, %3 trade%4")
                          .arg(insiders)
                          .arg(insiders == 1 ? QString() : QStringLiteral("s"))
                          .arg(trades)
                          .arg(trades == 1 ? QString() : QStringLiteral("s")));
        rows_table_->setItem(i, 2, n);
        // An absent value is unknown, not zero.
        const double value = r.value(QLatin1String("value")).toDouble(NAN);
        rows_table_->setItem(i, 3,
                             ou::num_cell(std::isfinite(value) ? fmt::format_compact(value) : fmt::placeholder(),
                                          ui::colors::GREEN()));
        rows_table_->setItem(i, 4,
                             ou::cell(so::trade_span(r.value(QLatin1String("first_trade")).toString(),
                                                     r.value(QLatin1String("last_trade")).toString())));
        QStringList roles;
        for (const auto& x : r.value(QLatin1String("roles")).toArray())
            roles << x.toString();
        auto* rc = ou::cell(roles.join(QStringLiteral(", ")), ui::colors::TEXT_SECONDARY());
        rc->setToolTip(roles.join(QLatin1Char('\n')));
        rows_table_->setItem(i, 5, rc);
    }
    rows_table_->setUpdatesEnabled(true);
    rows_table_->resizeColumnsToContents();
    rows_table_->setColumnWidth(1, qMin(qMax(rows_table_->columnWidth(1), 160), 260));
    rows_table_->setColumnWidth(5, qMin(rows_table_->columnWidth(5), 200));
    ou::fill_width(rows_table_, {1, 5});

    if (!have) {
        rows_note_->setText(brief_busy_ ? QString() : QStringLiteral("No issue loaded."));
        rows_note_->setToolTip({});
        return;
    }
    QStringList notes;
    if (brief_.rows.size() < brief_.count)
        notes << QStringLiteral("top %1 of %2 as published").arg(brief_.rows.size()).arg(brief_.count);
    else
        notes << QStringLiteral("%1 issuers as published").arg(brief_.count);
    notes << QStringLiteral("open-market purchases (SEC Form 4) by 2+ insiders");
    notes << QStringLiteral("hover for the disclaimer");
    rows_note_->setText(notes.join(QStringLiteral(" · ")));
    rows_note_->setToolTip(ui::tooltip_wrap(brief_.disclaimer));
}

void SocialScreen::render_history() {
    history_table_->setRowCount(int(history_.size()));
    for (int i = 0; i < history_.size(); ++i) {
        const auto& h = history_[i];
        history_table_->setItem(i, 0, ou::cell(h.issue));
        history_table_->setItem(i, 1, ou::cell(h.community.isEmpty() ? so::network_label(h.network)
                                                                     : so::network_label(h.network) +
                                                                           QStringLiteral(" · ") + h.community));
        auto* post = ou::cell(h.link.isEmpty() ? h.ref : h.link, h.link.isEmpty() ? QString() : ui::colors::CYAN());
        if (!h.link.isEmpty()) {
            post->setData(Qt::UserRole, h.link);
            post->setToolTip(QStringLiteral("Open the post"));
            QFont f = post->font();
            f.setUnderline(true);
            post->setFont(f);
        }
        history_table_->setItem(i, 2, post);
    }
    history_table_->resizeColumnsToContents();
    ou::fill_width(history_table_, {2});
    if (!history_error_.isEmpty())
        history_note_->setText(QStringLiteral("History unavailable: ") + history_error_);
    else if (history_loaded_ && history_.isEmpty())
        history_note_->setText(QStringLiteral("Nothing posted yet."));
    else
        history_note_->setText(QStringLiteral("Every post the app or the weekly timer made, newest issue first; "
                                              "launch posts you made by hand (Reddit / HN) on top."));
}

void SocialScreen::render_composer() {
    const so::Channel* ch = so::find_channel(channels_, network_);
    const QString label = so::network_label(network_);
    for (auto it = network_btns_.begin(); it != network_btns_.end(); ++it) {
        const bool posted = brief_.loaded && !brief_.posted.value(it.key()).isEmpty();
        const bool enabled = so::network_enabled(channels_, it.key());
        it.value()->setText(so::network_label(it.key()) + (posted ? QStringLiteral(" ✓") : QString()));
        // Until the channels answer, only the current choice stays clickable.
        it.value()->setEnabled(enabled || (!channels_loaded_ && it.key() == network_));
        it.value()->setToolTip(!enabled && channels_loaded_ ? QStringLiteral("Coming soon")
                               : posted ? QStringLiteral("Already posted for %1").arg(brief_.issue)
                                        : QString());
    }
    if (so::is_manual_network(network_)) {
        composer_stack_->setCurrentIndex(1);
        render_launch();
        return;
    }
    composer_stack_->setCurrentIndex(0);

    if (!ch)
        posting_as_->setText(QStringLiteral("Reading %1's connection…").arg(label));
    else if (ch->configured && !ch->account.isEmpty())
        posting_as_->setText(QStringLiteral("Posting as %1 on %2").arg(so::account_label(ch->account), label));
    else if (ch->configured)
        posting_as_->setText(QStringLiteral("Posting to the configured %1 account").arg(label));
    else
        posting_as_->setText(QStringLiteral("%1 is not connected").arg(label));

    const QString text = drafts_.value(network_);
    if (ch && ch->limit > 0) {
        const int len = so::channel_length(*ch, text);
        counter_->setText(QStringLiteral("%1 / %2").arg(len).arg(ch->limit));
        counter_->setStyleSheet(QStringLiteral("color:%1;font-weight:700;")
                                    .arg(len > ch->limit ? ui::colors::NEGATIVE() : ui::colors::TEXT_SECONDARY()));
        counter_->setToolTip(ch->url_cost >= 0
                                 ? QStringLiteral("Every link counts as %1 characters on %2").arg(ch->url_cost).arg(label)
                                 : QStringLiteral("Links count at their full length on %1").arg(label));
    } else {
        counter_->setText(QStringLiteral("%1 chars").arg(so::post_length(so::text_to_post(text), -1)));
        counter_->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    }
    reset_btn_->setEnabled(brief_.loaded && text != generated_.value(network_));

    const so::Preflight pf = so::preflight(brief_, ch, network_, text);
    for (int i = 0; i < check_labels_.size(); ++i) {
        auto* l = check_labels_[i];
        if (i >= pf.checks.size()) {
            l->clear();
            continue;
        }
        const auto& c = pf.checks[i];
        l->setText(QStringLiteral("%1 %2%3")
                       .arg(mark(c.ok), esc(c.label),
                            c.detail.isEmpty() ? QString()
                                               : QStringLiteral(" <span style='color:%1;'>— %2</span>")
                                                     .arg(ui::colors::TEXT_SECONDARY(), esc(c.detail))));
    }
    publish_btn_->setText(publish_busy_ ? QStringLiteral("Publishing to %1…").arg(label)
                                        : QStringLiteral("Publish to %1…").arg(label));
    publish_btn_->setEnabled(pf.all_ok() && !publish_busy_ && !brief_busy_);
    publish_btn_->setToolTip(brief_busy_ ? QStringLiteral("Waiting for the published issue to load")
                             : pf.all_ok() ? QStringLiteral("Shows the exact post and asks before posting")
                                           : QStringLiteral("Every pre-flight check must pass"));
}

void SocialScreen::select_network(const QString& network) {
    if (network == network_)
        return;
    if (!so::network_enabled(channels_, network)) {
        // Not enabled (or not known yet): keep the current choice checked.
        if (auto* b = network_btns_.value(network_))
            b->setChecked(true);
        return;
    }
    network_ = network;
    if (auto* b = network_btns_.value(network))
        b->setChecked(true);
    if (so::is_manual_network(network_)) {
        const auto targets = launch_.for_network(network_);
        if (!launch_target_.contains(network_) && !targets.isEmpty())
            launch_target_[network_] = targets.first()->id;
        show_launch_target(launch_target_.value(network_));
        render_composer();
        return;
    }
    setting_editor_ = true;
    editor_->setPlainText(drafts_.value(network_));
    setting_editor_ = false;
    render_composer();
}

void SocialScreen::set_status(const QString& html, bool error) {
    status_label_->setStyleSheet(
        QStringLiteral("color:%1;").arg(error ? ui::colors::NEGATIVE() : ui::colors::TEXT_PRIMARY()));
    status_label_->setText(html);
}

// ── Launch posts (Reddit / Hacker News, by hand) ────────────────────────────

const so::LaunchTarget* SocialScreen::current_launch_target() const {
    if (!so::is_manual_network(network_))
        return nullptr;
    const auto* t = launch_.find(launch_target_.value(network_));
    return t && t->network == network_ ? t : nullptr;
}

void SocialScreen::show_launch_target(const QString& id) {
    const auto* t = launch_.find(id);
    if (t && t->network == network_)
        launch_target_[network_] = id;
    if (const auto* cur = current_launch_target()) {
        const LaunchDraft d = launch_drafts_.value(cur->id);
        setting_editor_ = true;
        // Only what changed: setText would move the cursor and drop undo history.
        if (launch_title_->text() != d.title) {
            launch_title_->setText(d.title);
            launch_title_->setCursorPosition(0); // show a long title from its start
        }
        if (launch_url_->text() != d.url)
            launch_url_->setText(d.url);
        if (launch_body_->toPlainText() != d.body)
            launch_body_->setPlainText(d.body);
        if (launch_comment_->toPlainText() != d.comment)
            launch_comment_->setPlainText(d.comment);
        setting_editor_ = false;
    }
    render_launch();
}

void SocialScreen::render_launch() {
    const auto targets = launch_.for_network(network_);
    const auto* t = current_launch_target();
    if (!t && !targets.isEmpty()) {
        show_launch_target(targets.first()->id); // first visit: the first community
        return;
    }

    launch_combo_->blockSignals(true);
    launch_combo_->clear();
    for (const auto* x : targets) {
        const bool posted = launch_.posted.contains(x->id);
        launch_combo_->addItem(x->community + (posted ? QStringLiteral("  ✓ posted") : QString()), x->id);
        if (t && x->id == t->id)
            launch_combo_->setCurrentIndex(launch_combo_->count() - 1);
    }
    launch_combo_->blockSignals(false);

    const QList<QWidget*> needs_target = {launch_combo_,   launch_title_,   launch_url_,       launch_body_,
                                          launch_comment_, copy_title_btn_, copy_body_btn_,    copy_comment_btn_,
                                          open_submit_btn_, mark_posted_btn_, launch_reset_btn_};
    for (auto* w : needs_target)
        w->setEnabled(t != nullptr);
    if (!t) {
        launch_info_->setText(esc(launch_error_.isEmpty() ? QStringLiteral("Reading the launch drafts…")
                                                          : QStringLiteral("Launch drafts unavailable: ") + launch_error_));
        launch_info_->setStyleSheet(QStringLiteral("color:%1;").arg(launch_error_.isEmpty() ? ui::colors::TEXT_SECONDARY()
                                                                                             : ui::colors::NEGATIVE()));
        open_submit_btn_->setText(QStringLiteral("Open submit page"));
        launch_rules_->clear();
        launch_title_count_->clear();
        return;
    }

    // What this draft is, and anything that should stop the reader.
    QStringList info;
    info << QStringLiteral("<span style='color:%1;'>%2 post on %3 — copy it, post it yourself; nothing is "
                           "submitted by finterm.</span>")
                .arg(ui::colors::TEXT_SECONDARY(), t->is_link() ? QStringLiteral("Link") : QStringLiteral("Text"),
                     esc(so::network_label(t->network)));
    const auto warn = [](const QString& html) {
        return QStringLiteral("<span style='color:%1;font-weight:700;'>%2</span>").arg(ui::colors::AMBER(), html);
    };
    if (launch_.posted.contains(t->id)) {
        const auto& r = launch_.posted[t->id];
        info << warn(QStringLiteral("Posted %1 — %2. One launch post per community.")
                         .arg(r.at.isValid() ? esc(r.at.toLocalTime().date().toString(Qt::ISODate)) : QString(),
                              link_html(r.url, QStringLiteral("view"))));
    }
    const QString pace = so::pacing_hint(launch_.posted, t->id, QDateTime::currentDateTimeUtc());
    if (!pace.isEmpty())
        info << warn(esc(pace));
    if (t->data_missing)
        info << warn(QStringLiteral("This week's table could not be filled — refresh once the issue loads."));
    else if (t->needs_issue && !t->issue.isEmpty())
        info << QStringLiteral("<span style='color:%1;'>Table from issue %2.</span>")
                    .arg(ui::colors::TEXT_SECONDARY(), esc(t->issue));
    launch_info_->setStyleSheet({});
    launch_info_->setText(info.join(QStringLiteral("<br>")));

    const bool has_comment = !launch_templates_.value(t->id).comment.isEmpty();
    launch_url_label_->setVisible(t->is_link());
    launch_url_->setVisible(t->is_link());
    launch_body_label_->setVisible(!t->is_link());
    launch_body_->setVisible(!t->is_link());
    launch_comment_label_->setVisible(has_comment);
    launch_comment_->setVisible(has_comment);
    copy_comment_btn_->setVisible(has_comment);
    copy_body_btn_->setText(t->is_link() ? QStringLiteral("Copy URL") : QStringLiteral("Copy body"));

    open_submit_btn_->setText(QStringLiteral("Open %1 submit page").arg(t->community));
    open_submit_btn_->setToolTip(t->is_link() ? QStringLiteral("Opens the submit form with the title and URL filled in")
                                              : QStringLiteral("Copies the body, then opens the submit form with the "
                                                               "title filled in — paste the body there"));
    const bool posted = launch_.posted.contains(t->id);
    mark_posted_btn_->setText(posted ? QStringLiteral("Replace record…") : QStringLiteral("Mark as posted…"));
    mark_posted_btn_->setEnabled(!mark_busy_);
    render_launch_counts();

    QString rules = QStringLiteral("<b>%1</b><ul style='margin-left:-24px;'>").arg(esc(t->community));
    for (const auto& n : t->notes)
        rules += QStringLiteral("<li>%1</li>").arg(esc(n));
    rules += QStringLiteral("</ul><b>Before posting anywhere</b><ul style='margin-left:-24px;'>");
    for (const auto& r : std::as_const(launch_.rules))
        rules += QStringLiteral("<li>%1</li>").arg(esc(r));
    rules += QStringLiteral("</ul>");
    if (!launch_.skip.isEmpty())
        rules += QStringLiteral("<span style='color:%1;'><b>Skip:</b> %2</span>")
                     .arg(ui::colors::TEXT_SECONDARY(), esc(launch_.skip.join(QStringLiteral("; "))));
    launch_rules_->setText(rules);
}

void SocialScreen::render_launch_counts() {
    const auto* t = current_launch_target();
    if (!t)
        return;
    const int limit = so::title_limit(t->network);
    const int len = int(launch_title_->text().trimmed().size());
    launch_title_count_->setText(QStringLiteral("%1 / %2").arg(len).arg(limit));
    launch_title_count_->setStyleSheet(QStringLiteral("color:%1;font-weight:700;")
                                           .arg(len > limit ? ui::colors::NEGATIVE() : ui::colors::TEXT_SECONDARY()));
    open_submit_btn_->setEnabled(len > 0);
    launch_reset_btn_->setEnabled(!(launch_drafts_.value(t->id) == launch_templates_.value(t->id)));
}

void SocialScreen::copy_to_clipboard(const QString& text, const QString& what) {
    if (auto* cb = QGuiApplication::clipboard())
        cb->setText(text);
    set_status(QStringLiteral("%1 copied to the clipboard.").arg(esc(what)), false);
}

void SocialScreen::open_submit_page() {
    const auto* t = current_launch_target();
    if (!t)
        return;
    const QString title = launch_title_->text().trimmed();
    const QString link = launch_url_->text().trimmed();
    if (title.isEmpty())
        return;

    QStringList why;
    if (launch_.posted.contains(t->id)) {
        const auto& r = launch_.posted[t->id];
        why << QStringLiteral("%1 already has a launch post (%2). The plan is one per community.")
                   .arg(t->community, r.url);
    }
    const QString pace = so::pacing_hint(launch_.posted, t->id, QDateTime::currentDateTimeUtc());
    if (!pace.isEmpty())
        why << pace;
    if (t->data_missing && launch_body_->toPlainText().contains(QLatin1String("[this week's table")))
        why << QStringLiteral("The body still lacks this week's table.");
    if (title.size() > so::title_limit(t->network))
        why << QStringLiteral("The title is longer than %1 allows (%2 characters).")
                   .arg(so::network_label(t->network))
                   .arg(so::title_limit(t->network));
    if (!why.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Open the submit page?"), why.join(QStringLiteral("\n\n")),
                        QMessageBox::NoButton, this);
        auto* go = box.addButton(QStringLiteral("Open anyway"), QMessageBox::AcceptRole);
        auto* cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() != go)
            return;
    }

    const QString url = so::launch_submit_url(*t, title, link);
    if (url.isEmpty())
        return;
    // A text post's body can't ride in the URL: it goes by clipboard.
    if (!t->is_link()) {
        if (auto* cb = QGuiApplication::clipboard())
            cb->setText(launch_body_->toPlainText());
    }
    QDesktopServices::openUrl(QUrl::fromEncoded(url.toLatin1()));
    set_status(t->is_link() ? QStringLiteral("Opened the %1 submit page. Check it, submit it yourself, then "
                                             "<b>Mark as posted</b>.")
                                  .arg(esc(t->community))
                            : QStringLiteral("Body copied — paste it into the %1 submit page that opened. Submit it "
                                             "yourself, then <b>Mark as posted</b>.")
                                  .arg(esc(t->community)),
               false);
}

void SocialScreen::open_mark_posted() {
    const auto* t = current_launch_target();
    if (!t || mark_busy_)
        return;
    const QString id = t->id;
    const QString community = t->community;
    const so::LaunchTarget target = *t; // the dialog outlives a reload of launch_
    const bool replace = launch_.posted.contains(id);

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Mark %1 as posted").arg(community));
    dlg->setMinimumWidth(480);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    auto* intro = new QLabel(QStringLiteral("Paste the link of the post you made in <b>%1</b>:").arg(esc(community)));
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);
    auto* url = new QLineEdit;
    url->setPlaceholderText(target.network == QLatin1String("hackernews")
                                ? QStringLiteral("https://news.ycombinator.com/item?id=…")
                                : QStringLiteral("https://www.reddit.com/r/%1/comments/…").arg(so::reddit_sub(community)));
    v->addWidget(url);
    auto* why = new QLabel;
    why->setWordWrap(true);
    why->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::NEGATIVE()));
    v->addWidget(why);
    if (replace) {
        const auto& r = launch_.posted[id];
        auto* w = new QLabel(QStringLiteral("%1 already has a launch post recorded (%2). This replaces that record.")
                                 .arg(community, r.url));
        w->setWordWrap(true);
        w->setStyleSheet(QStringLiteral("color:%1;font-weight:700;").arg(ui::colors::AMBER()));
        v->addWidget(w);
    }
    auto* box = new QDialogButtonBox;
    auto* cancel = box->addButton(QDialogButtonBox::Cancel);
    auto* ok = box->addButton(replace ? QStringLiteral("Replace record") : QStringLiteral("Record"),
                              QDialogButtonBox::AcceptRole);
    ok->setEnabled(false);
    ok->setAutoDefault(false);
    cancel->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    v->addWidget(box);
    connect(url, &QLineEdit::textChanged, dlg, [url, why, ok, target]() {
        const QString text = url->text().trimmed();
        const QString err = text.isEmpty() ? QString() : so::validate_post_url(target, text);
        why->setText(err);
        ok->setEnabled(!text.isEmpty() && err.isEmpty());
    });

    QPointer<SocialScreen> self = this;
    connect(dlg, &QDialog::accepted, this, [self, url, id, community, replace]() {
        if (!self)
            return;
        const QString link = url->text().trimmed();
        self->mark_busy_ = true;
        self->render_busy();
        self->render_composer();
        call_backend(self, QStringLiteral("launch_mark"),
                     {{QStringLiteral("target"), id}, {QStringLiteral("url"), link}, {QStringLiteral("replace"), replace}},
                     kConnectTimeoutMs, [self, community](const QJsonObject&, const QString& err, Outcome) {
                         self->mark_busy_ = false;
                         if (err.isEmpty())
                             self->set_status(QStringLiteral("Recorded the %1 launch post.").arg(esc(community)), false);
                         else
                             self->set_status(QStringLiteral("Not recorded — %1").arg(esc(err)), true);
                         self->render_busy();
                         self->load_launch();
                         self->load_history();
                     });
    });
    dlg->open();
    url->setFocus();
}

// ── Connect / disconnect ────────────────────────────────────────────────────

void SocialScreen::confirm_disconnect(const QString& network) {
    const so::Channel* ch = so::find_channel(channels_, network);
    const QString label = so::network_label(network);
    const QString who = ch && !ch->account.isEmpty() ? so::account_label(ch->account)
                                                     : QStringLiteral("the connected account");
    const QString what = network == QLatin1String("x")
                             ? QStringLiteral("The X app's own keys stay; Connect signs an account in again.")
                         : network == QLatin1String("mastodon")
                             ? QStringLiteral("finterm also asks the server to revoke its token.")
                             : QStringLiteral("The app password stays valid at Bluesky until you delete it there.");
    auto* box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Disconnect %1").arg(label),
                                QStringLiteral("Disconnect %1 from %2?\n\nThis removes its sign-in from this computer's "
                                               "keyring; nothing more is posted there, by this screen or the weekly "
                                               "timer, until it is connected again. %3")
                                    .arg(who, label, what),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto* go = box->addButton(QStringLiteral("Disconnect"), QMessageBox::DestructiveRole);
    auto* cancel = box->addButton(QMessageBox::Cancel);
    box->setDefaultButton(cancel);
    box->setEscapeButton(cancel);
    QPointer<SocialScreen> self = this;
    connect(box, &QMessageBox::buttonClicked, this, [self, go, network, label](QAbstractButton* b) {
        if (!self || b != go)
            return;
        self->set_status(QStringLiteral("Disconnecting %1…").arg(esc(label)), false);
        call_backend(self, QStringLiteral("disconnect"), {{QStringLiteral("network"), network}}, kConnectTimeoutMs,
                     [self, label](const QJsonObject& o, const QString& err, Outcome) {
                         if (!err.isEmpty())
                             self->set_status(QStringLiteral("%1: not disconnected — %2").arg(esc(label), esc(err)), true);
                         else
                             self->set_status(QStringLiteral("%1 disconnected. %2")
                                                  .arg(esc(label), esc(o.value(QLatin1String("note")).toString())),
                                              false);
                         self->load_channels();
                     });
    });
    box->open();
}

void SocialScreen::open_bsky_connect() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Connect Bluesky"));
    dlg->setMinimumWidth(460);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    auto* intro = new QLabel(QStringLiteral("finterm posts to Bluesky with an <b>app password</b> — never the "
                                            "account's own password. Create one at %1 (name it \"finterm\"), then "
                                            "enter it below. Deleting it there revokes finterm's access.")
                                 .arg(link_html(QStringLiteral("https://bsky.app/settings/app-passwords"),
                                                QStringLiteral("bsky.app › Settings › App passwords"))));
    intro->setTextFormat(Qt::RichText);
    intro->setWordWrap(true);
    intro->setOpenExternalLinks(true);
    v->addWidget(intro);
    auto* handle = new QLineEdit;
    handle->setPlaceholderText(QStringLiteral("handle, e.g. name.bsky.social"));
    v->addWidget(handle);
    auto* pw = new QLineEdit;
    pw->setPlaceholderText(QStringLiteral("app password: xxxx-xxxx-xxxx-xxxx"));
    pw->setEchoMode(QLineEdit::Password);
    v->addWidget(pw);
    auto* status = new QLabel;
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    v->addWidget(status);
    auto* box = new QDialogButtonBox;
    auto* go = box->addButton(QStringLiteral("Connect"), QDialogButtonBox::ActionRole);
    go->setEnabled(false);
    box->addButton(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    v->addWidget(box);
    auto update = [go, handle, pw]() { go->setEnabled(!handle->text().trimmed().isEmpty() && !pw->text().isEmpty()); };
    connect(handle, &QLineEdit::textChanged, dlg, update);
    connect(pw, &QLineEdit::textChanged, dlg, update);

    QPointer<QDialog> dlg_guard = dlg;
    auto submit = [this, dlg_guard, handle, pw, go, status]() {
        if (!go->isEnabled())
            return;
        go->setEnabled(false);
        status->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        status->setText(QStringLiteral("Checking with Bluesky…"));
        call_backend(this, QStringLiteral("bsky_connect"),
                     {{QStringLiteral("handle"), handle->text().trimmed()}, {QStringLiteral("app_password"), pw->text()}},
                     kConnectTimeoutMs, [this, dlg_guard, go, status](const QJsonObject& o, const QString& err, Outcome) {
                         if (!err.isEmpty()) {
                             if (dlg_guard) {
                                 go->setEnabled(true);
                                 status->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::NEGATIVE()));
                                 status->setText(err);
                             } else {
                                 set_status(QStringLiteral("Bluesky not connected: ") + esc(err), true);
                             }
                             return;
                         }
                         set_status(QStringLiteral("Bluesky connected as %1.")
                                        .arg(esc(so::account_label(o.value(QLatin1String("account")).toString()))),
                                    false);
                         load_channels();
                         if (dlg_guard)
                             dlg_guard->accept();
                     });
    };
    connect(go, &QPushButton::clicked, dlg, submit);
    connect(pw, &QLineEdit::returnPressed, dlg, submit);
    dlg->open();
}

void SocialScreen::open_mastodon_connect() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Connect Mastodon"));
    dlg->setMinimumWidth(460);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    auto* intro = new QLabel(QStringLiteral("1. Enter your server and open its sign-in page; sign in as the POSTING "
                                            "account and click <b>Authorize</b>.<br>"
                                            "2. Paste the code it shows here and press Finish."));
    intro->setTextFormat(Qt::RichText);
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto* inst_row = new QHBoxLayout;
    auto* instance = new QLineEdit;
    instance->setPlaceholderText(QStringLiteral("server, e.g. mastodon.social"));
    inst_row->addWidget(instance, 1);
    auto* open_btn = new QPushButton(QStringLiteral("Open sign-in"));
    open_btn->setEnabled(false);
    inst_row->addWidget(open_btn);
    v->addLayout(inst_row);
    auto* code_row = new QHBoxLayout;
    auto* code = new QLineEdit;
    code->setPlaceholderText(QStringLiteral("authorization code"));
    code_row->addWidget(code, 1);
    auto* finish = new QPushButton(QStringLiteral("Finish"));
    finish->setEnabled(false);
    code_row->addWidget(finish);
    v->addLayout(code_row);
    auto* status = new QLabel;
    status->setWordWrap(true);
    status->setTextFormat(Qt::RichText);
    status->setOpenExternalLinks(true);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    v->addWidget(status);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    v->addWidget(box);

    auto say = [status](const QString& html, bool error) {
        status->setStyleSheet(
            QStringLiteral("color:%1;").arg(error ? ui::colors::NEGATIVE() : ui::colors::TEXT_SECONDARY()));
        status->setText(html);
    };
    connect(instance, &QLineEdit::textChanged, dlg,
            [open_btn, instance]() { open_btn->setEnabled(!instance->text().trimmed().isEmpty()); });
    connect(code, &QLineEdit::textChanged, dlg, [finish, code]() { finish->setEnabled(!code->text().trimmed().isEmpty()); });

    connect(open_btn, &QPushButton::clicked, dlg, [dlg, open_btn, instance, say]() {
        open_btn->setEnabled(false);
        say(QStringLiteral("Registering finterm on the server…"), false);
        call_backend(dlg, QStringLiteral("mastodon_connect_start"), {{QStringLiteral("instance"), instance->text().trimmed()}},
                     kConnectTimeoutMs, [open_btn, say](const QJsonObject& o, const QString& err, Outcome) {
                         open_btn->setEnabled(true);
                         // A new start replaces the pending sign-in: an older code no longer works.
                         open_btn->setText(QStringLiteral("Start over"));
                         const QUrl url(o.value(QLatin1String("url")).toString());
                         if (!err.isEmpty() || url.scheme() != QLatin1String("https")) {
                             say(esc(err.isEmpty() ? QStringLiteral("The server's answer had no sign-in link") : err), true);
                             return;
                         }
                         QDesktopServices::openUrl(url);
                         say(QStringLiteral("Opened in your browser — if it did not open, use %1. Use the code from "
                                            "this page; starting over makes it invalid.")
                                 .arg(link_html(url.toString(), QStringLiteral("this link"))),
                             false);
                     });
    });

    QPointer<QDialog> dlg_guard = dlg;
    auto do_finish = [this, dlg_guard, code, finish, open_btn, say]() {
        const QString c = code->text().trimmed();
        if (c.isEmpty())
            return;
        finish->setEnabled(false);
        open_btn->setEnabled(false);
        say(QStringLiteral("Exchanging the code…"), false);
        call_backend(this, QStringLiteral("mastodon_connect_finish"), {{QStringLiteral("code"), c}}, kConnectTimeoutMs,
                     [this, dlg_guard, finish, open_btn, say](const QJsonObject& o, const QString& err, Outcome) {
                         if (!err.isEmpty()) {
                             if (dlg_guard) {
                                 open_btn->setEnabled(true);
                                 finish->setEnabled(true);
                                 say(esc(err), true);
                             } else {
                                 set_status(QStringLiteral("Mastodon sign-in failed: ") + esc(err), true);
                             }
                             return;
                         }
                         set_status(QStringLiteral("Mastodon connected as %1.")
                                        .arg(esc(so::account_label(o.value(QLatin1String("account")).toString()))),
                                    false);
                         load_channels();
                         if (dlg_guard)
                             dlg_guard->accept();
                     });
    };
    connect(finish, &QPushButton::clicked, dlg, do_finish);
    connect(code, &QLineEdit::returnPressed, dlg, do_finish);
    dlg->open();
}

// ── X sign-in ───────────────────────────────────────────────────────────────

void SocialScreen::open_x_connect() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Connect X"));
    dlg->setMinimumWidth(460);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);

    auto* intro = new QLabel(QStringLiteral("1. Open X sign-in — sign in as the POSTING account, click "
                                            "<b>Authorize app</b>, copy the PIN.<br>"
                                            "2. Paste the PIN here and press Finish."));
    intro->setTextFormat(Qt::RichText);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto* open_btn = new QPushButton(QStringLiteral("Open X sign-in"));
    open_btn->setToolTip(QStringLiteral("Each sign-in replaces the previous one: use the PIN from the newest page"));
    v->addWidget(open_btn, 0, Qt::AlignLeft);

    auto* pin_row = new QHBoxLayout;
    auto* pin = new QLineEdit;
    pin->setPlaceholderText(QStringLiteral("PIN"));
    pin_row->addWidget(pin, 1);
    auto* finish = new QPushButton(QStringLiteral("Finish"));
    finish->setEnabled(false);
    pin_row->addWidget(finish);
    v->addLayout(pin_row);

    auto* status = new QLabel;
    status->setWordWrap(true);
    status->setTextFormat(Qt::RichText);
    status->setOpenExternalLinks(true);
    status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    v->addWidget(status);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    v->addWidget(box);

    auto say = [status](const QString& html, bool error) {
        status->setStyleSheet(
            QStringLiteral("color:%1;").arg(error ? ui::colors::NEGATIVE() : ui::colors::TEXT_SECONDARY()));
        status->setText(html);
    };
    connect(pin, &QLineEdit::textChanged, finish,
            [finish, pin]() { finish->setEnabled(!pin->text().trimmed().isEmpty()); });

    connect(open_btn, &QPushButton::clicked, dlg, [dlg, open_btn, say]() {
        open_btn->setEnabled(false);
        say(QStringLiteral("Asking X for a sign-in link…"), false);
        call_backend(dlg, QStringLiteral("x_connect_start"), {}, kConnectTimeoutMs,
                     [open_btn, say](const QJsonObject& o, const QString& err, Outcome) {
                         open_btn->setEnabled(true);
                         // A second start replaces the pending request: a
                         // PIN from an earlier page no longer works.
                         open_btn->setText(QStringLiteral("Start over (new PIN)"));
                         const QUrl url(o.value(QLatin1String("url")).toString());
                         if (!err.isEmpty() || url.scheme() != QLatin1String("https")) {
                             say(esc(err.isEmpty() ? QStringLiteral("X's answer had no sign-in link") : err), true);
                             return;
                         }
                         QDesktopServices::openUrl(url);
                         say(QStringLiteral("Opened in your browser — if it did not open, use %1. "
                                            "Make sure the browser is signed in as the POSTING account. "
                                            "Use the PIN from this page; starting over makes it invalid.")
                                 .arg(link_html(url.toString(), QStringLiteral("this link"))),
                             false);
                     });
    });

    QPointer<QDialog> dlg_guard = dlg;
    auto do_finish = [this, dlg_guard, pin, finish, open_btn, say]() {
        const QString code = pin->text().trimmed();
        if (code.isEmpty())
            return;
        finish->setEnabled(false);
        open_btn->setEnabled(false);
        say(QStringLiteral("Exchanging the PIN…"), false);
        // The screen, not the dialog, receives the answer: closing the dialog
        // while the PIN is exchanged must still refresh CHANNELS.
        call_backend(this, QStringLiteral("x_connect_finish"), {{QStringLiteral("pin"), code}}, kConnectTimeoutMs,
                     [this, dlg_guard, finish, open_btn, say](const QJsonObject& o, const QString& err, Outcome) {
                         if (!err.isEmpty()) {
                             if (dlg_guard) {
                                 open_btn->setEnabled(true);
                                 finish->setEnabled(true);
                                 say(esc(err), true);
                             } else {
                                 set_status(QStringLiteral("X sign-in failed: ") + esc(err), true);
                             }
                             return;
                         }
                         const QString acct = so::account_label(o.value(QLatin1String("account")).toString());
                         set_status(QStringLiteral("X connected%1.")
                                        .arg(acct.isEmpty() ? QString() : QStringLiteral(" as ") + esc(acct)),
                                    false);
                         load_channels();
                         if (dlg_guard)
                             dlg_guard->accept();
                     });
    };
    connect(finish, &QPushButton::clicked, dlg, do_finish);
    connect(pin, &QLineEdit::returnPressed, dlg, do_finish);
    dlg->open();
}

// ── Publish ─────────────────────────────────────────────────────────────────

void SocialScreen::confirm_publish() {
    const QString network = network_;
    const so::Channel* ch = so::find_channel(channels_, network);
    const QString text = so::text_to_post(drafts_.value(network));
    if (publish_busy_ || brief_busy_ || !so::preflight(brief_, ch, network, text).all_ok())
        return;
    const QString issue = brief_.issue;
    const QString label = so::network_label(network);
    const QString who = ch->account.isEmpty() ? QStringLiteral("the configured %1 account").arg(label)
                                              : so::account_label(ch->account);

    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Publish to %1").arg(label));
    dlg->setMinimumWidth(520);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    auto* head = new QLabel(QStringLiteral("Post to <b>%1</b> as <b>%2</b> — weekly brief %3:")
                                .arg(esc(label), esc(who), esc(issue)));
    head->setTextFormat(Qt::RichText);
    head->setWordWrap(true);
    v->addWidget(head);
    auto* body = new QPlainTextEdit(text);
    body->setReadOnly(true);
    body->setMinimumHeight(160);
    v->addWidget(body, 1);
    auto* len = new QLabel(QStringLiteral("%1 / %2 characters").arg(so::channel_length(*ch, text)).arg(ch->limit));
    len->setStyleSheet(QStringLiteral("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    v->addWidget(len);
    auto* warn = new QLabel(QStringLiteral("This posts publicly and can't be undone from finterm."));
    warn->setStyleSheet(QStringLiteral("color:%1;font-weight:700;").arg(ui::colors::AMBER()));
    v->addWidget(warn);

    auto* box = new QDialogButtonBox;
    auto* cancel = box->addButton(QDialogButtonBox::Cancel);
    auto* go = box->addButton(QStringLiteral("Publish to %1").arg(label), QDialogButtonBox::AcceptRole);
    // Enter must not post: Cancel is the default and holds the focus.
    go->setAutoDefault(false);
    go->setDefault(false);
    cancel->setDefault(true);
    cancel->setFocus();
    connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    v->addWidget(box);

    QPointer<SocialScreen> self = this;
    connect(dlg, &QDialog::accepted, this, [self, network, issue, text]() {
        if (self)
            self->publish(network, issue, text);
    });
    dlg->open();
}

void SocialScreen::publish(const QString& network, const QString& issue, const QString& text) {
    if (publish_busy_)
        return;
    const QString label = so::network_label(network);
    publish_busy_ = true;
    set_status(QStringLiteral("Publishing to %1…").arg(esc(label)), false);
    render_composer();
    render_busy();
    LOG_INFO("Social", QStringLiteral("publishing %1 to %2 (confirmed)").arg(issue, network));
    call_backend(
        this, QStringLiteral("publish"),
        {{QStringLiteral("network"), network}, {QStringLiteral("issue"), issue}, {QStringLiteral("text"), text}},
        kPublishTimeoutMs, [this, network, label](const QJsonObject& o, const QString& err, Outcome outcome) {
            publish_busy_ = false;
            using Sev = ui::ToastService::Severity;
            if (outcome == Outcome::NotStarted) {
                set_status(QStringLiteral("%1: not posted — %2").arg(esc(label), esc(err)), true);
                ui::ToastService::instance().post(Sev::Error, QStringLiteral("%1: not posted").arg(label),
                                                  QStringLiteral("social"));
            } else if (outcome == Outcome::NoAnswer) {
                // Killed or crashed mid-run: it may have posted before the
                // record was written. Say so instead of "failed".
                set_status(QStringLiteral("No answer from the publisher (%1). The post may or may not have gone "
                                          "out — check the %2 account before trying again.")
                               .arg(esc(err), esc(label)),
                           true);
                ui::ToastService::instance().post(Sev::Warning,
                                                  QStringLiteral("%1: publish outcome unknown").arg(label),
                                                  QStringLiteral("social"));
            } else if (!err.isEmpty()) {
                const QString hint = so::publish_error_hint(network, err);
                set_status(QStringLiteral("%1: not posted — %2%3")
                               .arg(esc(label), esc(err),
                                    hint.isEmpty() ? QString() : QStringLiteral("<br>Hint: ") + esc(hint)),
                           true);
                ui::ToastService::instance().post(Sev::Error, QStringLiteral("%1: not posted").arg(label),
                                                  QStringLiteral("social"));
            } else {
                const QString ref = o.value(QLatin1String("ref")).toString();
                const QString link = so::ref_link(network, ref);
                set_status(QStringLiteral("Posted to %1%2.")
                               .arg(esc(label), link.isEmpty() ? (ref.isEmpty() ? QString() : QStringLiteral(" (") +
                                                                                                    esc(ref) +
                                                                                                    QStringLiteral(")"))
                                                               : QStringLiteral(" — ") +
                                                                     link_html(link, QStringLiteral("view post"))),
                           false);
                ui::ToastService::instance().post(Sev::Success, QStringLiteral("Posted to %1").arg(label),
                                                  QStringLiteral("social"));
            }
            render_busy();
            // What was recorded is the truth either way.
            load_brief();
            load_history();
        });
}

} // namespace finterm::screens
