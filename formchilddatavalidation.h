

#ifndef FORMCHILDDATAVALIDATION_H
#define FORMCHILDDATAVALIDATION_H

#include <QWidget>

#include <QCloseEvent>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTabWidget>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QMessageBox>

#include <QClipboard>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QQueue>
#include <QFileInfo>
#include <QtConcurrent>
#include <QDebug>


#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#else
#include <QTextCodec>
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "dataconverter.h"


namespace Ui {
class FormChildDataValidation;
}

class FormChildDataValidation : public QWidget
{
    Q_OBJECT

public:
    explicit FormChildDataValidation(QWidget *parent = nullptr);
    ~FormChildDataValidation();
    void saveLog();
private slots:
    void on_calculateButton_clicked();
private:
    void connectSignals();
    void copyToClipboard(const QString &text);
    void showMessage(const QString &message, bool isError = false);
    void setChecksumBusy(bool busy);
    void appendLog(const QString &message);
    bool checksumRunning = false;

    QFutureWatcher<DataConverter::ChecksumResult> checksumWatcher;
    QQueue<QString> logEntries;
protected:
    void closeEvent(QCloseEvent *event) override;
private Q_SLOTS:
    void onUpperCaseToggled(bool enabled);
    void onChecksumFinished();
private:
    Ui::FormChildDataValidation *ui;

};

#endif // FORMCHILDDATAVALIDATION_H
