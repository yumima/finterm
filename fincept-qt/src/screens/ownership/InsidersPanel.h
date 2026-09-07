#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;

namespace fincept::screens {

class InsiderTimeline;

/// INSIDERS — Form 4, the way OpenInsider lays it out.
///
/// Open-market rows by default: grants, option exercises and tax withholding
/// vest on a calendar and are not decisions about price, and an insider table
/// that interleaves them reads as constant activity. A toggle brings them
/// back for the reader who wants the whole filing history.
///
/// The two columns the evidence asks for are here: the 10b5-1 plan flag (a
/// trade decided months before it printed) and "since trade" — what the stock
/// did after, from the close on the trade date, shown once the window has
/// elapsed and never projected. Buys by 10% owners are shown dimmed and are
/// never scored; the header line says why.
class InsidersPanel : public QWidget {
    Q_OBJECT
  public:
    explicit InsidersPanel(QWidget* parent = nullptr);

    void set_snapshot(const ownership::OwnershipSnapshot& snap, bool loading);
    void clear();

  private:
    void render();

    ownership::OwnershipSnapshot snap_;
    bool loading_ = false;

    QLabel* title_note_ = nullptr;
    QCheckBox* show_all_ = nullptr;
    InsiderTimeline* timeline_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

} // namespace fincept::screens
