#include "screens/dashboard/canvas/SelectionLink.h"

#include "screens/dashboard/canvas/DashboardCanvas.h"

namespace finterm::screens {

SelectionLink* SelectionLink::of(QWidget* w) {
    for (; w; w = w->parentWidget())
        if (auto* canvas = qobject_cast<DashboardCanvas*>(w)) {
            if (auto* link = canvas->findChild<SelectionLink*>(QString(), Qt::FindDirectChildrenOnly))
                return link;
            return new SelectionLink(canvas);
        }
    return nullptr;
}

void SelectionLink::select(const QString& symbol, const QString& label) {
    symbol_ = symbol;
    label_ = label;
    emit selected(symbol, label);
}

} // namespace finterm::screens
