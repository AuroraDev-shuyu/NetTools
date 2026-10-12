#include "formmqttclient.h"
#include "ui_formmqttclient.h"

#include <QTcpSocket>
#include <QRandomGenerator>
#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QDebug>

#include "mqttprotocol.h"
#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、套接字、定时器并恢复上次配置
FormMqttClient::FormMqttClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormMqttClient)
{
    ui->setupUi(this);
    this->setWindowTitle("MQTT客户端");

    // 布局微调：两列等宽（客户端ID/订阅主题并排），消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(0, 1);
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(7, 1);

    ui->comboBox_MQTTClientIP->setEditable(true);   // 允许手动输入Broker地址
    ui->pushButton_MQTTClientDisconnect->setEnabled(false);
    ui->pushButton_MQTTClientSendMsg->setEnabled(false);
    ui->checkBox_MQTTClientAutoTesting->setEnabled(false);

    // 心跳定时器：keepalive声明60秒，每30秒发送一次PINGREQ
    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(30000);
    connect(m_keepAliveTimer, &QTimer::timeout, this, [this]() {
        if (m_connected) {
            sendPacket(MqttProtocol::makePingReqPacket());
        }
    });

    // 自动测试定时器：每秒自动发布一次消息
    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(1000);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() {
        if (m_connected) {
            publishMessage();
        }
    });

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("MQTTClient/lastIp", "127.0.0.1").toString();
        const int lastPort = settings.value("MQTTClient/lastPort", 1883).toInt();

        int index = ui->comboBox_MQTTClientIP->findText(lastIp);
        if (index >= 0) {
            ui->comboBox_MQTTClientIP->setCurrentIndex(index);
        } else {
            ui->comboBox_MQTTClientIP->setEditText(lastIp);
        }

        if (lastPort >= ui->spinBox_MQTTClientPort->minimum() &&
            lastPort <= ui->spinBox_MQTTClientPort->maximum()) {
            ui->spinBox_MQTTClientPort->setValue(lastPort);
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
        "QPlainTextEdit#plainTextEdit_MQTTClientMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_MQTTClientSendData {"
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
        "QPushButton#pushButton_MQTTClientConnect {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTClientConnect:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_MQTTClientDisconnect {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTClientDisconnect:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_MQTTClientSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_MQTTClientSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_MQTTClientClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_MQTTClientClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );
}

// 析构函数：断开连接并释放UI
FormMqttClient::~FormMqttClient()
{
    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            sendPacket(MqttProtocol::makeDisconnectPacket()); // 尽量优雅断开
        }
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    delete ui;
}

// 功能：连接Broker——校验IP/端口，建立TCP连接后发送MQTT CONNECT
void FormMqttClient::on_pushButton_MQTTClientConnect_clicked()
{
    if (m_connected) {
        appendLog("[提示] 已连接Broker，无需重复连接");
        return;
    }

    QString ip = ui->comboBox_MQTTClientIP->currentText().trimmed();
    int port = ui->spinBox_MQTTClientPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_MQTTClientIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_MQTTClientPort->setFocus();
        return;
    }

    // 客户端ID：允许手动输入，为空则自动生成
    m_clientId = ui->lineEdit_MQTTClientClientId->text().trimmed();
    if (m_clientId.isEmpty()) {
        m_clientId = QString("NetTools-%1")
                         .arg(QRandomGenerator::global()->bounded(10000, 99999));
        ui->lineEdit_MQTTClientClientId->setText(m_clientId);
    }

    {
        QSettings settings;                 // 持久化最新IP/端口
        settings.setValue("MQTTClient/lastIp", ip);
        settings.setValue("MQTTClient/lastPort", port);
        settings.sync();
    }

    // 复用/重建套接字
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_socket = new QTcpSocket(this);
    m_recvBuf.clear();

    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        appendLog(QString("[%1] TCP连接已建立，发送MQTT CONNECT报文...")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        // 发送CONNECT：清理会话，keepalive 60秒
        sendPacket(MqttProtocol::makeConnectPacket(m_clientId, 60, true));
    });

    connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
        m_recvBuf.append(m_socket->readAll());
        parsePackets();
    });

    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_keepAliveTimer->stop();
        if (m_connected) {
            appendLog(QString("[%1] 与Broker的连接已断开")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        }
        resetConnectionState();
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            return; // 远端正常关闭，disconnected信号中已处理
        }
        appendLog(QString("[%1] 连接失败: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_socket->errorString()));
        resetConnectionState();
    });

    m_connected = false;
    ui->pushButton_MQTTClientConnect->setEnabled(false);
    ui->pushButton_MQTTClientDisconnect->setEnabled(true);

    appendLog(QString("[%1] 正在连接Broker %2:%3...")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(ip)
                  .arg(port));
    m_socket->connectToHost(ip, static_cast<quint16>(port));
}

// 功能：断开——发送DISCONNECT报文并关闭TCP连接
void FormMqttClient::on_pushButton_MQTTClientDisconnect_clicked()
{
    if (m_autoTimer->isActive()) {
        m_autoTimer->stop();
        ui->checkBox_MQTTClientAutoTesting->setChecked(false);
    }

    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        sendPacket(MqttProtocol::makeDisconnectPacket()); // 优雅断开
        m_socket->disconnectFromHost();
    }
    resetConnectionState();
    appendLog(QString("[%1] 已主动断开与Broker的连接")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：发送消息——向发布主题发布一条QoS0消息
void FormMqttClient::on_pushButton_MQTTClientSendMsg_clicked()
{
    if (!m_connected) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "未连接Broker，无法发布消息。", this);
        return;
    }

    QString topic = ui->lineEdit_MQTTClientPubTopic->text().trimmed();
    if (topic.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "发布主题不能为空！", this);
        return;
    }

    QString message = ui->plainTextEdit_MQTTClientSendData->toPlainText();
    if (message.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "消息内容不能为空！", this);
        return;
    }

    publishMessage();
}

// 功能：发布一条测试消息（供手动发送与自动测试共用）
void FormMqttClient::publishMessage()
{
    QString topic = ui->lineEdit_MQTTClientPubTopic->text().trimmed();
    if (topic.isEmpty()) {
        return;
    }

    QByteArray payload = ui->plainTextEdit_MQTTClientSendData->toPlainText().toUtf8();
    if (payload.isEmpty()) {
        return;
    }

    sendPacket(MqttProtocol::makePublishPacket(topic, payload, 0));

    appendLog(QString("[%1] 发布消息: %2 (%3字节)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(topic)
                  .arg(payload.size()));
}

// 功能：关闭窗口——保存日志并退出程序
void FormMqttClient::on_pushButton_MQTTClientClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：自动测试开关——勾选后每秒自动发布一次消息
void FormMqttClient::on_checkBox_MQTTClientAutoTesting_clicked()
{
    bool isAutoTesting = ui->checkBox_MQTTClientAutoTesting->isChecked();

    if (isAutoTesting) {
        if (!m_connected) {
            HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                         "未连接Broker，无法开始自动测试。", this);
            ui->checkBox_MQTTClientAutoTesting->setChecked(false);
            return;
        }
        if (ui->lineEdit_MQTTClientPubTopic->text().trimmed().isEmpty() ||
            ui->plainTextEdit_MQTTClientSendData->toPlainText().isEmpty()) {
            HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                         "发布主题和消息内容不能为空，无法开始自动测试！", this);
            ui->checkBox_MQTTClientAutoTesting->setChecked(false);
            return;
        }
        m_autoTimer->start();
        appendLog("[提示] 自动测试已开启，每秒自动发布一次消息");
    } else {
        m_autoTimer->stop();
        appendLog("[提示] 自动测试已停止");
    }
}

// 功能：发送报文到套接字
void FormMqttClient::sendPacket(const QByteArray &packet)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(packet);
    }
}

// 功能：解析接收缓冲区中的MQTT报文
void FormMqttClient::parsePackets()
{
    int pos = 0;
    while (pos < m_recvBuf.size()) {
        quint8 typeAndFlags = 0;
        QByteArray body;
        MqttProtocol::DecodeResult result =
            MqttProtocol::decodePacket(m_recvBuf, pos, typeAndFlags, body);

        if (result == MqttProtocol::DecodeResult::Incomplete) {
            break; // 等待更多数据
        }
        if (result == MqttProtocol::DecodeResult::Malformed) {
            appendLog("[错误] 收到格式错误的MQTT报文，断开连接");
            if (m_socket) {
                m_socket->abort();
            }
            m_recvBuf.clear();
            return;
        }

        handlePacket(typeAndFlags, body);
    }
    m_recvBuf.remove(0, pos);
}

// 功能：处理单个MQTT报文
void FormMqttClient::handlePacket(quint8 typeAndFlags, const QByteArray &body)
{
    switch (MqttProtocol::packetType(typeAndFlags)) {
    case MqttProtocol::ConnAck: {
        if (body.size() < 2) {
            return;
        }
        quint8 returnCode = static_cast<quint8>(body.at(1));
        if (returnCode == 0) {
            m_connected = true;
            setCommunicationControls(true);
            m_keepAliveTimer->start();

            appendLog(QString("[%1] 已连接Broker (客户端ID: %2)")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(m_clientId));

            // 连接成功后自动订阅配置的主题
            QString subTopic = ui->lineEdit_MQTTClientSubTopic->text().trimmed();
            if (!subTopic.isEmpty()) {
                sendPacket(MqttProtocol::makeSubscribePacket(nextPacketId(), subTopic, 0));
                appendLog(QString("[%1] 已发送订阅请求: %2")
                              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                              .arg(subTopic));
            }
        } else {
            appendLog(QString("[%1] 连接被Broker拒绝 (返回码: %2)")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(returnCode));
            if (m_socket) {
                m_socket->disconnectFromHost();
            }
        }
        break;
    }
    case MqttProtocol::SubAck: {
        if (body.size() < 3) {
            return;
        }
        quint8 granted = static_cast<quint8>(body.at(2));
        if (granted == 0x80) {
            appendLog("[错误] 订阅失败：主题过滤格式无效或权限不足");
        } else {
            appendLog(QString("[%1] 订阅成功 (授权QoS: %2)")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(granted));
        }
        break;
    }
    case MqttProtocol::Publish: {
        int pos = 0;
        QByteArray topicBytes;
        if (!MqttProtocol::decodeUtf8String(body, pos, topicBytes)) {
            return;
        }
        quint8 qos = (MqttProtocol::packetFlags(typeAndFlags) >> 1) & 0x03;
        if (qos > 0) {
            pos += 2; // 跳过报文标识符
        }
        QByteArray payload = body.mid(pos);

        appendLog(QString("[%1] 收到消息: %2 (%3字节)")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(QString::fromUtf8(topicBytes))
                      .arg(payload.size()));
        appendLog(QString::fromUtf8(payload));
        break;
    }
    case MqttProtocol::PingResp: // 心跳响应，无需处理
        break;
    default:
        break;
    }
}

// 功能：分配下一个报文标识符（1~65535循环）
quint16 FormMqttClient::nextPacketId()
{
    quint16 id = m_nextPacketId++;
    if (m_nextPacketId == 0) {
        m_nextPacketId = 1;
    }
    return id;
}

// 功能：复位连接状态与UI
void FormMqttClient::resetConnectionState()
{
    m_connected = false;
    m_recvBuf.clear();
    m_keepAliveTimer->stop();
    if (m_autoTimer->isActive()) {
        m_autoTimer->stop();
        ui->checkBox_MQTTClientAutoTesting->setChecked(false);
    }
    setCommunicationControls(false);
}

// 功能：批量更新与连接状态相关的按钮
void FormMqttClient::setCommunicationControls(bool connected)
{
    ui->pushButton_MQTTClientConnect->setEnabled(!connected);
    ui->pushButton_MQTTClientDisconnect->setEnabled(connected);
    ui->pushButton_MQTTClientSendMsg->setEnabled(connected);
    ui->checkBox_MQTTClientAutoTesting->setEnabled(connected);
}

// 功能：追加日志并自动滚动到底部
void FormMqttClient::appendLog(const QString &text)
{
    ui->plainTextEdit_MQTTClientMsg->appendPlainText(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormMqttClient::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->plainTextEdit_MQTTClientMsg->document();
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
void FormMqttClient::saveLog()
{
    if (ui && ui->plainTextEdit_MQTTClientMsg) {
        savePlainTextEditToFile(ui->plainTextEdit_MQTTClientMsg);
    }
}

// 功能：将日志文本保存到文件（与其他模块一致的追加式日志）
void FormMqttClient::savePlainTextEditToFile(QPlainTextEdit *plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormMqttClient::savePlainTextEditToFile: plainTextEdit is null";
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

    QString fileName = QDir(appDataPath).filePath("MQTTClientLogfile.txt");

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

        out << plainTextEdit->toPlainText() << Qt::endl;
        file.close();
        qDebug() << "MQTT客户端日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "MQTT客户端日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
