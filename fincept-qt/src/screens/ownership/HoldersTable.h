#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QTableWidget;

namespace fincept::screens {

/// HOLDERS — the 13F register, in the industry's columns.
///
/// Level and change sit in adjacent columns (FactSet's `Position | Pos
/// Change`, Nasdaq's `Shares Held | Change`), the filer's own weight beside
/// them, and a Type column that says honestly what the index can tell: a
/// named passive complex, a broad book, or a focused one. Default sort is
/// position size, as HDS; the weight sort is the second view and carries a
/// book-size floor so it reads Berkshire-at-22%, not a shell at 67%.
///
/// Filter chips as WhaleWisdom's: All · New · Added · Reduced. Closed
/// positions cannot appear in a list driven by current holdings; their count
/// is in the header. A 13D/13G badge on the holder's row is FactSet's pattern
/// and replaces the separate stakes table.
class HoldersTable : public QWidget {
    Q_OBJECT
  public:
    explicit HoldersTable(QWidget* parent = nullptr);

    void set_snapshot(const ownership::OwnershipSnapshot& snap, bool loading);
    void clear();

  signals:
    /// A holder row was clicked — carries the filer's CIK and display name.
    void holder_activated(const QString& cik, const QString& name);
    /// The reader picked the other ranking.
    void sort_requested(bool by_weight);

  private:
    void render();

    ownership::OwnershipSnapshot snap_;
    bool loading_ = false;
    QString filter_ = QStringLiteral("all");

    QLabel* title_note_ = nullptr;
    QButtonGroup* filters_ = nullptr;
    QComboBox* sort_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

} // namespace fincept::screens
