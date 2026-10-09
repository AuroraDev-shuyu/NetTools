#include "mainwidget.h"
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif

    QApplication a(argc, argv);
    QApplication::setOrganizationName("NDATools");
    QApplication::setApplicationName("NDATools");

    // 设置QSettings使用INI文件格式
    QSettings::setDefaultFormat(QSettings::IniFormat);
    MainWidget w;
    w.show();
    return a.exec();
}
