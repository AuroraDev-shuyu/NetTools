#ifndef FORMTCPSERVER_H
#define FORMTCPSERVER_H
#include <QWidget>

#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QDateTime>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QListWidget>
#include <QColor>
#include <QCloseEvent>

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include "errorhandler.h"


namespace Ui {
class FormTcpServer;
}
class FormTcpServer : public QWidget
{
    Q_OBJECT
public:
    explicit FormTcpServer(QWidget *parent = nullptr);
    ~FormTcpServer();
private slots:
    void on_pushButton_TCPServerStart_clicked();
    void on_pushButton_TCPServerStop_clicked();
    void on_pushButton_TCPServerClose_clicked();
    void on_pushButton_TCPServerSendMsg_clicked();
private:
    Ui::FormTcpServer *ui;
    QTcpServer *tcpServer = nullptr;
    QTcpSocket *tcpServerSocket = nullptr;
    QList<QTcpSocket*> tcpServerSocketList;
    bool serverRunning = false;
    int receivedMessageCount = 0;
    void appendColorLog(const QString &text, const QColor &color);
    void trimLog(int keepRows = 1000, int trimStep = 200);
public:
    bool CheckIPAddrIsVaild(QString strIpAddress);
    void saveListWidgetToFile(QListWidget* listWidget);
    void saveLog();
    void closeEvent(QCloseEvent *event) override;
public Q_SLOTS:
    void TcpServerConnectedFunc();
    void ClientDisconnectedFunc();
    void ReadAllDataFunc();

};

#endif // FORMTCPSERVER_H
