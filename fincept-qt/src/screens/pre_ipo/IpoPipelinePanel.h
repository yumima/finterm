// src/screens/pre_ipo/IpoPipelinePanel.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

/// Right panel showing IPO pipeline (S-1 filers) and recent Form D deal flow.
class IpoPipelinePanel : public QWidget {
    Q_OBJECT
  public:
    explicit IpoPipelinePanel(QWidget* parent = nullptr);

    void set_pipeline(const QVector<pre_ipo::S1Filing>& pipeline);
    void set_form_d(const QVector<pre_ipo::FormDFiling>& filings);

  private:
    void build_ui();
    void rebuild_pipeline();
    void rebuild_form_d();
    QWidget* make_form_d_card(const pre_ipo::FormDFiling& f) const;

    // ── Pipeline table ────────────────────────────────────────────────────────
    QTableWidget* pipeline_table_ = nullptr;

    // ── Form D feed ───────────────────────────────────────────────────────────
    QScrollArea*  feed_scroll_     = nullptr;
    QWidget*      feed_container_  = nullptr;
    QVBoxLayout*  feed_layout_     = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    QVector<pre_ipo::S1Filing>    pipeline_;
    QVector<pre_ipo::FormDFiling> form_d_;
};

} // namespace fincept::screens
