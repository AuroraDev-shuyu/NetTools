#ifndef FORMTCPCLIENT_H
#define FORMTCPCLIENT_H

#include <QWidget>

#include <QMessageBox>
#include <QRegularExpression>
#include <QString>
#include <QDateTime>
#include <QSettings>
#include <QTextCursor>
#include <QPlainTextEdit>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QDir>
#include "network.h"
#include "errorhandler.h"
#include "inputvalidator.h"

namespace Ui {
class FormTcpClient;
}
class FormTcpClient : public QWidget
{
    Q_OBJECT
public:
    explicit FormTcpClient(QWidget *parent = nullptr);
    ~FormTcpClient();
private slots:
    void on_pushButton_TCPClientConnect_clicked();
    void on_pushButton_TCPClientDisconnect_clicked();
    void on_pushButton_TCPClientClose_clicked();
    void on_pushButton_TCPClientSendMsg_clicked();
    void on_checkBox_TCPClientAutoTesting_clicked();
private:
    Ui::FormTcpClient *ui;
    Network NetworkClient;
    bool connected=false;

    void trimLog(int keepBlocks = 1000, int trimStep = 200);
    bool CheckIPAddrIsVaild(QString strIpAddress);
    void enableCommunicationControls(bool enable);
    void closeEvent(QCloseEvent *event) override;
public:

    void savePlainTextEditToFile(QPlainTextEdit* plainTextEdit);
    void saveLog();


};

#endif // FORMTCPCLIENT_H
