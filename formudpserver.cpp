#include "formudpserver.h"
#include "ui_formudpserver.h"
#include <QApplication>

FormUdpServer::FormUdpServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormUdpServer)
{
    ui->setupUi(this);

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);
    ui->comboBox_UDPServerIP->setEditable(true);
    QList<QHostAddress> addressList = QNetworkInterface::allAddresses();
    QStringList ipList;
    ipList.reserve(addressList.size());
    for(const QHostAddress &address:addressList){
        if(address.protocol()==QAbstractSocket::IPv4Protocol){
            // 将IPv4地址转换为字符串并添加到列表
            ipList.append(address.toString());
        }
    }
    ui->comboBox_UDPServerIP->setUpdatesEnabled(false);
    ui->comboBox_UDPServerIP->addItems(ipList);
    ui->comboBox_UDPServerIP->setUpdatesEnabled(true);
    {
        QSettings settings;
        const QString lastIp = settings.value("UDPServer/lastIp").toString();
        const int lastPort = settings.value("UDPServer/lastPort", 9999).toInt();
        if(!lastIp.isEmpty()){
            int index = ui->comboBox_UDPServerIP->findText(lastIp);

            if(index>=0){
                ui->comboBox_UDPServerIP->setCurrentIndex(index);
            }else{
                ui->comboBox_UDPServerIP->setEditText(lastIp);
            }
        }
        if(lastPort>=ui->spinBox_UDPServerPort->minimum() &&
            lastPort<=ui->spinBox_UDPServerPort->maximum()){
            ui->spinBox_UDPServerPort->setValue(lastPort);
        }
    }
    udpServerSocket=new QUdpSocket(this);
    // 建立信号槽连接：将UDP套接字readyRead信号连接到数据读取槽函数
    connect(udpServerSocket, &QUdpSocket::readyRead,
            this, &FormUdpServer::udpSeverReadPendingDatagrams);
    // 布局化UI由网格布局管理控件尺寸，不再固定控件大小
    // 设置界面样式：浅色精致主题（与MQTT/HTTP模块一致）
    setStyleSheet(
        "QWidget {"
        "  background-color: #EEF1F6;"
        "  color: #1F2933;"
        "}"
        "QGroupBox {"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #DCE2EC;"
        "  border-radius: 8px;"
        "  margin-top: 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 12px;"
        "  padding: 0 6px;"
        "  background-color: #EEF1F6;"
        "  color: #334155;"
        "}"
        "QLabel {"
        "  font-size: 13px;"
        "  color: #475569;"
        "  background-color: transparent;"
        "}"
        "QComboBox, QSpinBox {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 20px;"
        "}"
        "QComboBox:hover, QSpinBox:hover {"
        "  border-color: #94A3B8;"
        "}"
        "QComboBox:focus, QSpinBox:focus {"
        "  border-color: #3B82F6;"
        "}"
        "QListWidget#listWidget_UDPServerListMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "}"
        "QPlainTextEdit#plainTextEdit_UDPServerSendData {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPushButton {"
        "  font-size: 13px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "}"
        "QPushButton#pushButton_UDPServerStart {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_UDPServerStart:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_UDPServerSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_UDPServerSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_UDPServerClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_UDPServerClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );

    // 设置列表控件属性：优化列表控件的显示和选择行为
    ui->listWidget_UDPServerListMsg->setUniformItemSizes(true);

    // 设置选项模式
    ui->listWidget_UDPServerListMsg->setSelectionMode(QAbstractItemView::SingleSelection);

    // DPI设置已在main.cpp中统一配置，此处不再重复设置

}
FormUdpServer::~FormUdpServer()
{
    qDebug() << "FormUdpServer析构函数开始";
    try{
        if(udpServerSocket){
            udpServerSocket->disconnect();
            udpServerSocket->close();
            delete udpServerSocket;
            udpServerSocket = nullptr;
        }
        if(ui){
            delete ui;
            ui=nullptr;
        }

    }catch(const std::exception& e){
        qWarning() << "FormUdpServer析构时发生异常:" << e.what();

    }catch(...){
        qWarning() << "FormUdpServer析构时发生未知异常";

    }

    qDebug() << "FormUdpServer析构函数结束";
}
void FormUdpServer::on_pushButton_UDPServerStart_clicked()
{
    QString ip=ui->comboBox_UDPServerIP->currentText();
    QHostAddress hostAddress(ip);
    quint16 port=ui->spinBox_UDPServerPort->value();
    if(ui->pushButton_UDPServerStart->text()=="启动监听"){
        // 第一层验证：使用输入验证器验证IP地址格式
        auto ipValidation=InputValidator::validatorNetworkAddress(ip);
        if(!ipValidation.isValid){
            HANDLE_ERROR(ErrorHandler::ValidationError,ErrorHandler::Warning,ipValidation.errorMessage,this);

            return;
        }

        // 第二层验证：验证QHostAddress对象是否有效且为IPv4协议
        if(hostAddress.isNull() || hostAddress.protocol()!=QAbstractSocket::IPv4Protocol){
            HANDLE_ERROR(ErrorHandler::ValidationError,ErrorHandler::Warning,"无效的IP地址",this);

            return;
        }

        // 第三层验证：验证端口号是否在有效范围内
        if(port < 1 || port > 65535){
            HANDLE_ERROR(ErrorHandler::ValidationError,ErrorHandler::Warning,"端口号必须在1-65535范围内",this);

            return;
        }

        // 第四层验证：验证UDP套接字是否已初始化
        if(!udpServerSocket){
            HANDLE_ERROR(ErrorHandler::NetworkError,ErrorHandler::Critical,"UDP套接字未初始化",this);

            return;
        }

        // 绑定IP地址和端口
        if(udpServerSocket->bind(hostAddress,port)){
            ui->pushButton_UDPServerStart->setText("关闭监听");
            udpServerAppendStrItem(1,"UDP服务器启动监听成功",false);
            serverRunning=true;

            // 保存配置IP和端口
            QSettings settings;
            settings.setValue("UDPServer/lastIp",hostAddress.toString());
            settings.setValue("UDPServer/lastPort",static_cast<int>(port));

            // 立即同步磁盘
            settings.sync();
        }else{
            // 绑定失败
            if(udpServerSocket){
                HANDLE_ERROR(ErrorHandler::NetworkError,ErrorHandler::Warning,udpServerSocket->errorString(),this);
            }else{
                HANDLE_ERROR(ErrorHandler::NetworkError,ErrorHandler::Warning,"UDP套接字未初始化",this);
            }
        }
    }else{
        // 停止监听：当前按钮文本为“关闭监听”，需要停止服务器监听
        if(udpServerSocket){
            udpServerSocket->abort();
        }

        // 更新按钮文本：将按钮文本改回“启动监听”，表示当前已停止，下次点击将启动
        ui->pushButton_UDPServerStart->setText("启动监听");

        // 追加状态日志
        udpServerAppendStrItem(1,"UDP服务器已停止监听",false);

        // 更新运行状态标志
        serverRunning=false;
    }

}
void FormUdpServer::on_pushButton_UDPServerClose_clicked()
{
    saveListWidgetToFile(ui->listWidget_UDPServerListMsg);
    QCoreApplication::quit();

}
void FormUdpServer::on_pushButton_UDPServerSendMsg_clicked()
{
    if(!udpServerSocket){
        HANDLE_ERROR(ErrorHandler::NetworkError,ErrorHandler::Warning,"UDP套接字未初始化，无法发送数据。",this);
        return;
    }
    if(!serverRunning){
        HANDLE_ERROR(ErrorHandler::NetworkError,ErrorHandler::Warning,"请先启动UDP服务器,监听再发送测试消息。",this);
        return;
    }
    if(m_lastClientPort==0 || m_lastClientAddress.isNull()){
        HANDLE_ERROR(ErrorHandler::ValidationError,ErrorHandler::Warning,"当前还没有收到任何客户端的消息，无法确定发送目标。",this);
        return;
    }
    if(!ui->plainTextEdit_UDPServerSendData){
        return;
    }
    QString message=ui->plainTextEdit_UDPServerSendData->toPlainText().trimmed();
    if(message.isEmpty()){
        QMessageBox::warning(this,"提示","发送内容不能为空！");
        ui->plainTextEdit_UDPServerSendData->setFocus();
        return;
    }
    QByteArray data=message.toUtf8();
    qint64 bytes=udpServerSocket->writeDatagram(data,m_lastClientAddress,m_lastClientPort);
    if(bytes==-1){
        QMessageBox::warning(this, "错误", QString("发送失败: %1").arg(udpServerSocket->errorString()));
        return;
    }
    QString timestamp=QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    QString logEntry = QString("\n[%1] 服务器发送到 %2:%3\n%4\n")
                           .arg(timestamp, m_lastClientAddress.toString())
                           .arg(m_lastClientPort)
                           .arg(message);
    ui->listWidget_UDPServerListMsg->addItem(logEntry);
    ui->listWidget_UDPServerListMsg->scrollToBottom();
}
void FormUdpServer::udpSeverReadPendingDatagrams()
{
    if(!serverRunning){
        return;
    }

    if(!udpServerSocket){
        qWarning()<<"FormUdpServer::udpServerReadPendingDatagrams: udpServerSocket is null";
        return;
    }

    while(udpServerSocket->hasPendingDatagrams()){
        QByteArray datagram;
        qint64 datagramSize=udpServerSocket->pendingDatagramSize();
        if(datagramSize<=0){
            break;  // 无效的数据报大小，退出循环
        }
        datagram.resize(static_cast<int>(datagramSize));
        QHostAddress clientAddress;
        quint16 clientPort;
        qint64 bytesRead=udpServerSocket->readDatagram(datagram.data(),datagram.size(),&clientAddress,&clientPort);
        if(bytesRead==-1){
            qWarning()<<"读取UDP数据报失败："<<udpServerSocket->errorString();
            continue;   // 读取失败，继续处理下一个数据报
        }

        m_lastClientAddress=clientAddress;
        m_lastClientPort=clientPort;

        udpServerAppendStrItem(0,QString::fromUtf8(datagram),false);

        QByteArray response=QString("[Server reply:%1").arg(QString::fromUtf8(datagram)).toUtf8();
        udpServerSocket->writeDatagram(response,clientAddress,clientPort);

    }

}
void FormUdpServer::udpServerAppendStrItem(int type,const QString &strData,bool clear)
{
    Q_UNUSED(clear);

    QString timestamp=QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    QString processedData=strData;
    processedData.replace("\r","").replace("\n","");

    QString logEntry=QString("\n[%1] %2\n%3\n")
                           .arg(timestamp)
                           .arg(type==0?"接收数据":"服务器状态")
                           .arg(processedData);
    ui->listWidget_UDPServerListMsg->setUpdatesEnabled(false);
    ui->listWidget_UDPServerListMsg->addItem(logEntry);
    trimLog();
    ui->listWidget_UDPServerListMsg->setUpdatesEnabled(true);
    int lastRow=ui->listWidget_UDPServerListMsg->count()-1;
    if(lastRow==0){
        QListWidgetItem *item=ui->listWidget_UDPServerListMsg->item(lastRow);
        if(item){
            ui->listWidget_UDPServerListMsg->scrollToItem(item,QAbstractItemView::PositionAtBottom);
            ui->listWidget_UDPServerListMsg->setCurrentRow(lastRow);
        }
    }

}
void FormUdpServer::trimLog(int keepRows,int trimStep)
{
    static const int kKeepDefault=1000;
    static const int kTrimDefault=200;
    const int targetKeep=keepRows>0?keepRows:kKeepDefault;
    const int step=trimStep>0?trimStep:kTrimDefault;

    int count=ui->listWidget_UDPServerListMsg->count();
    if(count<=targetKeep+step)
    {
        return;
    }
    int removeCount=count-targetKeep;
    if(removeCount>10){
        ui->listWidget_UDPServerListMsg->setUpdatesEnabled(false);
    }
    QList<QListWidgetItem*> itemsToDelete;
    for(int i=0;i<removeCount;i++){
        QListWidgetItem *item=ui->listWidget_UDPServerListMsg->takeItem(0);
        if(item){
            itemsToDelete.append(item);
        }
    }
    if(removeCount>10){
        ui->listWidget_UDPServerListMsg->setUpdatesEnabled(true);
    }
    qDeleteAll(itemsToDelete);
}
void FormUdpServer::closeEvent(QCloseEvent *event)
{
    saveLog();
    event->accept();

}
void FormUdpServer::saveListWidgetToFile(QListWidget* listWidget)
{
    // 第一层检查：验证列表控件指针是否有效
    if(!listWidget){
        qWarning()<<"FormUdpServer::saveListWidgetToFile: listWidget is null";
        return;
    }

    // 获取应用程序数据目录：获取应用程序可以写入数据的标准目录路径
    // 使用AppDataLocation的父目录，确保与INI文件在同一目录：%APPDATA%\NDATools
    QString appDataPath=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    // 如果路径以应用名称结尾，则使用父目录
    QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath);
    QString fileName=QDir(appDataPath).filePath("UDPServerLogfile.txt");
    bool fileExists=QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);
    QFile file(fileName);
    if(file.open(openMode)){
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#else
        out.setEncoding(QStringConverter::Utf8);
#endif
        if(file.exists()){
            QString timestamp=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << "\n" << "========== " << timestamp << " ==========" << endl;
#else
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
#endif
            for(int i=0;i<listWidget->count();i++){
                QListWidgetItem *item=listWidget->item(i);
                if (item) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    out << item->text() << endl;
#else
                    out << item->text() << Qt::endl;
#endif
                }
            }
            file.close();
            qDebug()<<"UDP服务器日志已保存到："<<fileName;
            if(this->isVisible()){
                QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
            }
        }
        else{  // 文件打开失败：输出警告信息，记录错误原因
            qWarning() << "UDP服务器日志保存失败:" << file.errorString();
            if(this->isVisible()){
                QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
            }
        }
    }
}
void FormUdpServer::saveLog()
{
    if(ui && ui->listWidget_UDPServerListMsg){
        saveListWidgetToFile(ui->listWidget_UDPServerListMsg);
    }
}
