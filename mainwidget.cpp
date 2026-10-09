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
