#pragma once
// ArtefactsSection.h — Workbench artefacts panel (Track 5B).
//
// Surfaces every `chat_artefact` row — typed slash output the agent
// emitted via the `emit_artefact` tool — with actions:
//
//   - **View**     double-click → full-payload dialog
//   - **Re-run**   dispatch the same (agent, skill, args) again;
//                  predecessor is marked `superseded`, new artefact
//                  flows in via emit_artefact when the new run lands
//   - **Export**   save the payload to disk; format follows the kind
//                  (CSV for table, JSON for model, markdown for memo
//                  / report)
//   - **Mark superseded** soft-delete; the row stays for audit but
//                          drops off the default "active" filter

#include "core/result/Result.h"
#include "storage/repositories/ChatArtefactRepository.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

class ArtefactsSection : public QWidget {
    Q_OBJECT
  public:
    explicit ArtefactsSection(QWidget* parent = nullptr);

    void reload();

  private slots:
    void on_refresh();
    void on_double_clicked(int row, int column);
    void on_rerun();
    void on_export();
    void on_supersede();
    void on_selection_changed();

  private:
    QTableWidget* table_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QPushButton* rerun_btn_ = nullptr;
    QPushButton* export_btn_ = nullptr;
    QPushButton* supersede_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    void build_ui();
    void populate(const QVector<ChatArtefactRow>& rows);
    void show_status(const QString& msg, bool error = false);
    QString selected_id() const;
    Result<ChatArtefactRow> selected_artefact() const;
};

} // namespace fincept::screens
