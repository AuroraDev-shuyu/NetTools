
#ifndef FORMCHILDDATACONVERSION_H
#define FORMCHILDDATACONVERSION_H

#include <QWidget>

#include <QCloseEvent>
#include <QDebug>
#include <QtConcurrent>
#include <QMessageBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QQueue>


#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#else
#include <QTextCodec>
#endif


#include "dataconverter.h"

namespace Ui {
class FormChildDataConversion;
}

class FormChildDataConversion : public QWidget
{
    Q_OBJECT

public:
    explicit FormChildDataConversion(QWidget *parent = nullptr);
    ~FormChildDataConversion();
    void saveLog();

private slots:
    void on_convertButton_clicked();
    void on_clearButton_clicked();
    void on_swapButton_clicked();
    void on_copyButton_clicked();
    void onConversionFinished();
    void onInputTextChanged();

private:
    Ui::FormChildDataConversion *ui;
    bool conversionRunning = false;
    QTimer *inputUpdateTimer;
    QQueue<QString> logEntries;
    void showMessage(const QString &message, bool isError = false);
    static DataConverter::ConversionResult performConversion(const QString &conversionType, const QString &input);

    QFutureWatcher<DataConverter::ConversionResult> conversionWatcher;
    void setConversionBusy(bool busy);
    void showResult(const DataConverter::ConversionResult &result, QTextEdit *outputEdit);
    void updateInputInfo(const QString &text);
    void updateOutputInfo(const QString &text);
    void copyToClipboard(const QString &text);
    void connectSignals();
    void appendLog(const QString &message);
    void trimLog();
    void savePlainTextEditToFile(QPlainTextEdit* plainTextEdit);

protected:
    void closeEvent(QCloseEvent *event) override;



};

#endif // FORMCHILDDATACONVERSION_H
