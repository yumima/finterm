#pragma once
#include <QDialog>

#include "screens/ownership/OwnershipTypes.h"

namespace fincept::screens {

/// One Form 4, rendered.
///
/// The filing's own URL points at raw XML. Handing that to a browser shows a
/// wall of tags, and the SEC's rendered version is a separate document at a
/// path the filing does not carry — so "open the filing" was, in practice,
/// "leave the terminal and read markup". The transactions are already parsed
/// and in hand, so the filing is shown here instead: who, what role, which
/// codes, how many shares, at what price, and what they held afterwards.
///
/// The EDGAR link stays, one click away, because the filing is the evidence and
/// a reader must always be able to reach it.
class Form4Dialog : public QDialog {
    Q_OBJECT
  public:
    /// @param filing every transaction reported on the one filing being shown.
    Form4Dialog(const QVector<ownership::InsiderTransaction>& filing,
                const QString& issuer, QWidget* parent = nullptr);
};

} // namespace fincept::screens
