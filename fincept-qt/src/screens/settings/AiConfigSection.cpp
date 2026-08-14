// AiConfigSection.cpp — one home for everything AI.

#include "screens/settings/AiConfigSection.h"

#include "screens/settings/AiRolesSection.h"
#include "screens/settings/AiSystemSection.h"
#include "screens/settings/LlmConfigSection.h"
#include "screens/settings/McpServersSection.h"
#include "ui/theme/Theme.h"

#include <QVBoxLayout>

namespace fincept::screens {

AiConfigSection::AiConfigSection(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    tabs_ = new QTabWidget();
    tabs_->setDocumentMode(true);
    tabs_->setStyleSheet(QString("QTabWidget::pane{border:none;}"
                                "QTabBar::tab{background:transparent;color:%1;padding:8px 16px;"
                                "font-size:12px;font-weight:600;border-bottom:2px solid transparent;}"
                                "QTabBar::tab:selected{color:%2;border-bottom:2px solid %2;}"
                                "QTabBar::tab:hover{color:%2;}")
                             .arg(ui::colors::TEXT_DIM())
                             .arg(ui::colors::AMBER()));

    models_ = new LlmConfigSection();
    roles_ = new AiRolesSection();

    // Order follows the order you actually work in: pick a provider, say what
    // uses it, grant it tools, then see what it did.
    tabs_->addTab(models_, "Models");
    tabs_->addTab(roles_, "Roles");
    tabs_->addTab(new McpServersSection(), "Tools");
    tabs_->addTab(new AiSystemSection(), "Activity");

    // Changing the provider on Models changes which models Roles can offer, so
    // re-read bindings whenever Roles comes to the front rather than caching a
    // list that silently goes stale.
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        if (roles_ && tabs_->widget(idx) == roles_)
            roles_->on_shown();
    });

    root->addWidget(tabs_);
}

} // namespace fincept::screens
