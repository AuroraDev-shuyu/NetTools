#ifndef FORMUDPCLIENT_H
#define FORMUDPCLIENT_H
#include <QWidget>
#include <QUdpSocket>
#include <QHostAddress>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>

#include <QDateTime>
#include <QSettings>
#include <QTextCursor>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include "inputvalidator.h"
#include "errorhandler.h"

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
namespace Ui {
class FormUdpClient;
}
class FormUdpClient : public QWidget
{
    Q_OBJECT
public:
    explicit FormUdpClient(QWidget *parent = nullptr);
    ~FormUdpClient();
private slots:
    void on_pushButton_UDPClientSendMsg_clicked();
    void onResponseTimeout();
private:
    bool socketReady=false;
    QTimer *responseTimer=nullptr;
    void trimLog(int keepBlocks=1000,int trimStep=200);
    void closeEvent(QCloseEvent *event) override;

public:
    void savePlainTextEditToFile(QPlainTextEdit * plainTextEdit);
    void saveLog();
    QUdpSocket *UDPClientSocket=nullptr;
    QHostAddress UDPClientHostAddress;
public Q_SLOTS:
    void ReadServerDatagramFunc();
private:
    Ui::FormUdpClient *ui;
};

#endif // FORMUDPCLIENT_H
