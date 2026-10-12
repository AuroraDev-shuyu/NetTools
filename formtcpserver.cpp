#include "formtcpserver.h"
#include "ui_formtcpserver.h"
#include <QApplication>

// 功能：构造函数，初始化 UI、服务器实例、样式与信号
// 说明：恢复上次 IP/端口，填充本机 IPv4，下拉与按钮初始状态，应用统一 QSS
FormTcpServer::FormTcpServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormTcpServer)
{
    ui->setupUi(this);

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);

    tcpServer = new QTcpServer(this);                   // 服务器实例，父对象负责回收
    // 监听新连接信号
    connect(tcpServer, &QTcpServer::newConnection, this, &FormTcpServer::TcpServerConnectedFunc);

    ui->pushButton_TCPServerStop->setEnabled(false);    // 初始未运行时禁用“停止”
    ui->comboBox_TCPServerIP->setEditable(true);        // 设置IP下拉框为可编辑，允许手动输入IP地址

    // 枚举本机 IPv4，填充下拉框
    QList<QHostAddress> addrList = QNetworkInterface::allAddresses();
    QStringList ipList;
    ipList.reserve(addrList.size());                                    // 预分配空间

    for (const QHostAddress &address : addrList) {                      // 使用C++11范围for循环
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {      // 仅保留IPv4
            ipList.append(address.toString());
        }
    }

    // 批量添加IP地址，减少UI更新次数
    ui->comboBox_TCPServerIP->setUpdatesEnabled(false);
    ui->comboBox_TCPServerIP->addItems(ipList);
    ui->comboBox_TCPServerIP->setUpdatesEnabled(true);


    // 恢复上次使用的IP与端口
    {
        QSettings settings;                                                         // 使用默认组织/应用名保存配置
        qDebug() << "QSettings配置文件路径:" << settings.fileName(); // 输出配置文件路径
        const QString lastIp = settings.value("TCPServer/lastIp").toString();       // 上次IP
        const int lastPort = settings.value("TCPServer/lastPort", 12345).toInt();   // 上次端口（默认12345）

        if (!lastIp.isEmpty()) {
            int index = ui->comboBox_TCPServerIP->findText(lastIp);
            if (index >= 0) {
                ui->comboBox_TCPServerIP->setCurrentIndex(index);       // 已存在则选中
            } else {
                ui->comboBox_TCPServerIP->setEditText(lastIp);          // 不存在则直接填入可编辑框
            }
        }


        if (lastPort >= ui->spinBox_TCPServerPort->minimum() &&
            lastPort <= ui->spinBox_TCPServerPort->maximum()) {         // 校验范围
            ui->spinBox_TCPServerPort->setValue(lastPort);              // 恢复端口
        }
    }



    ui->listWidget_TCPServerListMsg->setUniformItemSizes(true);                 // 固定高度绘制，性能更好
    ui->listWidget_TCPServerListMsg->setSelectionMode(QAbstractItemView::SingleSelection); // 单选模式
    ui->pushButton_TCPServerClose->setIconSize(QSize(15, 15));                  // 统一按钮图标尺寸
    ui->pushButton_TCPServerStop->setIconSize(QSize(15, 15));
    ui->pushButton_TCPServerStart->setIconSize(QSize(15, 15));
    if (ui->pushButton_TCPServerSendMsg) {                                      // 发送按钮存在时设置图标尺寸
        ui->pushButton_TCPServerSendMsg->setIconSize(QSize(15, 15));
    }

    // DPI设置已在main.cpp中统一配置，此处不再重复设置


    // 布局化UI由网格布局管理控件尺寸，不再固定控件大小


    // QSS：浅色精致主题（与MQTT/HTTP模块一致）
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
        "QListWidget#listWidget_TCPServerListMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "}"
        "QPlainTextEdit#plainTextEdit_TCPServerSendData {"
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
        "QPushButton#pushButton_TCPServerStart {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_TCPServerStart:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_TCPServerStop {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_TCPServerStop:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_TCPServerSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_TCPServerSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_TCPServerClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_TCPServerClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );

}


// 功能：析构函数，清理 socket、关闭服务器与 UI
// 说明：主动断开/关闭所有 socket，防止资源悬挂；异常捕获避免析构抛出
FormTcpServer::~FormTcpServer()
{
    qDebug() << "FormTcpServer析构函数开始"; // 记录析构起点

    try {
        for (auto client : tcpServerSocketList) {      // 遍历所有客户端
            if (client) {                              // 判空
                client->disconnect();                  // 断开信号槽

                if (client->state() != QAbstractSocket::UnconnectedState) { // 若仍连接
                    client->abort();                                        // 立即中断
                }

                client->setParent(nullptr);            // 解除父子关系
                delete client;                         // 删除实例
            }
        }

        tcpServerSocketList.clear();                   // 清空列表

        if (tcpServer) {                               // 若服务器存在
            tcpServer->disconnect();                   // 断开信号槽
            tcpServer->close();                        // 关闭监听
        }

        tcpServer = nullptr;                           // 置空指针
        tcpServerSocket = nullptr;

        if (ui) {                                      // 释放 UI
            delete ui;
            ui = nullptr;
        }

    } catch (const std::exception& e) {
        qWarning() << "FormTcpServer析构时发生异常:" << e.what(); // 捕获标准异常
    } catch (...) {
        qWarning() << "FormTcpServer析构时发生未知异常";         // 捕获未知异常
    }

    qDebug() << "FormTcpServer析构函数完成"; // 记录析构结束

}


// 功能：启动监听，校验 IP/端口并持久化配置
// 说明：防止重复启动，基础合法性校验后调用 listen；成功则记忆 配置并更新按钮状态
void FormTcpServer::on_pushButton_TCPServerStart_clicked()
{
    QString ipText = ui->comboBox_TCPServerIP->currentText();               // 读取选中IP
    QHostAddress address(ipText);
    int port = ui->spinBox_TCPServerPort->value();                          // 读取端口


    if (serverRunning) {                                                    // 若已在运行
        appendColorLog("[Server already running]", QColor("#666666"));      // 提示
        return;                                                             // 直接返回
    }

    // 验证QHostAddress是否有效
    if (address.isNull() || !CheckIPAddrIsVaild(ipText)) {                  // IP校验失败
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "Invalid IP address", this);
        return;
    }
    if (port < 1 || port > 65535) {                                         // 端口范围校验
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "Invalid port", this);
        return;
    }

    if (!tcpServer) {                                                      // 检查服务器实例
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Critical, "TCP服务器未初始化", this);
        return;
    }

    if (tcpServer->listen(address, port)) {                                 // 尝试开始监听
        appendColorLog("[Server started successfully]", QColor("#666666")); // 成功提示
        ui->pushButton_TCPServerStart->setEnabled(false);                   // 禁用“启动”
        ui->pushButton_TCPServerStop->setEnabled(true);                     // 启用“停止”
        serverRunning = true;                                               // 标记运行中


        QSettings settings;                                                 // 保存当前配置
        settings.setValue("TCPServer/lastIp", address.toString());          // 记忆IP
        settings.setValue("TCPServer/lastPort", port);                      // 记忆端口
        settings.sync();                                                    // 立即同步到磁盘，确保数据保存
    }
    else
    {
        appendColorLog(QString("[Failed to start server]: %1").arg(tcpServer->errorString()), QColor("#CC0000"));
    }

}

// 功能：停止监听并断开所有客户端
// 说明：关闭服务器监听，断开并清空所有 socket，复位按钮状态
void FormTcpServer::on_pushButton_TCPServerStop_clicked()
{
    if (!serverRunning) {                                               // 若未启动，提示并返回
        appendColorLog("[Server not running]", QColor("#666666"));
        return;
    }

    if (!tcpServer) {
        appendColorLog("[Server instance is null]", QColor("#CC0000"));
        return;
    }

    tcpServer->close();                                                 // 停止监听

    // 断开所有客户端连接
    QList<QTcpSocket*> clientsToClose = tcpServerSocketList;            // 复制列表，避免迭代时修改
    for(auto client : clientsToClose) {                                 // 遍历所有客户端并关闭
        if (client) {                                                   // 判空
            // 断开信号槽，避免在关闭过程中触发 ClientDisconnectedFunc
            client->disconnect();                                       // 断开信号槽

            if (client->state() != QAbstractSocket::UnconnectedState) {
                client->abort();                                        // 立即中断连接
            }

            // 从列表中移除（如果还在列表中）
            tcpServerSocketList.removeOne(client);

            // 异步删除，避免在信号处理中直接删除
            client->deleteLater();
        }
    }

    tcpServerSocketList.clear();                                        // 清空列表（双重保险）
    appendColorLog("\n[Prompt:Disconnect all client connections.]\n", QColor("#666666")); // 提示断开


    ui->pushButton_TCPServerStart->setEnabled(true);                    // 允许重新启动
    ui->pushButton_TCPServerStop->setEnabled(false);                    // 禁用停止按钮
    serverRunning = false;                                              // 标记停止
}


// 功能：关闭窗口前保存日志并退出
void FormTcpServer::on_pushButton_TCPServerClose_clicked()
{
    saveListWidgetToFile(ui->listWidget_TCPServerListMsg); // 退出前保存日志到文件

    QCoreApplication::instance()->quit(); // 触发应用退出
}


// 功能：服务器主动群发输入框中的消息
// 说明：校验服务运行、文本非空且存储客户端，再将 UTF-8 字节广播给所有连接
void FormTcpServer::on_pushButton_TCPServerSendMsg_clicked()
{
    if (!ui->plainTextEdit_TCPServerSendData) { // 未找到输入框
        return;
    }
    if (!serverRunning) { // 服务未启动
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning, "服务器未启动，无法发送。", this);
        return;
    }

    QString message = ui->plainTextEdit_TCPServerSendData->toPlainText().trimmed(); // 获取输入
    if (message.isEmpty()) { // 空内容
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning, "发送内容不能为空！", this);
        return;
    }

    if (tcpServerSocketList.isEmpty()) { // 无连接客户端
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning, "当前没有任何已连接的客户端，无法发送测试消息。", this);
        return;
    }

    QByteArray data = message.toUtf8(); // 文本转 UTF-8

    for (QTcpSocket *client : qAsConst(tcpServerSocketList)) { // 群发
        if (client && client->state() == QAbstractSocket::ConnectedState) {
            client->write(data);
        }
    }


    QString timestamp = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss"); // 时间戳
    QString logEntry = QString("\n[%1] Server send: %2\n").arg(timestamp, message);   // 组装日志
    appendColorLog(logEntry, QColor("#008000"));                                     // 绿色提示

}


// 功能：在日志列表追加彩色行
void FormTcpServer::appendColorLog(const QString &text,const QColor &color)
{
    // 去掉头尾行并把内部换行压成空格，避免QListWidget ...
    QString sanitized = text;
    sanitized.replace('\n', ' ');
    sanitized = sanitized.simplified();
    if (sanitized.isEmpty())
        return;

    // 性能优化：批量更新UI，减少重绘次数
    ui->listWidget_TCPServerListMsg->setUpdatesEnabled(false);

    // 创建项目并直接设置颜色，避免后续查找
    QListWidgetItem *item = new QListWidgetItem(sanitized);
    item->setForeground(color);
    ui->listWidget_TCPServerListMsg->addItem(item);

    trimLog(); // 防止日志过长导致UI卡顿

    // 恢复更新并一次性滚动
    ui->listWidget_TCPServerListMsg->setUpdatesEnabled(true);

    // 自动滚动到底部，显示最新消息（只调用一次）
    int row = ui->listWidget_TCPServerListMsg->count() - 1;
    if (row >= 0) {
        ui->listWidget_TCPServerListMsg->scrollToItem(item, QAbstractItemView::PositionAtBottom);
        ui->listWidget_TCPServerListMsg->setCurrentRow(row); // 选中最新行
    }
}

// 裁剪日志行数，超限时指删除最早的行，避免控件过大。
// keepRows：期望保留行数；trimStep：每次删除的行数
void FormTcpServer::trimLog(int keepRows,int trimStep)
{
    static const int kKeepDefault = 1000;                           // 默认保留行
    static const int kTrimDefault = 200;                            // 默认删除步长
    const int targetKeep = keepRows > 0 ? keepRows : kKeepDefault;  // 实际保留行
    const int step = trimStep > 0 ? trimStep : kTrimDefault;        // 实际步长

    int count = ui->listWidget_TCPServerListMsg->count();           // 当前行数
    if (count <= targetKeep + step)                                 // 未超限则返回
        return;
    int removeCount = count - targetKeep;                           // 需删除的行数

    // 性能优化：批量删除时禁用更新，减少UI重绘
    if (removeCount > 10) {  // 只有删除数量较多时才禁用更新
        ui->listWidget_TCPServerListMsg->setUpdatesEnabled(false);
    }

    // 批量删除项目
    QList<QListWidgetItem*> itemsToDelete;
    for (int i = 0; i < removeCount; ++i) {
        QListWidgetItem *item = ui->listWidget_TCPServerListMsg->takeItem(0);
        if (item) {
            itemsToDelete.append(item);
        }
    }

    // 恢复更新
    if (removeCount > 10) {
        ui->listWidget_TCPServerListMsg->setUpdatesEnabled(true);
    }

    // 批量删除项目（在UI更新恢复后）
    qDeleteAll(itemsToDelete);
}


// 功能：检验 IP 字符串是否合法
bool FormTcpServer::CheckIPAddrIsVaild(QString strIpAddress)
{
    QHostAddress addr; // 临时地址对象
    return addr.setAddress(strIpAddress) && addr.protocol() == QAbstractSocket::IPv4Protocol; // 仅接受IPv4
}


// 功能：将列表控件内部保存到文件（追加或覆盖由实现决定）
// 参数：listWdiget 日志列表指针
void FormTcpServer::saveListWidgetToFile(QListWidget* listWidget)
{
    if (!listWidget) {
        qWarning() << "FormTcpServer::saveListWidgetToFile: listWidget is null";
        return;
    }

    // 获取配置文件所在目录（与QSettings配置文件同一目录）
    // 使用AppDataLocation的父目录，确保与INI文件在同一目录：%APPDATA%\NDATools
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
    QDir().mkpath(appDataPath);         // 确保目录存在

    // 使用固定文件名，追加模式
    QString fileName = QDir(appDataPath).filePath("TCPServerLogfile.txt");

    // 检查文件是否存在，决定是追加还是创建
    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);

    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8"); // Qt5 设置编码
#else
        out.setEncoding(QStringConverter::Utf8);        // Qt6设置编码
#endif

        // 如果是追加模式，先写入分隔符和时间戳
        if (fileExists) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << "\n" << "========== " << timestamp << " ==========" << endl;
#else
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
#endif
        }

        // 性能优化：批量写入，减少I/O操作
        // 先收集所有文本，然后一次性写入
        QStringList allItems;
        allItems.reserve(listWidget->count());                  // 预分配空间

        for (int i = 0; i < listWidget->count(); ++i) {         // 遍历列表项
            QListWidgetItem *item = listWidget->item(i);        // 获取第 i 项
            if (item) {                                         // 判空防护
                allItems.append(item->text());
            }
        }

        // 一次性写入所有内容
        out << allItems.join('\n');
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out << endl;
#else
        out << Qt::endl;
#endif
        file.close();
        qDebug() << "TCP服务器日志已保存至:" << fileName;
        // 注意：窗口关闭时不显示消息框，避免阻塞
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "TCP服务器日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}


// 功能：保存日志（public接口，供主窗口调用）
// 返回：无
void FormTcpServer::saveLog()
{
    if (ui && ui->listWidget_TCPServerListMsg) {
        saveListWidgetToFile(ui->listWidget_TCPServerListMsg);
    }
}

// 功能：窗口关闭事件；保存日志、清理资源
void FormTcpServer::closeEvent(QCloseEvent *event)
{
    saveLog();  // 调用保存日志函数
    event->accept();
}

// 功能：处理新客户端连接，记录并更新列表，保存socket并绑定readyread/disconnected
void FormTcpServer::TcpServerConnectedFunc()
{
    tcpServerSocket = tcpServer->nextPendingConnection(); // 获取最新待处理连接
    if(!tcpServerSocketList.contains(tcpServerSocket))    // 若列表未包含
        tcpServerSocketList.append(tcpServerSocket);      // 记录到列表，避免重复

    connect(tcpServerSocket,&QTcpSocket::readyRead,this,&FormTcpServer::ReadAllDataFunc);           // 数据可读
    connect(tcpServerSocket,&QTcpSocket::disconnected,this,&FormTcpServer::ClientDisconnectedFunc); // 断开通知

    QString strPort=QString::number((tcpServerSocket->peerPort())); // 获取对端端口（暂未使用）
    appendColorLog("\n[Prompt:New client connection.]\n", QColor("#666666")); // 记录连接日志

    receivedMessageCount = 0; // 重置计数
}

// 功能：处理客户端断开，清理列表与 UI 状态
void FormTcpServer::ClientDisconnectedFunc()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());       // 获取断开方
    if (client) {

        tcpServerSocketList.removeOne(client);                      // 从列表移除
        client->deleteLater();                                      // 异步删除
    }

    QString msg = QString("\n[Prompt:Client disconnected.]");
    appendColorLog(msg, QColor("#666666"));

}

// 功能：读取所有可用数据并计数、展示日志
void FormTcpServer::ReadAllDataFunc()
{
    if (QTcpSocket *client = qobject_cast<QTcpSocket*>(sender())) {         // 获取触发的客户端
        // 检查客户端是否仍然连接
        if (client->state() != QAbstractSocket::ConnectedState) {
            qWarning() << "Client is not connected, skip reading data";
            return;
        }

        QByteArray data = client->readAll();                                // 读取全部可用数据
        if (data.isEmpty()) {                                               // 检查数据是否为空
            return;
        }

        QString message = QString::fromUtf8(data);                          // 按UTF-8转字符串


        // 性能优化：使用QStringBuilder或直接拼接，避免多次arg调用
        QString timestamp = QDateTime::currentDateTime().toString("yyyy/MM/dd hh:mm:ss");   // 记录时间
        QString logEntry = QString("\n[%1] Received: %2\n").arg(timestamp, message);        // 组装日志
        appendColorLog(logEntry, QColor("#0078D4"));                                        // 蓝色显示收到


        QString response = "Server response: " + message;                   // 简单回显
        // 再次检查连接状态，确保可以发送
        if (client->state() == QAbstractSocket::ConnectedState)
        {
            qint64 bytesWritten = client->write(response.toUtf8());         // 发送回客户端
            if (bytesWritten == -1)
            {
                qWarning() << "Failed to send response to client:" << client->errorString();
            }
        } else
        {
            qWarning() << "Client disconnected before sending response";
        }
    }
}




