#include "app/SettingsWindow.h"

#include "app/AppController.h"
#include "app/Settings.h"

#include <QIcon>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

namespace padmirror::app {

SettingsWindow::SettingsWindow(
    AppController* controller,
    Settings* settings,
    QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("PadMirror Settings"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setWindowIcon(QIcon(QStringLiteral(":/assets/icons/PadMirror-256.png")));
    resize(480, 720);
    setMinimumSize(420, 560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* view = new QQuickWidget(this);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(QColor(QStringLiteral("#0d1112")));
    view->rootContext()->setContextProperty(QStringLiteral("app"), controller);
    view->rootContext()->setContextProperty(QStringLiteral("settings"), settings);
    view->setSource(QUrl(QStringLiteral("qrc:/ui/SettingsView.qml")));
    if (auto* root = view->rootObject()) {
        connect(root, SIGNAL(closeRequested()), this, SLOT(hide()));
    }
    layout->addWidget(view);
}

void SettingsWindow::showSettings() {
    show();
    raise();
    activateWindow();
}

} // namespace padmirror::app
