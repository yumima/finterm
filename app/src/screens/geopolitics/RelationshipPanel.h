// src/screens/geopolitics/RelationshipPanel.h
#pragma once
#include "services/geopolitics/GeopoliticsTypes.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace finterm::screens {

/// Network relationship visualization of conflicts, crises, and organizations.
class RelationshipPanel : public QWidget {
    Q_OBJECT
  public:
    explicit RelationshipPanel(QWidget* parent = nullptr);

  private:
    void build_ui();
    QWidget* build_node_card(const finterm::services::geo::RelationshipNode& node, QWidget* parent);

    QLabel* selected_label_ = nullptr;
    QWidget* detail_panel_ = nullptr;
};

} // namespace finterm::screens
