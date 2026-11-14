#include "screens/social/SocialScreen.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "screens/ownership/OwnershipUi.h"
#include "ui/components/TooltipText.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/notifications/ToastService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace finterm::screens {

namespace {
namespace so = finterm::social;
namespace ou = finterm::screens::ownership_ui;
namespace fmt = finterm::ui::formatting;

constexpr const char* kScript = "social_app.py";
// issue.json (15s) then the page itself (10s), plus interpreter start-up.
constexpr int kBriefTimeoutMs = 60'000;
// A post is fetch issue.json + fetch page + the network's API. Killing the
// process mid-post would leave the outcome unknown, so it gets room.
constexpr int kPublishTimeoutMs = 120'000;
constexpr int kConnectTimeoutMs = 60'000;
constexpr qint64 kStaleAfterMs = 10 * 60 * 1000;

/// How a backend call ended. For a post the difference matters: a process
/// that never started posted nothing; one that ran and gave no answer
/// (killed, crashed) may have posted before it could say so.
enum class Outcome { Answered, NoAnswer, NotStarted };

/// @p done gets the answer object, an error (the backend's own {error}, or
/// why there was none) and how the call ended.
using Done = std::function<void(const QJsonObject& answer, const QString& error, Outcome outcome)>;

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
    proc->start(python, {script, action, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))});
    if (timeout_ms > 0)
        timer->start(timeout_ms);
}

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
    split->addWidget(build_issue_pane());
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
    v->setContentsMargins(8, 8, 8, 8);
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
    auto* v = new QVBoxLayout(pane);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(6);
    v->addWidget(ou::section_title(QStringLiteral("COMPOSER")).widget);

    auto* nets = new QHBoxLayout;
    nets->setSpacing(4);
    network_group_ = new QButtonGroup(this);
    network_group_->setExclusive(true);
    for (const auto& n : so::automated_networks()) {
        auto* b = small_button(so::network_label(n));
        b->setCheckable(true);
        b->setChecked(n == network_);
        network_group_->addButton(b);
        network_btns_.insert(n, b);
        connect(b, &QPushButton::clicked, this, [this, n]() { select_network(n); });
        nets->addWidget(b);
    }
    nets->addStretch(1);
    v->addLayout(nets);

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

    status_label_ = new QLabel;
    status_label_->setWordWrap(true);
    status_label_->setTextFormat(Qt::RichText);
    status_label_->setOpenExternalLinks(true);
    status_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    v->addWidget(status_label_);
    return pane;
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
    load_brief();
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
    if (publish_busy_)
        what << QStringLiteral("publishing");
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
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(2, 4, 2, 4);
        h->setSpacing(6);
        const bool live = c.configured && !c.manual && c.error.isEmpty();
        auto* dot = new QLabel(QStringLiteral("●"));
        dot->setStyleSheet(QStringLiteral("color:%1;").arg(!c.error.isEmpty() ? ui::colors::AMBER()
                                                           : live            ? ui::colors::POSITIVE()
                                                                             : ui::colors::TEXT_TERTIARY()));
        h->addWidget(dot);
        auto* name = new QLabel(so::network_label(c.network));
        h->addWidget(name);
        h->addStretch(1);

        if (c.manual) {
            auto* m = new QLabel(QStringLiteral("manual"));
            m->setObjectName("socialDim");
            h->addWidget(m);
            row->setEnabled(false);
            row->setToolTip(QStringLiteral("manual — phase 2"));
        } else if (c.network == QLatin1String("x")) {
            if (live && !c.account.isEmpty()) {
                auto* a = new QLabel(so::account_label(c.account));
                h->addWidget(a);
            }
            auto* btn = small_button(c.configured ? QStringLiteral("reconnect") : QStringLiteral("connect"));
            btn->setToolTip(QStringLiteral("Sign in to X as the posting account (PIN sign-in)"));
            connect(btn, &QPushButton::clicked, this, [this]() { open_x_connect(); });
            h->addWidget(btn);
        } else if (live) {
            h->addWidget(new QLabel(c.account.isEmpty() ? QStringLiteral("connected") : so::account_label(c.account)));
        } else {
            auto* m = new QLabel(QStringLiteral("not connected"));
            m->setObjectName("socialDim");
            h->addWidget(m);
            row->setToolTip(QStringLiteral("not connected — set up with tools/announce.py --store-keys"));
        }
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
        history_table_->setItem(i, 1, ou::cell(so::network_label(h.network)));
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
        history_note_->setText(QStringLiteral("Every post the app or the weekly timer made, newest issue first."));
}

void SocialScreen::render_composer() {
    const so::Channel* ch = so::find_channel(channels_, network_);
    const QString label = so::network_label(network_);
    for (auto it = network_btns_.begin(); it != network_btns_.end(); ++it) {
        const bool posted = brief_.loaded && !brief_.posted.value(it.key()).isEmpty();
        it.value()->setText(so::network_label(it.key()) + (posted ? QStringLiteral(" ✓") : QString()));
        it.value()->setToolTip(posted ? QStringLiteral("Already posted for %1").arg(brief_.issue) : QString());
    }

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
    network_ = network;
    if (auto* b = network_btns_.value(network))
        b->setChecked(true);
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
