#pragma once
// NetworkSection.h — Settings › Data Sources › "Network": how finterm reaches
// the internet (core/network/ProxySettings) — the system's proxy, none, or a
// proxy the user names (a VPN client's local proxy) — and a test that asks
// Yahoo Finance through the choice and says, in plain words, what happened.

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;

namespace finterm::screens {

class NetworkSection : public QWidget {
    Q_OBJECT
  public:
    explicit NetworkSection(QWidget* parent = nullptr);

  private:
    void on_apply();
    void on_test();
    void update_state();

    QButtonGroup* mode_ = nullptr;
    QLineEdit* url_ = nullptr;
    QLabel* in_use_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* test_ = nullptr;
    QNetworkAccessManager* nam_ = nullptr;
};

} // namespace finterm::screens
