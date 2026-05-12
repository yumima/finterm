// src/screens/pre_ipo/views/PicksView.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace fincept::screens {

/// Daily digest of investable signals: high-readiness names, fresh S-1s,
/// fund mark-ups, amendment bursts. Default landing tab — answers
/// "what should I look at this week?" at a glance.
class PicksView : public QWidget {
    Q_OBJECT
  public:
    explicit PicksView(QWidget* parent = nullptr);

    void set_summary(const pre_ipo::PreIpoSummary& summary);

  signals:
    /// User clicked a pick card. id is the company slug.
    void company_selected(QString id);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void build_ui();
    QWidget* make_pick_card(const pre_ipo::PrivateCompany& c) const;
    QWidget* make_signal_card(const pre_ipo::Signal& s) const;

    QLabel*      header_lbl_         = nullptr;
    QVBoxLayout* picks_layout_       = nullptr;
    QVBoxLayout* signals_layout_     = nullptr;
    QLabel*      picks_count_lbl_    = nullptr;
    QLabel*      signals_count_lbl_  = nullptr;
};

} // namespace fincept::screens
