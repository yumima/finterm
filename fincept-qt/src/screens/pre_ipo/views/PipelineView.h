// src/screens/pre_ipo/views/PipelineView.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QWidget>

class QTableWidget;
class QLabel;

namespace fincept::screens {

/// Full IPO pipeline view — every active S-1/F-1 filer with amendment cadence
/// and days-since-first-file. Surfaces "pricing imminent" candidates via the
/// amendment count and a derived expected-window column.
class PipelineView : public QWidget {
    Q_OBJECT
  public:
    explicit PipelineView(QWidget* parent = nullptr);

    void set_pipeline(const QVector<pre_ipo::S1Filing>& pipeline,
                      const QVector<pre_ipo::PrivateCompany>& companies);

  signals:
    void company_selected(QString id);

  private:
    void build_ui();
    void rebuild_table();

    QTableWidget* table_  = nullptr;
    QLabel*       count_lbl_ = nullptr;

    QVector<pre_ipo::S1Filing>       pipeline_;
    QVector<pre_ipo::PrivateCompany> companies_;
};

} // namespace fincept::screens
