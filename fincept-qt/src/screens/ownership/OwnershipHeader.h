#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace fincept::screens {

/// The ownership summary: the six figures every terminal puts at the top
/// (Bloomberg OWN, FactSet's statistics block, Nasdaq's summary), each with
/// the date it was true, and under them the three flags — or the line saying
/// none of the three applies, which is itself a finding.
///
/// Every number is computed by OwnershipFlags::derive_header from the
/// snapshot, so the widget holds no arithmetic of its own; what it adds is
/// the layout and the words.
class OwnershipHeader : public QWidget {
    Q_OBJECT
  public:
    explicit OwnershipHeader(QWidget* parent = nullptr);

    void set_snapshot(const ownership::OwnershipSnapshot& snap, bool loading);
    void clear();

  private:
    struct Row {
        QLabel* key = nullptr;
        QLabel* value = nullptr;
        QLabel* note = nullptr;
    };
    Row add_row(const QString& key);

    QGridLayout* grid_ = nullptr;
    Row institutions_, insiders_, top10_, change_, shorts_, stakes_;
    QVBoxLayout* flags_ = nullptr;
    QLabel* none_ = nullptr;
    QVector<QWidget*> flag_widgets_;
};

} // namespace fincept::screens
