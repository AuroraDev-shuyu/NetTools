#include "mainwidget.h"
#include "ui_mainwidget.h"
MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWidget)
{
    ui->setupUi(this);
    setFixedSize(this->width(),this->height());
}
MainWidget::~MainWidget()
{
    delete ui;
}
void MainWidget::closeEvent(QCloseEvent *event)
{
    // 保存TCP服务器日志
    if(ui->tab_TCPServer){
        ui->tab_TCPServer->saveLog();
    }

    // 保存TCP客户端日志
    if(ui->tab_TCPClient){
        ui->tab_TCPClient->saveLog();
    }

    // 保存UDP服务器日志
    if(ui->tab_UDPServer){
        ui->tab_UDPServer->saveLog();
    }

    // 保存UDP客户端日志
    if(ui->tab_UDPClient){
        ui->tab_UDPClient->saveLog();
    }

    // 保存HTTP服务器日志
    if(ui->tab_HTTPServer){
        ui->tab_HTTPServer->saveLog();
    }

    // 保存HTTP客户端日志
    if(ui->tab_HTTPClient){
        ui->tab_HTTPClient->saveLog();
    }

    // 保存MQTT服务器日志
    if(ui->tab_MQTTServer){
        ui->tab_MQTTServer->saveLog();
    }

    // 保存MQTT客户端日志
    if(ui->tab_MQTTClient){
        ui->tab_MQTTClient->saveLog();
    }

    // 保存POP3客户端日志
    if(ui->tab_POP3Client){
        ui->tab_POP3Client->saveLog();
    }

    // 保存QUIC服务器日志
    if(ui->tab_QUICServer){
        ui->tab_QUICServer->saveLog();
    }

    // 保存QUIC客户端日志
    if(ui->tab_QUICClient){
        ui->tab_QUICClient->saveLog();
    }

    // 保存音视频解析日志
    if(ui->tab_AvAnalyzer){
        try{
            ui->tab_AvAnalyzer->saveLog();
        }catch(const std::exception& e){
            qWarning()<<"保存音视频解析日志失败:"<<e.what();
        }
    }

    // 保存数据转换工具日志
    if(ui->tab_DataExchange){
        try{
            ui->tab_DataExchange->saveLog();
        }catch(const std::exception& e){
            qWarning()<<"保存数据转换工具日志失败:"<<e.what();
        }
    }

    event->accept();

}
