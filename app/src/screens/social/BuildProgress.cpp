#include "screens/social/BuildProgress.h"

#include "ui/notifications/ToastService.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace finterm::screens {
namespace {
constexpr int kTickMs = 500;
constexpr int kSettleMs = 1500; // the draft is stored just after its progress says done

QString clock(double seconds) {
    const int s = std::max(0, int(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

double now() {
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}
} // namespace

BuildProgress BuildProgress::parse(const QJsonObject& o) {
    BuildProgress p;
    const QJsonArray steps = o.value(QStringLiteral("steps")).toArray();
    p.valid = !steps.isEmpty();
    p.done = o.value(QStringLiteral("done")).toBool();
    p.started = o.value(QStringLiteral("started")).toDouble();
    p.body = o.value(QStringLiteral("body")).toString();
    for (const auto& v : steps) {
        const QJsonObject s = v.toObject();
        p.steps.push_back({s.value(QStringLiteral("label")).toString(), s.value(QStringLiteral("state")).toString(),
                           s.value(QStringLiteral("detail")).toString(),
                           std::clamp(s.value(QStringLiteral("part")).toDouble(), 0.0, 1.0),
                           s.value(QStringLiteral("t0")).toDouble(), s.value(QStringLiteral("t1")).toDouble()});
    }
    return p;
}

double BuildProgress::fraction() const {
    if (done)
        return 1;
    if (steps.isEmpty())
        return 0;
    double sum = 0;
    for (const auto& s : steps)
        sum += s.state == QLatin1String("done") || s.state == QLatin1String("warn") ? 1.0
               : s.state == QLatin1String("run")                                     ? 0.95 * s.part
                                                                                     : 0.0;
    return sum / double(steps.size());
}

BuildWatch& BuildWatch::instance() {
    static BuildWatch w;
    return w;
}

BuildWatch::BuildWatch() {
    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &BuildWatch::tick);
}

void BuildWatch::watch(const QString& path) {
    if (path == path_)
        return;
    path_ = path;
    stamp_ = -1;
    progress_ = {};
    if (path_.isEmpty()) {
        timer_->stop();
    } else {
        timer_->start();
        tick();
    }
}

void BuildWatch::tick() {
    ++spin_;
    const QFileInfo fi(path_);
    const qint64 stamp = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
    if (stamp < 0) {
        progress_ = {}; // not begun, or cancelled
        stamp_ = -1;
    } else if (stamp != stamp_) {
        QFile f(path_);
        if (f.open(QIODevice::ReadOnly)) {
            const auto p = BuildProgress::parse(QJsonDocument::fromJson(f.readAll()).object());
            if (p.valid) { // replaced whole by the generator: a half-read file does not happen
                progress_ = p;
                stamp_ = stamp;
            }
        }
    }
    emit updated();
    // Told once per build: the draft's own state follows the file by a moment,
    // and until a panel has read it this keeps being watched.
    if (progress_.done && progress_.started != told_) {
        told_ = progress_.started;
        ui::ToastService::instance().post(ui::ToastService::Severity::Success, QStringLiteral("Weekly brief built"),
                                          QStringLiteral("Review it in NEWS › What's On › WEEKLY BRIEF."));
        QTimer::singleShot(kSettleMs, this, &BuildWatch::finished);
    }
}

BuildProgressView::BuildProgressView(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(4);
    auto* top = new QHBoxLayout;
    head_ = new QLabel;
    head_->setStyleSheet(QStringLiteral("color:%1;font-weight:700;background:transparent;").arg(ui::colors::AMBER()));
    top->addWidget(head_);
    top->addStretch(1);
    cancel_ = new QPushButton(QStringLiteral("Cancel"));
    cancel_->setStyleSheet(QStringLiteral("QPushButton{background:transparent;color:%1;border:none;font-size:11px;}"
                                          "QPushButton:hover{color:%2;}")
                               .arg(ui::colors::TEXT_SECONDARY(), ui::colors::NEGATIVE()));
    cancel_->setCursor(Qt::PointingHandCursor);
    connect(cancel_, &QPushButton::clicked, this, &BuildProgressView::cancel);
    top->addWidget(cancel_);
    v->addLayout(top);
    bar_ = new QProgressBar;
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(3);
    bar_->setStyleSheet(QStringLiteral("QProgressBar{background:%1;border:none;}QProgressBar::chunk{background:%2;}")
                            .arg(ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    v->addWidget(bar_);
    steps_ = new QLabel;
    steps_->setTextFormat(Qt::RichText);
    steps_->setWordWrap(true);
    steps_->setStyleSheet(QStringLiteral("background:transparent;"));
    v->addWidget(steps_);
    connect(&BuildWatch::instance(), &BuildWatch::updated, this, [this]() {
        if (isVisible())
            render();
    });
    hide();
}

void BuildProgressView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    render();
}

void BuildProgressView::render() {
    const auto& w = BuildWatch::instance();
    const auto& p = w.progress();
    if (!p.valid) {
        head_->setText(QStringLiteral("Starting…"));
        bar_->setValue(0);
        steps_->clear();
        return;
    }
    static const QString kSpin[] = {QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
    const double t = now();
    head_->setText(QStringLiteral("Building · %1").arg(clock(t - p.started)));
    bar_->setValue(int(p.fraction() * 1000));
    QString h = QStringLiteral("<table cellspacing='0' cellpadding='1' width='100%'>");
    for (const auto& s : p.steps) {
        const bool run = s.state == QLatin1String("run"), done = s.state == QLatin1String("done"),
                   warn = s.state == QLatin1String("warn");
        const QString colour = run    ? ui::colors::AMBER()
                               : done ? ui::colors::POSITIVE()
                               : warn ? ui::colors::WARNING()
                                      : ui::colors::TEXT_TERTIARY();
        const QString mark = run    ? kSpin[w.spin() % 4]
                             : done ? QStringLiteral("✓")
                             : warn ? QStringLiteral("!")
                                    : QStringLiteral("·");
        h += QStringLiteral("<tr><td width='18' style='color:%1'>%2</td><td style='color:%3'>%4"
                            "<span style='color:%5'>%6</span></td><td align='right' style='color:%5'>%7</td></tr>")
                 .arg(colour, mark, run ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_SECONDARY(),
                      s.label.toHtmlEscaped(), ui::colors::TEXT_TERTIARY(),
                      s.detail.isEmpty() ? QString() : QStringLiteral(" · ") + s.detail.toHtmlEscaped(),
                      run                              ? clock(t - s.t0)
                      : (done || warn) && s.t1 >= s.t0 ? clock(s.t1 - s.t0)
                                                       : QString());
    }
    steps_->setText(h + QStringLiteral("</table>"));
}

} // namespace finterm::screens
