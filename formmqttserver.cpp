#include "formmqttserver.h"
#include "ui_formmqttserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDateTime>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QRandomGenerator>
#include <QDebug>

#include "mqttprotocol.h"
#include "errorhandler.h"
#include "inputvalidator.h"

#include <QTimer>

// 出站QoS1/QoS2投递的重传参数
static const int kRetryIntervalMs = 5000;  // 重传间隔
static const int kMaxRetries = 10;         // 最大重传次数（超出则放弃投递）

// 功能：构造函数，初始化UI、TCP服务器、填充本机IP列表并恢复上次配置
FormMqttServer::FormMqttServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormMqttServer)
{
    ui->setupUi(this);
    this->setWindowTitle("MQTT服务器");

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(5, 1);

    ui->comboBox_MQTTServerIP->setEditable(true);     // 允许手动输入监听地址
    ui->pushButton_MQTTServerStop->setEnabled(false); // 初始未运行时禁用"停止"

    // 默认发布主题与消息内容
    ui->lineEdit_MQTTServerTopic->setText("netTools/test");
    ui->plainTextEdit_MQTTServerSendData->setPlainText("Hello from NetTools MQTT Broker!");

    // MQTT服务器实例，监听新连接信号
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &FormMqttServer::onNewConnection);

    // 周期定时器（每秒）：出站QoS重传 + 保活超时检测
    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(1000);
    connect(m_retryTimer, &QTimer::timeout, this, &FormMqttServer::retryPendingDeliveries);
    connect(m_retryTimer, &QTimer::timeout, this, &FormMqttServer::checkKeepAliveTimeouts);
    m_retryTimer->start();
    m_retryClock.start();

    // 枚举本机IPv4，填充下拉框
    QList<QHostAddress> addrList = QNetworkInterface::allAddresses();
    QStringList ipList;
    ipList.reserve(addrList.size());
    for (const QHostAddress &address : addrList) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            ipList.append(address.toString());
        }
    }

    // 批量添加IP地址，减少UI更新次数
    ui->comboBox_MQTTServerIP->setUpdatesEnabled(false);
    ui->comboBox_MQTTServerIP->addItems(ipList);
    ui->comboBox_MQTTServerIP->setUpdatesEnabled(true);

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("MQTTServer/lastIp").toString();
        const int lastPort = settings.value("MQTTServer/lastPort", 1883).toInt();

        if (!lastIp.isEmpty()) {
            int index = ui->comboBox_MQTTServerIP->findText(lastIp);
            if (index >= 0) {
                ui->comboBox_MQTTServerIP->setCurrentIndex(index);
            } else {
                ui->comboBox_MQTTServerIP->setEditText(lastIp);
            }
        }

        if (lastPort >= ui->spinBox_MQTTServerPort->minimum() &&
            lastPort <= ui->spinBox_MQTTServerPort->maximum()) {
            ui->spinBox_MQTTServerPort->setValue(lastPort);
        }
    }

    // 浅色精致主题：布局化界面配套的统一样式
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
        "QComboBox, QSpinBox, QLineEdit {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 20px;"
        "}"
        "QComboBox:hover, QSpinBox:hover, QLineEdit:hover {"
        "  border-color: #94A3B8;"
        "}"
        "QComboBox:focus, QSpinBox:focus, QLineEdit:focus {"
        "  border-color: #3B82F6;"
        "}"
        "QTextEdit#ListWidget_MQTTServerListMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_MQTTServerSendData {"
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
        "QPushButton#pushButton_MQTTServerStart {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTServerStart:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_MQTTServerStop {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTServerStop:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_MQTTServerSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTServerSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_MQTTServerClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_MQTTServerClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );
}

// 析构函数：停止监听并清理所有客户端连接
FormMqttServer::~FormMqttServer()
{
    if (m_tcpServer) {
        m_tcpServer->disconnect();
        m_tcpServer->close();
    }

    const QList<QTcpSocket *> sockets = m_clients.keys(); // 拷贝，避免迭代中修改
    for (QTcpSocket *socket : sockets) {
        removeClient(socket);
        if (socket) {
            socket->deleteLater();
        }
    }
    delete ui;
}

// 功能：启动监听——校验IP/端口并开始监听
void FormMqttServer::on_pushButton_MQTTServerStart_clicked()
{
    if (m_serverRunning) {
        appendLog("[提示] 服务器已在运行");
        return;
    }

    QString ip = ui->comboBox_MQTTServerIP->currentText().trimmed();
    int port = ui->spinBox_MQTTServerPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_MQTTServerIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_MQTTServerPort->setFocus();
        return;
    }

    if (!m_tcpServer) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Critical,
                     "MQTT服务器未初始化", this);
        return;
    }

    if (m_tcpServer->listen(QHostAddress(ip), static_cast<quint16>(port))) {
        m_serverRunning = true;
        ui->pushButton_MQTTServerStart->setEnabled(false);
        ui->pushButton_MQTTServerStop->setEnabled(true);

        // 持久化配置
        {
            QSettings settings;
            settings.setValue("MQTTServer/lastIp", ip);
            settings.setValue("MQTTServer/lastPort", port);
            settings.sync();
        }

        appendLog(QString("[%1] MQTT Broker已启动: %2:%3")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(ip)
                      .arg(port));
    } else {
        appendLog(QString("[%1] [错误] 启动失败: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_tcpServer->errorString()));
        QMessageBox::warning(this, "错误",
                             QString("MQTT服务器启动失败!\n%1").arg(m_tcpServer->errorString()));
    }
}

// 功能：停止监听并断开所有客户端
void FormMqttServer::on_pushButton_MQTTServerStop_clicked()
{
    if (!m_serverRunning) {
        appendLog("[提示] 服务器未在运行");
        return;
    }

    if (m_tcpServer) {
        m_tcpServer->close(); // 停止监听
    }

    // 断开所有客户端连接
    const QList<QTcpSocket *> sockets = m_clients.keys(); // 拷贝，避免迭代中修改
    for (QTcpSocket *socket : sockets) {
        if (socket) {
            socket->disconnect(this); // 避免触发onClientDisconnected重复清理
            socket->abort();
            removeClient(socket);
            socket->deleteLater();
        }
    }

    m_serverRunning = false;
    ui->pushButton_MQTTServerStart->setEnabled(true);
    ui->pushButton_MQTTServerStop->setEnabled(false);

    appendLog(QString("[%1] MQTT Broker已停止，所有客户端已断开")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：发送消息——Broker以发布主题向所有订阅者广播消息
void FormMqttServer::on_pushButton_MQTTServerSendMsg_clicked()
{
    if (!m_serverRunning) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "服务器未启动，无法发布消息。", this);
        return;
    }

    QString topic = ui->lineEdit_MQTTServerTopic->text().trimmed();
    if (topic.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "发布主题不能为空！", this);
        return;
    }

    QByteArray payload = ui->plainTextEdit_MQTTServerSendData->toPlainText().toUtf8();
    if (payload.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "消息内容不能为空！", this);
        return;
    }

    appendLog(QString("[%1] Broker发布消息: %2 (%3字节)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(topic)
                  .arg(payload.size()));
    deliverMessage(topic, payload, 0);
}

// 功能：关闭窗口——保存日志并退出程序
void FormMqttServer::on_pushButton_MQTTServerClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：处理新的TCP连接
void FormMqttServer::onNewConnection()
{
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        if (!socket) {
            continue;
        }

        m_recvBuffers.insert(socket, QByteArray());
        m_clients.insert(socket, MqttClientSession());

        connect(socket, &QTcpSocket::readyRead, this, &FormMqttServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &FormMqttServer::onClientDisconnected);

        appendLog(QString("[%1] 新的TCP连接: %2:%3 (等待MQTT CONNECT)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(socket->peerAddress().toString())
                      .arg(socket->peerPort()));
    }
}

// 功能：读取数据并解析MQTT报文
void FormMqttServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }
    m_recvBuffers[socket].append(socket->readAll());
    parsePackets(socket);
}

// 功能：解析缓冲区中的MQTT报文，逐条分发
void FormMqttServer::parsePackets(QTcpSocket *socket)
{
    QByteArray &buf = m_recvBuffers[socket];
    int pos = 0;

    while (pos < buf.size()) {
        quint8 typeAndFlags = 0;
        QByteArray body;
        MqttProtocol::DecodeResult result =
            MqttProtocol::decodePacket(buf, pos, typeAndFlags, body);

        if (result == MqttProtocol::DecodeResult::Incomplete) {
            break; // 等待更多数据
        }
        if (result == MqttProtocol::DecodeResult::Malformed) {
            appendLog("[错误] 收到格式错误的MQTT报文，断开连接");
            socket->disconnectFromHost();
            buf.clear();
            return;
        }

        handlePacket(socket, typeAndFlags, body);

        // 任意有效报文都刷新客户端活动时间（保活超时判断依据）
        if (m_clients.contains(socket)) {
            m_clients[socket].lastActivityMs = m_retryClock.elapsed();
        }

        // 报文处理可能已断开连接，停止继续解析
        if (socket->state() != QAbstractSocket::ConnectedState) {
            buf.clear();
            return;
        }
    }
    buf.remove(0, pos);
}

// 功能：按报文类型分发处理
void FormMqttServer::handlePacket(QTcpSocket *socket, quint8 typeAndFlags, const QByteArray &body)
{
    switch (MqttProtocol::packetType(typeAndFlags)) {
    case MqttProtocol::Connect:
        handleConnect(socket, body);
        break;
    case MqttProtocol::Publish:
        handlePublish(socket, MqttProtocol::packetFlags(typeAndFlags), body);
        break;
    case MqttProtocol::Subscribe:
        handleSubscribe(socket, body);
        break;
    case MqttProtocol::Unsubscribe:
        handleUnsubscribe(socket, body);
        break;
    case MqttProtocol::PingReq:
        sendToClient(socket, MqttProtocol::makePingRespPacket());
        break;
    case MqttProtocol::PubRel: { // QoS2入站：收到PUBREL后投递暂存消息并回复PUBCOMP
        if (body.size() >= 2) {
            quint16 packetId = (static_cast<quint8>(body.at(0)) << 8) |
                               static_cast<quint8>(body.at(1));
            sendToClient(socket, MqttProtocol::makePubCompPacket(packetId));
            auto pendingIt = m_qos2Inbound.find(socket);
            if (pendingIt != m_qos2Inbound.end()) {
                auto it = pendingIt->find(packetId);
                if (it != pendingIt->end()) {
                    appendLog(QString("[%1] QoS2发布完成: %2 (报文标识符%3)")
                                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                  .arg(it->topic)
                                  .arg(packetId));
                    deliverMessage(it->topic, it->payload, 2);
                    pendingIt->erase(it);
                }
            }
        }
        break;
    }
    case MqttProtocol::PubAck:   // QoS1出站：投递完成
    case MqttProtocol::PubRec:   // QoS2出站：收到PUBREC，回复PUBREL
    case MqttProtocol::PubComp:  // QoS2出站：投递完成
        handleOutgoingAck(socket, MqttProtocol::packetType(typeAndFlags), body);
        break;
    case MqttProtocol::Disconnect:
        appendLog(QString("[%1] 客户端主动断开")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        if (m_clients.contains(socket)) {
            m_clients[socket].cleanDisconnect = true; // 干净断开：取消遗嘱发布
        }
        socket->disconnectFromHost();
        break;
    default:
        break; // 其余报文类型忽略
    }
}

// 功能：处理CONNECT报文——校验协议并回复CONNACK
void FormMqttServer::handleConnect(QTcpSocket *socket, const QByteArray &body)
{
    int pos = 0;
    QByteArray protocolName;
    if (!MqttProtocol::decodeUtf8String(body, pos, protocolName) ||
        protocolName != "MQTT" || pos >= body.size()) {
        appendLog("[错误] CONNECT报文协议名无效，断开连接");
        socket->disconnectFromHost();
        return;
    }

    quint8 protocolLevel = static_cast<quint8>(body.at(pos++));
    if (protocolLevel != 0x04) {
        appendLog(QString("[错误] 不支持的MQTT协议级别: %1").arg(protocolLevel));
        sendToClient(socket, MqttProtocol::makeConnAckPacket(false, 0x01)); // 协议版本不支持
        socket->disconnectFromHost();
        return;
    }

    quint8 connectFlags = static_cast<quint8>(body.at(pos++));
    bool willFlag = (connectFlags & 0x04) != 0;
    bool willRetain = (connectFlags & 0x20) != 0;
    quint8 willQos = (connectFlags >> 3) & 0x03;
    bool cleanSession = (connectFlags & 0x02) != 0;
    bool usernameFlag = (connectFlags & 0x80) != 0;
    bool passwordFlag = (connectFlags & 0x40) != 0;

    // 保留位必须为0；遗嘱QoS=3非法
    if ((connectFlags & 0x01) != 0 || willQos == 3) {
        appendLog("[错误] CONNECT连接标志非法，断开连接");
        socket->disconnectFromHost();
        return;
    }

    quint16 keepAlive = 0;
    if (pos + 2 <= body.size()) {
        keepAlive = (static_cast<quint8>(body.at(pos)) << 8) |
                    static_cast<quint8>(body.at(pos + 1));
    }
    pos += 2;

    QByteArray clientIdBytes;
    if (!MqttProtocol::decodeUtf8String(body, pos, clientIdBytes)) {
        appendLog("[错误] CONNECT报文格式错误，断开连接");
        socket->disconnectFromHost();
        return;
    }

    // 遗嘱配置：存入会话，客户端异常断开时由Broker代为发布
    MqttProtocol::WillConfig will;
    if (willFlag) {
        QByteArray willTopicBytes;
        if (!MqttProtocol::decodeUtf8String(body, pos, willTopicBytes) ||
            !MqttProtocol::decodeBinaryData(body, pos, will.message)) {
            appendLog("[错误] CONNECT遗嘱消息格式错误，断开连接");
            socket->disconnectFromHost();
            return;
        }
        will.topic = QString::fromUtf8(willTopicBytes);
        will.qos = willQos;
        will.retain = willRetain;
    }

    QByteArray username;
    if (usernameFlag) {
        if (!MqttProtocol::decodeUtf8String(body, pos, username)) {
            socket->disconnectFromHost();
            return;
        }
    }
    if (passwordFlag) {
        QByteArray password;
        if (!MqttProtocol::decodeUtf8String(body, pos, password)) {
            socket->disconnectFromHost();
            return;
        }
        Q_UNUSED(password);
    }

    QString clientId = QString::fromUtf8(clientIdBytes);
    if (clientId.isEmpty()) {
        // 清理会话模式下允许Broker分配客户端ID
        clientId = QString("NetTools-%1")
                       .arg(QRandomGenerator::global()->bounded(10000, 99999));
    }

    // 相同客户端ID已连接时，按规范断开旧连接（服务器主动断开，不发布其遗嘱）
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.key() != socket && it.value().clientId == clientId) {
            appendLog(QString("[%1] 客户端ID %2 重复，断开旧连接")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(clientId));
            it.value().cleanDisconnect = true;
            it.key()->disconnect(this);
            it.key()->abort();
            removeClient(it.key());
            it.key()->deleteLater();
            break;
        }
    }

    MqttClientSession &session = m_clients[socket];
    session.clientId = clientId;
    session.username = QString::fromUtf8(username);
    session.willFlag = willFlag;
    session.will = will;
    session.keepAliveSeconds = keepAlive;
    session.lastActivityMs = m_retryClock.elapsed();

    sendToClient(socket, MqttProtocol::makeConnAckPacket(false, 0x00)); // 连接接受

    QString log = QString("[%1] 客户端已连接: %2 (来自 %3:%4, keepalive=%5s, cleanSession=%6)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(clientId)
                      .arg(socket->peerAddress().toString())
                      .arg(socket->peerPort())
                      .arg(keepAlive)
                      .arg(cleanSession ? "是" : "否");
    if (willFlag) {
        log += QString(", 遗嘱: %1 (QoS%2, 保留=%3)")
                   .arg(will.topic)
                   .arg(willQos)
                   .arg(willRetain ? "是" : "否");
    }
    appendLog(log);
}

// 功能：处理PUBLISH报文——retain存储/清除，按QoS确认并将消息路由给订阅者
void FormMqttServer::handlePublish(QTcpSocket *socket, quint8 flags, const QByteArray &body)
{
    const MqttProtocol::PublishInfo pub = MqttProtocol::decodePublishPacket(flags, body);
    if (!pub.valid) {
        appendLog("[错误] PUBLISH报文格式错误，断开连接");
        socket->disconnectFromHost();
        return;
    }

    QString clientId = m_clients.value(socket).clientId;

    appendLog(QString("[%1] 收到发布: %2 -> %3 (QoS%4, %5字节%6)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(clientId.isEmpty() ? "未知客户端" : clientId)
                  .arg(pub.topic)
                  .arg(pub.qos)
                  .arg(pub.payload.size())
                  .arg(pub.retain ? ", retain" : ""));
    appendLog(QString::fromUtf8(pub.payload).left(300));

    // 保留消息：retain=1时存储，空载荷表示清除该主题的保留消息
    if (pub.retain) {
        applyRetainedMessage(pub.topic, pub.payload, pub.qos);
    }

    if (pub.qos == 1) {
        // QoS1：立即确认并投递（至少一次）
        sendToClient(socket, MqttProtocol::makePubAckPacket(pub.packetId));
        deliverMessage(pub.topic, pub.payload, 1);
    } else if (pub.qos == 2) {
        // QoS2：PUBREC确认并暂存，收到PUBREL后才投递（恰好一次）；
        // 重复报文（重传）只重发PUBREC，不重复存储
        QHash<quint16, MqttPendingQos2> &pending = m_qos2Inbound[socket];
        if (!pending.contains(pub.packetId)) {
            MqttPendingQos2 stored;
            stored.topic = pub.topic;
            stored.payload = pub.payload;
            pending.insert(pub.packetId, stored);
        }
        sendToClient(socket, MqttProtocol::makePubRecPacket(pub.packetId));
    } else {
        deliverMessage(pub.topic, pub.payload, 0);
    }
}

// 功能：处理SUBSCRIBE报文——记录订阅（支持QoS2）、回复SUBACK并下发匹配的保留消息
void FormMqttServer::handleSubscribe(QTcpSocket *socket, const QByteArray &body)
{
    if (body.size() < 3) {
        socket->disconnectFromHost();
        return;
    }

    quint16 packetId = (static_cast<quint8>(body.at(0)) << 8) |
                       static_cast<quint8>(body.at(1));
    int pos = 2;

    QList<quint8> grantedQos;
    QStringList subscribed;
    QList<QPair<QString, quint8>> newSubscriptions; // (过滤器, 授权QoS)，用于下发保留消息

    while (pos < body.size()) {
        QByteArray filterBytes;
        if (!MqttProtocol::decodeUtf8String(body, pos, filterBytes) || pos >= body.size()) {
            socket->disconnectFromHost();
            return;
        }
        quint8 requestedQos = static_cast<quint8>(body.at(pos++)) & 0x03;
        if (requestedQos == 3) {
            appendLog("[错误] SUBSCRIBE请求QoS=3非法，断开连接");
            socket->disconnectFromHost();
            return;
        }

        QString filter = QString::fromUtf8(filterBytes);
        quint8 granted = requestedQos; // 支持QoS0/1/2

        QHash<QTcpSocket *, quint8> &subscribers = m_subscriptions[filter];
        if (!subscribers.contains(socket)) {
            m_clients[socket].topicFilters.append(filter); // 新过滤器才记录到会话
        }
        subscribers.insert(socket, granted); // 重复订阅按新QoS替换

        grantedQos.append(granted);
        subscribed.append(QString("%1(QoS%2)").arg(filter).arg(granted));
        newSubscriptions.append(qMakePair(filter, granted));
    }

    sendToClient(socket, MqttProtocol::makeSubAckPacket(packetId, grantedQos));

    QString clientId = m_clients.value(socket).clientId;
    appendLog(QString("[%1] 客户端 %2 订阅主题: %3")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(clientId.isEmpty() ? "未知客户端" : clientId)
                  .arg(subscribed.join(", ")));

    // 下发匹配的保留消息：retain标志置1，QoS取存储QoS与授权QoS的较小值
    for (const auto &sub : newSubscriptions) {
        for (auto it = m_retained.constBegin(); it != m_retained.constEnd(); ++it) {
            if (MqttProtocol::topicMatches(sub.first, it.key())) {
                sendPublishToSubscriber(socket, it.key(), it->payload,
                                        qMin(it->qos, sub.second), true);
                appendLog(QString("[%1] 向 %2 下发保留消息: %3")
                              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                              .arg(clientId.isEmpty() ? "未知客户端" : clientId)
                              .arg(it.key()));
            }
        }
    }
}

// 功能：处理UNSUBSCRIBE报文——移除订阅并回复UNSUBACK
void FormMqttServer::handleUnsubscribe(QTcpSocket *socket, const QByteArray &body)
{
    if (body.size() < 3) {
        socket->disconnectFromHost();
        return;
    }

    quint16 packetId = (static_cast<quint8>(body.at(0)) << 8) |
                       static_cast<quint8>(body.at(1));
    int pos = 2;

    QStringList unsubscribed;
    while (pos < body.size()) {
        QByteArray filterBytes;
        if (!MqttProtocol::decodeUtf8String(body, pos, filterBytes)) {
            socket->disconnectFromHost();
            return;
        }
        QString filter = QString::fromUtf8(filterBytes);

        QHash<QTcpSocket *, quint8> &subscribers = m_subscriptions[filter];
        subscribers.remove(socket);
        if (subscribers.isEmpty()) {
            m_subscriptions.remove(filter);
        }
        m_clients[socket].topicFilters.removeAll(filter);
        unsubscribed.append(filter);
    }

    sendToClient(socket, MqttProtocol::makeUnsubAckPacket(packetId));

    QString clientId = m_clients.value(socket).clientId;
    appendLog(QString("[%1] 客户端 %2 取消订阅: %3")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(clientId.isEmpty() ? "未知客户端" : clientId)
                  .arg(unsubscribed.join(", ")));
}

// 功能：客户端断开——清理会话与订阅；异常断开时发布遗嘱
void FormMqttServer::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    const MqttClientSession session = m_clients.value(socket); // 拷贝，removeClient后仍可用
    const QString clientId = session.clientId;

    removeClient(socket);
    socket->deleteLater();

    appendLog(QString("[%1] 客户端断开: %2 (%3:%4)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(clientId.isEmpty() ? "未完成CONNECT的连接" : clientId)
                  .arg(socket->peerAddress().toString())
                  .arg(socket->peerPort()));

    // 遗嘱发布：未收到DISCONNECT报文的断开均视为异常断开，由Broker代为发布遗嘱
    if (!session.willFlag || session.cleanDisconnect) {
        return;
    }
    if (session.will.topic.isEmpty()) {
        appendLog("[提示] 遗嘱主题为空，跳过发布");
        return;
    }

    appendLog(QString("[%1] 客户端 %2 异常断开，发布遗嘱: %3 (QoS%4, 保留=%5)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(clientId)
                  .arg(session.will.topic)
                  .arg(session.will.qos)
                  .arg(session.will.retain ? "是" : "否"));

    // 遗嘱保留：retain=1时写入保留消息存储（空消息=清除）
    if (session.will.retain) {
        applyRetainedMessage(session.will.topic, session.will.message, session.will.qos);
    }
    // 遗嘱按普通发布投递，retain位与Will Retain一致
    deliverMessage(session.will.topic, session.will.message, session.will.qos, session.will.retain);
}

// 功能：发送报文到指定客户端
void FormMqttServer::sendToClient(QTcpSocket *socket, const QByteArray &packet)
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(packet);
    }
}

// 功能：将消息路由给所有匹配主题过滤器的订阅者（支持+和#通配符，按授权QoS投递）
void FormMqttServer::deliverMessage(const QString &topic, const QByteArray &payload,
                                    quint8 incomingQos, bool retain)
{
    // 收集去重后的目标订阅者；同一客户端多个过滤器匹配时取最高授权QoS
    QHash<QTcpSocket *, quint8> targets;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        if (MqttProtocol::topicMatches(it.key(), topic)) {
            for (auto sub = it.value().constBegin(); sub != it.value().constEnd(); ++sub) {
                quint8 &granted = targets[sub.key()];
                granted = qMax(granted, sub.value());
            }
        }
    }

    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        // 实际投递QoS = min(入站QoS, 订阅者授权QoS)
        sendPublishToSubscriber(it.key(), topic, payload, qMin(incomingQos, it.value()), retain);
    }

    if (!targets.isEmpty()) {
        appendLog(QString("[%1] 消息已转发给 %2 个订阅者")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(targets.size()));
    } else {
        appendLog("[提示] 当前没有客户端订阅该主题");
    }
}

// 功能：向单个订阅者投递PUBLISH；QoS1/2进入出站确认与重传流程
void FormMqttServer::sendPublishToSubscriber(QTcpSocket *socket, const QString &topic,
                                             const QByteArray &payload, quint8 qos, bool retain)
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    quint16 packetId = 0;
    QByteArray packet;
    if (qos == 0) {
        packet = MqttProtocol::makePublishPacket(topic, payload, 0, retain);
    } else {
        packetId = allocateOutgoingPacketId(socket);
        packet = MqttProtocol::makePublishPacket(topic, payload, qos, retain, packetId);
    }

    sendToClient(socket, packet);

    if (qos > 0) {
        MqttOutgoingDelivery delivery;
        delivery.packet = packet;
        delivery.state = (qos == 1) ? 0 : 1; // 0=等待PUBACK, 1=等待PUBREC
        delivery.nextRetryMs = m_retryClock.elapsed() + kRetryIntervalMs;
        m_outgoing[socket].insert(packetId, delivery);
    }
}

// 功能：分配出站报文标识符（1~65535循环，跳过在途标识符）
quint16 FormMqttServer::allocateOutgoingPacketId(QTcpSocket *socket)
{
    quint16 &next = m_nextPacketId[socket];
    if (next == 0) {
        next = 1;
    }
    quint16 id = next++;
    if (next == 0) {
        next = 1; // 回绕，0保留
    }
    // 与在途报文冲突时继续分配（理论上循环一圈才可能冲突）
    while (m_outgoing.value(socket).contains(id)) {
        id = next++;
        if (next == 0) {
            next = 1;
        }
    }
    return id;
}

// 功能：处理出站QoS流程的确认报文（PUBACK/PUBREC/PUBCOMP）
void FormMqttServer::handleOutgoingAck(QTcpSocket *socket, quint8 packetType, const QByteArray &body)
{
    if (body.size() < 2) {
        return;
    }
    const quint16 packetId = (static_cast<quint8>(body.at(0)) << 8) |
                             static_cast<quint8>(body.at(1));

    auto socketIt = m_outgoing.find(socket);
    if (socketIt == m_outgoing.end()) {
        return;
    }
    auto it = socketIt->find(packetId);
    if (it == socketIt->end()) {
        return; // 未知报文标识符（重复确认），忽略
    }

    if (packetType == MqttProtocol::PubAck) {
        if (it->state == 0) {
            socketIt->erase(it); // QoS1投递完成
        }
    } else if (packetType == MqttProtocol::PubRec) {
        if (it->state == 1) {
            // QoS2第二阶段：发送PUBREL并等待PUBCOMP
            it->state = 2;
            it->retries = 0;
            it->packet = MqttProtocol::makePubRelPacket(packetId);
            it->nextRetryMs = m_retryClock.elapsed() + kRetryIntervalMs;
            sendToClient(socket, it->packet);
        }
    } else if (packetType == MqttProtocol::PubComp) {
        if (it->state == 2) {
            socketIt->erase(it); // QoS2投递完成
        }
    }
}

// 功能：重传出站超时未确认的QoS1/QoS2报文，超出重传上限则放弃
void FormMqttServer::retryPendingDeliveries()
{
    const qint64 now = m_retryClock.elapsed();
    for (auto socketIt = m_outgoing.begin(); socketIt != m_outgoing.end();) {
        QTcpSocket *socket = socketIt.key();
        for (auto it = socketIt.value().begin(); it != socketIt.value().end();) {
            if (now >= it->nextRetryMs) {
                if (it->retries >= kMaxRetries) {
                    appendLog(QString("[%1] 出站QoS报文重传超限，放弃投递 (报文标识符%2)")
                                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                  .arg(it.key()));
                    it = socketIt.value().erase(it);
                    continue;
                }
                ++it->retries;
                it->nextRetryMs = now + kRetryIntervalMs;
                sendToClient(socket, it->packet);
            }
            ++it;
        }
        if (socketIt.value().isEmpty()) {
            socketIt = m_outgoing.erase(socketIt);
        } else {
            ++socketIt;
        }
    }
}

// 功能：保活超时检测——超过1.5倍keepalive无任何报文则视为异常断开（MQTT-3.1.2-24）
void FormMqttServer::checkKeepAliveTimeouts()
{
    const qint64 now = m_retryClock.elapsed();
    const QList<QTcpSocket *> sockets = m_clients.keys(); // 拷贝，避免迭代中修改
    for (QTcpSocket *socket : sockets) {
        const MqttClientSession &session = m_clients.value(socket);
        if (session.keepAliveSeconds == 0 || session.clientId.isEmpty()) {
            continue; // 未启用保活，或尚未完成CONNECT的连接不检查
        }
        if (now - session.lastActivityMs > session.keepAliveSeconds * 1500LL) {
            appendLog(QString("[%1] 客户端 %2 保活超时（%3秒无报文），按异常断开处理")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(session.clientId)
                          .arg(session.keepAliveSeconds));
            socket->abort(); // 触发disconnected信号 → onClientDisconnected发布遗嘱
        }
    }
}

// 功能：处理保留消息存储（retain=1时存储，空载荷清除）
void FormMqttServer::applyRetainedMessage(const QString &topic, const QByteArray &payload, quint8 qos)
{
    if (payload.isEmpty()) {
        m_retained.remove(topic);
        appendLog(QString("[%1] 保留消息已清除: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(topic));
    } else {
        MqttRetainedMessage retained;
        retained.payload = payload;
        retained.qos = qos;
        m_retained.insert(topic, retained);
        appendLog(QString("[%1] 保留消息已存储: %2 (QoS%3)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(topic)
                      .arg(qos));
    }
}

// 功能：清理客户端连接与订阅记录
void FormMqttServer::removeClient(QTcpSocket *socket)
{
    if (!m_clients.contains(socket)) {
        m_recvBuffers.remove(socket);
        return;
    }

    const MqttClientSession session = m_clients.take(socket);
    m_recvBuffers.remove(socket);

    for (const QString &filter : session.topicFilters) {
        QHash<QTcpSocket *, quint8> &subscribers = m_subscriptions[filter];
        subscribers.remove(socket);
        if (subscribers.isEmpty()) {
            m_subscriptions.remove(filter);
        }
    }

    // 清理该连接的QoS流程状态（入站暂存、出站待确认、报文标识符分配）
    m_qos2Inbound.remove(socket);
    m_outgoing.remove(socket);
    m_nextPacketId.remove(socket);
}

// 功能：追加日志并自动滚动到底部
void FormMqttServer::appendLog(const QString &text)
{
    ui->ListWidget_MQTTServerListMsg->append(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormMqttServer::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->ListWidget_MQTTServerListMsg->document();
    int blocks = doc->blockCount();
    if (blocks <= keep + step) {
        return;
    }

    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    int removeBlocks = blocks - keep;
    for (int i = 0; i < removeBlocks; ++i) {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    }
    cursor.removeSelectedText();
    cursor.deleteChar();
}

// 功能：保存日志（public接口，供主窗口关闭时调用）
void FormMqttServer::saveLog()
{
    if (ui && ui->ListWidget_MQTTServerListMsg) {
        saveTextEditToFile(ui->ListWidget_MQTTServerListMsg);
    }
}

// 功能：将日志文本保存到文件（与其他模块一致的追加式日志）
void FormMqttServer::saveTextEditToFile(QTextEdit *textEdit)
{
    if (!textEdit) {
        qWarning() << "FormMqttServer::saveTextEditToFile: textEdit is null";
        return;
    }

    // 与QSettings配置文件同一目录：%APPDATA%\NetTools
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

    QString fileName = QDir(appDataPath).filePath("MQTTServerLogfile.txt");

    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);

    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        if (fileExists) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
        }

        out << textEdit->toPlainText() << Qt::endl;
        file.close();
        qDebug() << "MQTT服务器日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "MQTT服务器日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
