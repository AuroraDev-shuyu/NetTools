#include "errorhandler.h"
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
ErrorHandler::ErrorHandler(QObject *parent)
    : QObject(parent)
    , logFile(nullptr)
    , logStream(nullptr)
    , loggingEnabled(true)
    , currentLogPath()
{
    initializeLogFile();
}
ErrorHandler::~ErrorHandler()
{
    if (logStream) {
        delete logStream;
    }
    if (logFile) {
        logFile->close();
        delete logFile;
    }
}
ErrorHandler& ErrorHandler::instance()
{
    static ErrorHandler instance;
    return instance;
}
void ErrorHandler::handleError(ErrorType type, ErrorLevel level,const QString &message, QWidget *parent)
{
    QString context = errorTypeToString(type);
    logError(context, message, level);
    QMessageBox::Icon icon;
    QString title;
   switch (level) {
    case Info:
        icon = QMessageBox::Information; // 信息图标
        title = "信息";
        break;
    case Warning:
        icon = QMessageBox::Warning;     // 警告图标
        title = "警告";
        break;
    case Critical:
        icon = QMessageBox::Critical;    // 错误图标
        title = "错误";
        break;
    case Fatal:
        icon = QMessageBox::Critical;    // 严重错误同样用 Critical 图标
        title = "严重错误";
        break;
    }
    QMessageBox msgBox(parent);
    msgBox.setIcon(icon);                       // 设置图标
    msgBox.setWindowTitle(title);               // 设置标题
    msgBox.setText(message);                    // 主要文本
    msgBox.setStandardButtons(QMessageBox::Ok); // 仅确认按钮
    msgBox.exec();                              // 阻塞显示

    // Fatal 级别：记录后直接请求退出应用
    if (level == Fatal) {
        QApplication::quit();
    }
}
void ErrorHandler::logError(const QString &context, const QString &error, ErrorLevel level)
{
    if (!loggingEnabled || !logStream) {
        return;
    }

    QMutexLocker locker(&logMutex);
    rolloverIfNeeded_unlocked();
    reopenForToday_unlocked();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"); // 当前时间
    QString levelStr = errorLevelToString(level);                                     // 级别文本
    QString logMessage = QString("[%1] [%2] [%3] %4")
                             .arg(timestamp)
                             .arg(levelStr)
                             .arg(context)
                             .arg(error);

    writeToLog(logMessage);  // 持锁写入
}
void ErrorHandler::setLogFile(const QString &filename)
{
    QMutexLocker locker(&logMutex);
    if (logStream) {
        delete logStream;
        logStream = nullptr;
    }
    if (logFile) {
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
    logFile = new QFile(filename);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        logStream = new QTextStream(logFile); // 绑定文本流
        currentLogPath = filename;            // 记录当前路径

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        logStream->setCodec("UTF-8");             // Qt5设置编码
#else
        logStream->setEncoding(QStringConverter::Utf8); // Qt6设置编码
#endif
    } else { // 打开失败：释放指针并发出警告
        delete logFile;
        logFile = nullptr;
        qWarning() << "无法打开日志文件:" << filename;
    }
}
void ErrorHandler::writeToLog(const QString &message)
{
    if (logStream) {
        *logStream << message << Qt::endl;
        logStream->flush();
    }
}
QString ErrorHandler::errorTypeToString(ErrorType type)
{
    switch (type) { // 枚举转可读字符串
    case NetworkError:    return "网络错误";
    case ValidationError: return "验证错误";
    case FileError:       return "文件错误";
    case ConfigError:     return "配置错误";
    case UnknownError:
    default:              return "未知错误"; // 兜底，避免空字符串
    }
}
void ErrorHandler::rolloverIfNeeded_unlocked(qint64 maxBytes)
{
    if (currentLogPath.isEmpty() || !logFile) return;   // 未初始化则跳过
    QFileInfo info(*logFile);                           // 查询当前大小
    if (info.size() < maxBytes) return;                 // 未超阈值则不滚动
    QString timeSuffix = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString rolled = currentLogPath;
    rolled.replace(".log", QString("_%1.log").arg(timeSuffix));

    logStream->flush();
    logFile->close();
    QFile::rename(currentLogPath, rolled);
    delete logStream;
    delete logFile;
    logStream = nullptr;
    logFile = nullptr;
    setLogFile(currentLogPath);
}
void ErrorHandler::reopenForToday_unlocked()
{
    if (currentLogPath.isEmpty()) return;                           // 未初始化则跳过
    QString today = QDate::currentDate().toString("yyyyMMdd");      // 今日日期
    if (currentLogPath.contains(today)) return;                     // 已是今日文件则不切换


    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath);     // 确保目录存在
    QString newPath = QString("%1/NDAssistantTools_%2.log").arg(appDataPath).arg(today); // 新日期文件

    logStream->flush();             // 刷新当前文件
    logFile->close();               // 关闭旧文件
    delete logStream;               // 释放流
    delete logFile;                 // 释放文件
    logStream = nullptr;
    logFile = nullptr;
    setLogFile(newPath);            // 打开新日期文件
}
QString ErrorHandler::errorLevelToString(ErrorLevel level)
{
    switch (level) {                    // 枚举转可读字符串
    case Info:     return "信息";
    case Warning:  return "警告";
    case Critical: return "错误";
    case Fatal:    return "致命";
    default:       return "未知";        // 兜底
    }
}
void ErrorHandler::setLoggingEnabled(bool enabled)
{
    loggingEnabled = enabled; // 简单开关，未持锁因bool原子读写即可
}
void ErrorHandler::initializeLogFile()
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    QString logDir = appDataPath;
    QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {  // 进入父目录
            logDir = parentDir.absolutePath();
        }
    }
    QDir().mkpath(logDir);
    qDebug() << "日志文件目录:" << logDir;
    // 日志文件名：按日期分片，比如：NDATools_20260101.log
    QString logFileName = QString("%1/NDATools_%2.log")
                              .arg(logDir)
                              .arg(QDate::currentDate().toString("yyyyMMdd"));

    setLogFile(logFileName);
    if (logStream) {
        writeToLog("=== NDATools 启动 ===");
    }
}







