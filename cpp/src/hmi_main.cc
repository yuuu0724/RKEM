#include "hmi_mainwindow.h"
#include "logger.h"

#include <QApplication>
#include <QFont>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>

namespace {
QString decodeEscapedCommand(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('\\') || i + 1 >= text.size()) {
            out.append(text.at(i));
            continue;
        }
        const QChar next = text.at(++i);
        if (next == QLatin1Char('n')) {
            out.append(QLatin1Char('\n'));
        } else if (next == QLatin1Char('r')) {
            out.append(QLatin1Char('\r'));
        } else if (next == QLatin1Char('t')) {
            out.append(QLatin1Char('\t'));
        } else if (next == QLatin1Char('\\')) {
            out.append(QLatin1Char('\\'));
        } else {
            out.append(QLatin1Char('\\'));
            out.append(next);
        }
    }
    return out;
}

void printUsage()
{
    std::cout << "Usage: ./main_process [--serial-port /dev/ttyS9] [--serial-baud 115200]\n";
    std::cout << "                      [--motor-serial-port /dev/ttyS8] [--motor-serial-baud 115200]\n";
    std::cout << "                      [--employee-db employee_host.db] [--cloud-config config/cloud_upload.ini]\n";
    std::cout << "                      [--arrival-command DONE] [--move-command 'hex:AA 55 20 FF'] [--chip-slots 4]\n";
    std::cout << "                      [-platform offscreen]\n";
    std::cout << "Motor protocol: default binary command AA 55 20 FF, receive AA 55 21 FF when movement completes.\n";
    std::cout << "Text move command can be passed as --move-command 'MOVE_NEXT\\n'.\n";
}

HmiRuntimeOptions parseOptions(int argc, char *argv[])
{
    HmiRuntimeOptions options;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--serial-port") && i + 1 < argc) {
            options.serialPort = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("--serial-baud") && i + 1 < argc) {
            options.serialBaudrate = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (arg == QStringLiteral("--motor-serial-port") && i + 1 < argc) {
            options.motorSerialPort = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("--motor-serial-baud") && i + 1 < argc) {
            options.motorSerialBaudrate = QString::fromLocal8Bit(argv[++i]).toInt();
        } else if (arg == QStringLiteral("--arrival-command") && i + 1 < argc) {
            options.arrivalCommand = decodeEscapedCommand(QString::fromLocal8Bit(argv[++i]));
        } else if (arg == QStringLiteral("--move-command") && i + 1 < argc) {
            options.moveCommand = decodeEscapedCommand(QString::fromLocal8Bit(argv[++i]));
        } else if (arg == QStringLiteral("--employee-db") && i + 1 < argc) {
            options.employeeDatabasePath = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("--cloud-config") && i + 1 < argc) {
            options.cloudUploadConfigPath = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == QStringLiteral("--chip-slots") && i + 1 < argc) {
            options.chipSlots = std::max(1, QString::fromLocal8Bit(argv[++i]).toInt());
        } else if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            printUsage();
            std::exit(0);
        }
    }
    return options;
}
}

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

    const HmiRuntimeOptions options = parseOptions(argc, argv);
    Logger::GetInstance().Init("logs");

    QApplication app(argc, argv);

    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(10);
    app.setFont(font);

    int rc = 0;
    {
        MainWindow window(options);
        window.show();
        rc = app.exec();
    }
    Logger::GetInstance().Shutdown();
    return rc;
}
