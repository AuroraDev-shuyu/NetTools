#ifndef FORMUDPSERVER_H
#define FORMUDPSERVER_H

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include <QWidget>
#include <QUdpSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDateTime>
#include <QMessageBox>
#include <QSettings>
#include <QListWidget>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QDir>

#include "errorhandler.h"
#include "inputvalidator.h"


namespace Ui {
class FormUdpServer;
}
class FormUdpServer : public QWidget
{
    Q_OBJECT
public:
    explicit FormUdpServer(QWidget *parent = nullptr);
    ~FormUdpServer();
private slots:
    void on_pushButton_UDPServerStart_clicked();
    void on_pushButton_UDPServerClose_clicked();
    void on_pushButton_UDPServerSendMsg_clicked();
    void udpSeverReadPendingDatagrams();
    void udpServerAppendStrItem(int type,const QString &strData,bool clear);
private:
    Ui::FormUdpServer *ui;
    QUdpSocket *udpServerSocket;
    QHostAddress m_lastClientAddress;
    quint16 m_lastClientPort=0;
    bool serverRunning=false;
    void trimLog(int keepRows=1000,int trimStep=200);
    void closeEvent(QCloseEvent *event) override;
public:
    void saveListWidgetToFile(QListWidget* listWidget);
    void saveLog();

};

#endif // FORMUDPSERVER_H
