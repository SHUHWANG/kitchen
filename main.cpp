#include "mainwindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.showMaximized();  // 启动时最大化显示（适应屏幕尺寸）
    return app.exec();
}
