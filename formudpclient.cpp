#include "formudpclient.h"
#include "ui_formudpclient.h"
#include <QApplication>
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
static constexpr int kResponseTimeoutMs = 3000;
FormUdpClient::FormUdpClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormUdpClient)
{
    ui->setupUi(this);

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);
    ui->comboBox_UDPClientIP->setEditable(true);
    ui->plainTextEdit_UDPClientSendData->setPlainText("Hello Udp Server.");
    ui->plainTextEdit_UDPClientMsgList->setReadOnly(true);
    ui->plainTextEdit_UDPClientMsgList->setPlainText("Prompt: Please enter data and click to send test.");
    // 布局化UI由网格布局管理控件尺寸，不再固定控件大小
    // 设置整体界面样式（与MQTT/HTTP模块一致）
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
        "QPlainTextEdit#plainTextEdit_UDPClientMsgList {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_UDPClientSendData {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QCheckBox {"
        "  font-size: 13px;"
        "  color: #475569;"
        "  background-color: transparent;"
        "}"
        "QPushButton {"
        "  font-size: 13px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "}"
        "QPushButton#pushButton_UDPClientConnect {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_UDPClientConnect:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_UDPClientDisconnect {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_UDPClientDisconnect:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_UDPClientSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_UDPClientSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_UDPClientClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_UDPClientClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );

    // 设置发送按钮上图标的显示尺寸
    ui->pushButton_UDPClientSendMsg->setIconSize(QSize(16,16));
    {
        QSettings settings;
        const QString lastIp = settings.value("UDPClient/lastIp", "127.0.0.1").toString();
        const int lastPort = settings.value("UDPClient/lastPort", ui->spinBox_UDPClientPort->value()).toInt();
        int index=ui->comboBox_UDPClientIP->findText(lastIp);
        if(index>=0){
            ui->comboBox_UDPClientIP->setCurrentIndex(index);
        }        else{
            ui->comboBox_UDPClientIP->setEditText(lastIp);
        }
        if(lastPort>=ui->spinBox_UDPClientPort->minimum() &&
            lastPort<=ui->spinBox_UDPClientPort->maximum()){
            ui->spinBox_UDPClientPort->setValue(lastPort);
        }
    }
    responseTimer=new QTimer(this);
    responseTimer->setSingleShot(true);
    responseTimer->setInterval(kResponseTimeoutMs);
    connect(responseTimer,&QTimer::timeout,this,&FormUdpClient::onResponseTimeout);

}
FormUdpClient::~FormUdpClient()
{
    qDebug()<<"FormUdpClient析构函数开始";
    try{
        if(UDPClientSocket){
            qDebug()<<"正在关闭UDP客户端套接字...";
            UDPClientSocket->disconnect();
            UDPClientSocket->close();
            UDPClientSocket->setParent(nullptr);
            delete UDPClientSocket;
            UDPClientSocket=nullptr;
            qDebug()<<"UDP客户端套接字已关闭";
        }else{
            qDebug()<<"UDP客户端套接字为空，无需关闭";
        }
        if(ui){
            qDebug()<<"正在删除UI...";
            delete ui;
            ui=nullptr;
            qDebug()<<"UI已经删除";
        }

    }catch (const std::exception& e){
        qWarning()<<"FormUdpClient析构时发生异常："<<e.what();
    }catch(...){
        qWarning()<<"FormUdpClient析构时发生未知异常";
    }
    qDebug()<<"FormUdpClient析构函数结束";
}
void FormUdpClient::on_pushButton_UDPClientSendMsg_clicked()
{
    QString timestamp=QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    if(!UDPClientSocket){
        UDPClientSocket=new QUdpSocket(this);
        connect(UDPClientSocket,&QUdpSocket::readyRead,this,&FormUdpClient::ReadServerDatagramFunc);
        socketReady=true;
    }
    QString strIpAddress=ui->comboBox_UDPClientIP->currentText();
    int port=ui->spinBox_UDPClientPort->value();
    auto ipValidation = InputValidator::validatorNetworkAddress(strIpAddress);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, ipValidation.errorMessage, this);
        return;
    }
    if (port < 1 || port > 65535) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "端口号必须在1-65535范围内", this);
        return;
    }
    QHostAddress hostAddress(strIpAddress);
    if (hostAddress.isNull() || hostAddress.protocol() != QAbstractSocket::IPv4Protocol) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "无效的IP地址", this);
        return;
    }
    QString message=ui->plainTextEdit_UDPClientSendData->toPlainText().trimmed();
    if (message.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "发送内容不能为空！", this);
        return;
    }
    if(!UDPClientSocket){
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning, "UDP套接字未初始化", this);
        return;
    }
    QByteArray data = message.toUtf8();
    qint64 bytesWritten = UDPClientSocket->writeDatagram(data, hostAddress, port);
    if (bytesWritten == -1) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     QString("发送失败: %1").arg(UDPClientSocket->errorString()), this);
        return;
    }
    {
        QSettings settings;
        settings.setValue("UDPClient/lastIp", strIpAddress);
        settings.setValue("UDPClient/lastPort", static_cast<int>(port));
        settings.sync();
    }
    ui->plainTextEdit_UDPClientMsgList->appendPlainText(QString("\n%1\nSending:%2").arg(timestamp,message));
    if(responseTimer){
        responseTimer->start();
    }
    trimLog();
}
void FormUdpClient::trimLog(int keepBlocks,int trimStep)
{
    static const int kKeepDefault=1000;
    static const int kTrimDefault=200;
    const int keep=keepBlocks>0?keepBlocks:kKeepDefault;
    const int step=trimStep>0?trimStep:kTrimDefault;
    auto doc=ui->plainTextEdit_UDPClientMsgList->document();
    int blocks=doc->blockCount();
    if(blocks<=keep+step){
        return;
    }
    int removeBlocks=blocks-keep;
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    for(int i=0;i<removeBlocks;i++){
        cursor.movePosition(QTextCursor::NextBlock,QTextCursor::KeepAnchor);
    }
    cursor.removeSelectedText();
    cursor.deleteChar();

}
void FormUdpClient::closeEvent(QCloseEvent *event)
{
    saveLog();
    event->accept();
}
void FormUdpClient::savePlainTextEditToFile(QPlainTextEdit * plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormUdpClient::savePlainTextEditToFile: plainTextEdit is null";
        return;
    }
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath); // 确保目录存在
    QString fileName = QDir(appDataPath).filePath("UDPClientLogfile.txt");
    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);
    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#else
        out.setEncoding(QStringConverter::Utf8);
#endif
        if (fileExists) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << "\n" << "========== " << timestamp << " ==========" << endl;
#else
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
#endif
        }
        out << plainTextEdit->toPlainText();
        file.close();
        qDebug() << "UDP客户端日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "UDP客户端日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
void FormUdpClient::saveLog()
{
    if(ui && ui->plainTextEdit_UDPClientMsgList){
        savePlainTextEditToFile(ui->plainTextEdit_UDPClientMsgList);
    }
}
void FormUdpClient::ReadServerDatagramFunc()
{
    if(!UDPClientSocket){
        return;
    }
    if(responseTimer){
        responseTimer->stop();
    }
    while(UDPClientSocket->hasPendingDatagrams()){
        qint64 datagramSize = UDPClientSocket->pendingDatagramSize();
        if (datagramSize <= 0) {
            break;
        }
        QByteArray data;
        data.resize(static_cast<int>(datagramSize));
        QHostAddress senderAddress;
        quint16 senderPort = 0;
        qint64 bytesRead = UDPClientSocket->readDatagram(data.data(), data.size(),
                                                         &senderAddress, &senderPort);
        if (bytesRead == -1) {
            qWarning() << "读取UDP数据报失败:" << UDPClientSocket->errorString();
            continue;
        }
        if (bytesRead < datagramSize) {
            data.resize(static_cast<int>(bytesRead));
        }
        QString timestamp = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
        QString payload = QString::fromUtf8(data);
        ui->plainTextEdit_UDPClientMsgList->appendPlainText(
            QString("\n%1\nReceive:%2").arg(timestamp, payload));
    }
    trimLog();
}
void FormUdpClient::onResponseTimeout()
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");
    ui->plainTextEdit_UDPClientMsgList->appendPlainText(
        QString("\n%1\nWarning: 服务器无响应，可能已关闭或网络不可达").arg(timestamp));
    trimLog();

}

