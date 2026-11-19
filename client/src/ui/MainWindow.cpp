#include "ui/MainWindow.h"

#include "devicescope/protocol/Version.h"

#include <QCoreApplication>
#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("DeviceScope %1")
                       .arg(QCoreApplication::applicationVersion()));
    setMinimumSize(800, 500);
    resize(960, 640);

    auto *placeholder = new QLabel(
        tr("DeviceScope\n\nStep 0 skeleton\n%1")
            .arg(devicescope::protocol::versionString()),
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}