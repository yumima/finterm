// src/screens/relationship_map/RelationshipGraphScene.h
#pragma once

#include "screens/relationship_map/RelationshipMapTypes.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTimer>

namespace finterm::relmap {

class RelNode;

/// Interactive graph scene for relationship map visualization.
/// Force-directed layout with elastic nodes.
class RelationshipGraphScene : public QGraphicsScene {
    Q_OBJECT
  public:
    explicit RelationshipGraphScene(QObject* parent = nullptr);

    /// Build graph from data, applying layout and filters.
    void build_graph(const RelationshipData& data, const FilterState& filters, LayoutMode layout);

    /// Clear all items.
    void clear_graph();

  signals:
    void center_card_clicked(const QString& ticker);

  private:

    QMap<QString, QPointF> node_positions_;
    QVector<RelNode*> nodes_;
    QTimer* sim_timer_ = nullptr;
};

/// Zooming/panning view for the graph.
class RelationshipGraphView : public QGraphicsView {
    Q_OBJECT
  public:
    explicit RelationshipGraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void fit_to_content();

  protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

  private:
    double current_zoom_{1.0};
    static constexpr double kMinZoom = 0.15;
    static constexpr double kMaxZoom = 4.0;
};

} // namespace finterm::relmap
