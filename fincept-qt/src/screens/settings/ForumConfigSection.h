#pragma once
// ForumConfigSection.h — Settings → Forum (Track 6B).
//
// Single user-visible knob: the forum backend's base URL.  finterm
// doesn't ship a hosted forum yet; users point this at whatever
// compatible backend they want (a finterm-hosted forum once it
// lands, or a self-hosted instance).  Empty string keeps the
// forum surface disabled — no outbound traffic, no empty-state
// noise on the Forum screen beyond "configure a backend".

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

class ForumConfigSection : public QWidget {
    Q_OBJECT
  public:
    explicit ForumConfigSection(QWidget* parent = nullptr);

  private slots:
    void on_save();
    void on_clear();

  private:
    QLineEdit* url_edit_ = nullptr;
    QPushButton* save_btn_ = nullptr;
    QPushButton* clear_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    void build_ui();
    void load_current();
    void show_status(const QString& msg, bool error = false);
};

} // namespace fincept::screens
