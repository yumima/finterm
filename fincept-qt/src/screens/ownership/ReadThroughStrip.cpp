#include "screens/ownership/ReadThroughStrip.h"

#include "ui/theme/Theme.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace fincept::screens {

using namespace fincept::ownership;

namespace {
constexpr int kPad     = 6;
constexpr int kChipH   = 22;
constexpr int kGap     = 6;
constexpr int kBarW    = 3;    // the severity bar down the chip's left edge
constexpr int kHeadH   = 16;

QString weight_colour(Weight w) {
    switch (w) {
        case Weight::Elevated: return ui::colors::AMBER();
        case Weight::Notable:  return ui::colors::CYAN();
        case Weight::Context:  break;
    }
    return ui::colors::TEXT_SECONDARY();
}

/// Strongest first. The signals layer asserts an ordering by Weight and
/// nothing finer, so ties keep the order derive_reads produced rather than
/// inventing a second criterion.
bool stronger(const Read& a, const Read& b) {
    return static_cast<int>(a.weight) > static_cast<int>(b.weight);
}
} // namespace

ReadThroughStrip::ReadThroughStrip(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void ReadThroughStrip::set_empty_text(const QString& text) {
    empty_text_ = text;
    update();
}

void ReadThroughStrip::set_reads(const QVector<Read>& reads) {
    reads_ = reads;
    chips_.clear();
    updateGeometry();
    update();
}

QSize ReadThroughStrip::minimumSizeHint() const { return {240, kChipH + kPad * 2}; }

int ReadThroughStrip::heightForWidth(int w) const {
    QVector<Chip> tmp;
    return layout_chips(w, &tmp);
}

int ReadThroughStrip::layout_chips(int width, QVector<Chip>* out) const {
    out->clear();
    if (reads_.isEmpty())
        return kChipH + kPad * 2;

    QFont f = font();
    f.setPixelSize(11);
    const QFontMetrics fm(f);
    int y = kPad;

    // Grouped by lens, because "what it means for the stock" and "what it means
    // for how it trades" are two different questions and a reader is usually
    // asking one of them.
    const std::pair<Lens, QString> groups[] = {
        {Lens::Stock, QStringLiteral("FOR THE STOCK")},
        {Lens::Flows, QStringLiteral("FOR HOW IT TRADES")},
    };
    for (const auto& [lens, heading] : groups) {
        QVector<Read> group;
        for (const auto& r : reads_)
            if (r.lens == lens)
                group.push_back(r);
        if (group.isEmpty())
            continue;
        std::stable_sort(group.begin(), group.end(), stronger);

        Chip h;
        h.is_heading = true;
        h.heading = heading;
        h.box = QRect(kPad, y, std::max(40, width - kPad * 2), kHeadH);
        out->push_back(h);
        y += kHeadH + 2;

        int x = kPad;
        for (const auto& r : group) {
            const int w = kBarW + 8 + fm.horizontalAdvance(r.headline) + 10;
            if (x > kPad && x + w > width - kPad) {   // wrap
                x = kPad;
                y += kChipH + kGap;
            }
            Chip c;
            c.read = r;
            c.box = QRect(x, y, w, kChipH);
            out->push_back(c);
            x += w + kGap;
        }
        y += kChipH + kGap + 2;
    }
    return y + kPad;
}

void ReadThroughStrip::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layout_chips(width(), &chips_);
    updateGeometry();
}

void ReadThroughStrip::paintEvent(QPaintEvent*) {
    QPainter g(this);
    QFont f = font();
    f.setPixelSize(11);
    g.setFont(f);

    if (reads_.isEmpty()) {
        g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        g.drawText(rect().adjusted(kPad, kPad, -kPad, -kPad),
                   Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, empty_text_);
        return;
    }
    if (chips_.isEmpty())
        layout_chips(width(), &chips_);

    for (const auto& c : chips_) {
        if (c.is_heading) {
            g.setPen(QColor(ui::colors::TEXT_DIM()));
            g.drawText(c.box, Qt::AlignVCenter | Qt::AlignLeft, c.heading);
            continue;
        }
        const QColor col(weight_colour(c.read.weight));

        // Ground tinted by severity, faintly: an Elevated finding should look
        // different from context before any word is read, but a wall of amber
        // blocks would be its own kind of noise.
        QColor bg(col);
        bg.setAlpha(c.read.weight == Weight::Elevated ? 34 : 20);
        g.fillRect(c.box, bg);
        // The severity bar. Height IS the reading: full for Elevated, and
        // progressively shorter down the scale.
        const int frac = c.read.weight == Weight::Elevated ? c.box.height()
                       : c.read.weight == Weight::Notable  ? c.box.height() * 2 / 3
                                                           : c.box.height() / 3;
        g.fillRect(QRect(c.box.left(), c.box.bottom() - frac + 1, kBarW, frac), col);

        g.setPen(QColor(c.read.weight == Weight::Context ? ui::colors::TEXT_SECONDARY()
                                                         : ui::colors::TEXT_PRIMARY()));
        g.drawText(c.box.adjusted(kBarW + 8, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   c.read.headline);
    }
}

bool ReadThroughStrip::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        if (chips_.isEmpty() && !reads_.isEmpty())
            layout_chips(width(), &chips_);
        for (const auto& c : chips_) {
            if (c.is_heading || !c.box.contains(he->pos()))
                continue;
            // Exactly what the prose card used to show, for the one finding the
            // reader asked about: the sentence with its number, then the rule
            // that produced it.
            QString t = c.read.detail;
            if (!c.read.basis.isEmpty())
                t += QStringLiteral("\n\n") + c.read.basis;
            QToolTip::showText(he->globalPos(), t, this);
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace fincept::screens
