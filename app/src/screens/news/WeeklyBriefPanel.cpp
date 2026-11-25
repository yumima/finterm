#include "screens/news/WeeklyBriefPanel.h"

#include "screens/social/BuildProgress.h"
#include "screens/social/UserSymbols.h"

#include "services/social/SocialBackend.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens {

using services::social_backend::call_backend;
using services::social_backend::Outcome;

namespace {
constexpr int kQuickMs = 60'000;
constexpr int kPostMs = 180'000;
constexpr int kTickMs = 600'000; // as BriefsPanel's: a tick may publish (fetch, commit, push)

struct Opt {
    const char* key;
    const char* label;
    const char* tip;
};
const Opt kTopics[] = {
    {"summary", "At a glance", "The market stance with its record since 1990, the past week and the week ahead in a line each"},
    {"macro", "Markets and economy", "Rates, dollar, oil, GDP, inflation and jobs with their trend; the week's US releases ahead"},
    {"insider", "Insider buying", "Companies where two or more insiders bought on the open market (SEC Form 4)"},
    {"funds", "Fund moves", "What institutions added and trimmed, net by stock (SEC 13F)"},
    {"screens", "Screen leaders", "The first names of each of finterm's screens, with each screen's own record"},
    {"earnings", "Earnings ahead", "Large companies expected to report in the next 7 days, confirmed by SEC filings"},
    {"ipo", "IPOs and lock-ups", "IPOs expected to price and lock-ups ending in the next 7 days, from SEC filings"},
    {"mine", "Your holdings (private)",
     "This week on your portfolios and watchlists — in your draft only; never published or posted"},
};
const Opt kPlatforms[] = {
    {"x", "X", "Posted as the account connected for X"},
    {"bluesky", "Bluesky", "Posted as the Bluesky handle connected with an app password"},
    {"mastodon", "Mastodon", "Posted to the Mastodon account connected"},
};

QString cap(const QString& s) {
    return QStringLiteral("color:%1;font-weight:700;letter-spacing:1px;font-size:11px;padding-top:8px;")
        .arg(ui::colors::TEXT_SECONDARY()) + s;
}

QLabel* caption(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(cap(QString()));
    return l;
}
} // namespace

WeeklyBriefPanel::WeeklyBriefPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;}"
                                 "QCheckBox,QRadioButton{color:%1;}"
                                 "QPushButton{background:%2;color:%3;border:1px solid %4;padding:3px 10px;"
                                 "font-weight:700;}"
                                 "QPushButton:hover{color:%5;border-color:%5;}"
                                 "QPushButton:disabled{color:%6;border-color:%7;}"
                                 "QComboBox{background:%2;color:%1;border:1px solid %7;padding:2px 6px;}")
                      .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(), ui::colors::AMBER(),
                           ui::colors::AMBER_DIM(), ui::colors::TEXT_PRIMARY(), ui::colors::TEXT_TERTIARY(),
                           ui::colors::BORDER_DIM()));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 6, 8, 6);
    v->setSpacing(4);

    v->addWidget(caption(QStringLiteral("THIS WEEK")));
    issue_ = new QLabel;
    issue_->setTextFormat(Qt::RichText);
    issue_->setWordWrap(true);
    issue_->setOpenExternalLinks(true);
    v->addWidget(issue_);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(4);
    generate_btn_ = new QPushButton(QStringLiteral("Generate"));
    generate_btn_->setToolTip(QStringLiteral("Build this week's brief from the latest data (a few minutes)"));
    connect(generate_btn_, &QPushButton::clicked, this, [this]() { generate(false); });
    review_btn_ = new QPushButton(QStringLiteral("Review"));
    review_btn_->setToolTip(QStringLiteral("Read, correct, schedule or publish the brief"));
    connect(review_btn_, &QPushButton::clicked, this,
            [this]() { emit open_beside(QStringLiteral("social"), {{QStringLiteral("tab"), QStringLiteral("briefs")}}); });
    post_btn_ = new QPushButton(QStringLiteral("Post"));
    post_btn_->setToolTip(QStringLiteral("Post to the platforms ticked below; a draft is published first"));
    connect(post_btn_, &QPushButton::clicked, this, &WeeklyBriefPanel::post);
    actions->addWidget(generate_btn_);
    actions->addWidget(review_btn_);
    actions->addWidget(post_btn_);
    actions->addStretch(1);
    v->addLayout(actions);
    build_ = new BuildProgressView;
    connect(build_, &BuildProgressView::cancel, this, &WeeklyBriefPanel::cancel_build);
    connect(&BuildWatch::instance(), &BuildWatch::finished, this, &WeeklyBriefPanel::refresh);
    v->addWidget(build_);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);
    v->addWidget(status_);

    v->addWidget(caption(QStringLiteral("TOPICS")));
    for (const auto& t : kTopics) {
        auto* c = new QCheckBox(QString::fromUtf8(t.label));
        c->setToolTip(QString::fromUtf8(t.tip));
        connect(c, &QCheckBox::toggled, this, [this]() { save_settings(); });
        topics_.insert(QString::fromLatin1(t.key), c);
        v->addWidget(c);
    }

    v->addWidget(caption(QStringLiteral("LEVEL OF DETAIL")));
    detail_ = new QComboBox;
    detail_->setToolTip(QStringLiteral("Rows a table holds; Standard fits one page"));
    detail_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    detail_->setMinimumContentsLength(8);
    detail_->addItem(QStringLiteral("Short — 3 rows"), QStringLiteral("short"));
    detail_->addItem(QStringLiteral("Standard — 5 rows"), QStringLiteral("standard"));
    detail_->addItem(QStringLiteral("Full — 10 rows"), QStringLiteral("full"));
    connect(detail_, &QComboBox::currentIndexChanged, this, [this](int) { save_settings(); });
    v->addWidget(detail_);

    v->addWidget(caption(QStringLiteral("POST TO (ONCE THE PAGE IS LIVE)")));
    for (const auto& p : kPlatforms) {
        auto* row = new QHBoxLayout;
        auto* c = new QCheckBox(QString::fromUtf8(p.label));
        c->setToolTip(QString::fromUtf8(p.tip));
        connect(c, &QCheckBox::toggled, this, [this]() { save_settings(); });
        auto* note = new QLabel;
        note->setTextFormat(Qt::RichText);
        note->setWordWrap(true);
        row->addWidget(c);
        row->addWidget(note, 1);
        v->addLayout(row);
        platforms_.insert(QString::fromLatin1(p.key), c);
        platform_notes_.insert(QString::fromLatin1(p.key), note);
    }
    auto* connect_btn = new QPushButton(QStringLiteral("Connect accounts ▸"));
    connect_btn->setToolTip(QStringLiteral("Sign a platform in (X's PIN, a Bluesky app password, Mastodon's code) — "
                                           "beside this drawer"));
    connect(connect_btn, &QPushButton::clicked, this,
            [this]() { emit open_beside(QStringLiteral("social"), {{QStringLiteral("tab"), QStringLiteral("channels")}}); });
    v->addWidget(connect_btn, 0, Qt::AlignLeft);

    v->addWidget(caption(QStringLiteral("EVERY SUNDAY 09:00")));
    auto_ = new QButtonGroup(this);
    auto* draft = new QRadioButton(QStringLiteral("Draft for my review"));
    auto* publish = new QRadioButton(QStringLiteral("Publish && post automatically"));
    publish->setToolTip(QStringLiteral("The Sunday run builds the brief, publishes the page and posts it to the "
                                       "platforms ticked above — without review. An incomplete week is never "
                                       "published."));
    auto_->addButton(draft, 0);
    auto_->addButton(publish, 1);
    connect(auto_, &QButtonGroup::idClicked, this, [this](int) { save_settings(); });
    draft->setToolTip(QStringLiteral("The Sunday run builds a draft; nothing is published or posted until you "
                                     "do it"));
    v->addWidget(draft);
    v->addWidget(publish);
    auto* hint = new QLabel(QStringLiteral("Topics and detail apply to the next build."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:11px;padding-top:6px;").arg(ui::colors::TEXT_TERTIARY()));
    v->addWidget(hint);
    v->addStretch(1);

    poll_ = new QTimer(this);
    poll_->setInterval(15'000); // a build or a post finishing
    connect(poll_, &QTimer::timeout, this, &WeeklyBriefPanel::refresh);
}

void WeeklyBriefPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    load_settings();
    refresh();
    poll_->start();
}

void WeeklyBriefPanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    poll_->stop();
}

void WeeklyBriefPanel::load_settings() {
    QPointer<WeeklyBriefPanel> self = this;
    call_backend(this, QStringLiteral("brief_settings"), {}, kQuickMs,
                 [self](const QJsonObject& o, const QString& err, Outcome) {
                     if (!self)
                         return;
                     if (!err.isEmpty()) {
                         self->status_->setText(QStringLiteral("Settings not read: %1 — reopen this tab to try "
                                                               "again.")
                                                    .arg(err.toHtmlEscaped()));
                         return;
                     }
                     self->settings_ = o;
                     self->settings_loaded_ = true;
                     self->render();
                 });
    call_backend(this, QStringLiteral("channels"), {}, kQuickMs,
                 [self](const QJsonObject& o, const QString& err, Outcome) {
                     if (!self || !err.isEmpty())
                         return;
                     self->channels_ = o;
                     self->render();
                 });
}

void WeeklyBriefPanel::refresh() {
    // "drafts" also runs the tick (a due publish, a pending post): one at a
    // time, and never cut short — a killed tick could leave a push or a post
    // half done.
    if (refresh_inflight_)
        return;
    refresh_inflight_ = true;
    QPointer<WeeklyBriefPanel> self = this;
    call_backend(this, QStringLiteral("drafts"), {}, kTickMs, [self](const QJsonObject& o, const QString& err, Outcome) {
        if (!self)
            return;
        self->refresh_inflight_ = false;
        if (err.isEmpty()) {
            self->drafts_ = o;
            self->follow_build();
        }
        self->render();
    });
}

QJsonObject WeeklyBriefPanel::current_weekly() const {
    QJsonObject best;
    for (const auto& v : drafts_.value("drafts").toArray()) {
        const QJsonObject d = v.toObject();
        if (d.value("kind").toString() == QLatin1String("weekly") &&
            d.value("id").toString() > best.value("id").toString())
            best = d;
    }
    return best;
}

void WeeklyBriefPanel::render() {
    loading_ = true;
    // Settings → controls.
    const QJsonArray topics = settings_.value("topics").toArray();
    for (auto it = topics_.begin(); it != topics_.end(); ++it)
        it.value()->setChecked(topics.contains(it.key()));
    const int di = detail_->findData(settings_.value("detail").toString(QStringLiteral("standard")));
    detail_->setCurrentIndex(di >= 0 ? di : 1);
    const QJsonArray plats = settings_.value("platforms").toArray();
    QHash<QString, QJsonObject> ch;
    for (const auto& v : channels_.value("channels").toArray())
        ch.insert(v.toObject().value("network").toString(), v.toObject());
    for (auto it = platforms_.begin(); it != platforms_.end(); ++it) {
        const QJsonObject c = ch.value(it.key());
        it.value()->setChecked(plats.contains(it.key()));
        const bool enabled = c.isEmpty() || c.value("enabled").toBool(true);
        it.value()->setEnabled(enabled && settings_loaded_);
        it.value()->setToolTip(!c.isEmpty() && !c.value("configured").toBool()
                                   ? QStringLiteral("Not connected: while it is not, a tick here is skipped. "
                                                    "Connect accounts ▸ signs it in.")
                                   : QString());
        QString note;
        if (!enabled)
            note = QStringLiteral("coming soon");
        else if (c.value("configured").toBool())
            note = QStringLiteral("<span style='color:%1'>connected</span> %2")
                       .arg(ui::colors::POSITIVE(), c.value("account").toString().toHtmlEscaped());
        else if (!c.isEmpty())
            note = QStringLiteral("<span style='color:%1'>not connected</span>").arg(ui::colors::WARNING());
        platform_notes_.value(it.key())->setText(note);
    }
    if (auto* b = auto_->button(settings_.value("auto").toString() == QLatin1String("publish") ? 1 : 0))
        b->setChecked(true);
    for (auto* c : topics_)
        c->setEnabled(settings_loaded_);
    detail_->setEnabled(settings_loaded_);
    for (auto* b : auto_->buttons())
        b->setEnabled(settings_loaded_);
    loading_ = false;

    // This week.
    const QJsonObject d = current_weekly();
    const QString st = d.value("state").toString();
    QStringList lines;
    if (d.isEmpty()) {
        lines << QStringLiteral("No brief yet this week.");
    } else {
        const QString key = d.value("key").toString(d.value("title").toString());
        QString state = st == QLatin1String("building")    ? QStringLiteral("being built…")
                        : st == QLatin1String("published") ? QStringLiteral("<span style='color:%1'>published</span>")
                                                                 .arg(ui::colors::POSITIVE())
                        : st == QLatin1String("failed")    ? QStringLiteral("<span style='color:%1'>build failed</span>")
                                                              .arg(ui::colors::NEGATIVE())
                        : !d.value("schedule_at").toString().isEmpty()
                            ? QStringLiteral("scheduled %1").arg(QDateTime::fromString(d.value("schedule_at").toString(),
                                                                                       Qt::ISODate)
                                                                     .toLocalTime()
                                                                     .toString(QStringLiteral("ddd d MMM HH:mm")))
                            : QStringLiteral("draft ready for review");
        lines << QStringLiteral("<b>%1</b> · %2").arg(key.toHtmlEscaped(), state);
        if (!d.value("published_at").toString().isEmpty() && !d.value("url").toString().isEmpty())
            lines << QStringLiteral("<a href='%1' style='color:%2'>Open the page ↗</a>")
                         .arg(d.value("url").toString().toHtmlEscaped(), ui::colors::CYAN());
        const QJsonObject posted = d.value("posted").toObject();
        const QJsonObject errs = d.value("post_errors").toObject();
        QStringList where;
        for (const auto& p : kPlatforms) {
            const QString k = QString::fromLatin1(p.key);
            if (posted.contains(k))
                where << QStringLiteral("%1 ✓").arg(QString::fromUtf8(p.label));
            else if (errs.contains(k))
                where << QStringLiteral("<span style='color:%1' title='%2'>%3 ✗</span>")
                             .arg(ui::colors::NEGATIVE(), errs.value(k).toString().toHtmlEscaped(),
                                  QString::fromUtf8(p.label));
            else if (d.value("skipped").toArray().contains(k))
                where << QStringLiteral("%1 skipped (not connected)").arg(QString::fromUtf8(p.label));
            else if (d.value("post_to").toArray().contains(k))
                where << QStringLiteral("%1 (when live)").arg(QString::fromUtf8(p.label));
        }
        lines << (where.isEmpty() ? QStringLiteral("Not posted anywhere.") : QStringLiteral("Posted: ") + where.join(" · "));
        if (!d.value("note").toString().isEmpty())
            lines << QStringLiteral("<span style='color:%1'>%2</span>")
                         .arg(ui::colors::WARNING(), d.value("note").toString().toHtmlEscaped());
    }
    const QString health = drafts_.value("health").toString();
    if (!health.isEmpty())
        lines << QStringLiteral("<span style='color:%1'>%2</span>").arg(ui::colors::WARNING(), health.toHtmlEscaped());
    issue_->setText(lines.join(QStringLiteral("<br>")));

    // Generate and Post are on unless something is running; a click that
    // cannot act says what it needs. Review stays on: the build shows there.
    const bool building = st == QLatin1String("building");
    build_->setVisible(building);
    if (building) {
        if (!building_)
            built_before_ = d.value("built_at").toString(); // cancelled or failed, it is still this
        building_ = true;
    } else if (building_) { // seen running, and now it is not
        building_ = false;
        const QString built = d.value("built_at").toString();
        const bool fresh = st == QLatin1String("ready") && built != built_before_;
        say(fresh ? QStringLiteral("Built.") : QString());
        if (fresh && isVisible()) // the finished brief, to read and correct
            QTimer::singleShot(0, this, [this]() {
                emit open_beside(QStringLiteral("social"), {{QStringLiteral("tab"), QStringLiteral("briefs")}});
            });
    }
    render_actions();
}

void WeeklyBriefPanel::save_settings() {
    if (loading_ || !settings_loaded_) // never write controls that were not filled from the file
        return;
    QJsonArray topics, plats;
    for (auto it = topics_.begin(); it != topics_.end(); ++it)
        if (it.value()->isChecked())
            topics.append(it.key());
    for (auto it = platforms_.begin(); it != platforms_.end(); ++it)
        if (it.value()->isChecked())
            plats.append(it.key());
    const QJsonObject s{{"v", 4}, // this list has a box for every section (brief_drafts.settings_set)
                        {"topics", topics},
                        {"detail", detail_->currentData().toString()},
                        {"platforms", plats},
                        {"auto", auto_->checkedId() == 1 ? QStringLiteral("publish") : QStringLiteral("draft")}};
    QPointer<WeeklyBriefPanel> self = this;
    call_backend(this, QStringLiteral("brief_settings_set"), {{QStringLiteral("settings"), s}}, kQuickMs,
                 [self](const QJsonObject& o, const QString& err, Outcome) {
                     if (!self)
                         return;
                     if (!err.isEmpty()) {
                         self->status_->setText(
                             QStringLiteral("<span style='color:%1'>%2</span>").arg(ui::colors::NEGATIVE(), err.toHtmlEscaped()));
                         self->render(); // back to what is stored
                         return;
                     }
                     self->settings_ = o;
                     self->status_->setText(QStringLiteral("Saved."));
                     self->render();
                 });
}

void WeeklyBriefPanel::render_actions() {
    const bool idle = !busy_ && current_weekly().value("state").toString() != QLatin1String("building");
    generate_btn_->setEnabled(idle);
    post_btn_->setEnabled(idle);
}

bool WeeklyBriefPanel::start(const QString& what) {
    if (busy_ || current_weekly().value("state").toString() == QLatin1String("building"))
        return false;
    busy_ = true;
    render_actions(); // at once: a second click must find them off
    say(what);
    return true;
}

void WeeklyBriefPanel::cancel_build() {
    const QString id = current_weekly().value("id").toString();
    if (id.isEmpty())
        return;
    say(QStringLiteral("Cancelling…"));
    QPointer<WeeklyBriefPanel> self = this;
    call_backend(this, QStringLiteral("draft_build_cancel"), {{QStringLiteral("id"), id}}, kQuickMs,
                 [self](const QJsonObject&, const QString& err, Outcome) {
                     if (!self)
                         return;
                     self->building_ = false; // told here, not as "Built."
                     self->say(err.isEmpty() ? QStringLiteral("Cancelled.") : err,
                               err.isEmpty() ? QString() : ui::colors::WARNING());
                     self->refresh();
                 });
}

void WeeklyBriefPanel::say(const QString& text, const QString& colour) {
    status_->setText(colour.isEmpty() ? text.toHtmlEscaped()
                                      : QStringLiteral("<span style='color:%1'>%2</span>").arg(colour, text.toHtmlEscaped()));
}

bool WeeklyBriefPanel::ask(const QString& title, const QString& question) {
    return QMessageBox::question(this, title, question) == QMessageBox::Yes;
}

void WeeklyBriefPanel::generate(bool force) {
    if (!start(QString()))
        return;
    QPointer<WeeklyBriefPanel> self = this;
    call_backend(this, QStringLiteral("draft_build_weekly"), {{QStringLiteral("force"), force}, {QStringLiteral("symbols"), user_symbols()}}, kQuickMs,
                 [self](const QJsonObject& o, const QString& err, Outcome) {
                     if (!self)
                         return;
                     self->busy_ = false;
                     self->render_actions();
                     // Corrections, or a published brief: the backend says which.
                     if (o.value("needs_force").toBool() || o.value("needs_new_edition").toBool()) {
                         self->say({});
                         if (self->ask(QStringLiteral("Generate"), err) && self)
                             self->generate(true);
                         return;
                     }
                     if (!err.isEmpty()) {
                         self->say(err, ui::colors::NEGATIVE());
                     } else {
                         // The page fills in beside the drawer as it is built.
                         self->adopt(o);
                         emit self->open_beside(QStringLiteral("social"),
                                                {{QStringLiteral("tab"), QStringLiteral("briefs")}});
                     }
                     self->refresh();
                 });
}

void WeeklyBriefPanel::follow_build() {
    const QJsonObject d = current_weekly();
    BuildWatch::instance().watch(d.value("state").toString() == QLatin1String("building")
                                     ? d.value("progress_path").toString()
                                     : QString());
}

void WeeklyBriefPanel::adopt(const QJsonObject& draft) {
    QJsonArray list = drafts_.value("drafts").toArray();
    const QString id = draft.value("id").toString();
    int at = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toObject().value("id").toString() == id)
            at = i;
    if (id.isEmpty())
        return;
    if (at < 0)
        list.prepend(draft);
    else
        list[at] = draft;
    drafts_[QStringLiteral("drafts")] = list;
    follow_build();
    render();
}

void WeeklyBriefPanel::post() {
    const QJsonObject d = current_weekly();
    const QString st = d.value("state").toString();
    const bool published = st == QLatin1String("published");
    if (!published && st != QLatin1String("ready")) {
        say(st == QLatin1String("building") ? QStringLiteral("Still building.") : QStringLiteral("Generate it first."));
        return;
    }
    QJsonArray fresh, ticked;
    QStringList fresh_names, names;
    for (auto it = platforms_.begin(); it != platforms_.end(); ++it) {
        if (!it.value()->isChecked())
            continue;
        ticked.append(it.key());
        names << it.value()->text();
        if (!d.value("posted").toObject().contains(it.key())) {
            fresh.append(it.key());
            fresh_names << it.value()->text();
        }
    }
    if (ticked.isEmpty()) {
        say(QStringLiteral("Tick a platform below."));
        return;
    }
    const bool again = fresh.isEmpty(); // every ticked platform has it: asked for once more
    const bool incomplete = !published && !d.value("complete").toBool(true);
    const QString key = d.value("key").toString();
    const QString where = (again ? names : fresh_names).join(QStringLiteral(", "));
    QString q = published ? QStringLiteral("Post %1 to %2?").arg(key, where)
                          : QStringLiteral("Publish %1 and post it to %2?").arg(key, where);
    if (again)
        q = published ? QStringLiteral("%1 is already on %2. Post again?").arg(key, where)
                      : QStringLiteral("Publish the new edition of %1 and post it to %2 again? (Waits for the page.)").arg(key, where);
    if (incomplete)
        q += QStringLiteral("\n\nSome days of filings could not be read.");
    QPointer<WeeklyBriefPanel> self = this;
    if (!ask(QStringLiteral("Post"), q) || !self || !start(published ? QStringLiteral("Posting…") : QStringLiteral("Publishing…")))
        return;
    call_backend(this, QStringLiteral("draft_post_now"),
                 {{QStringLiteral("id"), d.value("id").toString()},
                  {QStringLiteral("networks"), again ? ticked : fresh},
                  {QStringLiteral("publish"), !published},
                  {QStringLiteral("again"), again},
                  {QStringLiteral("accept_incomplete"), incomplete}},
                 published ? kPostMs : kTickMs, [self](const QJsonObject& o, const QString& err, Outcome) {
                     if (!self)
                         return;
                     self->busy_ = false;
                     const QJsonObject errs = o.value("post_errors").toObject();
                     QStringList bad;
                     for (auto it = errs.begin(); it != errs.end(); ++it)
                         bad << QStringLiteral("%1: %2").arg(it.key(), it.value().toString());
                     for (const auto& v : o.value("off").toArray())
                         bad << QStringLiteral("%1: not connected").arg(v.toString());
                     if (o.value("waiting").toBool())
                         self->say(err); // published; the post follows the page
                     else if (!err.isEmpty())
                         self->say(err, ui::colors::WARNING());
                     else if (bad.isEmpty())
                         self->say(QStringLiteral("Posted."));
                     else
                         self->say(QStringLiteral("Not posted to ") + bad.join(QStringLiteral("; ")), ui::colors::NEGATIVE());
                     self->refresh();
                 });
}

} // namespace finterm::screens
