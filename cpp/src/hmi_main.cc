#include "hmi_mainwindow.h"

#include <QApplication>
#include <QFont>

#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    const int lockFd = open("/tmp/integrated_inspection_hmi.lock", O_CREAT | O_RDWR, 0644);
    if (lockFd >= 0 && flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "[ERROR] integrated_inspection_hmi is already running\n");
        close(lockFd);
        return 2;
    }

    setenv("RKNN_LOG_LEVEL", "0", 0);
    setenv("RGA_LOG_LEVEL", "3", 0);

    QApplication app(argc, argv);

    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.showFullScreen();

    return app.exec();
}
