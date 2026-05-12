// src/screens/pre_ipo/views/PipelineView.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QScrollArea;

namespace fincept::screens {

/// Full IPO pipeline view — every active S-1/F-1 filer with amendment cadence
/// and days-since-first-file. Surfaces "pricing imminent" candidates via the
/// amendment count and a derived expected-window column.
///
/// Layout: 3-column responsive grid of cards. The previous single-column
/// table forced the user to horizontal-scroll to see the date columns and
/// made the list feel taller than it was. A card grid renders the same
/// fields with the dates always visible.
class PipelineView : public QWidget {
    Q_OBJECT
  public:
    explicit PipelineView(QWidget* parent = nullptr);

    void set_pipeline(const QVector<pre_ipo::S1Filing>& pipeline,
                      const QVector<pre_ipo::PrivateCompany>& companies);

  signals:
    void company_selected(QString id);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void build_ui();
    void rebuild_grid();
    QWidget* make_card(const pre_ipo::S1Filing& s, const QString& company_id) const;

    QGridLayout*  grid_     = nullptr;
    QScrollArea*  scroll_   = nullptr;
    QWidget*      grid_host_ = nullptr;
    QLabel*       count_lbl_ = nullptr;

    QVector<pre_ipo::S1Filing>       pipeline_;
    QVector<pre_ipo::PrivateCompany> companies_;

    static constexpr int kColumns = 3;
};

} // namespace fincept::screens
