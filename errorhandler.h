#ifndef ERRORHANDLER_H
#define ERRORHANDLER_H

#include <QObject>

#include <QString>
#include <QWidget>
#include <QMessageBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QApplication>
#include <QDebug>


#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

class ErrorHandler : public QObject
{
    Q_OBJECT
public:
    explicit ErrorHandler(QObject *parent = nullptr);
    ~ErrorHandler();

    enum ErrorType {
        NetworkError,
        ValidationError,
        FileError,
        ConfigError,
        UnknownError
    };

    enum ErrorLevel {
        Info,
        Warning,
        Critical,
        Fatal
    };


    static ErrorHandler& instance();


    void handleError(ErrorType type, ErrorLevel level, const QString &message,
                     QWidget *parent = nullptr);


    void logError(const QString &context, const QString &error,
                  ErrorLevel level = Warning);

    void setLogFile(const QString &filename);
    void setLoggingEnabled(bool enabled);
    static QString errorTypeToString(ErrorType type);
    static QString errorLevelToString(ErrorLevel level);

private:
    ErrorHandler(const ErrorHandler&) = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;

    QFile *logFile;
    QTextStream *logStream;
    QMutex logMutex;
    bool loggingEnabled;
    void initializeLogFile();
    void writeToLog(const QString &message);
    void rolloverIfNeeded_unlocked(qint64 maxBytes = 5 * 1024 * 1024);
    QString currentLogPath;
    void reopenForToday_unlocked();

};

#define LOG_ERROR(context, message) \
ErrorHandler::instance().logError(context, message, ErrorHandler::Critical)

#define LOG_WARNING(context, message) \
    ErrorHandler::instance().logError(context, message, ErrorHandler::Warning)

#define LOG_INFO(context, message) \
    ErrorHandler::instance().logError(context, message, ErrorHandler::Info)

#define HANDLE_ERROR(type, level, message, parent) \
    ErrorHandler::instance().handleError(type, level, message, parent)

#endif // ERRORHANDLER_H
