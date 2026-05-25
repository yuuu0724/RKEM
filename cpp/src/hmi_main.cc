#include "hmi_mainwindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.showFullScreen();

    return app.exec();
}
