#pragma once
#include <QWidget>

namespace finterm::screens {

/// Trademarks — finterm and third-party trademark acknowledgments.
class TrademarksScreen : public QWidget {
    Q_OBJECT
  public:
    explicit TrademarksScreen(QWidget* parent = nullptr);

  signals:
    void navigate_back();
};

} // namespace finterm::screens
